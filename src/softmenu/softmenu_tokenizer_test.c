#include <stdio.h>
#include <string.h>

#include "token.h"

static int expect_token(char* input, const char* expected_word, SHORT expected_type)
{
	char word[512];
	SHORT length = -1;
	SHORT type = -1;
	char* next = softmenu_tokenize_word(input, word, &length, &type, false);

	if (type != expected_type || strcmp(word, expected_word) != 0 ||
		length != (SHORT)strlen(expected_word)) {
		fprintf(stderr, "token mismatch: got type %d word '%s' length %d\n", type, word, length);
		return 1;
	}
	if (next == NULL) {
		fprintf(stderr, "tokenizer stopped unexpectedly\n");
		return 1;
	}
	return 0;
}

int main(void)
{
	char name[] = "  pull_item";
	char number[] = "0x2a";
	char string[] = "\"line\\ntext\"";
	char character[] = "'\\n'";
	char brace[] = "}";

	if (expect_token(name, "pull_item", TOK_UNDEF) != 0 ||
		expect_token(number, "0X2A", TOK_INT) != 0 ||
		expect_token(string, "line\ntext", TOK_QUO) != 0 ||
		expect_token(character, "\n", TOK_SQUO) != 0 || expect_token(brace, "}", TOK_RBRACE) != 0) {
		return 1;
	}

	if (softmenu_htol("2A") != 42 || softmenu_htol("0X2A") != 42) {
		fprintf(stderr, "hex conversion mismatch\n");
		return 1;
	}
	return 0;
}
