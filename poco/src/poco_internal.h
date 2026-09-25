/*****************************************************************************
 *
 * poco_internal.h	- Main private header file for compiling the Poco compiler.
 *					  The public embedding API lives in <poco/poco.h>; nothing
 *					  outside poco/src (and poco's own tests) includes this.
 *
 * MAINENANCE
 *	08/20/90	(Ian)
 *				General cleanup.  Made sure all globally-visible routines
 *				in poco have a prototype in here, or in one of the header
 *				files included from here.  Changed lotsa #defines to enums.
 *	08/24/90	(Ian)
 *				Added cached_frame field to the exp_frame structure, as an
 *				assist to the new espression frame caching logic.  See
 *				comments in poco.c for more details.
 * 08/25/90 	(Ian)
 *				Added #define for poc_gentle_freemem(), to check for NULL
 *				pointer inline instead of calling a routine to do it.
 * 08/28/90 	(Ian)
 *				Removed the cached_frame field from exp_frame & code_buf
 *				structs; new memory management routines don't require it anymore.
 *				Added CACHED_CODE_SIZE constant to define the size of a
 *				code buffer in cache.  Added Cache_ctl structure definition,
 *				and added instances of this struct into the Poco_cb for
 *				poco_frame, expression_frame, and code_buf caching. Added
 *				prototypes for items in new POCMEMRY.C module.
 * 08/29/90 	(Ian)
 *				Changed prototype for po_compile_file(), it takes less parms now.
 * 08/30/90 	(Ian)
 *				Changed CACHED_CODE_SIZE to SMALLBLK_CACHE_SIZE. Added
 *				prototype for po_code_elsize() routine, now globally visible.
 *				Increased SMALL_CODE_SIZE from 32 to 48, (this reduced the
 *				need for new code_buf areas by, like, a factor of five).
 * 09/04/90 	(Ian)
 *				Added parmcount field to Text_symbol structure for new pp,
 *				and changed is_macro field to flags field, of which is_macro
 *				is one of the possible flag values.
 * 09/08/90 	(Ian)
 *				Added else_done field to Conditional struct, for #elif.
 * 09/19/90 	(Ian)
 *				Added support for union (is_union field in Struct_info).
 *				Added TYPE_xxxx constants for all ANSI type keywords and
 *				modifiers (const, auto, etc).
 * 10/01/90 	(Ian)
 *				Added support for enum (is_union field changed to type in
 *				Struct_info, TYPE_ENUM added to list.)
 * 10/21/90 	(Ian)
 *				Nuked the next_line and next_line_data fields in the pcb,
 *				added the library_protos field.
 * 10/22/90 	(Ian)
 *				Eliminated the ttype, ctoke, val, and dval fields from the
 *				token structure; they are now part of the Tstack structure.
 *				Added curtoken and free_tokens fields, and eliminated ts[1]
 *				from the Poco_cb.
 * 10/24/90 	(Ian)
 *				Removed array_dims field from Exp_frame struct -- nothing
 *				was using it.  Removed proto for po_cat_exp(), it is now
 *				static in bop.c.
 *	10/25/90	(Ian)
 *				Added flags field to File_stack, groundwork for using the
 *				structure for both files and builtin libraries.
 *	10/26/90	(Ian)
 *				Added toktype field to token structure so that token type
 *				can be accessed without dereferencing the curtoken pointer.
 *				Added new datatype PoBoolean, typedef'd as a byte.  This is
 *				used selectively to speed up testing of Booleans.  Right now
 *				it's being used only for the t.reuse flag.
 *	05/01/91	(Ian)
 *				Added enable_debug_trace field to Poco_run_env structure.
 *				This field will contain FALSE when the interpreter is running
 *				to fold constants during the compile phase, and TRUE when
 *				running the compiled program.  If an error occurs when
 *				folding constants, we don't want a function call trace to be
 *				generated (indeed, the code in TRACE.C fails if it is invoked
 *				during the compile phase).	Also, the global builtin_err var
 *				now belongs to us, not to the host.  Added extern for it here.
 *	09/06/91	(Jim)
 *				Started to add in String type.	Changed unused TYPE_RAST
 *				to TYPE_STRING.  Eliminated TYPE_NCPT.
 *				Added po_ido_table.
 *	09/08/91	(Jim)
 *				Made all string stuff conditionally compiled with
 *				STRING_EXPERIMENT, as it doesn't look like this is
 *				going to be a production feature.
 *	06/12/92	(Ian)
 *				Another general cleanup.  Mostly, converted things to enums
 *				and added typedefs for enumerated types, because debuggers
 *				like enumerated things.
 ****************************************************************************/

