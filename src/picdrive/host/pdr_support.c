/*****************************************************************************
 * Support functions for the skeleton PDR.
 ****************************************************************************/

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "stdtypes.h"


Errcode pdr_boxfmt(char* fmt, ...);
char* stristr(char* string, char* pattern);

Errcode pdr_boxfmt(char* fmt, ...)
{
	Errcode err;
	va_list args;

	va_start(args, fmt);
	err = vprintf(fmt, args);
	va_end(args);
	return (err);
}

static void strlwr(char* str)
{
	while (*str) {
		if (*str >= 'a' && *str <= 'z') {
			*str -= 32;
		}
		++str;
	}
}

static bool substreq(char* str, char* substr)
{
	while (*substr) {
		if (*substr++ != *str++) {
			return false;
		}
	}
	return true;
}

char* stristr(char* string, char* pattern)
{
	char patc1 = *pattern;
	char* pstr = string;

	strlwr(string);
	strlwr(pattern);

	while (*pstr) {
		if (*pstr == patc1 && substreq(pstr, pattern)) {
			return pstr;
		} else {
			++pstr;
		}
	}

	return NULL;
}
