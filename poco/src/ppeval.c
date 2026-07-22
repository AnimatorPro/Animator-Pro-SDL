/*****************************************************************************
 * ppeval.c - limited expression evaluator for preprocessor #if statements
 *
 * MAINTENANCE
 *	10/19/90	(Ian)
 *				Fixed order-of-evaluation glitches for && and || in
 *				pp_log_or(pcb) and pp_log_and(pcb) routines.
 ****************************************************************************/

#include "poco.h"
#include <stdio.h>
#include <ctype.h>
#include "token.h"

static long pp_exp(Poco_cb* pcb);
extern void pp_say_fatal(Poco_cb* pcb, char* fmt, ...);

static bool pp_token(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
	if (pcb->pp_eval_reuse) {
		return false == (pcb->pp_eval_reuse = false); /* ie, return TRUE */
	} else {
		if (pcb->pp_eval_lbuf == NULL) {
			return (false);
		}
		return (NULL != (pcb->pp_eval_lbuf = tokenize_word(pcb->pp_eval_lbuf, pcb->pp_eval_tok,
														   NULL, NULL, &pcb->pp_eval_ttype, true)));
	}
}

static long pp_atom(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
	long ret;

	if (!pp_token(pcb)) {
		return 0;
	}
	if (pcb->pp_eval_ttype == TOK_LPAREN) {
		ret = pp_exp(pcb);
		if (!pp_token(pcb) || pcb->pp_eval_ttype != TOK_RPAREN) {
			pp_say_fatal(pcb, "missing )");
			PO_CHECK_ABORT(pcb, 0);
		}
		return ret;
	} else if (isdigit(pcb->pp_eval_tok[0])) {
		if (pcb->pp_eval_tok[0] == '0' && pcb->pp_eval_tok[1] == 'X') {
			return htol(pcb->pp_eval_tok);
		} else {
			return atol(pcb->pp_eval_tok);
		}
	} else {
		return 0;
	}
}

static long pp_not(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
	if (!pp_token(pcb)) {
		return 0;
	}
	if (pcb->pp_eval_ttype == '!') {
		return !pp_atom(pcb);
	} else if (pcb->pp_eval_ttype == '~') {
		return ~pp_atom(pcb);
	} else if (pcb->pp_eval_ttype == '-') {
		return -pp_atom(pcb);
	} else {
		pcb->pp_eval_reuse = 1;
		return pp_atom(pcb);
	}
}

static long pp_multiply(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
	long ret;
	long tmp;

	ret = pp_not(pcb);
	for (;;) {
		if (pp_token(pcb)) {
			if (pcb->pp_eval_ttype == '/') {
				if (0 == (tmp = pp_not(pcb))) {
					ret = 0;
				} else {
					ret /= tmp;
				}
			} else if (pcb->pp_eval_ttype == '%') {
				if (0 == (tmp = pp_not(pcb))) {
					ret = 0;
				} else {
					ret %= tmp;
				}
			} else if (pcb->pp_eval_ttype == '*') {
				ret *= pp_not(pcb);
			} else {
				pcb->pp_eval_reuse = true;
				return ret;
			}
		} else {
			return ret;
		}
	}
}

static long pp_plus(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
	long ret;

	ret = pp_multiply(pcb);
	for (;;) {
		if (pp_token(pcb)) {
			if (pcb->pp_eval_ttype == '+') {
				ret += pp_multiply(pcb);
			} else if (pcb->pp_eval_ttype == '-') {
				ret -= pp_multiply(pcb);
			} else {
				pcb->pp_eval_reuse = 1;
				return ret;
			}
		} else {
			return ret;
		}
	}
}


static long pp_shift(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
	long ret;

	ret = pp_plus(pcb);
	for (;;) {
		if (pp_token(pcb)) {
			if (pcb->pp_eval_ttype == TOK_RSHIFT) {
				ret >>= pp_plus(pcb);
			} else if (pcb->pp_eval_ttype == TOK_LSHIFT) {
				ret <<= pp_plus(pcb);
			} else {
				pcb->pp_eval_reuse = 1;
				return ret;
			}
		} else {
			return ret;
		}
	}
}