#ifndef POCO_INTERNAL_H
#define POCO_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * #include's used by most everything in poco...
 *
 * poco_limits.h holds the fixed sizes and tweakables; the three type headers
 * hold the compiler's shared vocabulary.  What is left in this file is the
 * state the compiler carries across a whole compile -- Poco_run_env and
 * Poco_cb -- plus the prototypes of poco.c itself.
 ****************************************************************************/

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "poco_limits.h"

#ifndef POCO_STDTYPES_H
#include "stdtypes.h"
#endif

#include "port.h"

#ifndef TOKEN_H
#if defined token_t
#undef token_t
#endif

#include "token.h"
#endif

#ifndef POCO_ERRCODES_H
#include "poco_errcodes.h"
#endif

#ifndef POCO_PTRMACRO_H
#include "ptrmacro.h"
#endif

#ifndef POCOOP_H
#include "pocoop.h"
#endif

#ifndef POCO_LINKLIST_H
#include "linklist.h"
#endif

/* The low-level shared types only.  pocoface.h itself now sits *above* this
 * header (it declares entry points that take a Poco_cb*), so it is included by
 * the modules that call into it, not from here. */
#ifndef POCO_NAMES_H
#include "poco_names.h"
#endif

#ifndef POCOLIB_H
#include "pocolib.h"
#endif

#ifndef LIBFFI_H
#include <ffi.h>
#endif

#include "poco_typemodel.h"
#include "poco_frames.h"
#include "poco_pp_state.h"

/*****************************************************************************
 * compiler-wide state
 ****************************************************************************/

typedef struct cache_ctl { /* Control structure for struct caching.	*/
	UBYTE* pbase;          /* -> base of struct cache memory			*/
	SHORT slot_size;       /* size of one item in the cache			*/
	SHORT num_slots;       /* number of items allocated in the cache	*/
	SHORT nxt_slot;        /* next slot to check when allocating		*/
	UBYTE* inuse;          /* -> table of cache slots in use.			*/
} Cache_ctl;

/*----------------------------------------------------------------------------
 * FFI handles.  The binding layout and its calls live in poco_ffi.h; these two
 * opaque handles are here because Poco_run_env carries them.
 *--------------------------------------------------------------------------*/

/* Wrapper for HashMap, forward declaraton */
struct po_func_map;
typedef struct po_func_map Po_FuncMap;

/* VM-owned metadata for Popot/host spans used by contracted FFI calls. */
typedef struct poco_pointer_registry PocoPointerRegistry;
/*----------------------------------------------------------------------------
 * the compiled program image.
 *
 * This is what the compiler produces and what Poco_program_code is populated
 * from; it holds no per-run state.  Everything a running program mutates --
 * the stack and data areas, the abort hook, the trace settings, the result and
 * builtin_error, the variadic descriptor and the pointer registry -- lives in
 * PocoActivation (activation.h) instead.
 *--------------------------------------------------------------------------*/

typedef struct poco_run_env {
	long stack_size;
	long data_size;
	Func_frame* fff;
	Names* literals; /* string constants */
	Poco_lib* lib;   /* list of arrays of library function info */
	Func_frame* protos;
	Struct_info* struct_infos; /* retained compiler layouts used by FFI descriptors */
	Poco_lib* loaded_libs;     /* loaded (from disk via pragma) libraries */
	Po_FuncMap* func_map;      /* for fast lookups of C function calls */
	PocoVm* vm;                /* owning VM; supplies the active run context */
	void* compile_pcb;         /* Poco_cb used during compilation; for cleanup */
} Poco_run_env;

/*----------------------------------------------------------------------------
 * the poco control block, used during compile...
 *--------------------------------------------------------------------------*/

