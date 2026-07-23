#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "poco_errcodes.h"
#include "port.h"
#include "poco.h"
#include "pocolib.h"
#include "ptrmacro.h"
#include "standard_library.h"

#define builtin_err (*poco_vm_builtin_error(vm))

/*****************************************************************************/
static char* strlwr(char* s)
{
	for (char* p = s; *p; p++) {
		*p = tolower(*p);
	}
	return s;
}

static char* strupr(char* s)
{
	for (char* p = s; *p; p++) {
		*p = toupper(*p);
	}
	return s;
}

/*****************************************************************************
 * int sprintf(char *buf, char *format, ...)
 ****************************************************************************/
static int po_sprintf(char* buf, char* format, PocoVm* vm, ...)
{
	int rv;
	va_list args;

	if (buf == NULL || format == NULL) {
		return (builtin_err = Err_null_ref);
	}

	va_start(args, vm);
	rv = vsprintf(buf, format, args);
	va_end(args);

	return rv;
}

/*****************************************************************************
 * int snprintf(char *buf, int maxlen, char *format, ...)
 *
 * This is the bounded formatting alternative.  sprintf remains intentionally
 * trusted for legacy scripts because its prototype carries no destination
 * capacity for the FFI contract to validate.
 ****************************************************************************/
static int po_snprintf(char* buf, int maxlen, char* format, PocoVm* vm, ...)
{
	int rv;
	va_list args;

	if (buf == NULL || format == NULL) {
		return (builtin_err = Err_null_ref);
	}
	if (maxlen < 0) {
		return (builtin_err = Err_parameter_range);
	}
	va_start(args, vm);
	rv = vsnprintf(buf, (size_t)maxlen, format, args);
	va_end(args);
	return rv;
}

/*****************************************************************************
 * int strcmp(char *a, char *b)
 ****************************************************************************/
static int po_strcmp(char* d, char* s, PocoVm* vm)
{
	if (d == NULL || s == NULL) {
		return (builtin_err = Err_null_ref);
	}
	return (strcmp(d, s));
}

/*****************************************************************************
 * int stricmp(char *a, char *b)
 ****************************************************************************/
static int po_stricmp(char* d, char* s, PocoVm* vm)
{
	if (d == NULL || s == NULL) {
		return (builtin_err = Err_null_ref);
	}
	return (stricmp(d, s));
}

/*****************************************************************************
 * int strncmp(char *a, char *b, int maxlen)
 ****************************************************************************/
static int po_strncmp(char* d, char* s, int maxlen, PocoVm* vm)
{
	if (d == NULL || s == NULL) {
		return (builtin_err = Err_null_ref);
	}
	return (strncmp(d, s, maxlen));
}

/*****************************************************************************
 * int strlen(char *a)
 ****************************************************************************/
static int po_strlen(char* s, PocoVm* vm)
{
	if (s == NULL) {
		return (builtin_err = Err_null_ref);
	}
	return (strlen(s));
}

/*****************************************************************************
 * char *strcpy(char *dest, char *source)
 ****************************************************************************/
static char* po_strcpy(char* d, char* s, PocoVm* vm)
{
	if (d == NULL || s == NULL) {
		builtin_err = Err_null_ref;
	} else {
		strcpy(d, s);
	}

	return (d);
}

/*****************************************************************************
 * char *strncpy(char *dest, char *source, int maxlen)
 ****************************************************************************/
static char* po_strncpy(char* d, char* s, int maxlen, PocoVm* vm)
{
	if (d == NULL || s == NULL) {
		builtin_err = Err_null_ref;
	} else {
		strncpy(d, s, maxlen);
	}
	return (d);
}

/*****************************************************************************
 * char *strcat(char *dest, char *source)
 ****************************************************************************/
static char* po_strcat(char* d, char* tail, PocoVm* vm)
{
	if (d == NULL || tail == NULL) {
		builtin_err = Err_null_ref;
	} else {
		strcat(d, tail);
	}

	return (d);
}

/*****************************************************************************
 * char *strdup(char *source)
 ****************************************************************************/
static char* po_strdup(char* s, PocoVm* vm)
{
	Popot d;

	if (s == NULL) {
		builtin_err = Err_null_ref;
		return NULL;
	}
	int len = strlen(s) + 1;
	d = poco_lmalloc_for_vm(vm, len);
	if (d.pt == NULL) {
		builtin_err = Err_no_memory;
		return NULL;
	}
	strcpy(d.pt, s);
	return (char*)d.pt;
}

