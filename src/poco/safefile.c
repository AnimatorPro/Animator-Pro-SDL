/* safefile.c - Manages resources such as files and blocks of memory.
   Frees them on poco program exit.   Does a bit of sanity
   checking on file-function parameters. */

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "poco.h"
#include "pocolib.h"
#include "ptrmacro.h"
#include "errcodes.h"
#include "linklist.h"
#include "memory.h"

#include "formatf.h"    /* PJ text formatter used by local sprintf etc */

void po_free(Popot ppt);

extern Errcode builtin_err;

extern Poco_lib po_FILE_lib, po_mem_lib;


/*****************************************************************************
 * file functions...
 ****************************************************************************/

/*****************************************************************************
 *
 ****************************************************************************/
static void free_safe_files(Poco_lib *lib)
{
	Dlheader *sfi = &lib->resources;
	Dlnode *node, *next;

	for(node = sfi->head; NULL != (next = node->next); node = next)
		{
		fclose(((Rnode *)node)->resource);
		pj_free(node);
		}
	init_list(sfi); /* just defensive programming */
}

/*****************************************************************************
 *
 ****************************************************************************/
Rnode *po_in_rlist(Dlheader *sfi, void *f)
{
	Dlnode *node, *next;

	for(node = sfi->head; NULL != (next = node->next); node = next)
		{
		if (((Rnode *)node)->resource == f)
			return (Rnode *)node;
		}
	return NULL;
}

/*****************************************************************************
 *
 ****************************************************************************/
static Errcode safe_file_check(Popot *fp, Popot *bp, size_t size)
{
	if (NULL == fp->pt)
		return builtin_err = Err_null_ref;
	if (NULL == po_in_rlist(&po_FILE_lib.resources,fp->pt))
		return builtin_err = Err_invalid_FILE;

	if (NULL != bp)
		{
		if (NULL == bp->pt)
			return builtin_err = Err_null_ref;
		if ((size > 0) && (Popot_bufsize(bp) < size))
			return builtin_err = Err_buf_too_small;
		}
	return Success;
}

/*****************************************************************************
 * int fread(void *buf, int size, int count, FILE *f)
 ****************************************************************************/
static int po_fread(Popot buf, unsigned size, unsigned n, Popot f)
{
	if (Success != safe_file_check(&f, &buf, size*n))
		return 0;

	return fread(buf.pt, size, n, f.pt);
}

/*****************************************************************************
 * int fwrite(void *buf, int size, int count, FILE *f)
 ****************************************************************************/
static size_t po_fwrite(Popot buf, unsigned size, unsigned n, Popot f)
{
	if (Success != safe_file_check(&f, &buf, size*n))
		return 0;

	return fwrite(buf.pt, size, n, f.pt);
}


/*****************************************************************************
 * FILE *fopen(char *name, char *mode)
 ****************************************************************************/
static Popot po_fopen(Popot name, Popot mode)
{
	Rnode *sn;
	Popot f;

	f.pt = f.min = f.max = NULL;
	if (name.pt == NULL || mode.pt == NULL)
		{
		builtin_err = Err_null_ref;
		return f;
		}
	if ((f.pt = fopen(name.pt, mode.pt)) == NULL)
		return f;
	if ((sn = pj_zalloc(sizeof(*sn))) == NULL)
		{
		fclose(f.pt);
		f.pt = NULL;
		builtin_err = Err_no_memory;
		return f;
		}
	add_head(&po_FILE_lib.resources,&sn->node);
	sn->resource = f.pt;

	return f;
}

/*****************************************************************************
 * void fclose(FILE *f)
 ****************************************************************************/
static void po_fclose(Popot f)
{
	Rnode *sn;

	if ((sn = po_in_rlist(&po_FILE_lib.resources, f.pt)) == NULL)
		{
		builtin_err = Err_invalid_FILE;
		return;
		}
	fclose(f.pt);
	rem_node((Dlnode *)sn);
	pj_free(sn);
}

/*****************************************************************************
 * int fseek(FILE *f, long offset, int mode)
 ****************************************************************************/
static int po_fseek(Popot f, long offset, int whence)
{
	if (Success != safe_file_check(&f, NULL, 0))
		return builtin_err;
	return fseek(f.pt,offset,whence);
}

/*****************************************************************************
 * long ftell(FILE *f)
 ****************************************************************************/
static long po_ftell(Popot f)
{
	if (Success != safe_file_check(&f, NULL, 0))
		return builtin_err;
	return ftell(f.pt);
}

/*****************************************************************************
 * int fprintf(FILE *f, char *format, ...)
 ****************************************************************************/
