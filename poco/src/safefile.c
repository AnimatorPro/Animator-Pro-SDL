/* safefile.c - Manages resources such as files and blocks of memory.
   Frees them on poco program exit.   Does a bit of sanity
   checking on file-function parameters. */

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "poco_internal.h"
#include "pocolib.h"
#include "ptrmacro.h"
#include "poco_errcodes.h"
#include "linklist.h"
#include "standard_library.h"
#include "safefile.h"
#include "poco_ffi.h"
#include "pocoface.h"

/*
 * A binding called without a PocoVm* still has a status slot: the one belonging
 * to the VM running on this thread, or a thread-local scratch slot outside a
 * run.  Resolving here is what makes a failure reported by po_free() visible to
 * the caller that provoked it instead of silently dropped.
 */
static Errcode* builtin_error_slot(PocoVm* vm)
{
	Errcode* target = vm != NULL ? poco_vm_builtin_error(vm) : NULL;

	return target != NULL ? target : poco_active_builtin_error();
}

static Errcode poco_record_builtin_error(PocoVm* vm, Errcode err)
{
	*builtin_error_slot(vm) = err;
	return err;
}

static Errcode poco_current_builtin_error(PocoVm* vm)
{
	return *builtin_error_slot(vm);
}

/* No PocoVm* argument means the VM running on this thread. */
static PocoVm* effective_vm(PocoVm* vm)
{
	return vm != NULL ? vm : poco_active_vm();
}

/*
 * Resource lists live on the VM's own copy of a standard library, never on the
 * process-global descriptor it was cloned from.  po_FILE_lib and po_mem_lib
 * below are templates: a host hands one to a VM, the VM clones it
 * (activation.c:clone_libraries) and the clone owns the list.
 *
 * There is no VM-less fallback.  A resource list reachable without a VM is a
 * list that outlives the run that filled it, which is how a block left behind
 * by one VM became visible to the next one on the same thread.  Outside a run
 * this returns NULL and its callers report a failure rather than touching
 * shared state.  Registrations are matched by the canonical identity first and
 * by the legacy descriptor name second, because Animator registers these
 * libraries under their old names.
 */
static Poco_lib* current_standard_library(PocoVm* vm, const char* identity, const char* legacy_name)
{
	Poco_lib* library;

	vm = effective_vm(vm);
	if ((library = poco_active_library(vm, identity)) == NULL) {
		library = poco_active_library(vm, legacy_name);
	}
	return library;
}

static Poco_lib* current_file_library(PocoVm* vm)
{
	return current_standard_library(vm, POCO_STANDARD_FILE_LIBRARY_ID, po_FILE_lib.name);
}

static Poco_lib* current_memory_library(PocoVm* vm)
{
	return current_standard_library(vm, POCO_STANDARD_MEMORY_LIBRARY_ID, po_mem_lib.name);
}

/*****************************************************************************
 * file functions...
 ****************************************************************************/

/*****************************************************************************
 *
 ****************************************************************************/
void poco_standard_file_cleanup(Poco_lib* lib)
{
	Dlheader* sfi = &lib->resources;
	Dlnode *node, *next;

	/* Dlheader uses Animator-compatible head/tail sentinels in the legacy ABI. */
	for (node = sfi->head; node->next != NULL; node = next) {
		next = node->next;
		fclose(((Rnode*)node)->resource);
		pj_free(node);
	}
	init_list(sfi); /* just defensive programming */
}

/*****************************************************************************
 *
 ****************************************************************************/
Rnode* po_in_rlist(Dlheader* sfi, void* f)
{
	Dlnode* node;

	for (node = sfi->head; node->next != NULL; node = node->next) {
		if (((Rnode*)node)->resource == f) {
			return (Rnode*)node;
		}
	}
	return NULL;
}

/*****************************************************************************
 * Validate file pointer (and optionally buffer) for file I/O.
 ****************************************************************************/
static Errcode safe_file_check(PocoVm* vm, void* f, void* buf, size_t size)
{
	Poco_lib* library;

	(void)size;
	if (f == NULL) {
		return poco_record_builtin_error(vm, Err_null_ref);
	}
	library = current_file_library(vm);
	if (library == NULL || po_in_rlist(&library->resources, f) == NULL) {
		return poco_record_builtin_error(vm, Err_invalid_FILE);
	}
	if (buf == NULL && size > 0) {
		return poco_record_builtin_error(vm, Err_null_ref);
	}
	return Success;
}

