/* token.h - C-tokenizer definitions used by Animator's Softmenu parser.
 *
 * Poco has its own private tokenizer under poco/src.  This header retains
 * the token values Softmenu needs without importing Poco into Animator. */

#ifndef ANIMATOR_TOKEN_H
#define ANIMATOR_TOKEN_H

#include <stdlib.h>
#include "stdtypes.h"

typedef enum softmenu_token_t {
	TOK_LBRACE = '{',
	TOK_RBRACE = '}',
	TOK_HIASC = 257,
	TOK_EOF,
	TOK_INT,
	TOK_QUO,
	TOK_UNDEF,
	/* Preserve the original Poco token values for Softmenu's stored tokens. */
	TOK_SQUO = 270,
	TOK_LONG = 287,
	TOK_DOUBLE = 288
} Token_t;

long softmenu_htol(const char* text);
char* softmenu_tokenize_word(char* line, char* word, SHORT* length, SHORT* token_type,
							 bool preserve_quotes);

#endif /* ANIMATOR_TOKEN_H */
