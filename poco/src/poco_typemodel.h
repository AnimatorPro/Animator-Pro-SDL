/*
 * poco_typemodel.h - the compiler's type and symbol vocabulary: the TypeComp
 * component list, the Ido expression types, Type_info, Symbol and the
 * struct/enum layout records.
 */
#ifndef POCO_TYPEMODEL_H
#define POCO_TYPEMODEL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "poco_limits.h"
#include "stdtypes.h"
#include "token.h"
#include "pocolib.h"
#include "poco_names.h"
#include "pocoop.h"

/*----------------------------------------------------------------------------
 * type names and modifiers...
 *--------------------------------------------------------------------------*/

typedef enum type_comp {
	TYPE_END,
	TYPE_CHAR,
	TYPE_UCHAR,
	TYPE_SHORT,
	TYPE_USHORT,
	TYPE_INT,
	TYPE_UINT,
	TYPE_LONG,
	TYPE_ULONG,
	TYPE_FLOAT,
	TYPE_DOUBLE,
	TYPE_POINTER,
	TYPE_FUNCTION, /* poco function prototype */
	TYPE_VOID,
	TYPE_ARRAY,
	TYPE_ELLIPSIS,
	TYPE_SCREEN,
#ifdef STRING_EXPERIMENT
	TYPE_STRING,
#endif /* STRING_EXPERIMENT */
	TYPE_FILE,
	TYPE_UNUSED0,
	TYPE_CPT, /* C pointer - parameter to printf most likely... */
	TYPE_UNUSED1,
	TYPE_STRUCT, /* End of "Base" types */

	TYPE_UNUSED2,
	TYPE_CFUNCTION, /* poco library function prototype */
	TYPE_UNION,     /* Converted to TYPE_STRUCT shortly */
	TYPE_ENUM,      /* Converted to TYPE_INT shortly */

	TYPE_REGISTER, /* type modifiers... these are not types, as such, */
	TYPE_AUTO,     /* they are used only during the parsing of a type */
	TYPE_EXTERN,
	TYPE_STATIC,
	TYPE_SIGNED,
	TYPE_UNSIGNED,
	TYPE_CONST,
	TYPE_VOLATILE,

	TYPE_BAD,
} TypeComp;

/*----------------------------------------------------------------------------
 * type flags (used in type_info)...
 *--------------------------------------------------------------------------*/

typedef enum type_flags {
	TFL_STATIC = 1,
	TFL_SHORT = 2,
	TFL_LONG = 4,
	TFL_SIGNED = 8,
	TFL_UNSIGNED = 16,
	TFL_EXTERN = 32,
} TypeFlags;

/*----------------------------------------------------------------------------
 * expression types...
 *--------------------------------------------------------------------------*/

typedef enum ido_type {
	IDO_BAD = -1,
	IDO_INT,     /* int or promoted short or char */
	IDO_LONG,    /* long */
	IDO_DOUBLE,  /* double (or promoted float) */
	IDO_POINTER, /* a Poco 12 byte bounds checked pointer */
	IDO_CPT,     /* a C type 4 byte pointer */
	IDO_VOID,    /* void - only used to generate C function calls */
	IDO_VPT,     /* a function - no code generated for this type */
#ifdef STRING_EXPERIMENT
	IDO_STRING, /* a String type. */
#endif          /* STRING_EXPERIMENT */
	IDO_STRUCT, /* an aggregate passed to C by value */
	IDO_LAST
} IdoType;

#define NUM_IDOS (IDO_LAST)

/*----------------------------------------------------------------------------
 * token types for keyword tokens...
 *--------------------------------------------------------------------------*/