/*****************************************************************************
 * int fread(void *buf, int size, int count, FILE *f)
 ****************************************************************************/
static int po_fread(void* buf, unsigned size, unsigned n, FILE* f, PocoVm* vm)
{
	if (Success != safe_file_check(vm, f, buf, size * n)) {
		return 0;
	}
	return fread(buf, size, n, f);
}

/*****************************************************************************
 * int fwrite(void *buf, int size, int count, FILE *f)
 ****************************************************************************/
static size_t po_fwrite(void* buf, unsigned size, unsigned n, FILE* f, PocoVm* vm)
{
	if (Success != safe_file_check(vm, f, buf, size * n)) {
		return 0;
	}
	return fwrite(buf, size, n, f);
}

/*****************************************************************************
 * FILE *fopen(char *name, char *mode)
 ****************************************************************************/
static FILE* po_fopen(char* name, char* mode, PocoVm* vm)
{
	Poco_lib* library;
	Rnode* sn;
	FILE* f;

	if (name == NULL || mode == NULL) {
		poco_record_builtin_error(vm, Err_null_ref);
		return NULL;
	}
	if ((library = current_file_library(vm)) == NULL) {
		poco_record_builtin_error(vm, Err_invalid_FILE);
		return NULL;
	}
	if ((f = fopen(name, mode)) == NULL) {
		return NULL;
	}
	if ((sn = pj_zalloc(sizeof(*sn))) == NULL) {
		fclose(f);
		poco_record_builtin_error(vm, Err_no_memory);
		return NULL;
	}
	add_head(&library->resources, &sn->node);
	sn->resource = f;
	return f;
}

/*****************************************************************************
 * void fclose(FILE *f)
 ****************************************************************************/
static void po_fclose(FILE* f, PocoVm* vm)
{
	Poco_lib* library = current_file_library(vm);
	Rnode* sn;

	if (f == NULL) {
		poco_record_builtin_error(vm, Err_null_ref);
		return;
	}
	if (library == NULL || (sn = po_in_rlist(&library->resources, f)) == NULL) {
		poco_record_builtin_error(vm, Err_invalid_FILE);
		return;
	}
	fclose(f);
	rem_from_list(&library->resources, (Dlnode*)sn);
	pj_free(sn);
}

/*****************************************************************************
 * int fseek(FILE *f, long offset, int mode)
 ****************************************************************************/
static int po_fseek(FILE* f, long offset, int whence, PocoVm* vm)
{
	if (Success != safe_file_check(vm, f, NULL, 0)) {
		return poco_current_builtin_error(vm);
	}
	return fseek(f, offset, whence);
}

/*****************************************************************************
 * long ftell(FILE *f)
 ****************************************************************************/
static long po_ftell(FILE* f, PocoVm* vm)
{
	if (Success != safe_file_check(vm, f, NULL, 0)) {
		return poco_current_builtin_error(vm);
	}
	return ftell(f);
}

/*****************************************************************************
 * int fprintf(FILE *f, char *format, ...)
 ****************************************************************************/
static int po_fprintf(FILE* f, char* format, PocoVm* vm, ...)
{
	va_list args;
	int result;

	if (Success != safe_file_check(vm, f, NULL, 0)) {
		return poco_current_builtin_error(vm);
	}

	va_start(args, vm);
	result = vfprintf(f, format, args);
	if (result < 0) {
		va_end(args);
		return Err_write;
	}
	va_end(args);
	return result;
}

/*****************************************************************************
 * int getc(FILE *f)
 ****************************************************************************/
static int po_getc(FILE* f, PocoVm* vm)
{
	if (Success != safe_file_check(vm, f, NULL, 0)) {
		return poco_current_builtin_error(vm);
	}
	return getc(f);
}

/*****************************************************************************
 * int putc(int c, FILE *f)
 ****************************************************************************/
static int po_putc(int c, FILE* f, PocoVm* vm)
{
	if (Success != safe_file_check(vm, f, NULL, 0)) {
		return poco_current_builtin_error(vm);
	}
	return putc(c, f);
}

/* Keep the standard aliases as distinct FFI targets.  The descriptor map is
 * keyed by native address, so registering fgetc/fputc through these same
 * wrappers would make the library ambiguous. */
static int po_fgetc(FILE* f, PocoVm* vm)
{
	return po_getc(f, vm);
}

static int po_fputc(int c, FILE* f, PocoVm* vm)
{
	return po_putc(c, f, vm);
}

