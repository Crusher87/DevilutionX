#!/usr/bin/env python3
"""Tests of tools/hd_pack.py: pack -> verify -> unpack gives the same files, a stopped run carries on, damage is found.

    python tools/hd_pack_test.py

Makes its files in a temporary folder and removes them. Only Python 3's own library is used.
"""

import filecmp
import io
import os
import random
import shutil
import struct
import sys
import tempfile
import unittest
import zlib
from contextlib import redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hd_pack  # noqa: E402


def run(*args):
    """The tool's exit code and what it printed."""
    out = io.StringIO()
    with redirect_stdout(out):
        code = hd_pack.main(list(args))
    return code, out.getvalue()


def png_like(rng, size):
    """Bytes that begin like a PNG; the tool never looks inside."""
    return b"\x89PNG\r\n\x1a\n" + bytes(rng.getrandbits(8) for _ in range(size))


def make_tree(root, seed=1):
    """A small hd folder with duplicates, an empty file, mixed-case names and a text file. Returns {relative path: bytes}."""
    rng = random.Random(seed)
    files = {}
    shared = png_like(rng, 700)
    for direction in range(8):
        for frame in range(6):
            # Every third frame is the same picture, as a standing monster's are.
            data = shared if frame % 3 == 0 else png_like(rng, 300 + rng.randrange(900))
            files["monsters/sking/skinga/d%d_f%02d.png" % (direction, frame)] = data
    for tile in range(40):
        files["tiles/l1/%d.png" % tile] = png_like(rng, 2000) if tile % 4 else shared
    files["tiles/l1/@trn/red/1.png"] = files["tiles/l1/1.png"]
    files["Mixed/Case/Name.PNG"] = png_like(rng, 123)
    files["pack.txt"] = b"hd pack\r\n"
    files["empty.png"] = b""
    files["name with spaces.png"] = png_like(rng, 50)
    for relative, data in files.items():
        path = os.path.join(root, *relative.split("/"))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as file:
            file.write(data)
    return files


def tree_bytes(root):
    found = {}
    for folder, _, files in os.walk(root):
        for file in files:
            path = os.path.join(folder, file)
            with open(path, "rb") as handle:
                found[os.path.relpath(path, root).replace(os.sep, "/")] = handle.read()
    return found


def same_trees(a, b):
    compared = filecmp.dircmp(a, b)
    if compared.left_only or compared.right_only or compared.funny_files:
        return False
    _, mismatch, errors = filecmp.cmpfiles(a, b, compared.common_files, shallow=False)
    if mismatch or errors:
        return False
    return all(same_trees(os.path.join(a, sub), os.path.join(b, sub)) for sub in compared.common_dirs)


class HdPackTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.mkdtemp(prefix="hd_pack_test_")
        self.hd = os.path.join(self.temp, "hd")
        self.packs = os.path.join(self.temp, "packs")
        self.files = make_tree(self.hd)

    def tearDown(self):
        shutil.rmtree(self.temp, ignore_errors=True)

    def pack(self, *extra):
        code, out = run("pack", self.hd, self.packs, "--pack-size-mb", "0.02", *extra)
        self.assertEqual(code, 0, out)
        return out

    def test_round_trip(self):
        before = tree_bytes(self.hd)
        out = self.pack()
        self.assertEqual(tree_bytes(self.hd), before, "packing changed the source folder")
        self.assertIn("files:                 %d" % len(self.files), out)
        # The small limit splits the art over several pack files, none above it.
        packs = sorted(name for name in os.listdir(self.packs) if name.endswith(".hdp"))
        self.assertGreater(len(packs), 3)
        for name in packs:
            self.assertLessEqual(os.path.getsize(os.path.join(self.packs, name)), int(0.02 * 1048576))
        code, out = run("verify", self.hd, self.packs)
        self.assertEqual(code, 0, out)
        code, out = run("check", self.packs)
        self.assertEqual(code, 0, out)
        unpacked = os.path.join(self.temp, "unpacked")
        code, out = run("unpack", self.packs, unpacked)
        self.assertEqual(code, 0, out)
        # Names come back in lower case; the bytes are the same.
        self.assertEqual(tree_bytes(unpacked), {name.lower(): data for name, data in self.files.items()})
        lowered = os.path.join(self.temp, "lowered")
        for name, data in self.files.items():
            path = os.path.join(lowered, *name.lower().split("/"))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as file:
                file.write(data)
        self.assertTrue(same_trees(lowered, unpacked))

    def test_identical_files_are_stored_once(self):
        self.pack()
        pack = hd_pack.Pack(self.packs)
        self.assertEqual(len(pack.names), len(self.files))
        self.assertEqual(len(pack.blobs), len(set(self.files.values())))
        self.assertLess(len(pack.blobs), len(pack.names))
        self.assertEqual(pack.stored_bytes(), sum(len(data) for data in set(self.files.values())))
        for name, data in self.files.items():
            self.assertEqual(pack.read_blob(pack.blob_of[hd_pack.stored_name(name)]), data)
        pack.close()
        code, out = run("list", self.packs)
        self.assertEqual(code, 0)
        self.assertIn("tiles\\l1\\@trn\\red\\1.png", out)
        self.assertIn("mixed\\case\\name.png", out)

    def test_a_stopped_run_carries_on(self):
        # The run stops (as a crash or Ctrl+C would stop it) part of the way through.
        real = hd_pack.read_and_hash
        calls = [0]

        def failing(entry):
            calls[0] += 1
            if calls[0] > 30:
                raise OSError("the disk went away")
            return real(entry)

        hd_pack.read_and_hash = failing
        saved = hd_pack.CHECKPOINT_BYTES
        hd_pack.CHECKPOINT_BYTES = 4096
        try:
            with self.assertRaises(OSError):
                run("pack", self.hd, self.packs, "--pack-size-mb", "0.02", "--jobs", "1")
        finally:
            hd_pack.read_and_hash = real
            hd_pack.CHECKPOINT_BYTES = saved
        self.assertFalse(os.path.exists(os.path.join(self.packs, "index.hdx")), "a half-made pack must not look finished")
        # Bytes written after the last checkpoint, and half a line in the journal, as a real crash leaves them.
        with open(os.path.join(self.packs, "hd_pack.journal"), "ab") as file:
            file.write(b"B 0123")
        last = sorted(name for name in os.listdir(self.packs) if name.endswith(".hdp"))[-1]
        with open(os.path.join(self.packs, last), "ab") as file:
            file.write(b"junk after the checkpoint")
        out = self.pack()
        self.assertIn("Carrying on: 30 files", out)
        self.assertIn("read this run:         %d files; 30 were in the pack files already" % (len(self.files) - 30), out)
        code, out = run("verify", self.hd, self.packs)
        self.assertEqual(code, 0, out)
        # Nothing was stored twice for having been stopped.
        pack = hd_pack.Pack(self.packs)
        self.assertEqual(len(pack.blobs), len(set(self.files.values())))
        pack.close()

    def test_a_second_run_adds_only_what_is_new(self):
        self.pack()
        with open(os.path.join(self.hd, "tiles", "l1", "new.png"), "wb") as file:
            file.write(b"\x89PNG new art")
        changed = os.path.join(self.hd, "tiles", "l1", "3.png")
        with open(changed, "wb") as file:
            file.write(b"\x89PNG repainted, and longer than nothing")
        os.remove(os.path.join(self.hd, "tiles", "l1", "5.png"))
        code, out = run("verify", self.hd, self.packs)
        self.assertEqual(code, 1, "a pack behind its folder must not verify")
        out = self.pack()
        self.assertIn("read this run:         2 files", out)
        code, out = run("verify", self.hd, self.packs)
        self.assertEqual(code, 0, out)
        self.assertIn("in the pack, no loose file:   0", out)
        unpacked = os.path.join(self.temp, "unpacked")
        self.assertEqual(run("unpack", self.packs, unpacked)[0], 0)
        self.assertEqual(tree_bytes(unpacked), {name.lower(): data for name, data in tree_bytes(self.hd).items()})

    def test_packing_into_the_hd_folder_itself(self):
        code, out = run("pack", self.hd, self.hd)
        self.assertEqual(code, 0, out)
        # Run again, the pack files are not taken for art.
        code, out = run("pack", self.hd, self.hd)
        self.assertEqual(code, 0, out)
        self.assertIn("read this run:         0 files", out)
        code, out = run("verify", self.hd, self.hd)
        self.assertEqual(code, 0, out)
        pack = hd_pack.Pack(self.hd)
        self.assertEqual(len(pack.names), len(self.files))
        pack.close()
        code, out = run("pack", self.hd, os.path.join(self.hd, "inside"))
        self.assertEqual(code, 2, out)

    def test_damage_is_found(self):
        self.pack()
        first = os.path.join(self.packs, "pack_000.hdp")
        with open(first, "r+b") as file:
            file.seek(200)
            byte = file.read(1)
            file.seek(200)
            file.write(bytes([byte[0] ^ 0x40]))
        code, out = run("check", self.packs)
        self.assertEqual(code, 1, out)
        self.assertIn("damaged", out)
        code, out = run("verify", self.hd, self.packs)
        self.assertEqual(code, 1, out)
        # A pack file cut short, as a copy that stopped half way.
        self.pack("--fresh")
        self.assertEqual(run("check", self.packs)[0], 0)
        with open(first, "r+b") as file:
            file.truncate(os.path.getsize(first) - 10)
        code, out = run("check", self.packs)
        self.assertEqual(code, 1, out)
        self.assertIn("cut short", out)
        # A damaged index is refused whole.
        self.pack("--fresh")
        index = os.path.join(self.packs, "index.hdx")
        with open(index, "r+b") as file:
            file.seek(os.path.getsize(index) - 5)
            file.write(b"X")
        code, out = run("check", self.packs)
        self.assertEqual(code, 2, out)
        self.assertIn("checksum", out)
        # A pack file of another set beside this index.
        self.pack("--fresh")
        other = os.path.join(self.temp, "other")
        self.assertEqual(run("pack", self.hd, other, "--pack-size-mb", "0.02")[0], 0)
        shutil.copyfile(os.path.join(other, "pack_001.hdp"), os.path.join(self.packs, "pack_001.hdp"))
        code, out = run("check", self.packs)
        self.assertEqual(code, 1, out)
        self.assertIn("another index", out)

    def test_unpack_writes_over_nothing(self):
        self.pack()
        unpacked = os.path.join(self.temp, "unpacked")
        os.makedirs(unpacked)
        with open(os.path.join(unpacked, "pack.txt"), "wb") as file:
            file.write(b"mine")
        code, out = run("unpack", self.packs, unpacked)
        self.assertEqual(code, 2, out)
        with open(os.path.join(unpacked, "pack.txt"), "rb") as file:
            self.assertEqual(file.read(), b"mine")
        self.assertEqual(run("unpack", self.packs, unpacked, "--force")[0], 0)
        code, out = run("unpack", self.packs, unpacked)
        self.assertEqual(code, 0, out)
        self.assertIn("0 files written", out)

    def test_the_index_is_as_the_game_reads_it(self):
        self.pack()
        with open(os.path.join(self.packs, "index.hdx"), "rb") as file:
            data = file.read()
        magic, version, set_id, packs, blobs, names, name_bytes, body_crc, header_crc = struct.unpack_from("<8s8I", data)
        self.assertEqual(magic, b"HDPKIDX\0")
        self.assertEqual(version, 1)
        self.assertEqual(header_crc, zlib.crc32(data[:36]))
        self.assertEqual(body_crc, zlib.crc32(data[40:]))
        self.assertEqual(len(data), 40 + packs * 4 + blobs * 16 + names * 8 + name_bytes)
        with open(os.path.join(self.packs, "pack_002.hdp"), "rb") as file:
            self.assertEqual(file.read(24), struct.pack("<8s4I", b"HDPKDAT\0", 1, set_id, 2, 0))


if __name__ == "__main__":
    unittest.main()
