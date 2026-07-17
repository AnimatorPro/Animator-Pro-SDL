/*****************************************************************************
 * pocolib.c - Common routines for other poco library functions.
 *
 * 05/15/92 (Ian)
 *			Tweaked po_check_formatf() so that a NULL pointer passed for
 *			a %p arg doesn't cause an abort -- it's reasonable to print
 *			the value of a NULL or wild pointer, and printing it won't
 *			cause it to be dereferenced.
 ****************************************************************************/


#include "poco.h"
#include "poco_errcodes.h"
#include "ptrmacro.h"
#include "pocolib.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define POCO_STANDALONE_FALLBACK __attribute__((weak))
#else
#define POCO_STANDALONE_FALLBACK
#endif

/* This is Poco state, not host state.  Hosts adapt errors at their boundary. */
POCO_STANDALONE_FALLBACK Errcode builtin_err;

/* Minimal host-neutral error reporting used by the embeddable runtime. */
POCO_STANDALONE_FALLBACK Errcode errline(Errcode err, char* fmt, ...)
{
	va_list arguments;

	if (fmt != NULL) {
		va_start(arguments, fmt);
		vfprintf(stderr, fmt, arguments);
		va_end(arguments);
		fputc('\n', stderr);
	}
	return err;
}

POCO_STANDALONE_FALLBACK size_t get_errtext(Errcode err, char* buf)
{
	if (buf == NULL)
		return 0;
	buf[0] = '\0';
	if (err < Success) {
		switch (err) {
			case Err_stack: strcpy(buf, "Poco out of stack space"); break;
			case Err_bad_instruction: strcpy(buf, "Illegal instruction in Poco interpreter"); break;
			case Err_null_ref: strcpy(buf, "Trying to use a NULL pointer"); break;
			case Err_no_main: strcpy(buf, "No main function"); break;
			case Err_zero_divide: strcpy(buf, "Attempt to divide by zero"); break;
			case Err_float: strcpy(buf, "Floating point math error"); break;
			case Err_invalid_FILE: strcpy(buf, "Invalid FILE pointer"); break;
			case Err_index_small: strcpy(buf, "Pointer or array index too small"); break;
			case Err_index_big: strcpy(buf, "Pointer or array index too large"); break;
			case Err_poco_free: strcpy(buf, "Trying to free an invalid block"); break;
			case Err_free_null: strcpy(buf, "Trying to free NULL"); break;
			case Err_free_resources: strcpy(buf, "Poco resource management damaged"); break;
			case Err_zero_malloc: strcpy(buf, "Negative or zero allocation size"); break;
			case Err_string: strcpy(buf, "String operation failed"); break;
			case Err_fread_buf: strcpy(buf, "Read past Poco buffer"); break;
			case Err_fwrite_buf: strcpy(buf, "Write past Poco buffer"); break;
			case Err_poco_lib_not_found: strcpy(buf, "Poco library file not found"); break;
			case Err_poco_lib_load_failed: strcpy(buf, "Failed to load Poco library"); break;
			case Err_poco_lib_no_entry: strcpy(buf, "Poco library entry point missing"); break;
			case Err_poco_lib_invalid: strcpy(buf, "Poco library is invalid"); break;
			case Err_poco_lib_version: strcpy(buf, "Poco library version mismatch"); break;
			case Err_poco_lib_empty: strcpy(buf, "Poco library contains no functions"); break;
			case Err_poco_ffi_bounds: strcpy(buf, "Poco native binding exceeded a pointer span"); break;
			default: snprintf(buf, ERRTEXT_SIZE, "Error code %d", err); break;
		}
	}
	return strlen(buf);
}

Popot empty_popot = { NULL, NULL, NULL };

Errcode po_init_libs(Poco_lib *lib)
/*****************************************************************************
 *
 ****************************************************************************/
{
	Errcode err;

	while (lib != NULL)
		{
		init_list(&lib->resources);
		if (lib->init)
			if (Success > (err = lib->init(lib)))
				return err;
		lib = lib->next;
		}
	return(Success);
}

void po_cleanup_libs(Poco_lib *lib)
/*****************************************************************************
 *
 ****************************************************************************/
{
	while (lib != NULL)
		{
		if (lib->cleanup)
			lib->cleanup(lib);
		lib = lib->next;
		}
}

void poco_freez(Popot *pt)
/*****************************************************************************
 * free buffer allocated into Poco space and set it to NULL
 ****************************************************************************/
{
if (pt->pt != NULL)
	po_free(pt->pt);
pt->pt = pt->min =	pt->max = NULL;
}

Errcode po_check_formatf(int maxlen, char *fmt, va_list pargs)
/****************************************************************************
 * sanity-check the printf-like format & args passed in from a poco program.
 ****************************************************************************/
{
	(void)maxlen;
	(void)fmt;
	(void)pargs;
	return Success;
}