/*****************************************************************************
 * int fputs(char *s, FILE *f)
 ****************************************************************************/
static int po_fputs(char* string, FILE* f, PocoVm* vm)
{
	if (string == NULL) {
		poco_record_builtin_error(vm, Err_null_ref);
		return EOF;
	}
	if (Success != safe_file_check(vm, f, NULL, 0)) {
		return poco_current_builtin_error(vm);
	}
	return fputs(string, f);
}

/*****************************************************************************
 * char *fgets(char *s, int maxlen, FILE *f)
 ****************************************************************************/
static char* po_fgets(char* string, int maxlen, FILE* f, PocoVm* vm)
{
	if (string == NULL) {
		poco_record_builtin_error(vm, Err_null_ref);
		return NULL;
	}
	if (Success != safe_file_check(vm, f, string, (size_t)maxlen)) {
		return NULL;
	}
	return fgets(string, maxlen, f);
}

/*****************************************************************************
 * int fflush(FILE *f)
 ****************************************************************************/
static int po_fflush(FILE* f, PocoVm* vm)
{
	if (Success != safe_file_check(vm, f, NULL, 0)) {
		return poco_current_builtin_error(vm);
	}
	return fflush(f);
}

/*****************************************************************************
 * used with the #define errno in the libprotos below to access errno.
 ****************************************************************************/
static int* po_get_errno_pointer(void)
{
	return &errno;
}

/*****************************************************************************
 * memory functions...
 ****************************************************************************/
typedef struct mem_node {
	RNODE_FIELDS;
	long size;
} Mem_node;

/*****************************************************************************
 *
 ****************************************************************************/
Popot poco_lmalloc_for_vm(PocoVm* vm, long size)
{
	Popot pp = {NULL, NULL, NULL};
	Poco_lib* library = current_memory_library(vm);
	Mem_node* sn;

	vm = effective_vm(vm);
	if (size <= 0) {
		poco_record_builtin_error(vm, Err_zero_malloc);
		return pp;
	}
	if (library == NULL) {
		poco_record_builtin_error(vm, Err_no_memory);
		return pp;
	}
	if ((sn = pj_zalloc(sizeof(*sn))) == NULL) {
		return pp;
	}
	if ((sn->resource = pp.min = pp.max = pp.pt = pj_zalloc((long)size)) != NULL) {
		pp.max = OPTR(pp.max, size - 1);
		sn->size = size;
		add_head(&library->resources, &sn->node);
	} else {
		pj_free(sn);
	}
	return pp;
}

/* The VM-less spelling Animator's bindings use: the running VM owns the block. */
Popot poco_lmalloc(long size)
{
	return poco_lmalloc_for_vm(NULL, size);
}

/*****************************************************************************
 * void *malloc(int size)
 *
 * Returns void* to match the FFI prototype.  The OP_CPT_TO_PPT opcode
 * converts it to a Popot with permissive bounds for script-level use.
 * Internal C callers that need a Popot should call poco_lmalloc() directly.
 ****************************************************************************/
void* po_malloc(int size)
{
	return poco_lmalloc(size).pt;
}

static void* po_malloc_in_vm(int size, PocoVm* vm)
{
	return poco_lmalloc_for_vm(vm, size).pt;
}

/*****************************************************************************
 * void *calloc(int size_el, int el_count)
 ****************************************************************************/
void* po_calloc(int size_el, int el_count)
{
	return poco_lmalloc(size_el * el_count).pt;
}

static void* po_calloc_in_vm(int size_el, int el_count, PocoVm* vm)
{
	return poco_lmalloc_for_vm(vm, size_el * el_count).pt;
}

/*****************************************************************************
 *
 ****************************************************************************/
void poco_standard_memory_cleanup(Poco_lib* lib)
{
	Dlheader* sfi = &lib->resources;
	Dlnode *node, *next;

	/* Dlheader uses Animator-compatible head/tail sentinels in the legacy ABI. */
	for (node = sfi->head; node->next != NULL; node = next) {
		next = node->next;
		poco_pointer_registry_release_owned(poco_active_pointer_registry(lib->vm),
											((Rnode*)node)->resource);
		pj_free(((Rnode*)node)->resource);
		pj_free(node);
	}
	init_list(sfi); /* just defensive programming */
}

/*****************************************************************************
 * void free(void *pt)
 *
 * The VM-less spelling is the same function as po_free_for_vm(), not a variant
 * of it: a block's allocating and releasing library must be the same one, and
 * both spellings resolve to the library the running VM owns.
 ****************************************************************************/
