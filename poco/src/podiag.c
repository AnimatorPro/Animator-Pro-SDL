/*******************************************************************************
 * podiag.c - Compiler diagnostics.
 * Formats and reports the compiler's warnings, fatal errors and internal
 * errors, plus the small family of 'expected X, got Y' helpers the parser
 * uses.  Split out of poco.c; the declarations stay in poco_internal.h.
 ******************************************************************************/

#include "poco_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/****** MODULE ERROR error handling messages ********/

/*****************************************************************************
 * po_say_err - Format error message text, including input filename and line #.
 ****************************************************************************/
static void po_say_err(Poco_cb* pcb, char* s)
{
	PreprocessorState* t = &pcb->t;
	File_stack* fs = t->file_stack;
	char errtxtbuf[128];
	long line_num = 0;
	long char_num = 0;

	if (0 == (line_num = pcb->error_line_number)) {
		if (pcb->curtoken != NULL) {
			line_num = pcb->curtoken->line_num;
			char_num = pcb->curtoken->char_num;
		} else if (fs != NULL) {
			line_num = fs->line_count;
			char_num = 0;
		}
		pcb->error_line_number = line_num;
		pcb->error_char_number = char_num;
	}

	if (pcb->global_err == Err_syntax) {
		strcpy(errtxtbuf, "Error");
	} else {
		get_errtext(pcb->global_err, errtxtbuf);
	}

	if (fs == NULL) {
		fprintf(t->err_file, "%s\n%s\n", errtxtbuf, s);
	} else {
		fprintf(t->err_file, "%s in %s near line %ld:\n%s\n", errtxtbuf, fs->name, line_num, s);
	}
}

/*****************************************************************************
 * format & output an error message but don't stop & don't save errline #.
 ****************************************************************************/
void po_say_warning(Poco_cb* pcb, const char* fmt, ...)
{
	char sbuf[512];
	va_list args;

	va_start(args, fmt);
	vsprintf(sbuf, fmt, args);
	va_end(args);

	if (pcb->global_err >= 0) {
		pcb->global_err = Err_syntax;
	}

	po_say_err(pcb, sbuf);
	pcb->error_line_number = 0;  // forget error line #, just a warning
}

/*****************************************************************************
 * format & output an error message, then longjump to error handler.
 ****************************************************************************/
void po_say_fatal(Poco_cb* pcb, const char* fmt, ...)
{
	char sbuf[512];
	va_list args;

	va_start(args, fmt);
	vsprintf(sbuf, fmt, args);
	va_end(args);

	if (pcb->global_err >= 0) {
		pcb->global_err = Err_syntax;
	}

	po_say_err(pcb, sbuf);

	pcb->compile_aborted = true;
	if (pcb->global_err >= 0) {
		pcb->compile_err = Err_syntax;
	} else {
		pcb->compile_err = pcb->global_err;
	}
}

/*****************************************************************************
 * format & output an error message, then longjump to error handler.
 ****************************************************************************/
void po_say_internal(Poco_cb* pcb, const char* fmt, ...)
{
	char sbuf[512];
	va_list args;

	va_start(args, fmt);
	vsprintf(sbuf, fmt, args);
	va_end(args);

	pcb->global_err = Err_poco_internal;
	po_say_fatal(pcb, "poco internal error: %s", sbuf);
	PO_CHECK_ABORT_VOID(pcb);
}

/*****************************************************************************
 * output a message saying we were expecting one token and got another.
 ****************************************************************************/
void po_expecting_got(Poco_cb* pcb, const char* expecting)
{
	po_expecting_got_str(pcb, expecting, pcb->curtoken->ctoke);
}

/*****************************************************************************
 * output a message saying we expecting something and got something else.
 ****************************************************************************/
void po_expecting_got_str(Poco_cb* pcb, const char* expecting, const char* got)
{
	po_say_fatal(pcb, "expecting %s got '%s'", expecting, got);
	PO_CHECK_ABORT_VOID(pcb);
}

/*****************************************************************************
 * get a token, and if it is TOK_EOF, complain and die.
 ****************************************************************************/
bool po_need_token(Poco_cb* pcb)
{
	lookup_token(pcb);
	if (pcb->t.toktype == TOK_EOF) {
		po_say_fatal(pcb, "unexpected end of file");
		PO_CHECK_ABORT(pcb, false);
	}
	return (true);
}

/*****************************************************************************
 * redefined symbol error - complain and die.
 ****************************************************************************/
void po_redefined(Poco_cb* pcb, char* s)
{
	po_say_fatal(pcb, "%s redefined", s);
	PO_CHECK_ABORT_VOID(pcb);
}

/*****************************************************************************
 * undefined symbol error - complain and die.
 ****************************************************************************/
void po_undefined(Poco_cb* pcb, char* s)
{
	po_say_fatal(pcb, "%s undefined", s);
	PO_CHECK_ABORT_VOID(pcb);
}

/*****************************************************************************
 * unmatched parens error - complain and die.
 ****************************************************************************/
void po_unmatched_paren(Poco_cb* pcb)
{
	po_say_fatal(pcb, "unmatched parenthesis");
	PO_CHECK_ABORT_VOID(pcb);
}

/*****************************************************************************
 * we wanted to see an open brace - complain and die.
 ****************************************************************************/
void po_expecting_lbrace(Poco_cb* pcb)
{
	po_expecting_got(pcb, "{");
}

/*****************************************************************************
 * we wanted to see a closing brace - complain and die.
 ****************************************************************************/
void po_expecting_rbrace(Poco_cb* pcb)
{
	po_expecting_got(pcb, "}");
}
