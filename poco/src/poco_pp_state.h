/*
 * poco_pp_state.h - preprocessor and token-stack state.
 *
 * PreprocessorState is embedded in Poco_cb, which is why these layouts are a
 * header of their own rather than living in pp.h with the routines that drive
 * them.
 */
#ifndef POCO_PP_STATE_H
#define POCO_PP_STATE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "poco_limits.h"
#include "stdtypes.h"
#include "token.h"
#include "pocolib.h"
#include "poco_names.h"
#include "pocoop.h"
#include "poco_typemodel.h"

/*----------------------------------------------------------------------------
 * preprocessor flags...
 *--------------------------------------------------------------------------*/

typedef enum {
	FSF_ISFILE = 1,
	FSF_ISLIB = 2,
	FSF_MACSUB = 4,
	FSF_ISBUFFER = 8,
} Fsflags;

typedef enum {
	TSFL_HASPARMS = 1,  /* Complex (has parms) macro				*/
	TSFL_ISSPECIAL = 2, /* Special-handling macro (__TIME__, etc)	*/
	TSFL_ISBUILTIN = 4, /* Builtin (cannot #undef) macro			*/
} Ts_flags;

typedef enum {
	PPTOK_PARMN = 1, /* For macros with parms, normal parm follows */
	PPTOK_PARMQ = 2, /* For macros with parms, quoted parm follows */
	PPTOK_SFILE = 1, /* For ISSPECIAL macros, __FILE__	*/
	PPTOK_SLINE = 2, /* For ISSPECIAL macros, __LINE__	*/
	PPTOK_SDATE = 3, /* For ISSPECIAL macros, __DATE__	*/
	PPTOK_STIME = 4, /* For ISSPECIAL macros, __TIME__	*/
	PPTOK_SNULL = 5, /* For ISSPECIAL macros, NULL		*/
} PPToken;

typedef enum {
	PP_NO_ELSE_SEEN = 0,
	PP_DONE_ELSE,
	PP_DONE_ELIF,
} PP_else_state;

/*----------------------------------------------------------------------------
 * preprocessor structures...
 *--------------------------------------------------------------------------*/

typedef struct file_stack { /* file management structure for #includes */
	struct file_stack* pred;

	union {
		FILE* file;
		Poco_lib* lib;

		struct {
			const char* data;
			size_t length;
			size_t position;
		} buffer;
	} source;

	char* name;
	long line_count;
	Fsflags flags;
	PoBoolean native_region; /* '#pragma poco native' state on entry */
} File_stack;

typedef struct text_symbol {
	struct text_symbol* next;
	char* name;
	char* value;
	SHORT parmcount;
	Ts_flags flags;
} Text_symbol;

typedef struct conditional {
	struct conditional* next;
	PoBoolean state;          /* current arm: active or not?	*/
	PP_else_state else_state; /* track handling of #elif arms */
} Conditional;

/*----------------------------------------------------------------------------
 * token-related structures...
 *--------------------------------------------------------------------------*/

typedef union tunion {
	char* string;
	long num;
	Symbol* symbol;
	double dnum;
} Tunion;

typedef struct tstack {
	struct tstack* next;
	Tunion val;
	PToken_t type;
	long line_num;
	short char_num;
	short ctoke_size;
	char ctoke[MAX_SYM_LEN];
	PoBoolean is_symbol;
	PoBoolean native_region; /* token came from a '#pragma poco native' region */
} Tstack;

/*----------------------------------------------------------------------------
 * preprocessor state: the macro table, the include-file stack, the search
 * directories and the two line buffers the token chopper works over.  (This
 * was historically named Token; the actual token is Tstack/PToken_t.)
 *
 * pp_expand() in pp.c relies on line_b1 and line_b2 being physically adjacent
 * so it can treat them as one 2*SZTOKE buffer; see the comments in pp.c.  The
 * static assertion below is the enforcement of that layout contract.
 *--------------------------------------------------------------------------*/

typedef struct preprocessor_state {
	PoBoolean reuse;
	UBYTE out_of_it;
	PToken_t toktype;
	FILE* err_file;
	File_stack* file_stack;
	const char* script_path; /* physical main script path; NULL for buffers */
	char* line_buf;
	char* line_pos;
	Conditional* ifdef_stack;
	Names* include_dirs; /* directory to find include files */
	Names* library_dirs; /* host-added directories to find .poe modules */
	Names* pre_defines;  /* symbols DEFINED before we start */
	PoBoolean verbose;   /* enable verbose debug output for searches */
	/* inside a '#pragma poco native begin' ... 'end' region: the declarations
	 * it encloses are provided by the host and resolved by name at load. */
	PoBoolean native_region;
	struct text_symbol* define_list[HASH_SIZE];
	char line_b1[SZTOKE];
	char line_b2[SZTOKE];
} PreprocessorState;

_Static_assert(offsetof(PreprocessorState, line_b2) ==
				   offsetof(PreprocessorState, line_b1) + SZTOKE,
			   "pp_expand() requires line_b1 and line_b2 to be adjacent");

#endif /* POCO_PP_STATE_H */
