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

static long 	pp_exp(Poco_cb* pcb);
extern void 	pp_say_fatal(Poco_cb* pcb, char *fmt, ...);

static char 	*lbuf;
static char 	*tok;
static bool reuse;
static SHORT	ttype;

static char 	missing_rparen[] = "missing )";

static bool pp_token(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
if (reuse)
	return false == (reuse = false);	/* ie, return TRUE */
else
	{
	if (lbuf == NULL)
		return(false);
	return (NULL != (lbuf = tokenize_word(lbuf, tok, NULL, NULL, &ttype, true)));
	}
}

static long pp_atom(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
long ret;

if (!pp_token(pcb))
	return 0;
if (ttype == TOK_LPAREN)
	{
	ret = pp_exp(pcb);
	if (!pp_token(pcb) || ttype != TOK_RPAREN) {
		pp_say_fatal(pcb, missing_rparen);
		PO_CHECK_ABORT(pcb, 0);
	}
	return ret;
	}
else if (isdigit(tok[0]))
	{
	if (tok[0] == '0' && tok[1] == 'X')
		return htol(tok);
	else
		return atol(tok);
	}
else
	{
	return 0;
	}
}

static long pp_not(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
if (!pp_token(pcb))
	return 0;
if (ttype == '!')
	return !pp_atom(pcb);
else if (ttype == '~')
	return ~pp_atom(pcb);
else if (ttype == '-')
	return -pp_atom(pcb);
else
	{
	reuse = 1;
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
for (;;)
	{
	if (pp_token(pcb))
		{
		if (ttype == '/')
			{
			if (0 == (tmp = pp_not(pcb)))
				ret = 0;
			else
				ret /= tmp;
			}
		else if (ttype == '%')
			{
			if (0 == (tmp = pp_not(pcb)))
				ret = 0;
			else
				ret %= tmp;
			}
		else if (ttype == '*')
			ret *= pp_not(pcb);
		else
			{
			reuse = true;
			return ret;
			}
		}
	else
		return ret;
	}
}

static long pp_plus(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
long ret;

ret = pp_multiply(pcb);
for (;;)
	{
	if (pp_token(pcb))
		{
		if (ttype == '+')
			ret += pp_multiply(pcb);
		else if (ttype == '-')
			ret -= pp_multiply(pcb);
		else
			{
			reuse = 1;
			return ret;
			}
		}
	else
		return ret;
	}
}


static long pp_shift(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
long ret;

ret = pp_plus(pcb);
for (;;)
	{
	if (pp_token(pcb))
		{
		if (ttype == TOK_RSHIFT)
			ret >>= pp_plus(pcb);
		else if (ttype == TOK_LSHIFT)
			ret <<= pp_plus(pcb);
		else
			{
			reuse = 1;
			return ret;
			}
		}
	else
		return ret;
	}
}

static long pp_compare(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
long ret;

ret = pp_shift(pcb);
for (;;)
	{
	if (pp_token(pcb))
		{
		if (ttype == '<')
			ret = (ret <  pp_shift(pcb));
		else if (ttype == TOK_LE)
			ret = (ret <= pp_shift(pcb));
		else if (ttype == '>')
			ret = (ret >  pp_shift(pcb));
		else if (ttype == TOK_GE)
			ret = (ret >= pp_shift(pcb));
		else
			{
			reuse = 1;
			return ret;
			}
		}
	else
		return ret;
	}
}

static long pp_equality(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
long ret;

ret = pp_compare(pcb);
for (;;)
	{
	if (pp_token(pcb))
		{
		if (ttype == TOK_NE)
			ret = (ret != pp_compare(pcb));
		else if (ttype == TOK_EQ)
			ret = (ret == pp_compare(pcb));
		else
			{
			reuse = 1;
			return ret;
			}
		}
	else
		return ret;
	}
}

static long pp_bit_and(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
long ret;

ret = pp_equality(pcb);
for (;;)
	{
	if (pp_token(pcb))
		{
		if (ttype == '&')
			ret &= pp_equality(pcb);
		else
			{
			reuse = 1;
			return ret;
			}
		}
	else
		return ret;
	}
}

static long pp_bit_xor(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
long ret;

ret = pp_bit_and(pcb);
for (;;)
	{
	if (pp_token(pcb))
		{
		if (ttype == '^')
			ret ^= pp_bit_and(pcb);
		else
			{
			reuse = 1;
			return ret;
			}
		}
	else
		return ret;
	}
}

static long pp_bit_or(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
long ret;

ret = pp_bit_xor(pcb);
for (;;)
	{
	if (pp_token(pcb))
		{
		if (ttype == '|')
			ret |= pp_bit_xor(pcb);
		else
			{
			reuse = 1;
			return ret;
			}
		}
	else
		return ret;
	}
}

static long pp_log_and(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
long ret;

ret = pp_bit_or(pcb);
for (;;)
	{
	if (pp_token(pcb))
		{
		if (ttype == TOK_LAND)
			{
			ret = pp_bit_or(pcb) && ret;
			}
		else
			{
			reuse = 1;
			return ret;
			}
		}
	else
		return ret;
	}
}


static long pp_log_or(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
long ret;

ret = pp_log_and(pcb);
for (;;)
	{
	if (pp_token(pcb))
		{
		if (ttype == TOK_LOR)
			{
			ret = pp_log_and(pcb) || ret;
			}
		else
			{
			reuse = 1;
			return ret;
			}
		}
	else
		return ret;
	}
}

static long pp_exp(Poco_cb* pcb)
/*****************************************************************************
 *
 ****************************************************************************/
{
return(pp_log_or(pcb));
}

long po_pp_eval(Poco_cb* pcb, char *line, char *buf)
/*****************************************************************************
 *
 ****************************************************************************/
{
/* null expression evaluates to zero  */
if (NULL == tokenize_word(line, buf, NULL, NULL, &ttype, true))
	return 0;

reuse = 0;
lbuf  = line;
tok   = buf;
return(pp_exp(pcb));
}
