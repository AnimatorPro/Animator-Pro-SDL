/*
 * Compatibility-only legacy ABI header.
 *
 * Deprecated for new embedding hosts and native modules: use
 * <poco/poco.h>, PocoVm/PocoProgram, PocoLibrary, and PocoModuleDescriptor.
 * This header is the single authoritative definition of the legacy Poco_lib
 * and Pocorex families while Animator and existing POE modules migrate.  Do
 * not copy these layouts into host headers.
 *
 * It lives under poco/compat rather than poco/include because it is not part
 * of Poco's installed public surface: only the two in-tree consumers reach it,
 * through poco/src/pocolib.h and src/inc/pocolib.h.  Include one of those
 * rather than this file directly - each supplies the Errcode domain and the
 * POCO_LEGACY_HOST_* declarations its own tree requires.
 */
#ifndef POCO_LEGACY_H
#define POCO_LEGACY_H

#include <poco/poco.h>
#include <poco/poco_legacy_types.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(ERRCODES_H) && !defined(POCO_ERRCODES_H)
#error "include an Errcode header (errcodes.h or poco_errcodes.h) before <poco/poco_legacy.h>"
#endif

extern const Popot empty_popot;

typedef struct string_ref {
	Dlnode node;
	int ref_count;
	Popot string;
} String_ref;

typedef String_ref* PoString;

#define PoStringBuf(s) ((*(s))->string.pt)

typedef union pt_num /* Overlap popular datatypes in the same space */
{
	int i;
	int inty;
	short s;
	UBYTE* bpt;
	char c;
	long l;
	float f;
	double d;
	void* p;
	int doff;      /* data offset */
	int (*func)(); /* code pointer */
	Popot ppt;
	PoString postring;
} Pt_num;

/*
 * Compatibility callback entry for Animator and legacy POE hosts. Invoke code
 * returned by po_fuf_code() with typed values in source-parameter order. No
 * platform varargs representation crosses this boundary, and the public
 * embedding API does not expose the compiler function pointer.
 */
Errcode poco_invoke_callback(void* code_pt, Pt_num* result, const PocoCallbackValue* values,
							 size_t value_count);

typedef struct lib_proto /* Poco library prototype lines */
{
	void* func;
	char* proto;
	const PocoBindingContract* contract;
	uint32_t flags;
} Lib_proto;

typedef struct poco_lib /* Poco library main control structure */
{
	struct poco_lib* next;
	char* name;
	Lib_proto* lib;
	int count;
	Errcode (*init)(struct poco_lib* lib);
	void (*cleanup)(struct poco_lib* lib);
	void* local_data;
	Dlheader resources;
	void* rexhead;
	PocoVm* vm;
	char reserved[4];
} Poco_lib;

#ifndef RNODE_FIELDS
#define RNODE_FIELDS \
	Dlnode node;     \
	void* resource;
#endif

typedef struct rnode /* Used for resource tracking in builtin libs */
{
	RNODE_FIELDS
} Rnode;

Errcode po_check_formatf(int maxlen, char* fmt, va_list pargs);
Errcode po_init_libs(Poco_lib* lib);
void po_cleanup_libs(Poco_lib* lib);
void po_free(void* pt);
void* po_malloc(int size);
void* po_calloc(int size_el, int el_count);
Popot poco_lmalloc(long size);
Popot poco_lmalloc_for_vm(PocoVm* vm, long size);
void po_free_for_vm(PocoVm* vm, void* pt);
void poco_freez(Popot* pt);

/*
 * Legacy bounded-pointer helper.  It is retained for Animator and POE source
 * compatibility; new generic modules should validate their own inputs and
 * use the canonical PocoModuleDescriptor API.
 */
/* Reports through the activation running on this thread; see
 * poco_active_builtin_error() in pocoface.c. */
Errcode* poco_active_builtin_error(void);
#define Popot_bufcheck(p, length)          \
	(*poco_active_builtin_error() =        \
		 (((p)->pt == NULL) ? Err_null_ref \
							: ((Popot_bufsize((p)) < (length)) ? Err_buf_too_small : Success)))

#ifndef Array_els
#define Array_els(array) (sizeof(array) / sizeof((array)[0]))
#endif
#define Popot_bufsize(p) ((size_t)((char*)((p)->max) - (char*)((p)->pt) + 1))
#define Popot_make_null(p) ((p)->pt = (p)->min = (p)->max = NULL)

/*****************************************************************************
 * Compatibility-only legacy POE module ABI.
 *
 * Deprecated for new modules: export poco_module_get from <poco/poco.h> and
 * return a PocoModuleDescriptor.  Pocorex/poco_rexlib_get remain available
 * only while existing Animator POE modules migrate.
 ****************************************************************************/

#if defined(_WIN32)
#define POCO_EXPORT __declspec(dllexport)
#else
#define POCO_EXPORT __attribute__((visibility("default")))
#endif

typedef struct pocorex_hdr {
	USHORT version;
	/* Equivalent of Animator's EFUNC and VFUNC */
	Errcode (*init)();
	void (*cleanup)();
	char* id_string;
} Pocorex_hdr;

#define POCOREX_VERSION 200

typedef struct pocorex {
	Pocorex_hdr hdr;
	Poco_lib lib;
} Pocorex;

typedef Pocorex* (*Poco_rexlib_get_func)(void);

#define Setup_Pocorex(init, cleanup, libname, libprotos)                         \
	static char _l_name[] = libname;                                             \
	Pocorex rexlib_header = {                                                    \
		{POCOREX_VERSION, init, cleanup, _l_name},                               \
		{NULL, _l_name, libprotos, (sizeof(libprotos) / sizeof(libprotos[0]))}}; \
	POCO_EXPORT Pocorex* poco_rexlib_get(void)                                   \
	{                                                                            \
		return &rexlib_header;                                                   \
	}

#endif /* POCO_LEGACY_H */
