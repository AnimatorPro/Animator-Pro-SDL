/*
 * poco_frames.h - the units of compilation the parser builds and hands on:
 * code buffers, expression frames, loop/label frames, the scope frame and the
 * compiled function frame.
 */
#ifndef POCO_FRAMES_H
#define POCO_FRAMES_H

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

struct PocoActivation;

/*----------------------------------------------------------------------------
 * function-related flags...
 *--------------------------------------------------------------------------*/

typedef enum cff_type { /* this indicates whether function is poco or C */
						CFF_POCO = 0,
						CFF_C,
} Cff_type;

typedef enum code_magic { /* this validates a code buffer */
						  CCRYPTIC = 0x8765,
						  CTRASHED = 0x8674,
} CodeMagic;

typedef struct code_buf { /* code buffer management... */
	Code* code_pt;
	Code* code_buf;
	Code* alloced_end;
	CodeMagic cryptic;
	Code cbuf[SMALL_CODE_SIZE];
} Code_buf;

/*----------------------------------------------------------------------------
 * looping and label (goto) management structures...
 *--------------------------------------------------------------------------*/

typedef struct use_label {
	struct use_label* next;
	long code_pos;
} Use_label;

typedef struct code_label {
	struct code_label* next;
	long code_pos;
	Use_label* uses;
	Symbol* lvar;
} Code_label;

typedef struct loop_frame {
	struct loop_frame* next;
	Code_label* start;
	Code_label* end;
	PoBoolean is_switch;
	PoBoolean got_default; /* these fields only valid when */
	Type_info* con_type;   /* is_switch is TRUE... 		*/
	long last_case_beq;
	int svar_offset;
} Loop_frame;

/*----------------------------------------------------------------------------
 * expression parsing structures...
 *--------------------------------------------------------------------------*/

typedef struct exp_frame {
	void* next;
	Code_buf ecd;
	Code_buf left;
	Type_info ctc;
	Symbol* var;
	Pt_long ctc_dims[MAX_TYPE_COMPS];
	TypeComp ctc_comp[MAX_TYPE_COMPS];
	int doff;
	PoBoolean includes_function;
	PoBoolean includes_assignment;
	PoBoolean pure_const;
	PoBoolean left_complex;
} Exp_frame;

typedef struct line_data {
	int count;
	int alloc;
	long* offsets;
	long* lines;
} Line_data;

#define CFF_FIELDS      \
	char* name;         \
	short pcount;       \
	short type;         \
	Symbol* parameters; \
	Type_info* return_type;

typedef struct poco_frame {  /* holds the local frame of reference */
	struct poco_frame* next; /* during parsing (scoped vars, etc)  */
	CFF_FIELDS
	Symbol* symbols;
	int doff;
	SHORT scope;
	Symbol* root_sym;
	Symbol** hash_table;
	Code_label* labels;
	Code_buf fcd;
	Struct_info* fsif;
	FrameType frame_type;
	PoBoolean is_proto_frame;
#ifdef STRING_EXPERIMENT
	Local_string* local_string_list; /* tracks string variables */
#endif                               /* STRING_EXPERIMENT */
	Line_data* ld;
} Poco_frame;

typedef struct func_frame {
	/* what's left of a poco_frame after    */
	/* a function is all compiled...		*/
	struct func_frame* next;
	CFF_FIELDS
	const char* unit_name;
	size_t unit_index;
	PoBoolean is_static;
	const PocoBindingContract* binding_contract;
	uint32_t binding_flags;
	long magic;
	Code* code_pt;
	long code_size;
	Line_data* ld;
	PocoDebugLocal* debug_locals;
	struct func_frame* mlink;                /* list of fuf's to free */
	const struct func_frame* compiled_frame; /* activation-handle source */
	struct PocoActivation* activation;       /* owner for callback handles */
	PoBoolean got_code;
	/* Declared inside a '#pragma poco native' region: a CFF_C frame whose
	 * code_pt stays null until the host's binding is found by name at load. */
	PoBoolean host_provided;
} Func_frame;

typedef Func_frame C_frame;

#endif /* POCO_FRAMES_H */