void po_free(void* pt)
{
	po_free_for_vm(NULL, pt);
}

void po_free_for_vm(PocoVm* vm, void* pt)
{
	Poco_lib* library = current_memory_library(vm);
	Mem_node* sn;

	vm = effective_vm(vm);
	if (pt == NULL) {
		poco_record_builtin_error(vm, Err_free_null);
		return;
	}
	if (library == NULL) {
		poco_record_builtin_error(vm, Err_poco_free);
		return;
	}
	if ((sn = (Mem_node*)po_in_rlist(&library->resources, pt)) == NULL) {
		poco_record_builtin_error(vm, Err_poco_free);
		return;
	}
	poco_zero_bytes(sn->resource, sn->size);
	poco_pointer_registry_release_owned(poco_active_pointer_registry(vm), pt);
	pj_free(pt);
	rem_from_list(&library->resources, (Dlnode*)sn);
	pj_free(sn);
}

static void po_free_in_vm(void* pt, PocoVm* vm)
{
	po_free_for_vm(vm, pt);
}

/*****************************************************************************
 * void *memcpy(void *dest, void *source, int size)
 *
 * NOTE: This function now uses direct C pointers instead of Popot to match
 * the prototype string and work correctly with the FFI calling convention.
 * Contracted FFI calls validate the source and destination spans before this
 * native implementation receives their raw pointers.  Uncontracted callers
 * retain the legacy trusted-pointer behavior.
 ****************************************************************************/
static void* po_memcpy_for_vm(void* dest, void* source, int size, PocoVm* vm)
{
	if (dest == NULL || source == NULL) {
		poco_record_builtin_error(vm, Err_null_ref);
		return dest;
	}
	if (size <= 0) {
		return dest;
	}
	memcpy(dest, source, size);
	return dest;
}

void* po_memcpy(void* dest, void* source, int size)
{
	return po_memcpy_for_vm(dest, source, size, NULL);
}

/*****************************************************************************
 * void *memmove(void *dest, void *source, int size)
 ****************************************************************************/
static void* po_memmove_for_vm(void* dest, void* source, int size, PocoVm* vm)
{
	if (dest == NULL || source == NULL) {
		poco_record_builtin_error(vm, Err_null_ref);
		return dest;
	}
	if (size <= 0) {
		return dest;
	}
	memmove(dest, source, size);
	return dest;
}

void* po_memmove(void* dest, void* source, int size)
{
	return po_memmove_for_vm(dest, source, size, NULL);
}

/*****************************************************************************
 * int memcmp(void *a, void *b, int size)
 ****************************************************************************/
static int po_memcmp_for_vm(void* a, void* b, int size, PocoVm* vm)
{
	if (a == NULL || b == NULL) {
		poco_record_builtin_error(vm, Err_null_ref);
		return poco_current_builtin_error(vm);
	}
	if (size <= 0) {
		return 0;
	}
	return memcmp(a, b, size);
}

int po_memcmp(void* a, void* b, int size)
{
	return po_memcmp_for_vm(a, b, size, NULL);
}

/*****************************************************************************
 * void *memset(void *dest, int fill_char, int size)
 ****************************************************************************/
static void* po_memset_for_vm(void* dest, int fill_char, int size, PocoVm* vm)
{
	if (dest == NULL) {
		poco_record_builtin_error(vm, Err_null_ref);
		return dest;
	}
	if (size <= 0) {
		return dest;
	}
	memset(dest, fill_char, size);
	return dest;
}

void* po_memset(void* dest, int fill_char, int size)
{
	return po_memset_for_vm(dest, fill_char, size, NULL);
}

/*****************************************************************************
 * void *memchr(void *a, int match_char, int size)
 ****************************************************************************/
static void* po_memchr_for_vm(void* a, int match_char, int size, PocoVm* vm)
{
	if (a == NULL) {
		poco_record_builtin_error(vm, Err_null_ref);
		return NULL;
	}
	if (size <= 0) {
		return NULL;
	}
	return memchr(a, match_char, size);
}