typedef struct poco_cb {
	Poco_run_env run; /* the program image being emitted */
	FILE* po_dump_file;
	void* libfunc;
	const PocoBindingContract* libcontract;
	uint32_t libflags;
	Poco_lib* builtin_lib;
	char* stack_bottom;
	PreprocessorState t;
	Tstack* curtoken;
	Tstack* free_tokens;
	Symbol* fsym;
	Errcode global_err;
	long error_line_number;
	long error_char_number;
	struct poco_frame* rframe;
	Loop_frame* loops;
	C_frame* cframes;
	UBYTE qbop_table[PTOK_MAX]; /* table for binary operations */
	char strlit_work[MAX_STRLIT_LEN + SZTOKE];
	Cache_ctl smallblk_cache;      /* Small block cache control */
	Cache_ctl expf_cache;          /* Expression frame cache control */
	Cache_ctl pocf_cache;          /* Poco frame cache control */
	struct mblk_ctl* mblk_cur;     /* Current compiler allocation arena block */
	char* pp_eval_lbuf;            /* Preprocessor evaluator input cursor */
	char* pp_eval_tok;             /* Preprocessor evaluator token buffer */
	bool pp_eval_reuse;            /* Reuse the current preprocessor token */
	SHORT pp_eval_ttype;           /* Current preprocessor evaluator token type */
	const char* current_unit_name; /* Current translation-unit identity */
	size_t current_unit_index;
	const size_t* current_use_indices; /* directly used, already compiled units */
	size_t current_use_count;
	bool compile_aborted; /* set to true when a fatal error occurs */
	Errcode compile_err;  /* the error code from the abort */
} Poco_cb;

/*****************************************************************************
 * macros for inline-ing common code fragments...
 ****************************************************************************/

#define Alloc_a(pcb, type) po_memalloc(pcb, sizeof(type))
#define Alloc_z(pcb, type) po_memzalloc(pcb, sizeof(type))

#define poc_gentle_freemem(a) \
	if ((a) != NULL) po_freemem((a))

#define any_code(pcb, c) ((c)->code_pt != (c)->code_buf)
#define clear_code_buf(pcb, c) ((c)->code_pt = (c)->code_buf)

#define PO_CHECK_ABORT(pcb, err)      \
	do {                              \
		if ((pcb)->compile_aborted) { \
			return (err);             \
		}                             \
	} while (0)

#define PO_CHECK_ABORT_VOID(pcb)      \
	do {                              \
		if ((pcb)->compile_aborted) { \
			return;                   \
		}                             \
	} while (0)

#define pushback_token(t) (t)->reuse = true
#define lookup_token(pcb)       \
	if ((pcb)->t.reuse)         \
		(pcb)->t.reuse = false; \
	else                        \
		po_lookup_freshtoken((pcb));

#define po_find_pop_op(pcb, ti) (po_pop_ops[(ti)->ido_type])
#define po_find_push_op(pcb, ti) (po_push_ops[(ti)->ido_type])
#define po_find_clean_op(pcb, ti) (po_clean_ops[(ti)->ido_type])

/*****************************************************************************
 * Prototypes for all global vars and routines in Poco...
 ****************************************************************************/

extern const Ido_table po_ido_table[];

/* Byte and string helpers that used to live in the hand-written pocoutil.asm.
 * There is no assembly module any more; these are plain library calls. */

#define po_cmatch_scan(line) strpbrk((line), "\"'/")
#define poco_copy_bytes(s, d, c) memcpy((d), (s), (c))
#define poco_zero_bytes(d, c) memset((d), 0, (c))
#define poco_stuff_bytes(d, v, c) memset((d), (v), (c))
#define po_eqstrcmp strcmp

/*----------------------------------------------------------------------------
 * poco.c -- the compiler driver: symbol table, expression parser, unit linker
 * and diagnostics.  Every other module declares itself in its own header now:
 * bop.h, chopper.h, code.h, declare.h, fold.h, funccall.h, mathlib.h,
 * pocmemry.h, poco_ffi.h, pocodis.h, pocoface.h, pocolib.h, pocotype.h,
 * postring.h, pp.h, ppeval.h, runops.h, safefile.h, statemen.h, strlib.h,
 * struct.h, token.h, trace.h and varinit.h.
 *--------------------------------------------------------------------------*/

extern const int po_scoped_address_op[2]; /* indexed by StorageScope */

