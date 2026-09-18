/*******************************************************************************
 * potokface.c - Tokenizer interface.
 * Builds and owns the compiler's token list, resolves identifiers to
 * symbols as tokens are produced, and provides the lookahead and
 * token-eating helpers the recursive descent parser calls.
 * Split out of poco.c.
 ******************************************************************************/

#include "poco_internal.h"
#include "posymbol.h"
#include "potokface.h"
#include "pocmemry.h"
#include "pp.h"
#include <limits.h> /* so we can properly determine max int value */
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <xlocale.h>
#endif

/*
 * Source text always spells floating constants with the C decimal point.
 * atof() instead follows the process-global numeric locale, which made the
 * same graph source compile to a different constant after a host selected a
 * comma-decimal locale.  Use an explicit C locale without mutating ambient
 * process state.  Locale creation is kept per conversion so the compiler does
 * not add another shared mutable singleton.
 */
static double poco_parse_source_double(const char* text)
{
#ifdef _WIN32
	_locale_t c_locale = _create_locale(LC_NUMERIC, "C");
	double value;

	if (c_locale == NULL) {
		return 0.0;
	}
	value = _strtod_l(text, NULL, c_locale);
	_free_locale(c_locale);
	return value;
#else
	locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
	double value;

	if (c_locale == (locale_t)0) {
		return 0.0;
	}
	value = strtod_l(text, NULL, c_locale);
	freelocale(c_locale);
	return value;
#endif
}

/****** MODULE TOKENFACE ******** interface to tokenizer */

/*****************************************************************************
 *
 ****************************************************************************/
static Tstack* new_token(Poco_cb* pcb)
{
	Tstack* t;

	if (NULL == (t = pcb->free_tokens)) {
		t = po_memalloc(pcb, sizeof(*t));
	} else {
		pcb->free_tokens = t->next;
	}
	return t;
}

/*****************************************************************************
 *
 ****************************************************************************/
static void free_token(Poco_cb* pcb, Tstack* t)
{
	t->next = pcb->free_tokens;
	pcb->free_tokens = t;
}

/*****************************************************************************
 *
 ****************************************************************************/
void po_free_token_lists(Poco_cb* pcb)
{
	Tstack* cur;
	Tstack* next;

	for (cur = pcb->curtoken; cur != NULL; cur = next) {
		next = cur->next;
		po_freemem(cur);
	}
	for (cur = pcb->free_tokens; cur != NULL; cur = next) {
		next = cur->next;
		po_freemem(cur);
	}
}

/*****************************************************************************
 * read the next input line and add its tokens to the lookahead list.
 *	items of note...
 *	> we currently guarantee only one token of lookahead.  (that is, you can
 *	  always look at curtoken->next->whatever_field without worrying about a
 *	  NULL next pointer.  if this number is increased, this routine will have
 *	  to be tweaked to loop over multiple input lines, as it is possible to
 *	  get a line that has only one token on it.
 *	> the preprocessor routine po_pp_next_line is now the single source of input
 *	  lines from our point of view.  it will decide whether to get data from
 *	  the list of library prototype lines or from the input file.
 *	> when we hit EOF, we add a couple of TOK_EOF entries to the end of the
 *	  token list.  the number of entries added should be one more than the
 *	  maximum number of lookahead items supported.
 *	> this is the routine responsible for concatenating adjacent string
 *	  literals.  if a line of input ends on a string token, we'll loop to
 *	  read the next line.
 *	> one day we will support reading a pre-tokenized file. this is probably
 *	  the point at which we would decided whether to read tokens from a file
 *	  or to call po_pp_next_line to get more tokens that way.
 ****************************************************************************/
