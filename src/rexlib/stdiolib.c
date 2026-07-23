#ifdef WITH_POCO

#include <stdarg.h>

#define REXLIB_INTERNALS
#include "stdtypes.h"
#include <errno.h>

#include "stdiolib.h"

#include "formatf.h"

/********************************************/
int rexlib_boxf(char* fmt, ...)
{
	Errcode err;
	va_list args;

	va_start(args, fmt);
	err = vprintf(fmt, args);
	va_end(args);
	return err;
}

/********* shells needed for lfile **********/

static void clearerr_shell(FILE* fp)
{
	clearerr(fp);
#undef clearerr
#define clearerr clearerr_shell
}

static int feof_shell(FILE* fp)
{
	return feof(fp);
#undef feof
#define feof feof_shell
}

static int ferror_shell(FILE* fp)
{
	return ferror(fp);
#undef ferror
#define ferror ferror_shell
}

Errcode pj_errno_errcode()
{
	return errno;
}

/****************** interface for remote code ***************/
static int* get_perrno()
{
	return &errno;
}

/********** we only need this if SEEK codes are different *********/

#if (SEEK_SET != 0) || (SEEK_CUR != 1) || (SEEK_END != 2)

static int fseek_shell(FILE* fp, long int offset, int whence)
{
	ststic int whences[] = {SEEK_SET, SEEK_CUR, SEEK_END};

	if ((unsigned)whence > 2) {
		return -1;
	}
	return fseek_shell(fp, offset, whences[whence]);
}

#define fseek fseek_shell

#endif /* ***************** seek modes not same */

/* just like ansii C.  Name is different to avoid conflict with prototype
   in stdio.h.  Watcom C has some bugs handling const declarations or
   we wouldn't have to do this.  */
int local_sprintf(char* buf, char* format, ...)
{
	Formatarg fa;
	start_formatarg(fa, format);
	while ((*buf++ = fa_getc(&fa)) != 0) {
	}
	end_formatarg(fa);
	return fa.count - 1;
}

Stdiolib aa_stdiolib = {
	/* header */
	{
		sizeof(Stdiolib),
		AA_STDIOLIB,
		AA_STDIOLIB_VERSION,
	},
	NULL,
	get_perrno,
	clearerr,
	feof,
	ferror,
	pj_errno_errcode,

	fopen,
	fclose,

	fseek,
	ftell,
	fflush,
	rewind,

	fread,
	fgetc,
	fgets,
	ungetc,

	fwrite,
	fputc,
	fputs,
	fprintf,

	rexlib_boxf,
	local_sprintf,
};

#endif /* WITH_POCO */
