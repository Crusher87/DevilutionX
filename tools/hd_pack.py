#!/usr/bin/env python3
"""HD pack files for POE2 Mashup: the HD art in a few big files instead of one file a picture.

    python hd_pack.py pack   <hd folder> <pack folder>   make (or carry on making, or bring up to date) the pack files
    python hd_pack.py verify <hd folder> <pack folder>   read every loose file again and check the pack gives the same bytes
    python hd_pack.py check  <pack folder>               check a pack against its own checksums (after copying it)
    python hd_pack.py list   <pack folder>               the names in a pack
    python hd_pack.py unpack <pack folder> <out folder>  write the pack out as loose files again

A pack is `index.hdx` and `pack_000.hdp`, `pack_001.hdp` ... The game reads them from `assets\\hd\\`; a loose file
of the same name there is used instead of the pack's.

What is stored is each file's bytes exactly as they are (nothing is decoded or compressed again), once for every
different content however many files have it. The source folder is only ever read.

The files (all numbers 32-bit little-endian; the game's reader is Source/poe2/hd_pack.cpp):

    index.hdx     "HDPKIDX\\0", version (1), set id, pack count, blob count, name count, bytes of names,
                  CRC-32 of everything after this header, CRC-32 of the 36 bytes before it        (40 bytes)
                  for each pack:  size of the file
                  for each blob:  pack number, offset, length, CRC-32 of its bytes
                  for each name:  offset into the names, blob number      (sorted by name, as bytes)
                  the names, each ended by a zero byte
    pack_NNN.hdp  "HDPKDAT\\0", version (1), set id, pack number, 0                                (24 bytes)
                  then the blobs, end to end

A name is the file's path under the hd folder in UTF-8, with backslashes and A-Z in lower case.

Only Python 3's own library is used.
"""

import argparse
import hashlib
import os
import struct
import sys
import time
import zlib
from collections import deque
from concurrent.futures import ThreadPoolExecutor

INDEX_MAGIC = b"HDPKIDX\0"
PACK_MAGIC = b"HDPKDAT\0"
VERSION = 1
INDEX_HEADER = struct.Struct("<8s8I")
PACK_HEADER = struct.Struct("<8s4I")
BLOB = struct.Struct("<4I")
NAME = struct.Struct("<2I")
INDEX_NAME = "index.hdx"
JOURNAL_NAME = "hd_pack.journal"
MAX_PACK_BYTES = 0x7FFFFFFF
MAX_NAME_BYTES = 500
DEFAULT_PACK_MB = 1900
# How much is written to the packs between two points a stopped run can carry on from.
CHECKPOINT_BYTES = 64 << 20
CHECKPOINT_SECONDS = 20.0
PROGRESS_SECONDS = 5.0