Tstack* po_build_token_list(Poco_cb* pcb)
{
	Tstack* ts;
	Tstack* first_ts;
	Tstack* prev_ts;
	Tstack dummy_ts;
	char* line_pos;
	char* line_start;
	char* strbase;
	char* strwrk;
	long line_count;
	short strlit_len;
	short ctoke_size;
	SHORT token_type;

	ts = new_token(pcb);

	first_ts = ts;
	strbase = pcb->strlit_work;
	strwrk = strbase;
	strlit_len = 0;
	prev_ts = &dummy_ts;
	prev_ts->type = PTOK_MAX + 1;

NEED_MORE:

	line_start = line_pos = po_pp_next_line(pcb);

	if (line_pos == NULL) /* handle EOF */
	{
		ts->type = po_ptoken(TOK_EOF);
		ts->line_num = 0;
		ts->char_num = 0;
		ts->ctoke[0] = '\0';
		prev_ts = ts;
		ts = ts->next = new_token(pcb);
		ts->type = po_ptoken(TOK_EOF);
		ts->line_num = 0;
		ts->char_num = 0;
		ts->ctoke[0] = '\0';
		goto ENDFILE;
	}

	line_count = pcb->t.file_stack->line_count;

	for (;;) {
		ts->is_symbol = false;
		ts->line_num = line_count;

		line_pos = (char*)tokenize_word((UBYTE*)line_pos, (UBYTE*)ts->ctoke, (UBYTE*)strwrk,
										&ctoke_size, &token_type, false);
		if (line_pos == NULL) {
			goto ENDLINE;
		}
		ts->type = (PToken_t)token_type;

		ts->char_num = 1 + ((line_pos - ctoke_size) - line_start);

		if (prev_ts->type == TOK_QUO && ts->type != TOK_QUO) {
			register Names* n;

			n = po_memzalloc(pcb, sizeof(*n) + strlit_len + 1);
			n->name = (char*)(n + 1);
			poco_copy_bytes(strbase, n->name, strlit_len + 1);
			n->next = pcb->run.literals;
			pcb->run.literals = n;
			prev_ts->val.string = n->name;
			prev_ts->type = PTOK_QUO;
			prev_ts->ctoke_size = strlit_len;
			strncpy(prev_ts->ctoke, prev_ts->val.string, MAX_SYM_LEN - 1);
			strlit_len = 0;
			strwrk = strbase;
		}

		ts->ctoke_size = ctoke_size;

		switch (ts->type) {
			case TOK_INT:
			case TOK_LONG:

				if (ts->ctoke[1] == 'X') {
					ts->val.num = htol(ts->ctoke);
				} else {
					ts->val.num = atol(ts->ctoke);
				}

				if (ts->val.num > INT_MAX) {
					ts->type = po_ptoken(TOK_LONG);
				}

				break;

			case TOK_DOUBLE:

				ts->val.dnum = poco_parse_source_double(ts->ctoke);
				break;

			case TOK_SQUO:

				if (ctoke_size > 1) {
					if (ctoke_size > 1) {
						po_say_fatal(pcb, "invalid character constant '%s'", ts->ctoke);
						PO_CHECK_ABORT(pcb, NULL);
					}
				}
				ts->val.num = (unsigned char)(ts->ctoke[0]);
				ts->type = po_ptoken(TOK_INT);
				break;

			case TOK_QUO:

				strwrk = strbase + (strlit_len += ctoke_size);
				if (strlit_len > MAX_STRLIT_LEN) {
					po_say_fatal(pcb, "string literal exceeds length limit of %d characters",
								 MAX_STRLIT_LEN);
				}
				break;

		} /* END switch (type) */

		if (prev_ts->type != TOK_QUO) {
			prev_ts = ts;
			ts = ts->next = new_token(pcb);
		}

	} /* END  for(;;) */

ENDLINE:

	if (prev_ts == &dummy_ts || prev_ts->type == TOK_QUO) {
		goto NEED_MORE;
	}

ENDFILE:

	prev_ts->next = NULL;
	free_token(pcb, ts);

#ifdef POCO_DEBUG_DUMP
	for (ts = first_ts; ts; ts = ts->next) {
		printf("%s ", ts->ctoke);
	}
	printf("\n");
#endif

	return first_ts;
}

/*****************************************************************************
 * return the type of the next token.
 ****************************************************************************/