/*****************************************************************************
 * char *strchr(char *source, int c)
 ****************************************************************************/
static char* po_strchr(char* s1, int c, PocoVm* vm)
{
	if (s1 == NULL) {
		builtin_err = Err_null_ref;
		return NULL;
	}
	return strchr(s1, c);
}

/*****************************************************************************
 * char *strrchr(char *source, int c)
 ****************************************************************************/
static char* po_strrchr(char* s1, int c, PocoVm* vm)
{
	if (s1 == NULL) {
		builtin_err = Err_null_ref;
		return NULL;
	}
	return strrchr(s1, c);
}

/*****************************************************************************
 * char *strstr(char *string, char *substring)
 ****************************************************************************/
static char* po_strstr(char* s1, char* s2, PocoVm* vm)
{
	if (s1 == NULL || s2 == NULL) {
		builtin_err = Err_null_ref;
		return NULL;
	}
	return strstr(s1, s2);
}

/*****************************************************************************
 * char *stristr(char *string, char *substring)
 ****************************************************************************/
static char* po_stristr(char* s1, char* s2, PocoVm* vm)
{
	char *p1 = NULL, *p2 = NULL;
	char* res;
	char* result = NULL;

	if (s1 == NULL || s2 == NULL) {
		builtin_err = Err_null_ref;
		goto OUT;
	}
	if ((p1 = clone_string(s1)) == NULL) {
		builtin_err = Err_no_memory;
		goto OUT;
	}
	if ((p2 = clone_string(s2)) == NULL) {
		builtin_err = Err_no_memory;
		goto OUT;
	}
	upc(p1);
	upc(p2);
	if ((res = strstr(p1, p2)) == NULL) {
		goto OUT;
	}
	result = s1 + (res - p1);
OUT:
	pj_gentle_free(p1);
	pj_gentle_free(p2);
	return result;
}

/*****************************************************************************
 * int atoi(char *str);
 ****************************************************************************/
static int po_atoi(char* str, PocoVm* vm)
{
	if (str == NULL) {
		return builtin_err = Err_null_ref;
	}

	return atoi(str);
}

/*****************************************************************************
 * double atof(char *str);
 ****************************************************************************/
static double po_atof(char* str, PocoVm* vm)
{
	if (str == NULL) {
		return builtin_err = Err_null_ref;
	}

	return atof(str);
}

/*****************************************************************************
 * int strspn(char *string, char *charset)
 ****************************************************************************/
static int po_strspn(char* s1, char* s2, PocoVm* vm)
{
	if (s1 == NULL || s2 == NULL) {
		return builtin_err = Err_null_ref;
	}
	return strspn(s1, s2);
}

/*****************************************************************************
 * int strcspn(char *string, char *charset)
 ****************************************************************************/
static int po_strcspn(char* s1, char* s2, PocoVm* vm)
{
	if (s1 == NULL || s2 == NULL) {
		return builtin_err = Err_null_ref;
	}
	return strcspn(s1, s2);
}

/*****************************************************************************
 * char *strpbrk(char *string, char *breakset)
 ****************************************************************************/
static char* po_strpbrk(char* s1, char* s2, PocoVm* vm)
{
	if (s1 == NULL || s2 == NULL) {
		builtin_err = Err_null_ref;
		return NULL;
	}
	return strpbrk(s1, s2);
}

/*****************************************************************************
 * char *strtok(char *string, char *delimset)
 ****************************************************************************/
static char* po_strtok(char* s1, char* s2, PocoVm* vm)
{
	if (s2 == NULL) /* note that NULL s1 is allowed! */
	{
		builtin_err = Err_null_ref;
		return NULL;
	}
	return strtok(s1, s2);
}

/*****************************************************************************
 * char *getenv(char *varname)
 ****************************************************************************/
static char* po_getenv(char* s1, PocoVm* vm)
{
	if (s1 == NULL) {
		builtin_err = Err_null_ref;
		return NULL;
	}
	return getenv(s1);
}

/*****************************************************************************
 * char *strlwr(char *string)
 ****************************************************************************/
static char* po_strlwr(char* d, PocoVm* vm)
{
	if (d == NULL) {
		builtin_err = Err_null_ref;
	} else {
		strlwr(d);
	}
	return (d);
}