void* po_memchr(void* a, int match_char, int size)
{
	return po_memchr_for_vm(a, match_char, size, NULL);
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

#define POCO_NO_PARAMETER POCO_BINDING_PARAMETER_NONE

static void po_release_owned_memory(void* pointer, void* user_data)
{
	(void)user_data;
	po_free(pointer);
}

static const PocoBindingPointerContract file_read_span[] = {
	{0, POCO_POINTER_PERMISSION_WRITE, 1, POCO_BINDING_SPAN_BYTES, 1, 1, 2, POCO_NO_PARAMETER},
};
static const PocoBindingPointerContract file_write_span[] = {
	{0, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_BYTES, 1, 1, 2, POCO_NO_PARAMETER},
};
static const PocoBindingPointerContract fgets_span[] = {
	{0, POCO_POINTER_PERMISSION_WRITE, 1, POCO_BINDING_SPAN_BYTES, 1, 1, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER},
};
static const PocoBindingPointerContract string_read_span[] = {
	{0, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_C_STRING, 0, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER, POCO_NO_PARAMETER},
};
static const PocoBindingPointerContract format_read_span[] = {
	{1, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_C_STRING, 0, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER, POCO_NO_PARAMETER},
};
static const PocoBindingPointerContract memory_copy_spans[] = {
	{0, POCO_POINTER_PERMISSION_WRITE, 1, POCO_BINDING_SPAN_BYTES, 1, 2, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER},
	{1, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_BYTES, 1, 2, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER},
};
static const PocoBindingPointerContract memory_compare_spans[] = {
	{0, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_BYTES, 1, 2, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER},
	{1, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_BYTES, 1, 2, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER},
};
static const PocoBindingPointerContract memory_set_span[] = {
	{0, POCO_POINTER_PERMISSION_WRITE, 1, POCO_BINDING_SPAN_BYTES, 1, 2, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER},
};
static const PocoBindingPointerContract memory_find_span[] = {
	{0, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_BYTES, 1, 2, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER},
};

static const PocoBindingContract fread_contract = {file_read_span, Array_els(file_read_span), {0}};
static const PocoBindingContract fwrite_contract = {
	file_write_span, Array_els(file_write_span), {0}};
static const PocoBindingContract fgets_contract = {
	fgets_span, Array_els(fgets_span), {POCO_POINTER_RETURN_ALIAS, 0}};
static const PocoBindingContract fputs_contract = {
	string_read_span, Array_els(string_read_span), {0}};
static const PocoBindingContract fprintf_contract = {
	format_read_span, Array_els(format_read_span), {0}};
static const PocoBindingContract malloc_contract = {
	NULL,
	0,
	{POCO_POINTER_RETURN_OWNED, POCO_NO_PARAMETER, POCO_BINDING_SPAN_BYTES, 1, 0, POCO_NO_PARAMETER,
	 POCO_NO_PARAMETER, POCO_POINTER_PERMISSION_READ | POCO_POINTER_PERMISSION_WRITE,
	 po_release_owned_memory, NULL}};
static const PocoBindingContract calloc_contract = {
	NULL,
	0,
	{POCO_POINTER_RETURN_OWNED, POCO_NO_PARAMETER, POCO_BINDING_SPAN_BYTES, 1, 0, 1,
	 POCO_NO_PARAMETER, POCO_POINTER_PERMISSION_READ | POCO_POINTER_PERMISSION_WRITE,
	 po_release_owned_memory, NULL}};
static const PocoBindingContract memcpy_contract = {
	memory_copy_spans, Array_els(memory_copy_spans), {POCO_POINTER_RETURN_ALIAS, 0}};
static const PocoBindingContract memmove_contract = {
	memory_copy_spans, Array_els(memory_copy_spans), {POCO_POINTER_RETURN_ALIAS, 0}};
static const PocoBindingContract memcmp_contract = {
	memory_compare_spans, Array_els(memory_compare_spans), {0}};
static const PocoBindingContract memset_contract = {
	memory_set_span, Array_els(memory_set_span), {POCO_POINTER_RETURN_ALIAS, 0}};
static const PocoBindingContract memchr_contract = {
	memory_find_span, Array_els(memory_find_span), {POCO_POINTER_RETURN_ALIAS, 0}};

static const Lib_proto filelib[] = {
	{po_fopen, "FILE    *fopen(char *name, char *mode);", NULL, POCO_BINDING_RUN_CONTEXT},
	{po_fclose, "void    fclose(FILE *f);", NULL, POCO_BINDING_RUN_CONTEXT},
	{po_fread, "int     fread(void *buf, int size, int count, FILE *f);", &fread_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_fwrite, "int     fwrite(void *buf, int size, int count, FILE *f);", &fwrite_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_fprintf, "int     fprintf(FILE *f, char *format, ...);", &fprintf_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_fseek, "int     fseek(FILE *f, long offset, int mode);", NULL, POCO_BINDING_RUN_CONTEXT},
	{po_ftell, "long    ftell(FILE *f);", NULL, POCO_BINDING_RUN_CONTEXT},
	{po_fflush, "int     fflush(FILE *f);", NULL, POCO_BINDING_RUN_CONTEXT},
	{po_getc, "int     getc(FILE *f);", NULL, POCO_BINDING_RUN_CONTEXT},
	{po_fgetc, "int     fgetc(FILE *f);", NULL, POCO_BINDING_RUN_CONTEXT},
	{po_putc, "int     putc(int c, FILE *f);", NULL, POCO_BINDING_RUN_CONTEXT},
	{po_fputc, "int     fputc(int c, FILE *f);", NULL, POCO_BINDING_RUN_CONTEXT},
	{po_fgets, "char    *fgets(char *s, int maxlen, FILE *f);", &fgets_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_fputs, "int     fputs(char *s, FILE *f);", &fputs_contract, POCO_BINDING_RUN_CONTEXT},

	{po_get_errno_pointer, "int     *__GetErrnoPointer(void);"},
	{NULL, "#define errno (*__GetErrnoPointer())"},

};

const Poco_lib po_FILE_lib = {
	NULL, "(C Standard) FILE", filelib, Array_els(filelib), NULL, poco_standard_file_cleanup,
};

/*----------------------------------------------------------------------------
 * protos for memory functions...
 *--------------------------------------------------------------------------*/

static const Lib_proto memlib[] = {
	{po_malloc_in_vm, "void    *malloc(int size);", &malloc_contract, POCO_BINDING_RUN_CONTEXT},
	{po_calloc_in_vm, "void    *calloc(int size_el, int el_count);", &calloc_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_free_in_vm, "void    free(void *pt);", NULL, POCO_BINDING_RUN_CONTEXT},
	{po_memcpy_for_vm, "void    *memcpy(void *dest, void *source, int size);", &memcpy_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_memmove_for_vm, "void    *memmove(void *dest, void *source, int size);", &memmove_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_memcmp_for_vm, "int     memcmp(void *a, void *b, int size);", &memcmp_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_memset_for_vm, "void    *memset(void *dest, int fill_char, int size);", &memset_contract,
	 POCO_BINDING_RUN_CONTEXT},
	{po_memchr_for_vm, "void    *memchr(void *a, int match_char, int size);", &memchr_contract,
	 POCO_BINDING_RUN_CONTEXT},
};

const Poco_lib po_mem_lib = {
	NULL, "(C Standard) Memory Manager", memlib, Array_els(memlib),
	NULL, poco_standard_memory_cleanup,
};

const PocoLibrary* poco_standard_file_library(void)
{
	static PocoBinding bindings[Array_els(filelib) - 1];
	static PocoLibrary library = {
		POCO_STANDARD_FILE_LIBRARY_ID, bindings, 0, NULL, NULL, NULL,
	};
	static int initialized;
	size_t source_index;
	size_t binding_index = 0;

	if (!initialized) {
		for (source_index = 0; source_index < Array_els(filelib); ++source_index) {
			if (filelib[source_index].func == NULL) {
				continue;
			}
			bindings[binding_index].prototype = filelib[source_index].proto;
			bindings[binding_index].function = (PocoNativeFunction)filelib[source_index].func;
			bindings[binding_index].contract = filelib[source_index].contract;
			bindings[binding_index].flags = filelib[source_index].flags;
			++binding_index;
		}
		library.binding_count = binding_index;
		initialized = 1;
	}
	return &library;
}

const PocoLibrary* poco_standard_memory_library(void)
{
	static PocoBinding bindings[Array_els(memlib)];
	static const PocoLibrary library = {
		POCO_STANDARD_MEMORY_LIBRARY_ID, bindings, Array_els(bindings), NULL, NULL, NULL,
	};
	static int initialized;
	size_t index;

	if (!initialized) {
		for (index = 0; index < Array_els(memlib); ++index) {
			bindings[index].prototype = memlib[index].proto;
			bindings[index].function = (PocoNativeFunction)memlib[index].func;
			bindings[index].contract = memlib[index].contract;
			bindings[index].flags = memlib[index].flags;
		}
		initialized = 1;
	}
	return &library;
}