static int po_fprintf(long vargcount, long vargsize, Popot f, Popot format, ...)
{
	Errcode   err;
	va_list   args;
	Formatarg fa;
	char c;

	if (Success != safe_file_check(&f, NULL, 0))
		return builtin_err;

	va_start(args, format);

	if (Success > (err = po_check_formatf(0, vargcount, vargsize, format.pt, args)))
		return err;

	copy_va_list(args, fa.args);
	init_formatarg(&fa,format.pt);
	while ((c = fa_getc(&fa)) != 0)
		if (putc(c,(FILE *)f.pt) == EOF)
			return Err_write;
	va_end(args);
	return fa.count - 1;
}

/*****************************************************************************
 * int getc(FILE *f)
 ****************************************************************************/
static int po_getc(Popot f)
{
	if (Success != safe_file_check(&f, NULL, 0))
		return builtin_err;

	return getc((FILE *)(f.pt));
}

/*****************************************************************************
 * int putc(int c, FILE *f)
 ****************************************************************************/
static int po_putc(int c, Popot f)
{
	if (Success != safe_file_check(&f, NULL, 0))
		return builtin_err;

	return putc(c,(FILE *)(f.pt));
}

/*****************************************************************************
 * int fputs(char *s, FILE *f)
 ****************************************************************************/
static int po_fputs(Popot string, Popot f)
{
	if (Success != safe_file_check(&f, &string, 0))
		return builtin_err;

	return fputs(string.pt, f.pt);
}

/*****************************************************************************
 * char *fgets(char *s, int maxlen, FILE *f)
 ****************************************************************************/
static Popot po_fgets(Popot string, int maxlen, Popot f)
{
	Popot rv = {NULL,NULL,NULL};

	if (Success != safe_file_check(&f, &string, maxlen))
		return rv;

	rv = string;
	rv.pt = fgets(string.pt, maxlen, f.pt);
	return rv;
}

/*****************************************************************************
 * int fflush(FILE *f)
 ****************************************************************************/
static int po_fflush(Popot f)
{
	if (Success != safe_file_check(&f, NULL, 0))
		return builtin_err;

	return fflush(f.pt);
}

/*****************************************************************************
 * used with the #define errno in the libprotos below to access errno.
 ****************************************************************************/
static Popot po_get_errno_pointer(void)
{
	Popot rv = {&errno, &errno, OPTR(&errno, sizeof(int)-1)};
	return rv;
}


/*****************************************************************************
 * memory functions...
 ****************************************************************************/
typedef struct mem_node
	{
	RNODE_FIELDS
	long size;
	} Mem_node;

/*****************************************************************************
 *
 ****************************************************************************/
Popot poco_lmalloc(long size)
{
	Popot	pp = {NULL,NULL,NULL};
	Mem_node *sn;

	if (size <= 0)
		{
		builtin_err = Err_zero_malloc;
		return pp;
		}
	if ((sn = pj_zalloc(sizeof(*sn))) == NULL)
		{
		return pp;
		}
	if ((sn->resource = pp.min = pp.max = pp.pt = pj_zalloc((long)size)) != NULL)
		{
		pp.max = OPTR(pp.max,size-1);
		sn->size = size;
		add_head(&po_mem_lib.resources,&sn->node);
		}
	else
		pj_free(sn);
	return pp;
}

/*****************************************************************************
 * void *malloc(int size)
 ****************************************************************************/
Popot po_malloc(int size)
{
	return poco_lmalloc(size);
}

/*****************************************************************************
 * void *calloc(int size_el, int el_count)
 ****************************************************************************/
Popot po_calloc(int size_el, int el_count)
{
	return poco_lmalloc(size_el*el_count);
}

/*****************************************************************************
 *
 ****************************************************************************/
static void free_safe_mem(Poco_lib *lib)
{
	Dlheader *sfi = &lib->resources;
	Dlnode *node, *next;

	for(node = sfi->head; NULL != (next = node->next); node = next)
		{
		pj_free(((Rnode *)node)->resource);
		pj_free(node);
		}
	init_list(sfi); /* just defensive programming */
}


/*****************************************************************************
 * void free(void *pt)
 ****************************************************************************/
void po_free(Popot ppt)
{
	Mem_node *sn;

	if (ppt.pt == NULL)
		builtin_err = Err_free_null;

	if ((sn = (Mem_node *)po_in_rlist(&po_mem_lib.resources, ppt.pt)) == NULL)
		builtin_err = Err_poco_free;
	else
		{
		poco_zero_bytes(sn->resource, sn->size);
		pj_free(ppt.pt);
		rem_node((Dlnode *)sn);
		pj_free(sn);
		}
}

/*****************************************************************************
 *
 ****************************************************************************/
static Errcode po_memchk(Popot *d, Popot *s, int size)
{
	if (s->pt == NULL || d->pt == NULL)
		return builtin_err = Err_null_ref;
	if (Popot_bufsize(d) < size || Popot_bufsize(s) < size)
		return builtin_err = Err_buf_too_small;
	return Success;
}

/*****************************************************************************
 * void *memcpy(void *dest, void *source, int size)
 ****************************************************************************/
