#ifdef WITH_POCO
/**** host side declaration of aa_syslib vector table ****/

#include <stdlib.h>
#include <string.h>

#define REXLIB_INTERNALS
#include "memory.h"
#include "jfile.h"
#include "xfile.h"
#include "rexlib.h"
#include "aasyslib.h"
#include "filepath.h"
#include "reqlib.h"
#include "jimk.h"

// the global error
Errcode builtin_err;

extern int rexlib_boxf(char* fmt, ...);  // from stdiolib.c

/*
 * Modern implementations of legacy PJ unbuffered file I/O using XFILE.
 */

static enum XReadWriteMode pjmode_to_xmode(int jmode, int creating)
{
	switch (jmode) {
		case JREADONLY:
			return XREADONLY;
		case JWRITEONLY:
			return creating ? XWRITEONLY : XWRITEONLY; /* best effort */
		case JREADWRITE:
			return creating ? XREADWRITE_CLOBBER : XREADWRITE_OPEN;
		default:
			return XUNDEFINED;
	}
}

Jfile pj_open(char* path, int mode)
{
	XFILE* xf = NULL;
	Errcode err = xffopen(path, &xf, pjmode_to_xmode(mode, 0));
	if (err < Success) {
		return JNONE;
	}
	return (Jfile)xf;
}

Jfile pj_create(char* path, int mode)
{
	/* Create/clobber semantics */
	XFILE* xf = NULL;
	enum XReadWriteMode xmode = pjmode_to_xmode(mode, 1);
	if (xmode == XUNDEFINED) {
		xmode = XREADWRITE_CLOBBER;
	}
	Errcode err = xffopen(path, &xf, xmode);
	if (err < Success) {
		return JNONE;
	}
	return (Jfile)xf;
}

Errcode pj_close(Jfile f)
{
	if (f == JNONE) {
		return Success;
	}
	XFILE* xf = (XFILE*)f;
	return xffclose(&xf);
}

static void pj_close_void(Jfile f)
{
	(void)pj_close(f);
}

Errcode pj_closez(Jfile* jf)
{
	if (jf == NULL || *jf == JNONE) {
		return Success;
	}
	Errcode err = pj_close(*jf);
	*jf = JNONE;
	return err;
}

long pj_read(Jfile f, void* buf, long size)
{
	if (f == JNONE) {
		return Failure;
	}
	XFILE* xf = (XFILE*)f;
	/* Return number of bytes read (like POSIX read) */
	size_t n = xfread(buf, 1, (size_t)size, xf);
	return (long)n;
}

long pj_write(Jfile f, void* buf, long size)
{
	if (f == JNONE) {
		return Failure;
	}
	XFILE* xf = (XFILE*)f;
	size_t n = xfwrite(buf, 1, (size_t)size, xf);
	return (long)n;
}

long pj_seek(Jfile f, long offset, int mode)
{
	if (f == JNONE) {
		return Failure;
	}
	XFILE* xf = (XFILE*)f;
	enum XSeekWhence whence = XSEEK_SET;
	switch (mode) {
		case JSEEK_START:
			whence = XSEEK_SET;
			break;
		case JSEEK_REL:
			whence = XSEEK_CUR;
			break;
		case JSEEK_END:
			whence = XSEEK_END;
			break;
		default:
			whence = XSEEK_SET;
			break;
	}
	Errcode err = xffseek(xf, offset, whence);
	if (err < Success) {
		return err;
	}
	return xfftell(xf);
}

long pj_tell(Jfile f)
{
	if (f == JNONE) {
		return Failure;
	}
	return xfftell((XFILE*)f);
}

Errcode pj_readoset(Jfile f, void* buf, long offset, size_t size)
{
	if (f == JNONE) {
		return Failure;
	}
	return xffreadoset((XFILE*)f, buf, offset, size);
}

Syslib aa_syslib = {
	/* header */
	{
		sizeof(Syslib),
		AA_SYSLIB,
		AA_SYSLIB_VERSION,
	},
	/** memory oriented utilities **/
	pj_malloc,
	pj_zalloc,
	pj_free,
	memset,
	memcpy,
	memcmp,
	strcpy,
	strlen,
	strcmp,
	pj_get_path_suffix,
	pj_get_path_name,

	/* Might as well let rexlibs load rexlibs... */
	pj_rex_load,
	pj_rex_free,
	pj_rexlib_load,
	pj_rexlib_init,
	pj_rexlib_free,

	/* dos unbuffered file io interface */
	pj_ioerr,
	pj_open,
	pj_create,
	pj_close_void,
	pj_read,
	pj_write,
	pj_seek,
	pj_tell,
	pj_delete,
	pj_rename,
	pj_exists,
	/* clock */
	pj_clock_1000,
	/* debugging printer */
	rexlib_boxf,
};

#endif /* WITH_POCO */
