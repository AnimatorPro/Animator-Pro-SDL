/*
 * Minimal C-style tokenizer owned by Softmenu.
 *
 * This preserves Softmenu's historical token values and text handling while
 * keeping the Poco compiler tokenizer private to Poco.
 */

#include <ctype.h>

#include "token.h"

#define SOFTMENU_TOKEN_MAX 512
#define SOFTMENU_SYMBOL_MAX 40

static int is_octal(int character)
{
	return character >= '0' && character <= '7';
}

static int is_symbol_first(int character)
{
	return isalpha((unsigned char)character) || character == '_';
}

static int is_symbol(int character)
{
	return isalnum((unsigned char)character) || character == '_';
}

static char* softmenu_chop_symbol(char* line, char* word, int max_length, char** word_next)
{
	while (--max_length && is_symbol(*line)) {
		*word++ = *line++;
	}
	*word_next = word;
	while (is_symbol(*line)) {
		++line;
	}
	return line;
}

long softmenu_htol(const char* text)
{
	long result = 0;
	int character;

	if (text[0] == '0' && text[1] == 'X') {
		text += 2;
	}
	while (isxdigit((unsigned char)(character = *text++))) {
		result <<= 4;
		if (isdigit((unsigned char)character)) {
			result += character - '0';
		} else {
			result += character - 'A' + 10;
		}
	}
	return result;
}

static char translate_escape(char** text)
{
	char* input = *text;
	char character = *input++;
	char result;
	short counter;

	switch (character) {
		case 'a':
			result = '\a';
			break;
		case 'b':
			result = '\b';
			break;
		case 'f':
			result = '\f';
			break;
		case 'n':
			result = '\n';
			break;
		case 'r':
			result = '\r';
			break;
		case 't':
			result = '\t';
			break;
		case 'v':
			result = '\v';
			break;
		case '?':
			result = '\?';
			break;
		case '\\':
			result = '\\';
			break;
		case '\'':
			result = '\'';
			break;
		case '"':
			result = '"';
			break;
		case 'x':
			result = 0;
			character = *input;
			while (isxdigit((unsigned char)character)) {
				if (character <= '9') {
					character -= '0';
				} else if (character <= 'F') {
					character -= 'A' - 10;
				} else {
					character -= 'a' - 10;
				}
				result = (result << 4) | character;
				character = *++input;
			}
			break;
		default:
			if (is_octal(character)) {
				counter = 4;
				result = 0;
				while (--counter) {
					result = (result << 3) | (character & 0x07);
					character = *input;
					if (is_octal(character)) {
						++input;
					} else {
						break;
					}
				}
			} else {
				result = character;
			}
			break;
	}

	*text = input;
	return result;
}

static int get_digits(char* line, char* word, SHORT* token_type)
{
	enum { DECIMAL, HEX, FLOAT } number_type = DECIMAL;

	int count = 0;
	int character;

	*token_type = TOK_INT;
	if (*line == '0') {
		*word++ = *line++;
		++count;
		if ('X' == (character = toupper((unsigned char)*line))) {
			*word++ = (char)character;
			++count;
			++line;
			number_type = HEX;
		}
	}

	for (;;) {
		character = toupper((unsigned char)*line++);
		if (character == 'L') {
			if (number_type != FLOAT) {
				*token_type = TOK_LONG;
			}
		} else if (character == 'U' && number_type != FLOAT) {
			/* Accepted but intentionally ignored. */
		} else if (character == 'F' && number_type != HEX) {
			number_type = FLOAT;
			*token_type = TOK_DOUBLE;
		} else if (character == '.' && number_type == DECIMAL) {
			number_type = FLOAT;
			*token_type = TOK_DOUBLE;
		} else if (character == 'E' && number_type == FLOAT) {
			if (*line == '-') {
				*word++ = (char)character;
				++count;
				character = *line++;
			}
		} else if ((number_type != HEX && !isdigit((unsigned char)character)) ||
				   (number_type == HEX && !isxdigit((unsigned char)character))) {
			break;
		}

		*word++ = (char)character;
		++count;
		if (count > SOFTMENU_SYMBOL_MAX - 2) {
			break;
		}
	}
	return count;
}

char* softmenu_tokenize_word(char* line, char* word, SHORT* length, SHORT* token_type,
							 bool preserve_quotes)
{
	char* work_pointer;
	char* start = word;
	int token_length;
	SHORT type;
	unsigned int character;

	while (isspace((unsigned char)*line)) {
		++line;
	}
	if ('\0' == (character = (unsigned char)*line)) {
		type = TOK_EOF;
		line = NULL;
		goto out;
	}

	if (is_symbol_first((int)character)) {
		line = softmenu_chop_symbol(line, word, SOFTMENU_SYMBOL_MAX - 1, &work_pointer);
		word = work_pointer;
		type = TOK_UNDEF;
	} else if (isdigit((unsigned char)character)) {
		token_length = get_digits(line, word, &type);
		line += token_length;
		word += token_length;
	} else {
		switch (character) {
			case '"':
				type = TOK_QUO;
				if (preserve_quotes) {
					*word++ = '"';
				}
				++line;
				token_length = SOFTMENU_TOKEN_MAX - 3;
				while (--token_length) {
					character = (unsigned char)*line++;
					if (character == 0) {
						break;
					} else if (character == '\\') {
						if (preserve_quotes) {
							*word++ = (char)character;
							*word++ = *line++;
						} else {
							work_pointer = line;
							*word++ = translate_escape(&work_pointer);
							line = work_pointer;
						}
					} else if (character == '"') {
						break;
					} else {
						*word++ = (char)character;
					}
				}
				if (preserve_quotes) {
					*word++ = '"';
				}
				break;
			case '\'':
				if (preserve_quotes) {
					*word++ = '\'';
				}
				type = TOK_SQUO;
				++line;
				for (;;) {
					character = (unsigned char)*line++;
					if (character == 0) {
						break;
					} else if (character == '\\') {
						if (preserve_quotes) {
							*word++ = (char)character;
							*word++ = *line++;
						} else {
							work_pointer = line;
							*word++ = translate_escape(&work_pointer);
							line = work_pointer;
						}
					} else if (character == '\'') {
						break;
					} else {
						*word++ = (char)character;
					}
				}
				if (preserve_quotes) {
					*word++ = '\'';
				}
				break;
			default:
				type = *word++ = (char)character;
				++line;
				break;
		}
	}

out:
	*token_type = type;
	*word = 0;
	if (length != NULL) {
		*length = (SHORT)(word - start);
	}
	return line;
}