Popot po_memcpy(Popot d, Popot s, int size)
{
	if (po_memchk(&d,&s,size) < Success) {
		return d;
	}

	poco_copy_bytes(s.pt, d.pt, size);
	return d;
}

/*****************************************************************************
 * void *memmove(void *dest, void *source, int size)
 ****************************************************************************/
Popot po_memmove(Popot d, Popot s, int size)
{
	if (po_memchk(&d,&s,size) < Success) {
		return d;
	}
	memmove(d.pt, s.pt, size);
	return d;
}

/*****************************************************************************
 * int memcmp(void *a, void *b, int size)
 ****************************************************************************/
int po_memcmp(Popot d, Popot s, int size)
{
	if (po_memchk(&d,&s,size) < Success) {
		return builtin_err;
	}
	return memcmp(d.pt, s.pt, size);
}

/*****************************************************************************
 * void *memset(void *dest, int source, int size)
 ****************************************************************************/
Popot po_memset(Popot d, int c, int length)
{
if (d.pt == NULL)
		{
		builtin_err = Err_null_ref;
		return d;
		}
	if (Popot_bufsize(&d) < length)
		{
		builtin_err = Err_buf_too_small;
		return d;
		}

	memset(d.pt, c, length);
	return d;
}

/*****************************************************************************
 * void *memchr(void *a, int c, int size)
 ****************************************************************************/
Popot po_memchr(Popot d, int c, int length)
{
	if (d.pt == NULL)
		{
		builtin_err = Err_null_ref;
		return d;
		}
	if (Popot_bufsize(&d) < length)
		{
		builtin_err = Err_buf_too_small;
		return d;
		}
	if (NULL == (d.pt = memchr(d.pt, c, length))) {
		d.min = d.max = NULL;
	}

	return d;
}

/*----------------------------------------------------------------------------
 * library protos...
 *
 * Maintenance notes:
 *	The stringent rules that apply to maintaining most builtin lib protos
 *	don't apply here.  These functions are not visible to poe modules;
 *	you can add or delete protos anywhere in the list below.
 *--------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * protos for file functions...
 *--------------------------------------------------------------------------*/

static Lib_proto filelib[] = {
{po_fopen,
	"FILE    *fopen(char *name, char *mode);"},
{po_fclose,
	"void    fclose(FILE *f);"},
{po_fread,
	"int     fread(void *buf, int size, int count, FILE *f);"},
{po_fwrite,
	"int     fwrite(void *buf, int size, int count, FILE *f);"},
{po_fprintf,
	"int     fprintf(FILE *f, char *format, ...);"},
{po_fseek,
	"int     fseek(FILE *f, long offset, int mode);"},
{po_ftell,
	"long    ftell(FILE *f);"},
{po_fflush,
	"int     fflush(FILE *f);"},
{po_getc,
	"int     getc(FILE *f);"},
{po_getc,
	"int     fgetc(FILE *f);"},
{po_putc,
	"int     putc(int c, FILE *f);"},
{po_putc,
	"int     fputc(int c, FILE *f);"},
{po_fgets,
	"char    *fgets(char *s, int maxlen, FILE *f);"},
{po_fputs,
	"int     fputs(char *s, FILE *f);"},

{po_get_errno_pointer,
	"int     *__GetErrnoPointer(void);"},
{NULL,
	"#define errno (*__GetErrnoPointer())"},

};

Poco_lib po_FILE_lib =
	{
	NULL, "(C Standard) FILE",
	filelib, Array_els(filelib),
	NULL, free_safe_files,
	};

/*----------------------------------------------------------------------------
 * protos for memory functions...
 *--------------------------------------------------------------------------*/

static Lib_proto memlib[] = {
{ po_malloc,
	"void    *malloc(int size);"},
{ po_calloc,
	"void    *calloc(int size_el, int el_count);"},
{ po_free,
	"void    free(void *pt);"},
{ po_memcpy,
	"void    *memcpy(void *dest, void *source, int size);"},
{ po_memmove,
	"void    *memmove(void *dest, void *source, int size);"},
{ po_memcmp,
	"int     memcmp(void *a, void *b, int size);"},
{ po_memset,
	"void    *memset(void *dest, int fill_char, int size);"},
{ po_memchr,
	"void    *memchr(void *a, int match_char, int size);"},
};

Poco_lib po_mem_lib =
	{
	NULL, "(C Standard) Memory Manager",
	memlib, Array_els(memlib),
	NULL, free_safe_mem,
	};

#ifdef DEADWOOD

Errcode po_file_to_stdout(char *name)
{
FILE *f;
int c;

if ((f = fopen(name, "r")) == NULL)
	return Err_create;
while ((c = fgetc(f)) != EOF)
	fputc(c,stdout);
fclose(f);
return Success;
}

#endif /* DEADWOOD */