/*****************************************************************************
 * char *strupr(char *string)
 ****************************************************************************/
static char* po_strupr(char* d, PocoVm* vm)
{
	if (d == NULL) {
		builtin_err = Err_null_ref;
	} else {
		strupr(d);
	}
	return (d);
}

/*****************************************************************************
 * char *strerror(int errnum)
 ****************************************************************************/
static char* po_strerror(int err, PocoVm* vm)
{
	static char errmsg[ERRTEXT_SIZE];

	get_errtext(err, errmsg);
	return errmsg;
}

/*----------------------------------------------------------------------------
 * library protos...
 *
 * Maintenance notes:
 *	The stringent rules that apply to maintaining most builtin lib protos
 *	don't apply here.  These functions are not visible to poe modules;
 *	you can add or delete protos anywhere in the list below.
 *--------------------------------------------------------------------------*/

#define POCO_NO_PARAMETER POCO_BINDING_PARAMETER_NONE

static void po_release_owned_string(void* pointer, void* user_data)
{
	(void)user_data;
	po_free(pointer);
}

static const PocoBindingPointerContract one_cstring_read[] = {
	{0, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_C_STRING, 0, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER, POCO_NO_PARAMETER},
};
static const PocoBindingPointerContract two_cstring_read[] = {
	{0, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_C_STRING, 0, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER, POCO_NO_PARAMETER},
	{1, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_C_STRING, 0, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER, POCO_NO_PARAMETER},
};
static const PocoBindingPointerContract bounded_format_spans[] = {
	{0, POCO_POINTER_PERMISSION_WRITE, 1, POCO_BINDING_SPAN_BYTES, 1, 1, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER},
	{2, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_C_STRING, 0, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER, POCO_NO_PARAMETER},
};
static const PocoBindingPointerContract strcpy_spans[] = {
	{0, POCO_POINTER_PERMISSION_WRITE, 1, POCO_BINDING_SPAN_C_STRING, 0, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER, 1},
	{1, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_C_STRING, 0, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER, POCO_NO_PARAMETER},
};
static const PocoBindingPointerContract strncpy_spans[] = {
	{0, POCO_POINTER_PERMISSION_WRITE, 1, POCO_BINDING_SPAN_BYTES, 1, 2, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER},
	{1, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_C_STRING, 0, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER, POCO_NO_PARAMETER},
};
static const PocoBindingPointerContract strcat_spans[] = {
	{0, POCO_POINTER_PERMISSION_READ | POCO_POINTER_PERMISSION_WRITE, 1,
	 POCO_BINDING_SPAN_APPEND_C_STRING, 0, POCO_NO_PARAMETER, POCO_NO_PARAMETER, 1},
	{1, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_C_STRING, 0, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER, POCO_NO_PARAMETER},
};
static const PocoBindingPointerContract strncmp_spans[] = {
	{0, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_BYTES, 1, 2, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER},
	{1, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_BYTES, 1, 2, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER},
};
static const PocoBindingPointerContract mutable_cstring_span[] = {
	{0, POCO_POINTER_PERMISSION_READ | POCO_POINTER_PERMISSION_WRITE, 1, POCO_BINDING_SPAN_C_STRING,
	 0, POCO_NO_PARAMETER, POCO_NO_PARAMETER, POCO_NO_PARAMETER},
};

static const PocoBindingContract snprintf_contract = {
	bounded_format_spans, Array_els(bounded_format_spans), {0}};
static const PocoBindingContract strcmp_contract = {
	two_cstring_read, Array_els(two_cstring_read), {0}};
static const PocoBindingContract strncmp_contract = {strncmp_spans, Array_els(strncmp_spans), {0}};
static const PocoBindingContract strlen_contract = {
	one_cstring_read, Array_els(one_cstring_read), {0}};
static const PocoBindingContract strcpy_contract = {
	strcpy_spans, Array_els(strcpy_spans), {POCO_POINTER_RETURN_ALIAS, 0}};
static const PocoBindingContract strncpy_contract = {
	strncpy_spans, Array_els(strncpy_spans), {POCO_POINTER_RETURN_ALIAS, 0}};
static const PocoBindingContract strcat_contract = {
	strcat_spans, Array_els(strcat_spans), {POCO_POINTER_RETURN_ALIAS, 0}};