int po_hashfunc(UBYTE* s);
void po_say_warning(Poco_cb* pcb, const char* fmt, ...);
void po_say_fatal(Poco_cb* pcb, const char* fmt, ...);
void po_say_internal(Poco_cb* pcb, const char* fmt, ...);
void po_expecting_got(Poco_cb* pcb, const char* expecting);
void po_expecting_got_str(Poco_cb* pcb, const char* expecting, const char* got);
bool po_need_token(Poco_cb* pcb);
void po_redefined(Poco_cb* pcb, char* s);
void po_undefined(Poco_cb* pcb, char* s);
void po_unmatched_paren(Poco_cb* pcb);
void po_expecting_lbrace(Poco_cb* pcb);
void po_expecting_rbrace(Poco_cb* pcb);
void po_freelist(void* l);
Symbol* po_unlink_el(Symbol* list, Symbol* el);
void po_unhash_symbol(Poco_cb* pcb, Symbol* s);
int po_rehash(Poco_cb* pcb, Symbol* s);
void po_free_symbol(Symbol* s);
void po_free_symbol_list(Symbol** ps);
Symbol* po_new_symbol(Poco_cb* pcb, char* name);
int po_link_len(Symbol* l);
void* po_reverse_links(Symbol* el);
void po_lookup_freshtoken(Poco_cb* pcb);
bool po_is_next_token(Poco_cb* pcb, SHORT ttype);
bool po_eat_token(Poco_cb* pcb, SHORT ttype);
bool po_eat_rbracket(Poco_cb* pcb);
bool po_eat_lparen(Poco_cb* pcb);
bool po_eat_rparen(Poco_cb* pcb);
bool po_check_rparen(Poco_cb* pcb);
SHORT po_find_local_assign(Poco_cb* pcb, Type_info* ti);
SHORT po_find_assign_op(Poco_cb* pcb, Symbol* var, Type_info* ti);
void po_var_too_complex(Poco_cb* pcb);
SHORT po_find_local_use(Poco_cb* pcb, Type_info* ti);
void po_code_elsize(Poco_cb* pcb, Exp_frame* e, int el_size);
void po_make_deref(Poco_cb* pcb, Exp_frame* e);
void po_new_var_space(Poco_cb* pcb, Symbol* var);
int po_get_temp_space(Poco_cb* pcb, int space);
void po_init_expframe(Poco_cb* pcb, Exp_frame* e);
Exp_frame* po_new_expframe(Poco_cb* pcb);
void po_trash_expframe(Poco_cb* pcb, Exp_frame* e);
void po_dispose_expframe(Poco_cb* pcb, Exp_frame* e);
SHORT po_force_num_exp(Poco_cb* pcb, Type_info* ti);
SHORT po_force_int_exp(Poco_cb* pcb, Type_info* ti);
SHORT po_force_ptr_or_num_exp(Poco_cb* pcb, Type_info* ti);
void po_coerce_to_boolean(Poco_cb* pcb, Exp_frame* e);
void po_coerce_numeric_exp(Poco_cb* pcb, Exp_frame* e, SHORT ido_type);
void po_coerce_expression(Poco_cb* pcb, Exp_frame* e, Type_info* ti, bool recast);
void po_get_prim(Poco_cb* pcb, Exp_frame* e);
void po_get_unop_expression(Poco_cb* pcb, Exp_frame* e);
bool po_assign_after_equals(Poco_cb* pcb, Exp_frame* e, Symbol* var, bool must_be_static_init);
void po_get_expression(Poco_cb* pcb, Exp_frame* e);
void po_get_comma_expression(Poco_cb* pcb, Exp_frame* e);
bool po_new_frame(Poco_cb* pcb, int scope, char* name, int type);
void po_old_frame(Poco_cb* pcb);
bool po_check_undefined_funcs(Poco_cb* pcb, Symbol* sl);
Errcode po_compile_file(Poco_cb* pcb, char* name);
Errcode po_compile_buffer(Poco_cb* pcb, char* name, const char* source, size_t source_length);
bool po_link_compiled_units(Poco_cb* pcb);
void po_free_run_env(Poco_run_env* pev);

/* end of protos */

// kiki additions
#define plural(x) (x == 1 ? "" : "s")

#ifdef __cplusplus
}
#endif

#endif /* POCO_INTERNAL_H */