ASCII_LOWER = bytes.maketrans(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ", b"abcdefghijklmnopqrstuvwxyz")


class PackError(Exception):
    pass


def say(text):
    print(text, flush=True)


def pack_name(number):
    return "pack_%03d.hdp" % number


def is_pack_output(name):
    """A file this tool writes: never taken for art, so a pack can be made inside the folder it packs."""
    lower = name.lower()
    return lower in (INDEX_NAME, INDEX_NAME + ".tmp", JOURNAL_NAME) or (lower.startswith("pack_") and lower.endswith(".hdp"))


def stored_name(relative):
    """The name a file goes by in the index, as bytes."""
    name = relative.replace("/", "\\").replace(os.sep, "\\").encode("utf-8").translate(ASCII_LOWER)
    if b"\0" in name or b"\n" in name or b"\r" in name:
        raise PackError("a file name with a line break or zero byte in it cannot be packed: %r" % relative)
    if len(name) > MAX_NAME_BYTES:
        raise PackError("this file's path is too long to pack (%d bytes, %d at most): %s" % (len(name), MAX_NAME_BYTES, relative))
    return name


def megabytes(count):
    return "%.1f MB" % (count / 1048576.0)


def scan_tree(root):
    """Every file under `root` but this tool's own: (stored name, path, size, modified time in ns), sorted by name."""
    if not os.path.isdir(root):
        raise PackError("no such folder: " + root)
    found = {}
    last = time.monotonic()
    for folder, dirs, files in os.walk(root):
        dirs.sort()
        at_root = os.path.normcase(os.path.abspath(folder)) == os.path.normcase(os.path.abspath(root))
        for file in files:
            if at_root and is_pack_output(file):
                continue
            path = os.path.join(folder, file)
            status = os.stat(path)
            name = stored_name(os.path.relpath(path, root))
            if name in found:
                raise PackError("two files have the same name but for the case of their letters: %s and %s" % (found[name][1], path))
            found[name] = (name, path, status.st_size, status.st_mtime_ns)
        if time.monotonic() - last >= PROGRESS_SECONDS:
            last = time.monotonic()
            say("  looking through the folder: %d files so far" % len(found))
    return [found[name] for name in sorted(found)]


def read_whole(path):
    with open(path, "rb") as file:
        return file.read()


def read_and_hash(entry):
    try:
        data = read_whole(entry[1])
    except FileNotFoundError:
        # Removed since the folder was looked through: left out.
        return None, None
    return data, hashlib.sha256(data).hexdigest()


def read_ahead(entries, jobs, work):
    """`work(entry)` for each entry in order, a few of them under way at a time: (entry, result)."""
    if jobs <= 1:
        for entry in entries:
            yield entry, work(entry)
        return
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        waiting = deque()
        entries = iter(entries)
        try:
            while True:
                while len(waiting) < jobs * 4:
                    entry = next(entries, None)
                    if entry is None:
                        break
                    waiting.append((entry, pool.submit(work, entry)))
                if not waiting:
                    return
                entry, future = waiting.popleft()
                yield entry, future.result()
        finally:
            for _, future in waiting:
                future.cancel()


# ---------------------------------------------------------------------------------------------------------------------
# Reading a pack


class Pack:
    """An index read into memory, and its pack files opened as they are asked for."""

    def __enter__(self):
        return self

    def __exit__(self, *error):
        self.close()

    def __init__(self, folder):
        self.folder = folder
        path = os.path.join(folder, INDEX_NAME)
        if not os.path.isfile(path):
            raise PackError("no %s in %s" % (INDEX_NAME, folder))
        data = read_whole(path)
        if len(data) < INDEX_HEADER.size or data[:8] != INDEX_MAGIC:
            raise PackError(path + " is not an HD pack index")
        magic, version, self.set_id, pack_count, blob_count, name_count, name_bytes, body_crc, header_crc = INDEX_HEADER.unpack_from(data)
        if zlib.crc32(data[:36]) != header_crc:
            raise PackError(path + " is damaged (the checksum of its start is wrong)")
        if version != VERSION:
            raise PackError("%s is of version %d; this tool reads version %d" % (path, version, VERSION))
        body = memoryview(data)[INDEX_HEADER.size:]
        if len(body) != pack_count * 4 + blob_count * BLOB.size + name_count * NAME.size + name_bytes:
            raise PackError(path + " is cut short or damaged (its size is wrong)")
        if zlib.crc32(body) != body_crc:
            raise PackError(path + " is damaged (its checksum is wrong)")
        at = 0
        self.pack_sizes = list(struct.unpack_from("<%dI" % pack_count, body, at))
        at += pack_count * 4
        self.blobs = [BLOB.unpack_from(body, at + i * BLOB.size) for i in range(blob_count)]
        at += blob_count * BLOB.size
        names_at = at + name_count * NAME.size
        names = bytes(body[names_at:])
        self.names = []
        self.blob_of = {}
        for i in range(name_count):
            offset, blob = NAME.unpack_from(body, at + i * NAME.size)
            end = names.find(b"\0", offset)
            if offset >= len(names) or end < 0 or blob >= blob_count:
                raise PackError(path + " has a name that leads outside its tables")
            name = names[offset:end]
            self.names.append(name)
            self.blob_of[name] = blob
        if any(self.names[i] >= self.names[i + 1] for i in range(len(self.names) - 1)):
            raise PackError(path + " has its names out of order")
        for pack, offset, length, _ in self.blobs:
            if pack >= pack_count or offset < PACK_HEADER.size or offset + length > self.pack_sizes[pack]:
                raise PackError(path + " names a place outside its pack files")
        self.files = {}

    def close(self):
        for file in self.files.values():
            file.close()
        self.files = {}

    def pack_file(self, number):
        file = self.files.get(number)
        if file is None:
            path = os.path.join(self.folder, pack_name(number))
            if not os.path.isfile(path):
                raise PackError(path + " is missing")
            if os.path.getsize(path) < self.pack_sizes[number]:
                raise PackError("%s is cut short (%d bytes of %d)" % (path, os.path.getsize(path), self.pack_sizes[number]))
            file = open(path, "rb")
            header = file.read(PACK_HEADER.size)
            if len(header) != PACK_HEADER.size or header[:8] != PACK_MAGIC:
                file.close()
                raise PackError(path + " is not an HD pack file")
            _, version, set_id, pack_number, _ = PACK_HEADER.unpack(header)
            if version != VERSION or set_id != self.set_id or pack_number != number:
                file.close()
                raise PackError(path + " belongs to another index (the packs and index.hdx must be copied together)")
            self.files[number] = file
        return file

    def read_blob(self, blob):
        pack, offset, length, crc = self.blobs[blob]
        file = self.pack_file(pack)
        file.seek(offset)
        data = file.read(length)
        if len(data) != length or zlib.crc32(data) != crc:
            raise PackError("%s is damaged at %d (%d bytes): its checksum is wrong" % (pack_name(pack), offset, length))
        return data

    def stored_bytes(self):
        return sum(length for _, _, length, _ in self.blobs)

    def named_bytes(self):
        return sum(self.blobs[self.blob_of[name]][2] for name in self.names)


def write_index(folder, set_id, pack_sizes, blobs, names):
    """`blobs` is a list of (pack, offset, length, crc); `names` a list of (name bytes, blob number)."""
    names = sorted(names)
    body = bytearray()
    body += struct.pack("<%dI" % len(pack_sizes), *pack_sizes)
    for blob in blobs:
        body += BLOB.pack(*blob)
    name_bytes = bytearray()
    for name, blob in names:
        body += NAME.pack(len(name_bytes), blob)
        name_bytes += name + b"\0"
    body += name_bytes
    if len(body) >= MAX_PACK_BYTES:
        raise PackError("too many names for one index")
    header = struct.pack("<8s6I", INDEX_MAGIC, VERSION, set_id, len(pack_sizes), len(blobs), len(names), len(name_bytes))
    header += struct.pack("<I", zlib.crc32(body))
    header += struct.pack("<I", zlib.crc32(header))
    path = os.path.join(folder, INDEX_NAME)
    # Written beside it and moved over it whole: the game never sees half an index.
    with open(path + ".tmp", "wb") as file:
        file.write(header)
        file.write(body)
        file.flush()
        os.fsync(file.fileno())
    os.replace(path + ".tmp", path)


# ---------------------------------------------------------------------------------------------------------------------
# Making a pack


class Journal:
    """
    What a run has put in the pack files so far, a line each, so that a stopped run carries on where it was and a later
    run only adds what is new:

        H 1 <set id>
        B <sha-256> <pack> <offset> <length> <crc-32>          a blob in a pack file
        N <size> <modified, ns> <sha-256> <name>               a source file and the blob that has its bytes

    A line is only written after the bytes it tells of are safely in their pack file.
    """

    def __init__(self, folder):
        self.path = os.path.join(folder, JOURNAL_NAME)
        self.set_id = None
        self.blobs = {}  # sha -> (pack, offset, length, crc)
        self.names = {}  # name -> (size, mtime, sha)
        self.file = None

    def load(self, folder):
        """Reads what is there, leaving out whatever the pack files don't bear out. True if there was a journal."""
        if not os.path.isfile(self.path):
            return False
        sizes = {}

        def size_of(pack):
            if pack not in sizes:
                path = os.path.join(folder, pack_name(pack))
                sizes[pack] = os.path.getsize(path) if os.path.isfile(path) else 0
            return sizes[pack]

        data = read_whole(self.path)
        lines = data.split(b"\n")
        # A last line with no end was being written when the run stopped.
        lines.pop()
        for line in lines:
            try:
                if line.startswith(b"H "):
                    kind, version, set_id = line.split(b" ")
                    if int(version) != VERSION:
                        raise PackError("%s was written by another version of this tool; start again with --fresh" % self.path)
                    self.set_id = int(set_id)
                elif line.startswith(b"B "):
                    kind, sha, pack, offset, length, crc = line.split(b" ")
                    blob = (int(pack), int(offset), int(length), int(crc))
                    if blob[1] + blob[2] <= size_of(blob[0]):
                        self.blobs[sha.decode("ascii")] = blob
                elif line.startswith(b"N "):
                    kind, size, mtime, sha, name = line.split(b" ", 4)
                    sha = sha.decode("ascii")
                    if sha in self.blobs:
                        self.names[name] = (int(size), int(mtime), sha)
                    else:
                        self.names.pop(name, None)
            except ValueError:
                raise PackError("%s has a line that cannot be read; start again with --fresh" % self.path)
        if self.set_id is None:
            raise PackError("%s has no first line; start again with --fresh" % self.path)
        return True

    def open(self):
        new = not os.path.isfile(self.path)
        if not new:
            # Anything after the last whole line is cut off before more is added.
            data = read_whole(self.path)
            whole = data.rfind(b"\n") + 1
            if whole != len(data):
                with open(self.path, "r+b") as file:
                    file.truncate(whole)
        self.file = open(self.path, "ab")
        if new:
            self.file.write(b"H %d %d\n" % (VERSION, self.set_id))
            self.sync()

    def write(self, lines):
        self.file.write(b"".join(lines))
        self.sync()

    def sync(self):
        self.file.flush()
        os.fsync(self.file.fileno())

    def close(self):
        if self.file is not None:
            self.file.close()
            self.file = None


class PackWriter:
    """Adds blobs to the end of the pack files, starting a new file when the next blob would pass the size limit."""

    def __init__(self, folder, set_id, limit, blobs):
        self.folder = folder
        self.set_id = set_id
        self.limit = limit
        self.file = None
        self.number = -1
        self.dirty = False
        # Where each pack file's kept bytes end: what lies after was written past the last checkpoint and is cut off.
        self.ends = {}
        for pack, offset, length, _ in blobs:
            self.ends[pack] = max(self.ends.get(pack, PACK_HEADER.size), offset + length)
        self.count = max(self.ends) + 1 if self.ends else 0
        for number in range(self.count):
            self.ends.setdefault(number, PACK_HEADER.size)
            path = os.path.join(folder, pack_name(number))
            if not os.path.isfile(path) or os.path.getsize(path) < PACK_HEADER.size:
                self.start_file(number)
        number = self.count
        while os.path.isfile(os.path.join(folder, pack_name(number))):
            # A pack file begun after the last checkpoint: nothing in it is known, it is begun again when needed.
            number += 1
        self.stale = range(self.count, number)
        if self.count > 0:
            self.open(self.count - 1)

    def start_file(self, number):
        with open(os.path.join(self.folder, pack_name(number)), "wb") as file:
            file.write(PACK_HEADER.pack(PACK_MAGIC, VERSION, self.set_id, number, 0))
        self.ends[number] = PACK_HEADER.size

    def open(self, number):
        if self.file is not None:
            self.sync()
            self.file.close()
        path = os.path.join(self.folder, pack_name(number))
        self.file = open(path, "r+b")
        header = self.file.read(PACK_HEADER.size)
        if header != PACK_HEADER.pack(PACK_MAGIC, VERSION, self.set_id, number, 0):
            self.file.close()
            self.file = None
            raise PackError("%s does not belong to this run's journal; start again with --fresh, or pack into an empty folder" % path)
        self.file.truncate(self.ends[number])
        self.file.seek(self.ends[number])
        self.number = number

    def add(self, data):
        """Where the bytes were put: (pack, offset)."""
        if len(data) + PACK_HEADER.size > self.limit:
            raise PackError("a file of %d bytes does not fit in a pack file of %d" % (len(data), self.limit))
        if self.file is None or self.ends[self.number] + len(data) > self.limit:
            self.start_file(self.count)
            self.count += 1
            self.open(self.count - 1)
        offset = self.ends[self.number]
        self.file.write(data)
        self.ends[self.number] = offset + len(data)
        self.dirty = True
        return self.number, offset

    def sync(self):
        if self.file is not None and self.dirty:
            self.file.flush()
            os.fsync(self.file.fileno())
            self.dirty = False

    def close(self):
        if self.file is not None:
            self.sync()
            self.file.close()
            self.file = None

    def sizes(self):
        return [self.ends[number] for number in range(self.count)]


def command_pack(args):
    source, out = args.hd_folder, args.pack_folder
    limit = int(args.pack_size_mb * 1048576)
    if limit > MAX_PACK_BYTES or limit < 4096:
        raise PackError("--pack-size-mb must be under 2048, and not tiny")
    started = time.monotonic()
    say("Looking through %s" % source)
    entries = scan_tree(source)
    total_bytes = sum(entry[2] for entry in entries)
    say("  %d files, %s" % (len(entries), megabytes(total_bytes)))
    if not entries:
        raise PackError("there is nothing to pack in " + source)
    inside = os.path.relpath(os.path.abspath(out), os.path.abspath(source)) if os.path.splitdrive(os.path.abspath(out))[0].lower() == os.path.splitdrive(os.path.abspath(source))[0].lower() else ".."
    if inside != "." and not inside.startswith(".."):
        raise PackError("the pack folder must not be a folder inside the hd folder: use the hd folder itself, or somewhere outside it")
    os.makedirs(out, exist_ok=True)

    journal = Journal(out)
    if args.fresh:
        # Only this tool's own files are ever removed, and only on this say-so.
        for file in os.listdir(out):
            if is_pack_output(file):
                os.remove(os.path.join(out, file))
    resumed = journal.load(out)
    if not resumed:
        if any(is_pack_output(file) for file in os.listdir(out)):
            raise PackError("%s has pack files but no %s to carry on from; pack into an empty folder, or start again with --fresh" % (out, JOURNAL_NAME))
        journal.set_id = int.from_bytes(os.urandom(4), "little")
    else:
        say("Carrying on: %d files and %d different contents are in the pack files already" % (len(journal.names), len(journal.blobs)))
    writer = PackWriter(out, journal.set_id, limit, journal.blobs.values())
    journal.open()

    # Files whose size and time are as the journal has them are in already; the rest are read.
    todo = []
    for entry in entries:
        known = journal.names.get(entry[0])
        if known is None or known[0] != entry[2] or known[1] != entry[3]:
            todo.append(entry)
    done_before = len(entries) - len(todo)
    gone = set()
    pending = []
    pending_bytes = 0
    read_bytes = 0
    added_bytes = 0
    last_checkpoint = last_progress = time.monotonic()

    def checkpoint():
        nonlocal pending, pending_bytes, last_checkpoint
        writer.sync()
        if pending:
            journal.write(pending)
        pending = []
        pending_bytes = 0
        last_checkpoint = time.monotonic()

    try:
        for number, (entry, (data, sha)) in enumerate(read_ahead(todo, args.jobs, read_and_hash), 1):
            name = entry[0]
            if data is None:
                say("  gone since the folder was looked through, left out: " + entry[1])
                gone.add(name)
                continue
            read_bytes += len(data)
            blob = journal.blobs.get(sha)
            if blob is not None and blob[2] != len(data):
                raise PackError("two different files have the same SHA-256, which should never be: " + entry[1])
            if blob is None:
                crc = zlib.crc32(data)
                pack, offset = writer.add(data)
                blob = (pack, offset, len(data), crc)
                journal.blobs[sha] = blob
                pending.append(b"B %s %d %d %d %d\n" % (sha.encode("ascii"), pack, offset, len(data), crc))
                pending_bytes += len(data)
                added_bytes += len(data)
            # The size and time are those seen before the file was read: had it changed meanwhile, the next run reads it again.
            journal.names[name] = (entry[2], entry[3], sha)
            pending.append(b"N %d %d %s %s\n" % (entry[2], entry[3], sha.encode("ascii"), name))
            now = time.monotonic()
            if pending_bytes >= CHECKPOINT_BYTES or now - last_checkpoint >= CHECKPOINT_SECONDS:
                checkpoint()
            if now - last_progress >= PROGRESS_SECONDS:
                last_progress = now
                say("  %d of %d files read (%s), %s added to the pack files, %d s" % (done_before + number, len(entries), megabytes(read_bytes), megabytes(added_bytes), now - started))
        checkpoint()
    except BaseException:
        # Stopped (Ctrl+C, a full disk, a file that cannot be read): what is safely written is kept for the next run.
        try:
            checkpoint()
        except Exception:
            pass
        writer.close()
        journal.close()
        say("Stopped. Run the same command again to carry on from here.")
        raise
    writer.close()
    journal.close()

    # The index names the files that are in the folder now; blobs only files of earlier runs had are left out of it.
    entries = [entry for entry in entries if entry[0] not in gone]
    total_bytes = sum(entry[2] for entry in entries)
    shas = sorted({journal.names[entry[0]][2] for entry in entries}, key=lambda sha: journal.blobs[sha])
    numbers = {sha: number for number, sha in enumerate(shas)}
    blobs = [journal.blobs[sha] for sha in shas]
    names = [(entry[0], numbers[journal.names[entry[0]][2]]) for entry in entries]
    sizes = writer.sizes()
    write_index(out, journal.set_id, sizes, blobs, names)
    for number in writer.stale:
        if number >= writer.count:
            os.remove(os.path.join(out, pack_name(number)))

    stored = sum(blob[2] for blob in blobs)
    on_disk = sum(sizes) + os.path.getsize(os.path.join(out, INDEX_NAME))
    say("")
    say("Done in %d s." % (time.monotonic() - started))
    say("  files:                 %d (%s)" % (len(entries), megabytes(total_bytes)))
    say("  different contents:    %d (%s)" % (len(blobs), megabytes(stored)))
    say("  duplicate files:       %d (%s not stored twice)" % (len(entries) - len(blobs), megabytes(total_bytes - stored)))
    say("  read this run:         %d files; %d were in the pack files already" % (len(todo), done_before))
    say("  pack files:            %d, with the index %s" % (len(sizes), megabytes(on_disk)))
    unused = sum(sizes) - len(sizes) * PACK_HEADER.size - stored
    if unused > 0:
        say("  no longer used:        %s in the pack files belongs to files changed or removed since; --fresh packs again without it" % megabytes(unused))
    say("  before: %d bytes in %d files    after: %d bytes in %d files" % (total_bytes, len(entries), on_disk, len(sizes) + 1))
    say("Now check it:  python hd_pack.py verify \"%s\" \"%s\"" % (source, out))
    return 0


# ---------------------------------------------------------------------------------------------------------------------
# Checking, listing, unpacking


def command_verify(args):
    with Pack(args.pack_folder) as pack:
        return verify_pack(pack, args)


def verify_pack(pack, args):
    say("Looking through %s" % args.hd_folder)
    entries = scan_tree(args.hd_folder)
    say("  %d files; the pack has %d names" % (len(entries), len(pack.names)))
    wrong = 0
    missing = 0
    checked_bytes = 0
    started = last = time.monotonic()

    def problem(text):
        if wrong + missing <= 50:
            say("  " + text)

    for number, (entry, data) in enumerate(read_ahead(entries, args.jobs, lambda entry: read_whole(entry[1])), 1):
        blob = pack.blob_of.get(entry[0])
        if blob is None:
            missing += 1
            problem("NOT IN THE PACK: " + entry[1])
        else:
            try:
                same = pack.read_blob(blob) == data
            except PackError as error:
                same = False
                problem(str(error))
            if not same:
                wrong += 1
                problem("DIFFERENT IN THE PACK: " + entry[1])
        checked_bytes += len(data)
        if time.monotonic() - last >= PROGRESS_SECONDS:
            last = time.monotonic()
            say("  %d of %d files compared (%s), %d s" % (number, len(entries), megabytes(checked_bytes), last - started))
    loose = {entry[0] for entry in entries}
    extra = sum(1 for name in pack.names if name not in loose)
    say("")
    say("  files compared:               %d (%s)" % (len(entries), megabytes(checked_bytes)))
    say("  the same in the pack:         %d" % (len(entries) - wrong - missing))
    say("  different in the pack:        %d" % wrong)
    say("  not in the pack:              %d" % missing)
    say("  in the pack, no loose file:   %d" % extra)
    if wrong or missing:
        say("FAILED: the pack does not match the folder. Run `pack` again to bring it up to date, then verify again.")
        return 1
    say("OK: every loose file comes out of the pack byte for byte the same.")
    return 0


def command_check(args):
    with Pack(args.pack_folder) as pack:
        return check_pack(pack, args)


def check_pack(pack, args):
    bad = 0
    checked_bytes = 0
    started = last = time.monotonic()
    for number in range(len(pack.blobs)):
        try:
            checked_bytes += len(pack.read_blob(number))
        except PackError as error:
            bad += 1
            if bad <= 50:
                say("  " + str(error))
        if time.monotonic() - last >= PROGRESS_SECONDS:
            last = time.monotonic()
            say("  %d of %d contents checked (%s), %d s" % (number + 1, len(pack.blobs), megabytes(checked_bytes), last - started))
    say("  names: %d   different contents: %d (%s)   pack files: %d" % (len(pack.names), len(pack.blobs), megabytes(checked_bytes), len(pack.pack_sizes)))
    if bad:
        say("FAILED: %d contents are damaged or missing. Copy the pack files again." % bad)
        return 1
    say("OK: every content in the pack has its right checksum.")
    return 0


def command_list(args):
    with Pack(args.pack_folder) as pack:
        return list_pack(pack, args)


def list_pack(pack, args):
    out = sys.stdout
    for name in pack.names:
        number, offset, length, _ = pack.blobs[pack.blob_of[name]]
        out.write("%10d  %s@%-10d  %s\n" % (length, pack_name(number), offset, name.decode("utf-8", "replace")))
    out.flush()
    stored = pack.stored_bytes()
    named = pack.named_bytes()
    say("%d files (%s), %d different contents (%s), %d duplicate files, %d pack files" % (len(pack.names), megabytes(named), len(pack.blobs), megabytes(stored), len(pack.names) - len(pack.blobs), len(pack.pack_sizes)))
    return 0


def command_unpack(args):
    with Pack(args.pack_folder) as pack:
        return unpack_pack(pack, args)


def unpack_pack(pack, args):
    out = args.out_folder
    written = same = 0
    written_bytes = 0
    started = last = time.monotonic()
    # In the order the contents lie in the pack files, which reads them front to back.
    for number, name in enumerate(sorted(pack.names, key=lambda name: pack.blobs[pack.blob_of[name]][:2]), 1):
        parts = name.decode("utf-8").split("\\")
        if any(part in ("", ".", "..") or ":" in part or "/" in part for part in parts):
            raise PackError("the pack has a name that is not a plain path under the folder: %r" % name)
        path = os.path.join(out, *parts)
        data = pack.read_blob(pack.blob_of[name])
        if os.path.exists(path):
            if read_whole(path) == data:
                same += 1
                continue
            if not args.force:
                raise PackError("%s is there already and is different; nothing is written over without --force" % path)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as file:
            file.write(data)
        written += 1
        written_bytes += len(data)
        if time.monotonic() - last >= PROGRESS_SECONDS:
            last = time.monotonic()
            say("  %d of %d files written (%s), %d s" % (number, len(pack.names), megabytes(written_bytes), last - started))
    say("Done: %d files written (%s), %d were there already and the same." % (written, megabytes(written_bytes), same))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="HD pack files for POE2 Mashup: make, check, list and unpack them.")
    commands = parser.add_subparsers(dest="command", required=True)
    pack = commands.add_parser("pack", help="make the pack files from a folder of loose HD art (carries on if it was stopped; adds only what is new if run again)")
    pack.add_argument("hd_folder", help="the folder of loose art, e.g. assets\\hd (only read, never changed)")
    pack.add_argument("pack_folder", help="where index.hdx and pack_NNN.hdp are written")
    pack.add_argument("--pack-size-mb", type=float, default=DEFAULT_PACK_MB, help="the most one pack file may hold (default %d, must be under 2048)" % DEFAULT_PACK_MB)
    pack.add_argument("--jobs", type=int, default=4, help="how many files are read at a time (default 4)")
    pack.add_argument("--fresh", action="store_true", help="remove the pack files, index and journal in the pack folder first and start from nothing")
    pack.set_defaults(run=command_pack)
    verify = commands.add_parser("verify", help="read every loose file again and check the pack gives the same bytes")
    verify.add_argument("hd_folder")
    verify.add_argument("pack_folder")
    verify.add_argument("--jobs", type=int, default=4)
    verify.set_defaults(run=command_verify)
    check = commands.add_parser("check", help="check a pack against its own checksums, with no loose files needed")
    check.add_argument("pack_folder")
    check.set_defaults(run=command_check)
    lister = commands.add_parser("list", help="the names in a pack, with their sizes and places")
    lister.add_argument("pack_folder")
    lister.set_defaults(run=command_list)
    unpack = commands.add_parser("unpack", help="write the pack out as loose files, byte for byte as they were packed")
    unpack.add_argument("pack_folder")
    unpack.add_argument("out_folder")
    unpack.add_argument("--force", action="store_true", help="write over files that are there already and different")
    unpack.set_defaults(run=command_unpack)
    args = parser.parse_args(argv)
    for stream in (sys.stdout, sys.stderr):
        # A name the console cannot show is shown with a ? in it, not as an error.
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")
    try:
        return args.run(args)
    except PackError as error:
        say("ERROR: %s" % error)
        return 2
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