static const PocoBindingContract strdup_contract = {
	one_cstring_read,
	Array_els(one_cstring_read),
	{POCO_POINTER_RETURN_OWNED, POCO_NO_PARAMETER, POCO_BINDING_SPAN_C_STRING, 0, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER, 0, POCO_POINTER_PERMISSION_READ | POCO_POINTER_PERMISSION_WRITE,
	 po_release_owned_string, NULL}};
static const PocoBindingContract first_string_alias_contract = {
	one_cstring_read, Array_els(one_cstring_read), {POCO_POINTER_RETURN_ALIAS, 0}};
static const PocoBindingContract first_of_two_alias_contract = {
	two_cstring_read, Array_els(two_cstring_read), {POCO_POINTER_RETURN_ALIAS, 0}};
static const PocoBindingContract mutable_string_alias_contract = {
	mutable_cstring_span, Array_els(mutable_cstring_span), {POCO_POINTER_RETURN_ALIAS, 0}};

static Lib_proto lib[] = {
	/* string stuff */
	/* sprintf is intentionally legacy/unsafe: use snprintf where capacity is known. */
	{ po_sprintf, "int     sprintf(char *buf, char *format, ...);", NULL,
	  POCO_BINDING_RUN_CONTEXT },
	{po_snprintf, "int     snprintf(char *buf, int maxlen, char *format, ...);", &snprintf_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_strcmp, "int     strcmp(char *a, char *b);", &strcmp_contract, POCO_BINDING_RUN_CONTEXT},
	{po_stricmp, "int     stricmp(char *a, char *b);", &strcmp_contract, POCO_BINDING_RUN_CONTEXT},
	{po_strncmp, "int     strncmp(char *a, char *b, int maxlen);", &strncmp_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_strlen, "int     strlen(char *a);", &strlen_contract, POCO_BINDING_RUN_CONTEXT},
	{po_strcpy, "char    *strcpy(char *dest, char *source);", &strcpy_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_strncpy, "char    *strncpy(char *dest, char *source, int maxlen);", &strncpy_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_strcat, "char    *strcat(char *dest, char *source);", &strcat_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_strdup, "char    *strdup(char *source);", &strdup_contract, POCO_BINDING_RUN_CONTEXT},
	{po_strchr, "char    *strchr(char *source, int c);", &first_string_alias_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_strrchr, "char    *strrchr(char *source, int c);", &first_string_alias_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_strstr, "char    *strstr(char *string, char *substring);", &first_of_two_alias_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_stristr, "char    *stristr(char *string, char *substring);", &first_of_two_alias_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_atoi, "int     atoi(char *string);", &strlen_contract, POCO_BINDING_RUN_CONTEXT},
	{po_atof, "double  atof(char *string);", &strlen_contract, POCO_BINDING_RUN_CONTEXT},
	{po_strpbrk, "char    *strpbrk(char *string, char *breakset);", &first_of_two_alias_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_strspn, "int     strspn(char *string, char *breakset);", &strcmp_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_strcspn, "int     strcspn(char *string, char *breakset);", &strcmp_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_strtok, "char    *strtok(char *string, char *delimset);", NULL, POCO_BINDING_RUN_CONTEXT},
	{po_getenv, "char    *getenv(char *varname);", &strlen_contract, POCO_BINDING_RUN_CONTEXT},
	{po_strlwr, "char    *strlwr(char *string);", &mutable_string_alias_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_strupr, "char    *strupr(char *string);", &mutable_string_alias_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_strerror, "char    *strerror(int errnum);", NULL, POCO_BINDING_RUN_CONTEXT},
};

Poco_lib po_str_lib = {
	NULL,
	"(C standard) String",
	lib,
	Array_els(lib),
};

const PocoLibrary* poco_standard_string_library(void)
{
	static PocoBinding bindings[Array_els(lib)];
	static const PocoLibrary library = {
		POCO_STANDARD_STRING_LIBRARY_ID, bindings, Array_els(bindings), NULL, NULL, NULL,
	};
	static int initialized;
	size_t index;

	if (!initialized) {
		for (index = 0; index < Array_els(lib); ++index) {
			bindings[index].prototype = lib[index].proto;
			bindings[index].function = (PocoNativeFunction)lib[index].func;
			bindings[index].contract = lib[index].contract;
			bindings[index].flags = lib[index].flags;
		}
		initialized = 1;
	}
	return &library;
}