SHORT po_lookahead_type(Poco_cb* pcb)
{
	Symbol* s;
	Tstack* ts = pcb->curtoken->next;
	SHORT ttype = ts->type;

	if (ttype != TOK_UNDEF) {
		return ttype;
	}

	if (NULL != (s = po_find_symbol(pcb, ts->ctoke))) {
		ttype = s->tok_type;
		if (ttype == PTOK_ENUMCONST) {
			return TOK_INT;
		} else {
			return ttype;
		}
	}
	return TOK_UNDEF;
}

/*****************************************************************************
 * get the next token from tokenize_word, handle symbols and constant values.
 * the lookup_token() function was converted to a #define in poco.h, so that
 * the check for the reuse flag (set by pushback_token) is generated inline.
 * thus, this routine will always get a 'fresh' token from the input file.
 * in addition, this routine used to call next_token(), an interface to
 * the tokenizer that would loop if necessary to get more lines of data for
 * the tokenizer; this looping is now handled here.
 ****************************************************************************/
void po_lookup_freshtoken(Poco_cb* pcb)
{
	Tstack* ts;
	Symbol* s;

	PO_CHECK_ABORT_VOID(pcb);

	if (((char*)&pcb) < pcb->stack_bottom) {
		po_say_fatal(pcb, "stack overflow (statements too deeply nested)");
	}
	PO_CHECK_ABORT_VOID(pcb);

	ts = pcb->curtoken->next;
	free_token(pcb, pcb->curtoken);
	pcb->curtoken = ts;
	if (ts->next == NULL) {
		ts->next = po_build_token_list(pcb);
	}

	if (ts->type == TOK_UNDEF) {
		if (NULL != (s = po_find_symbol(pcb, ts->ctoke))) {
			ts->type = s->tok_type;
			if (ts->type == PTOK_ENUMCONST) {
				ts->val.num = s->symval.i;
				ts->type = po_ptoken(TOK_INT);
			} else {
				ts->val.symbol = s;
				if (ts->type == PTOK_VAR || ts->type == PTOK_LABEL || ts->type == PTOK_UNDEF) {
					ts->is_symbol = true;
				}
			}
		} else {
			s = po_new_symbol(pcb, ts->ctoke);
			ts->type = PTOK_UNDEF;
			ts->val.symbol = s;
			ts->is_symbol = true;
		}
	}
	pcb->t.toktype = ts->type;
	return;
}

/********** MODULE EATSEMI ********************/

/*****************************************************************************
 * indicate whether the next token is the indicated type (ie, look ahead).
 ****************************************************************************/
bool po_is_next_token(Poco_cb* pcb, SHORT ttype)
{
	bool ret = false;

	lookup_token(pcb);
	if (pcb->t.toktype == ttype || pcb->t.toktype == TOK_EOF) {
		ret = true;
	}
	pushback_token(&pcb->t);
	return (ret);
}

/*****************************************************************************
 * if the next token is the right type, eat it, else complain and die.
 ****************************************************************************/
bool po_eat_token(Poco_cb* pcb, SHORT ttype)
{
	char buf[2];

	if (po_need_token(pcb)) {
		if (pcb->t.toktype == ttype) {
			return (true);
		} else {
			buf[0] = ttype;
			buf[1] = 0;
			po_expecting_got(pcb, buf);
		}
	}
	return (false);
}

/*****************************************************************************
 * eat a closing bracket, if wrong token appears next, complain and die.
 ****************************************************************************/
bool po_eat_rbracket(Poco_cb* pcb)
{
	return (po_eat_token(pcb, ']'));
}

/*****************************************************************************
 * eat an opening paren, if wrong token appears next, complain and die.
 ****************************************************************************/
bool po_eat_lparen(Poco_cb* pcb)
{
	return (po_eat_token(pcb, TOK_LPAREN));
}

/*****************************************************************************
 * eat a closing paren, if wrong token appears next, complain and die.
 ****************************************************************************/
bool po_eat_rparen(Poco_cb* pcb)
{
	return (po_eat_token(pcb, TOK_RPAREN));
}
