/*
 * Compatibility-only legacy ABI header.
 *
 * Deprecated for new embedding hosts and native modules: use
 * <poco/poco.h>, PocoVm/PocoProgram, PocoLibrary, and PocoModuleDescriptor.
 * This header remains the single authoritative definition of the legacy
 * Poco_lib family while Animator and existing POE modules migrate.  Do not
 * copy these layouts into host headers.
 */
#ifndef POCOLIB_H
#define POCOLIB_H

#include "poco/poco.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef LINKLIST_H
#include "linklist.h"
#endif

#ifndef STDTYPES_H
#include "stdtypes.h"
#endif
#ifndef POCO_ERRCODES_H
#include "poco_errcodes.h"
#endif

#include <stdarg.h>

extern Popot empty_popot;

typedef struct string_ref
{
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
	ULONG ul;
	float f;
	double d;
	void* p;
	int doff;    /* data offset */
	int (*func)(); /* code pointer */
	Popot ppt;
	PoString postring;
} Pt_num;

/*
 * Compatibility callback entry for Animator and legacy POE hosts.  The
 * canonical embedding API deliberately does not expose compiler function
 * pointers; this typed value list is the only retained way for those legacy
 * callers to resume a Poco function supplied by a running program.
 */
typedef enum PocoCallbackValueKind
{
	POCO_CALLBACK_VALUE_INT,
	POCO_CALLBACK_VALUE_LONG,
	POCO_CALLBACK_VALUE_DOUBLE,
	POCO_CALLBACK_VALUE_POPOT
} PocoCallbackValueKind;

typedef struct PocoCallbackValue
{
	PocoCallbackValueKind kind;
	union
	{
		int int_value;
		long long_value;
		double double_value;
		Popot popot_value;
	} value;
} PocoCallbackValue;

/*
 * Invoke code returned by po_fuf_code() with values in source-parameter
 * order.  No platform varargs representation crosses this boundary.
 */
Errcode poco_invoke_callback(void *code_pt, Pt_num *result,
	const PocoCallbackValue *values, size_t value_count);

typedef struct lib_proto /* Poco library prototype lines */
{
	void* func;
	char* proto;
	const PocoBindingContract* contract;
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
	char reserved[12];
} Poco_lib;

#ifndef RNODE_FIELDS
#define RNODE_FIELDS Dlnode node; void* resource;
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
void poco_freez(Popot* pt);

/*
 * Legacy bounded-pointer helper.  It is retained for Animator and POE source
 * compatibility; new generic modules should validate their own inputs and
 * use the canonical PocoModuleDescriptor API.
 */
extern Errcode builtin_err;
#define Popot_bufcheck(p, length) \
	(builtin_err = (((p)->pt == NULL) ? Err_null_ref : \
		((Popot_bufsize((p)) < (length)) ? Err_buf_too_small : Success)))

#ifndef Array_els
#define Array_els(array) (sizeof(array)/sizeof((array)[0]))
#endif
#define Popot_bufsize(p) ((size_t)((char*)((p)->max) - (char*)((p)->pt) + 1))
#define Popot_make_null(p) ((p)->pt = (p)->min = (p)->max = NULL)

#endif /* POCOLIB_H */
