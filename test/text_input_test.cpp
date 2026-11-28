/**
 * Pasting into the console crashed the game (Curtis 2026-11-28): a selection left over from text that was since run or
 * replaced was erased from the new, shorter text, and the copy ran far past the end of the buffer.
 */
#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include "DiabloUI/text_input.hpp"

namespace devilution {
namespace {

struct Input {
	char buffer[32] {};
	TextInputCursorState cursor;
	TextInputState state { TextInputState::Options { .value = buffer, .cursor = &cursor, .maxLength = sizeof(buffer) - 1 } };
};

TEST(TextInputTest, PastingAfterTheSelectedTextWasRunIsSafe)
{
	Input input;
	input.state.type("feel(\"sheen\", 4.5)");
	input.state.setSelectCursorToStart(); // select it all, as when the text is marked to copy it
	ASSERT_FALSE(input.cursor.selection.empty());
	input.state.clear(); // Enter runs the line and empties the input
	input.state.type("echo(0.4)");
	EXPECT_EQ(input.state.value(), "echo(0.4)");
}

TEST(TextInputTest, ReplacingTheTextDropsTheOldSelection)
{
	Input input;
	input.state.type("a long line of text here");
	input.state.setSelectCursorToStart();
	input.state.assign("up"); // history recall
	EXPECT_TRUE(input.cursor.selection.empty());
	input.state.type("!");
	EXPECT_EQ(input.state.value(), "up!");
}

TEST(TextInputTest, AStaleSelectionNeverErasesPastTheEnd)
{
	Input input;
	input.state.type("abc");
	input.cursor.selection.begin = 2;
	input.cursor.selection.end = 30;
	input.state.type("Z");
	EXPECT_EQ(input.state.value(), "abZ");
}

TEST(TextInputTest, APasteLongerThanTheBufferIsCut)
{
	Input input;
	input.state.type(std::string(100, 'x'));
	EXPECT_LE(input.state.value().size(), sizeof(input.buffer) - 1);
	EXPECT_EQ(std::strlen(input.buffer), input.state.value().size());
	input.state.assign(std::string(100, 'y'));
	EXPECT_EQ(std::strlen(input.buffer), input.state.value().size());
}

} // namespace
} // namespace devilution