static long pp_compare(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
	long ret;

	ret = pp_shift(pcb);
	for (;;) {
		if (pp_token(pcb)) {
			if (pcb->pp_eval_ttype == '<') {
				ret = (ret < pp_shift(pcb));
			} else if (pcb->pp_eval_ttype == TOK_LE) {
				ret = (ret <= pp_shift(pcb));
			} else if (pcb->pp_eval_ttype == '>') {
				ret = (ret > pp_shift(pcb));
			} else if (pcb->pp_eval_ttype == TOK_GE) {
				ret = (ret >= pp_shift(pcb));
			} else {
				pcb->pp_eval_reuse = 1;
				return ret;
			}
		} else {
			return ret;
		}
	}
}

static long pp_equality(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
	long ret;

	ret = pp_compare(pcb);
	for (;;) {
		if (pp_token(pcb)) {
			if (pcb->pp_eval_ttype == TOK_NE) {
				ret = (ret != pp_compare(pcb));
			} else if (pcb->pp_eval_ttype == TOK_EQ) {
				ret = (ret == pp_compare(pcb));
			} else {
				pcb->pp_eval_reuse = 1;
				return ret;
			}
		} else {
			return ret;
		}
	}
}

static long pp_bit_and(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
	long ret;

	ret = pp_equality(pcb);
	for (;;) {
		if (pp_token(pcb)) {
			if (pcb->pp_eval_ttype == '&') {
				ret &= pp_equality(pcb);
			} else {
				pcb->pp_eval_reuse = 1;
				return ret;
			}
		} else {
			return ret;
		}
	}
}

static long pp_bit_xor(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
	long ret;

	ret = pp_bit_and(pcb);
	for (;;) {
		if (pp_token(pcb)) {
			if (pcb->pp_eval_ttype == '^') {
				ret ^= pp_bit_and(pcb);
			} else {
				pcb->pp_eval_reuse = 1;
				return ret;
			}
		} else {
			return ret;
		}
	}
}

static long pp_bit_or(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
	long ret;

	ret = pp_bit_xor(pcb);
	for (;;) {
		if (pp_token(pcb)) {
			if (pcb->pp_eval_ttype == '|') {
				ret |= pp_bit_xor(pcb);
			} else {
				pcb->pp_eval_reuse = 1;
				return ret;
			}
		} else {
			return ret;
		}
	}
}

static long pp_log_and(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
	long ret;

	ret = pp_bit_or(pcb);
	for (;;) {
		if (pp_token(pcb)) {
			if (pcb->pp_eval_ttype == TOK_LAND) {
				ret = pp_bit_or(pcb) && ret;
			} else {
				pcb->pp_eval_reuse = 1;
				return ret;
			}
		} else {
			return ret;
		}
	}
}


static long pp_log_or(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
	long ret;

	ret = pp_log_and(pcb);
	for (;;) {
		if (pp_token(pcb)) {
			if (pcb->pp_eval_ttype == TOK_LOR) {
				ret = pp_log_and(pcb) || ret;
			} else {
				pcb->pp_eval_reuse = 1;
				return ret;
			}
		} else {
			return ret;
		}
	}
}

static long pp_exp(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
	return (pp_log_or(pcb));
}

long po_pp_eval(Poco_cb* pcb, char* line, char* buf)
/*****************************************************************************
 *
 ****************************************************************************/
{
	/* null expression evaluates to zero  */
	if (NULL == tokenize_word(line, buf, NULL, NULL, &pcb->pp_eval_ttype, true)) {
		return 0;
	}

	pcb->pp_eval_reuse = 0;
	pcb->pp_eval_lbuf = line;
	pcb->pp_eval_tok = buf;
	return (pp_exp(pcb));
}