typedef enum ptoken_t {
	PTOK_TYPE = TOK_TOK_MAX,
	PTOK_UNUSED1,
	PTOK_QUO,
	PTOK_ELLIPSIS,
	PTOK_LIT,
	PTOK_FOR,
	PTOK_IF,
	PTOK_WHILE,
	PTOK_RETURN,
	PTOK_SWITCH,
	PTOK_GOTO,
	PTOK_DO,
	PTOK_ELSE,
	PTOK_BREAK,
	PTOK_CONTINUE,
	PTOK_VAR,
	PTOK_LABEL,
	PTOK_ENUMCONST,
	PTOK_SIZEOF,
	PTOK_UNDEF,
	PTOK_NULL,
	PTOK_TYPEDEF,
	PTOK_USER_TYPE,
	PTOK_CASE,
	PTOK_DEFAULT,
	PTOK_MAX, /* this must be last! */
} PToken_t;

/*
 * poco_token_t and ptoken_t are two halves of one contiguous token space:
 * ptoken_t starts at TOK_TOK_MAX, exactly where poco_token_t stops, so a
 * poco_token_t value is always a valid PToken_t.  Token fields are typed
 * PToken_t (see Tstack::type), which makes every store of a lexer token an
 * enum-to-enum conversion.  Route those through this helper so the crossing
 * is explicit and the shared numbering stays documented in one place.
 */
static inline PToken_t po_ptoken(Token_t token)
{
	return (PToken_t)token;
}

/*----------------------------------------------------------------------------
 * symbol flags...
 *--------------------------------------------------------------------------*/

typedef enum symbol_flags {
	SFL_USED = 1,
	SFL_READ = 2,
	SFL_WRITTEN = 4,
	SFL_DEFINED = 8,
	SFL_ELLIP = 16,  /* it's a '...' parameter */
	SFL_STATIC = 32, /* it was declared with 'static' */
} SymbolFlags;

typedef enum storage_scope { /* this indiates the storage scope of a variable */
							 SCOPE_GLOBAL = 0,
							 SCOPE_LOCAL,
} StorageScope;

typedef enum frame_type { /* this indicates the type of a func/struct frame */
						  FTY_GLOBAL = 0,
						  FTY_FUNC,
						  FTY_STRUCT,
} FrameType;

/*----------------------------------------------------------------------------
 * type management structures...
 *--------------------------------------------------------------------------*/

typedef union pt_long { /* overlap a pointer and a longword */
	long l;
	void* pt;
} Pt_long;

typedef struct type_info { /* type information management... */
	TypeComp* comp;
	Pt_long* sdims; /* just for arrays/structures/functions */
	UBYTE comp_alloc;
	UBYTE comp_count;
	TypeFlags flags;
	IdoType ido_type;
} Type_info;

typedef struct itypi /* structure used during type parsing... */
{
	Type_info iti;
	Pt_long dimc[MAX_TYPE_COMPS];
	TypeComp typec[MAX_TYPE_COMPS];
} Itypi;

typedef struct {
	PoBoolean is_num;  /* is it numeric */
	PoBoolean can_add; /* can you do a+b?	(pointer or numeric) */
	IdoType ido_type;  /* cross-check */
} Ido_table;

/*----------------------------------------------------------------------------
 * symbol management structures...
 *--------------------------------------------------------------------------*/

typedef struct symbol {
	struct symbol* next;
	struct symbol* link;
	Pt_num symval;
	char* name;
	const char* unit_name;
	size_t unit_index;
	PToken_t tok_type;
	SHORT scope;
	StorageScope storage_scope;
	SymbolFlags flags;
	Type_info* ti;
} Symbol;

typedef struct poco_debug_local {
	struct poco_debug_local* next;
	char* name;
	long frame_offset;
	long live_start;
	long live_end;
	SHORT scope;
	StorageScope storage_scope;
	PoBoolean live_range_approximate;
	Type_info* type;
} PocoDebugLocal;

typedef struct struct_info {
	struct struct_info* next;
	char* name;
	Symbol* elements;
	long size;
	SHORT el_count;
	TypeComp type; /* TYPE_STRUCT, TYPE_UNION, TYPE_ENUM */
} Struct_info;

#ifdef STRING_EXPERIMENT

typedef struct local_string {
	struct local_string* next;
	Symbol* string_symbol;
} Local_string;

#endif /* STRING_EXPERIMENT */

#endif /* POCO_TYPEMODEL_H */
