#include "dfile.ih"
#include "memory.h"

Errcode jerr = Success;

extern Errcode pj_close(Jfile f);  // from syslib.c

Errcode pj_ioerr()
{
	return jerr;
}

static Jfl* jopen_it(char* name, int mode, Doserr (*openit)(int* phandle, char* name, int mode))
{
	Jfl* tf = pj_zalloc(sizeof(*tf));

	if (tf == NULL) {
		jerr = Err_no_memory;
		return NULL;
	}

	tf->jfl_magic = JFL_MAGIC;
	jerr = pj_mserror((*openit)(&(tf->handle.j), name, mode));
	if (jerr < Success) {
		goto error;
	}

	tf->rwmode = mode;
	return tf;

error:
	pj_close(tf);
	return NULL;
}
