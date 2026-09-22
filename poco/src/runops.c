/*****************************************************************************
 *
 * runops.c - A stack-based virtual machine for running poco programs.
 *
 * MAINTENANCE
 *	08/30/90	(Ian)
 *				Fixed OP_DEQ logic; it was comparing a double to an int,
 *				now it compares two doubles.
 *	09/21/90	(Ian)
 *				Changed all comparison ops (LE, GT, etc) to leave an INT on
 *				the stack.
 *	10/01/90	(Ian)
 *				Changed the OP_LEAVE instruction to clean local vars off the
 *				stack by restoring the base register to the stack pointer,
 *				instead of adding an offset value that was formerly coded
 *				as part of the instruction.  The base register was already
 *				extant in the interpreter.	This fixes a bug in which
 *				incorrect offset values would get coded into the OP_LEAVE
 *				instruction if it got generated several times in a function
 *				(ie, function had multiple return statements), and other
 *				things such as switch() statements grabbed extra temp space
 *				from the stack later in the function.
 *	10/22/90	(Ian)
 *				Fixed a bug in OP_ICCALL...the instruction pointer was being
 *				advanced before the check of 'cerr'. this caused problems
 *				with the po_print_trace() reporting when 'cerr' was non-zero,
 *				because the instruction pointer no longer pointed to the
 *				call instruction that resulted in the error.
 *	10/24/90	(Ian)
 *				Added new opcode OP_PTRDIFF to subtract two pointers.
 *	10/27/90	(Ian)
 *				Added integrity check to OP_CALLI processing.  The func_frame
 *				structure now contains a magic number field, and we verify
 *				that magic number before attempting to make an indirect
 *				function call.	This helps catch situations where something
 *				was cast to a function pointer that never should have been.
 *	11/28/90	(Ian)
 *				Added macros and protos for the new C function call glue
 *				routines found in runccall.asm.  This allows direct calls
 *				to C functions with proper parameter passing (ie, it removes
 *				the old limitation of 8 longwords of parms on the stack, with
 *				the rest accessible via poco_get_stack()).	This is required
 *				to support POE modules, which don't have access to the
 *				poco_get_stack() routine.  When compiled under TC, the old
 *				calling mechanism is used, except that it now stacks 32
 *				longwords of data instead of 8.  This is a big performance
 *				hit, but we don't care about performance under TC anyway.
 *	05/01/91	(Ian)
 *				Revamped trapping of floating point errors on the host side
 *				and detection and reporting in here.  The old 'ferr' global
 *				var was not being used at all, it's gone now.  On the host
 *				side, any fp-related err will now result in Err_float being
 *				stored in p->builtin_error.	If the
 *				error occurs in a library routine (eg, sqrt()), the existing
 *				error detection following a function call will catch it.
 *				Additional checks of p->builtin_error were added following floating
 *				point math calculations (OP_DMUL, OP_DDIV, etc) to catch
 *				overflow/underflow and div-by-zero conditions that happen
 *				in the inline code.
 *	09/06/91	(Jim)
 *				Added in String related opcodes.  (Just duplicate cases by
 *				pointer opcodes at the moment.
 ****************************************************************************/

#include "poco_internal.h"
#include "activation.h"
#include <limits.h>
#include <string.h>
#include "pocodis.h"
#include "trace.h"
#include "postring.h"

#define MIN_PCALL_STACK 512  /* we check real often, small is fine. */
#define MIN_CCALL_STACK 4096 /* we guarantee min 2k to poe users */
							 /* which means we really ensure 4k: safe */

/*
 * Originally these called out to some assembly functions that juggled
 * the stack pointer for a fast calling convention without breaking
 * Watcom C.
 */
/* prototypes for functions in runccall.asm... */
// extern void po_vccall(void* stack, void* func);
// extern int po_iccall(void* stack, void* func);
// extern long po_lccall(void* stack, void* func);
// extern double po_dccall(void* stack, void* func);
// extern Popot po_pccall(void* stack, void* func);
// extern PoString po_string_ccall(void* stack, void* func);
// #define VC_call(s, f) po_vccall(s, f)
// #define IC_call(s, f) po_iccall(s, f)
// #define LC_call(s, f) po_lccall(s, f)
// #define DC_call(s, f) po_dccall(s, f)
// #define PC_call(s, f) po_pccall(s, f)
// #define STRING_C_call(s, f) po_string_ccall(s, f)
#define VC_call(s, f) ((void)((*f)(s)))
#define IC_call(s, f) ((int)((*f)(s)))
#define LC_call(s, f) ((long)((*f)(s)))
#define DC_call(s, f) ((double)((*f)(s)))
#define PC_call(s, f) ((Popot)((*f)(s)))

typedef union eax {
	Func_frame* f;
	Pt_num ret;
	int* iptr;
} Eax;

static bool po_copy_span_has_capacity(const Popot* span, size_t byte_count)
{
	uintptr_t current;
	uintptr_t minimum;
	uintptr_t maximum;

	if (span == NULL || span->pt == NULL || span->min == NULL || span->max == NULL) {
		return false;
	}
	current = (uintptr_t)span->pt;
	minimum = (uintptr_t)span->min;
	maximum = (uintptr_t)span->max;
	if (minimum > current || current > maximum) {
		return false;
	}
	return byte_count == 0 || maximum - current >= byte_count - 1;
}

static bool po_registered_pointer_access_is_valid(PocoPointerRegistry* registry,
												  const Popot* pointer, size_t byte_count,
												  uint32_t permissions)
{
	return !poco_pointer_registry_is_managed(registry, pointer) ||
		   poco_pointer_registry_validate(registry, pointer, byte_count, permissions);
}

typedef struct {
	long data[32];
} Parmdata;

/*****************************************************************************
 * used as a dummy check_abort function when none is provided.
 ****************************************************************************/
static bool nofunc(void* d)
{
	(void)d;
	return false;
}

/*****************************************************************************
 * Interpreter state.
 *
 * The dispatch loop used to be one 1800-line function whose machine registers
 * were plain locals.  The registers now live here so that each opcode family
 * can be a separate handler; the handlers are force-inlined back into the
 * loop, so the generated code is the same flat interpreter it always was.
 ****************************************************************************/
typedef struct {
	PocoActivation* p;
	Pt_num* ip;
	Pt_num* stack;
	Pt_num* base;
	Pt_num* globals;
	UBYTE* stack_area;
	Eax* acc; /* kept out of line: the union defeats scalar promotion */
	Errcode err;
	size_t debug_depth_floor;
} PoRunState;

/*
 * What an opcode handler tells the dispatch loop to do next.  These replace
 * the gotos that used to jump straight from a case body to the error tails.
 */
typedef enum {
	PO_STEP_NEXT = 0, /* fetch the next instruction */
	PO_STEP_DONE,     /* OP_END: the instruction stream is finished */
	PO_STEP_ABORT,
	PO_STEP_TRAP, /* st->err holds the error code */
	PO_STEP_ERR_NOTAFUNC,
	PO_STEP_ERR_NULL,
	PO_STEP_ERR_SMALL,
	PO_STEP_ERR_BIG,
	PO_STEP_ERR_POINTER_ACCESS,
	PO_STEP_ERR_FPMATH,
	PO_STEP_ERR_LIBROUTINE,
	PO_STEP_ERR_FFI
} PoStep;

#if defined(_MSC_VER)
#define PO_STEP_HANDLER static __forceinline PoStep
#define PO_UNREACHABLE() __assume(0)
#else
#define PO_STEP_HANDLER static inline __attribute__((always_inline)) PoStep
#define PO_UNREACHABLE() __builtin_unreachable()
#endif

/*
 * One arm of the dispatch table.  The opcode is repeated as an argument so
 * that the handler's own switch folds away once it is inlined here - passing
 * the loop's `op` variable instead costs a second jump table per instruction.
 */
#define PO_DISPATCH_CASE(opcode, handler) \
	case opcode:                          \
		return handler(st, opcode)

#define PO_STACK_OVERFLOW(st, limit) ((UBYTE*)(st)->stack < ((st)->stack_area + (limit)))

#define PO_RECORD_VARIADIC_TYPE(st, type)                                                  \
	do {                                                                                   \
		(st)->err = po_ffi_variadic_types_append((st)->p->vm, &(st)->p->variadic, (type)); \
		if ((st)->err != Success) return PO_STEP_ERR_FFI;                                  \
	} while (0)

/*****************************************************************************
 * datatype conversions
 ****************************************************************************/
PO_STEP_HANDLER po_step_convert(PoRunState* st, int op)
{
	switch (op) {
		case OP_INT_TO_LONG:
			st->acc->ret.l = st->stack->inty;
			st->stack = OPTR(st->stack, INT_SIZE - sizeof(long));
			st->stack->l = st->acc->ret.l;
			return PO_STEP_NEXT;
		case OP_LONG_TO_INT:
			st->acc->ret.inty = (int)st->stack->l;
			st->stack = OPTR(st->stack, sizeof(long) - INT_SIZE);
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_INT_TO_DOUBLE:
			st->acc->ret.d = st->stack->inty;
			st->stack = OPTR(st->stack, INT_SIZE - sizeof(double));
			st->stack->d = st->acc->ret.d;
			return PO_STEP_NEXT;
		case OP_LONG_TO_DOUBLE:
			st->acc->ret.d = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(long) - sizeof(double));
			st->stack->d = st->acc->ret.d;
			return PO_STEP_NEXT;
		case OP_DOUBLE_TO_LONG:
			st->acc->ret.l = st->stack->d;
			st->stack = OPTR(st->stack, sizeof(double) - sizeof(long));
			st->stack->l = st->acc->ret.l;
			return PO_STEP_NEXT;
		case OP_DOUBLE_TO_INT:
			st->acc->ret.inty = st->stack->d;
			st->stack = OPTR(st->stack, sizeof(double) - sizeof(int));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_PPT_TO_CPT:
			// convert popot pointer to void*
			st->acc->ret.p = st->stack->ppt.pt;
			st->stack = OPTR(st->stack, sizeof(st->stack->ppt) - sizeof(st->stack->ppt.pt));
			st->stack->p = st->acc->ret.p;
			return PO_STEP_NEXT;
		case OP_CPT_TO_PPT:
			// convert void* to popot pointer
			/* Expression-tier programs cannot contain a native pointer
			 * binding, so the compiler cannot normally emit a reachable
			 * instance of this opcode.  Keep a runtime fence as defense in
			 * depth for malformed or future serialized code. */
			if (st->p->vm != NULL && st->p->vm->untrusted_expression_library_registered) {
				st->err = Err_bad_instruction;
				return PO_STEP_TRAP;
			}
			// NOTE: Since we don't know the size of the C-allocated memory,
			// we set permissive bounds (min=0, max=max_addr) to allow array
			// access. This trades safety for C interoperability.
			st->acc->ret.p = st->stack->p;
			st->stack = OPTR(st->stack, sizeof(st->stack->p) - sizeof(st->stack->ppt));
			st->stack->ppt.pt = st->acc->ret.p;
			st->stack->ppt.min = NULL;
			st->stack->ppt.max = (void*)~(size_t)0;
			return PO_STEP_NEXT;
	}
	PO_UNREACHABLE();
}

/*****************************************************************************
 * function entry and exit
 ****************************************************************************/
PO_STEP_HANDLER po_step_frame(PoRunState* st, int op)
{
	switch (op) {
		case OP_RET:
			st->ip = st->stack->p;
			st->stack = OPTR(st->stack, sizeof(st->ip));
			if (st->p->debug_call_depth > st->debug_depth_floor) {
				--st->p->debug_call_depth;
			}
			return PO_STEP_NEXT;
		case OP_ADD_STACK:
			st->stack = OPTR(st->stack, st->ip->doff);
			st->ip = OPTR(st->ip, sizeof(st->ip->doff));
			return PO_STEP_NEXT;
		case OP_ENTER:
			if (PO_STACK_OVERFLOW(st, st->ip->doff + MIN_PCALL_STACK)) {
				st->err = Err_stack;
				return PO_STEP_TRAP;
			}
			st->stack = OPTR(st->stack, -sizeof(st->base));
			st->stack->p = st->base;
			st->base = st->stack;
			st->stack = OPTR(st->stack, -st->ip->doff);
			poco_zero_bytes(st->stack, st->ip->doff);
			st->ip = OPTR(st->ip, sizeof(st->ip->doff));
			return PO_STEP_NEXT;
		case OP_LEAVE:
			st->stack = st->base;                          /* clear off local vars  */
			st->base = st->stack->p;                       /* restore parent base	 */
			st->stack = OPTR(st->stack, sizeof(st->base)); /* clean off parent base */
			return PO_STEP_NEXT;
	}
	PO_UNREACHABLE();
}

/*****************************************************************************
 * branches
 ****************************************************************************/
PO_STEP_HANDLER po_step_branch(PoRunState* st, int op)
{
	switch (op) {
		case OP_BRA:
			if (st->ip->inty < 0) {
				if ((st->p->check_abort)(st->p->check_abort_data)) {
					return PO_STEP_ABORT;
				}
			}
			st->ip = OPTR(st->ip, st->ip->inty);
			return PO_STEP_NEXT;
		case OP_BEQ:
			if (st->stack->inty == 0) {
				if (st->ip->inty < 0) {
					if ((st->p->check_abort)(st->p->check_abort_data)) {
						return PO_STEP_ABORT;
					}
				}
				st->ip = OPTR(st->ip, st->ip->inty);
			} else {
				st->ip = OPTR(st->ip, sizeof(st->ip->inty));
			}
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			return PO_STEP_NEXT;
		case OP_BNE:
			if (st->stack->inty != 0) {
				if (st->ip->inty < 0) {
					if ((st->p->check_abort)(st->p->check_abort_data)) {
						return PO_STEP_ABORT;
					}
				}
				st->ip = OPTR(st->ip, st->ip->inty);
			} else {
				st->ip = OPTR(st->ip, sizeof(st->ip->inty));
			}
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			return PO_STEP_NEXT;
	}
	PO_UNREACHABLE();
}

/*****************************************************************************
 * constants and effective addresses
 ****************************************************************************/
PO_STEP_HANDLER po_step_push_const(PoRunState* st, int op)
{
	switch (op) {
		case OP_ICON: /* push int constant onto data stack */
			st->stack = OPTR(st->stack, -INT_SIZE);
			st->stack->inty = st->ip->inty;
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_LCON: /* push long constant onto stack */
			st->stack = OPTR(st->stack, -sizeof(long));
			st->stack->l = st->ip->l;
			st->ip = OPTR(st->ip, sizeof(long));
			return PO_STEP_NEXT;
		case OP_DCON: /* push floating point constant onto stack */
			st->stack = OPTR(st->stack, -sizeof(double));
			st->stack->d = st->ip->d;
			st->ip = OPTR(st->ip, sizeof(double));
			return PO_STEP_NEXT;
		case OP_PCON: /* push pointer constant onto stack */
			st->stack = OPTR(st->stack, -sizeof(st->stack->ppt));
			st->stack->ppt = st->ip->ppt;
			st->ip = OPTR(st->ip, sizeof(st->ip->ppt));
			return PO_STEP_NEXT;

			/*----------------------------------------------------------------------------
			 * LOAD EFFECTIVE ADDRESS
			 *--------------------------------------------------------------------------*/

		case OP_GLO_ADDRESS:
			st->stack = OPTR(st->stack, -sizeof(st->stack->ppt));
			st->stack->ppt.min = st->stack->ppt.max = st->stack->ppt.pt =
				OPTR(st->globals, st->ip->inty);
			st->ip = OPTR(st->ip, sizeof(st->ip->inty));
			st->stack->ppt.max = OPTR(st->stack->ppt.max, st->ip->l);
			st->ip = OPTR(st->ip, sizeof(st->ip->l));
			return PO_STEP_NEXT;
		case OP_LOC_ADDRESS:
			st->stack = OPTR(st->stack, -sizeof(st->stack->ppt));
			st->stack->ppt.min = st->stack->ppt.max = st->stack->ppt.pt =
				OPTR(st->base, st->ip->inty);
			st->ip = OPTR(st->ip, sizeof(st->ip->inty));
			st->stack->ppt.max = OPTR(st->stack->ppt.max, st->ip->l);
			st->ip = OPTR(st->ip, sizeof(st->ip->l));
			return PO_STEP_NEXT;
		case OP_CODE_ADDRESS: /* put immediate code address onto stack */
			st->stack = OPTR(st->stack, -sizeof(st->stack->ppt));
			st->stack->ppt.min = st->stack->ppt.max = NULL;
			st->stack->ppt.pt = poco_activation_callback_handle(st->p, st->ip->p);
			st->ip = OPTR(st->ip, sizeof(void*));
			return PO_STEP_NEXT;
	}
	PO_UNREACHABLE();
}

/*****************************************************************************
 * direct variable loads
 ****************************************************************************/
PO_STEP_HANDLER po_step_load_direct(PoRunState* st, int op)
{
	switch (op) {
		case OP_GLO_CVAR: /* push a global variable onto data stack */
			st->stack = OPTR(st->stack, -INT_SIZE);
			st->stack->inty = ((char*)(OPTR(st->globals, st->ip->doff)))[0];
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_GLO_SVAR: /* push a global variable onto data stack */
			st->stack = OPTR(st->stack, -INT_SIZE);
			st->stack->inty = ((short*)(OPTR(st->globals, st->ip->doff)))[0];
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_GLO_IVAR: /* push a global variable onto data stack */
			st->stack = OPTR(st->stack, -INT_SIZE);
			st->stack->inty = ((int*)(OPTR(st->globals, st->ip->doff)))[0];
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_GLO_LVAR: /* push a global variable onto data stack */
			st->stack = OPTR(st->stack, -sizeof(long));
			st->stack->l = ((long*)(OPTR(st->globals, st->ip->doff)))[0];
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_GLO_PVAR: /* push a global pointer onto data stack */
		case OP_GLO_VVAR: /* push a global function pointer onto data stack */
			st->stack = OPTR(st->stack, -sizeof(st->stack->ppt));
			st->stack->ppt = ((Popot*)(OPTR(st->globals, st->ip->doff)))[0];
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_GLO_FVAR: /* push a global variable onto data stack */
			st->stack = OPTR(st->stack, -sizeof(double));
			st->stack->d = ((float*)(OPTR(st->globals, st->ip->doff)))[0];
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_GLO_DVAR: /* push a global variable onto data stack */
			st->stack = OPTR(st->stack, -sizeof(double));
			st->stack->d = ((double*)(OPTR(st->globals, st->ip->doff)))[0];
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_LOC_CVAR: /* push a local variable onto data stack */
			st->stack = OPTR(st->stack, -INT_SIZE);
			st->stack->inty = ((char*)(OPTR(st->base, st->ip->doff)))[0];
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_LOC_SVAR: /* push a local variable onto data stack */
			st->stack = OPTR(st->stack, -INT_SIZE);
			st->stack->inty = ((short*)(OPTR(st->base, st->ip->doff)))[0];
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_LOC_IVAR: /* push a local variable onto data stack */
			st->stack = OPTR(st->stack, -INT_SIZE);
			st->stack->inty = ((int*)(OPTR(st->base, st->ip->doff)))[0];
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_LOC_LVAR: /* push a local variable onto data stack */
			st->stack = OPTR(st->stack, -sizeof(long));
			st->stack->l = ((long*)(OPTR(st->base, st->ip->doff)))[0];
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_LOC_PVAR: /* push a local variable onto data stack */
			st->stack = OPTR(st->stack, -sizeof(st->stack->ppt));
			st->stack->ppt = ((Popot*)(OPTR(st->base, st->ip->doff)))[0];
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_LOC_FVAR: /* push a local variable onto data stack */
			st->stack = OPTR(st->stack, -sizeof(double));
			st->stack->d = ((float*)(OPTR(st->base, st->ip->doff)))[0];
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_LOC_DVAR: /* push a local variable onto data stack */
			st->stack = OPTR(st->stack, -sizeof(double));
			st->stack->d = ((double*)(OPTR(st->base, st->ip->doff)))[0];
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
	}
	PO_UNREACHABLE();
}

/*****************************************************************************
 * direct variable stores
 ****************************************************************************/
PO_STEP_HANDLER po_step_store_direct(PoRunState* st, int op)
{
	switch (op) {
		case OP_GLO_CASS: /* move top of stack to global variable */
			(((char*)OPTR(st->globals, st->ip->doff))[0]) = st->stack->inty;
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_GLO_SASS: /* move top of stack to global variable */
			((short*)(OPTR(st->globals, st->ip->doff)))[0] = st->stack->inty;
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_GLO_IASS: /* move top of stack to global variable */
			((int*)(OPTR(st->globals, st->ip->doff)))[0] = st->stack->inty;
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_GLO_LASS: /* move top of stack to global variable */
			((long*)(OPTR(st->globals, st->ip->doff)))[0] = st->stack->l;
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_GLO_PASS: /* move top of stack to global pointer variable */
			((Popot*)(OPTR(st->globals, st->ip->doff)))[0] = st->stack->ppt;
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_GLO_FASS: /* move top of stack to global variable */
			((float*)(OPTR(st->globals, st->ip->doff)))[0] = st->stack->d;
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_GLO_DASS: /* move top of stack to global variable */
			((double*)(OPTR(st->globals, st->ip->doff)))[0] = st->stack->d;
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_LOC_CASS: /* move top of stack to local variable */
			((char*)(OPTR(st->base, st->ip->doff)))[0] = st->stack->inty;
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_LOC_SASS: /* move top of stack to local variable */
			((short*)(OPTR(st->base, st->ip->doff)))[0] = st->stack->inty;
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_LOC_IASS: /* move top of stack to local variable */
			((int*)(OPTR(st->base, st->ip->doff)))[0] = st->stack->inty;
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_LOC_LASS: /* move top of stack to local variable */
			((long*)(OPTR(st->base, st->ip->doff)))[0] = st->stack->l;
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_LOC_PASS: /* move top of stack to local ptr variable */
			((Popot*)(OPTR(st->base, st->ip->doff)))[0] = st->stack->ppt;
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_LOC_FASS: /* move top of stack to local variable */
			((float*)(OPTR(st->base, st->ip->doff)))[0] = st->stack->d;
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
		case OP_LOC_DASS: /* move top of stack to local variable */
			((double*)(OPTR(st->base, st->ip->doff)))[0] = st->stack->d;
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
	}
	PO_UNREACHABLE();
}

/*****************************************************************************
 * indirect variable loads
 ****************************************************************************/
PO_STEP_HANDLER po_step_load_indirect(PoRunState* st, int op)
{
	switch (op) {
		case OP_CI_VAR:
			st->acc->ret.ppt = st->stack->ppt;
			if (st->acc->ret.ppt.pt == NULL) {
				return PO_STEP_ERR_NULL;
			}
			if (!po_registered_pointer_access_is_valid(st->p->pointer_registry, &st->acc->ret.ppt,
													   sizeof(char),
													   POCO_POINTER_PERMISSION_READ)) {
				return PO_STEP_ERR_POINTER_ACCESS;
			}
			if (st->acc->ret.ppt.pt < st->acc->ret.ppt.min) {
				return PO_STEP_ERR_SMALL;
			}
			if (st->acc->ret.ppt.pt > st->acc->ret.ppt.max) {
				return PO_STEP_ERR_BIG;
			}
			st->stack = OPTR(st->stack, sizeof(st->acc->ret.ppt) - sizeof(st->acc->ret.inty));
			st->stack->inty = *((char*)(st->acc->ret.ppt.pt));
			return PO_STEP_NEXT;
		case OP_SI_VAR:
			st->acc->ret.ppt = st->stack->ppt;
			if (st->acc->ret.ppt.pt == NULL) {
				return PO_STEP_ERR_NULL;
			}
			if (!po_registered_pointer_access_is_valid(st->p->pointer_registry, &st->acc->ret.ppt,
													   sizeof(short),
													   POCO_POINTER_PERMISSION_READ)) {
				return PO_STEP_ERR_POINTER_ACCESS;
			}
			if (st->acc->ret.ppt.pt < st->acc->ret.ppt.min) {
				return PO_STEP_ERR_SMALL;
			}
			if (st->acc->ret.ppt.pt > st->acc->ret.ppt.max) {
				return PO_STEP_ERR_BIG;
			}
			st->stack = OPTR(st->stack, sizeof(st->acc->ret.ppt) - sizeof(st->acc->ret.inty));
			st->stack->inty = *((short*)(st->acc->ret.ppt.pt));
			return PO_STEP_NEXT;
		case OP_II_VAR:
			st->acc->ret.ppt = st->stack->ppt;
			if (st->acc->ret.ppt.pt == NULL) {
				return PO_STEP_ERR_NULL;
			}
			if (!po_registered_pointer_access_is_valid(st->p->pointer_registry, &st->acc->ret.ppt,
													   sizeof(int), POCO_POINTER_PERMISSION_READ)) {
				return PO_STEP_ERR_POINTER_ACCESS;
			}
			if (st->acc->ret.ppt.pt < st->acc->ret.ppt.min) {
				return PO_STEP_ERR_SMALL;
			}
			if (st->acc->ret.ppt.pt > st->acc->ret.ppt.max) {
				return PO_STEP_ERR_BIG;
			}
			st->stack = OPTR(st->stack, sizeof(st->acc->ret.ppt) - sizeof(st->acc->ret.inty));
			st->stack->inty = *((int*)(st->acc->ret.ppt.pt));
			return PO_STEP_NEXT;
		case OP_PI_VAR:
			st->acc->ret.ppt = st->stack->ppt;
			if (st->acc->ret.ppt.pt == NULL) {
				return PO_STEP_ERR_NULL;
			}
			if (!po_registered_pointer_access_is_valid(st->p->pointer_registry, &st->acc->ret.ppt,
													   sizeof(Popot),
													   POCO_POINTER_PERMISSION_READ)) {
				return PO_STEP_ERR_POINTER_ACCESS;
			}
			if (st->acc->ret.ppt.pt < st->acc->ret.ppt.min) {
				return PO_STEP_ERR_SMALL;
			}
			if (st->acc->ret.ppt.pt > st->acc->ret.ppt.max) {
				return PO_STEP_ERR_BIG;
			}
			st->stack = OPTR(st->stack, sizeof(st->acc->ret.ppt) - sizeof(st->acc->ret.ppt));
			st->stack->ppt = *((Popot*)(st->acc->ret.ppt.pt));
			return PO_STEP_NEXT;
		case OP_LI_VAR:
			st->acc->ret.ppt = st->stack->ppt;
			if (st->acc->ret.ppt.pt == NULL) {
				return PO_STEP_ERR_NULL;
			}
			if (!po_registered_pointer_access_is_valid(st->p->pointer_registry, &st->acc->ret.ppt,
													   sizeof(long),
													   POCO_POINTER_PERMISSION_READ)) {
				return PO_STEP_ERR_POINTER_ACCESS;
			}
			if (st->acc->ret.ppt.pt < st->acc->ret.ppt.min) {
				return PO_STEP_ERR_SMALL;
			}
			if (st->acc->ret.ppt.pt > st->acc->ret.ppt.max) {
				return PO_STEP_ERR_BIG;
			}
			st->stack = OPTR(st->stack, sizeof(st->acc->ret.ppt) - sizeof(st->acc->ret.l));
			st->stack->l = *((long*)(st->acc->ret.ppt.pt));
			return PO_STEP_NEXT;
		case OP_FI_VAR:
			st->acc->ret.ppt = st->stack->ppt;
			if (st->acc->ret.ppt.pt == NULL) {
				return PO_STEP_ERR_NULL;
			}
			if (!po_registered_pointer_access_is_valid(st->p->pointer_registry, &st->acc->ret.ppt,
													   sizeof(float),
													   POCO_POINTER_PERMISSION_READ)) {
				return PO_STEP_ERR_POINTER_ACCESS;
			}
			if (st->acc->ret.ppt.pt < st->acc->ret.ppt.min) {
				return PO_STEP_ERR_SMALL;
			}
			if (st->acc->ret.ppt.pt > st->acc->ret.ppt.max) {
				return PO_STEP_ERR_BIG;
			}
			/* The slot this reserves must match what is written into it and
			 * what OP_DPOP later removes. A float is widened to double on the
			 * interpreter stack - OP_LOC_FVAR reserves sizeof(double) for
			 * exactly that reason - so reserving sizeof(float) here left the
			 * stack four bytes short on every indirect float read, and three
			 * such reads in one function drifted the frame far enough for
			 * OP_RET to fetch a garbage return address. */
			st->stack = OPTR(st->stack, sizeof(st->acc->ret.ppt) - sizeof(st->acc->ret.d));
			st->stack->d = *((float*)(st->acc->ret.ppt.pt));
			return PO_STEP_NEXT;
		case OP_DI_VAR:
			st->acc->ret.ppt = st->stack->ppt;
			if (st->acc->ret.ppt.pt == NULL) {
				return PO_STEP_ERR_NULL;
			}
			if (!po_registered_pointer_access_is_valid(st->p->pointer_registry, &st->acc->ret.ppt,
													   sizeof(double),
													   POCO_POINTER_PERMISSION_READ)) {
				return PO_STEP_ERR_POINTER_ACCESS;
			}
			if (st->acc->ret.ppt.pt < st->acc->ret.ppt.min) {
				return PO_STEP_ERR_SMALL;
			}
			if (st->acc->ret.ppt.pt > st->acc->ret.ppt.max) {
				return PO_STEP_ERR_BIG;
			}
			st->stack = OPTR(st->stack, sizeof(st->acc->ret.ppt) - sizeof(st->acc->ret.d));
			st->stack->d = *((double*)(st->acc->ret.ppt.pt));
			return PO_STEP_NEXT;
	}
	PO_UNREACHABLE();
}

/*****************************************************************************
 * indirect variable stores
 ****************************************************************************/
PO_STEP_HANDLER po_step_store_indirect(PoRunState* st, int op)
{
	switch (op) {
		case OP_CI_ASS:
			st->acc->ret.ppt = st->stack->ppt;
			if (st->acc->ret.ppt.pt == NULL) {
				return PO_STEP_ERR_NULL;
			}
			if (!po_registered_pointer_access_is_valid(st->p->pointer_registry, &st->acc->ret.ppt,
													   sizeof(char),
													   POCO_POINTER_PERMISSION_WRITE)) {
				return PO_STEP_ERR_POINTER_ACCESS;
			}
			if (st->acc->ret.ppt.pt < st->acc->ret.ppt.min) {
				return PO_STEP_ERR_SMALL;
			}
			if (st->acc->ret.ppt.pt > st->acc->ret.ppt.max) {
				return PO_STEP_ERR_BIG;
			}
			st->stack = OPTR(st->stack, sizeof(st->stack->ppt));
			((char*)(st->acc->ret.ppt.pt))[0] = st->stack->inty;
			return PO_STEP_NEXT;
		case OP_SI_ASS:
			st->acc->ret.ppt = st->stack->ppt;
			if (st->acc->ret.ppt.pt == NULL) {
				return PO_STEP_ERR_NULL;
			}
			if (!po_registered_pointer_access_is_valid(st->p->pointer_registry, &st->acc->ret.ppt,
													   sizeof(short),
													   POCO_POINTER_PERMISSION_WRITE)) {
				return PO_STEP_ERR_POINTER_ACCESS;
			}
			if (st->acc->ret.ppt.pt < st->acc->ret.ppt.min) {
				return PO_STEP_ERR_SMALL;
			}
			if (st->acc->ret.ppt.pt > st->acc->ret.ppt.max) {
				return PO_STEP_ERR_BIG;
			}
			st->stack = OPTR(st->stack, sizeof(st->stack->ppt));
			((short*)(st->acc->ret.ppt.pt))[0] = st->stack->inty;
			return PO_STEP_NEXT;
		case OP_II_ASS:
			st->acc->ret.ppt = st->stack->ppt;
			if (st->acc->ret.ppt.pt == NULL) {
				return PO_STEP_ERR_NULL;
			}
			if (!po_registered_pointer_access_is_valid(st->p->pointer_registry, &st->acc->ret.ppt,
													   sizeof(int),
													   POCO_POINTER_PERMISSION_WRITE)) {
				return PO_STEP_ERR_POINTER_ACCESS;
			}
			if (st->acc->ret.ppt.pt < st->acc->ret.ppt.min) {
				return PO_STEP_ERR_SMALL;
			}
			if (st->acc->ret.ppt.pt > st->acc->ret.ppt.max) {
				return PO_STEP_ERR_BIG;
			}
			st->stack = OPTR(st->stack, sizeof(st->stack->ppt));
			((int*)(st->acc->ret.ppt.pt))[0] = st->stack->inty;
			return PO_STEP_NEXT;
		case OP_PI_ASS:
			st->acc->ret.ppt = st->stack->ppt;
			if (st->acc->ret.ppt.pt == NULL) {
				return PO_STEP_ERR_NULL;
			}
			if (!po_registered_pointer_access_is_valid(st->p->pointer_registry, &st->acc->ret.ppt,
													   sizeof(Popot),
													   POCO_POINTER_PERMISSION_WRITE)) {
				return PO_STEP_ERR_POINTER_ACCESS;
			}
			if (st->acc->ret.ppt.pt < st->acc->ret.ppt.min) {
				return PO_STEP_ERR_SMALL;
			}
			if (st->acc->ret.ppt.pt > st->acc->ret.ppt.max) {
				return PO_STEP_ERR_BIG;
			}
			st->stack = OPTR(st->stack, sizeof(st->stack->ppt));
			((Popot*)(st->acc->ret.ppt.pt))[0] = st->stack->ppt;
			return PO_STEP_NEXT;
		case OP_LI_ASS:
			st->acc->ret.ppt = st->stack->ppt;
			if (st->acc->ret.ppt.pt == NULL) {
				return PO_STEP_ERR_NULL;
			}
			if (!po_registered_pointer_access_is_valid(st->p->pointer_registry, &st->acc->ret.ppt,
													   sizeof(long),
													   POCO_POINTER_PERMISSION_WRITE)) {
				return PO_STEP_ERR_POINTER_ACCESS;
			}
			if (st->acc->ret.ppt.pt < st->acc->ret.ppt.min) {
				return PO_STEP_ERR_SMALL;
			}
			if (st->acc->ret.ppt.pt > st->acc->ret.ppt.max) {
				return PO_STEP_ERR_BIG;
			}
			st->stack = OPTR(st->stack, sizeof(st->stack->ppt));
			((long*)(st->acc->ret.ppt.pt))[0] = st->stack->l;
			return PO_STEP_NEXT;
		case OP_FI_ASS:
			st->acc->ret.ppt = st->stack->ppt;
			if (st->acc->ret.ppt.pt == NULL) {
				return PO_STEP_ERR_NULL;
			}
			if (!po_registered_pointer_access_is_valid(st->p->pointer_registry, &st->acc->ret.ppt,
													   sizeof(float),
													   POCO_POINTER_PERMISSION_WRITE)) {
				return PO_STEP_ERR_POINTER_ACCESS;
			}
			if (st->acc->ret.ppt.pt < st->acc->ret.ppt.min) {
				return PO_STEP_ERR_SMALL;
			}
			if (st->acc->ret.ppt.pt > st->acc->ret.ppt.max) {
				return PO_STEP_ERR_BIG;
			}
			st->stack = OPTR(st->stack, sizeof(st->stack->ppt));
			((float*)(st->acc->ret.ppt.pt))[0] = st->stack->d;
			return PO_STEP_NEXT;
		case OP_DI_ASS:
			st->acc->ret.ppt = st->stack->ppt;
			if (st->acc->ret.ppt.pt == NULL) {
				return PO_STEP_ERR_NULL;
			}
			if (!po_registered_pointer_access_is_valid(st->p->pointer_registry, &st->acc->ret.ppt,
													   sizeof(double),
													   POCO_POINTER_PERMISSION_WRITE)) {
				return PO_STEP_ERR_POINTER_ACCESS;
			}
			if (st->acc->ret.ppt.pt < st->acc->ret.ppt.min) {
				return PO_STEP_ERR_SMALL;
			}
			if (st->acc->ret.ppt.pt > st->acc->ret.ppt.max) {
				return PO_STEP_ERR_BIG;
			}
			st->stack = OPTR(st->stack, sizeof(st->stack->ppt));
			((double*)(st->acc->ret.ppt.pt))[0] = st->stack->d;
			return PO_STEP_NEXT;
	}
	PO_UNREACHABLE();
}

/*****************************************************************************
 * arithmetic, negation and modulo
 ****************************************************************************/
PO_STEP_HANDLER po_step_arith(PoRunState* st, int op)
{
	switch (op) {
		case OP_IADD: /* replace top two elements of stack one result */
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, INT_SIZE);
			st->stack->inty += st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_LADD:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(long));
			st->stack->l += st->acc->ret.l;
			return PO_STEP_NEXT;
		case OP_DADD:
			st->acc->ret.d = st->stack->d;
			st->stack = OPTR(st->stack, sizeof(double));
			st->stack->d += st->acc->ret.d;
			if (st->p->builtin_error != Success) {
				return PO_STEP_ERR_FPMATH;
			}
			return PO_STEP_NEXT;
		case OP_PADD:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			st->stack->ppt.pt = OPTR(st->stack->ppt.pt, st->acc->ret.inty);
			return PO_STEP_NEXT;
			/*----------------------------------------------------------------------------
			 * SUBTRACTION
			 *--------------------------------------------------------------------------*/

		case OP_ISUB:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, INT_SIZE);
			st->stack->inty -= st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_LSUB:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(long));
			st->stack->l -= st->acc->ret.l;
			return PO_STEP_NEXT;
		case OP_DSUB:
			st->acc->ret.d = st->stack->d;
			st->stack = OPTR(st->stack, sizeof(double));
			st->stack->d -= st->acc->ret.d;
			if (st->p->builtin_error != Success) {
				return PO_STEP_ERR_FPMATH;
			}
			return PO_STEP_NEXT;
		case OP_PSUB:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			st->stack->ppt.pt = OPTR(st->stack->ppt.pt, -st->acc->ret.inty);
			return PO_STEP_NEXT;

			/*----------------------------------------------------------------------------
			 * MULTIPLICATION
			 *--------------------------------------------------------------------------*/

		case OP_IMUL:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, INT_SIZE);
			st->stack->inty *= st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_LMUL:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(long));
			st->stack->l *= st->acc->ret.l;
			return PO_STEP_NEXT;
		case OP_DMUL:
			st->acc->ret.d = st->stack->d;
			st->stack = OPTR(st->stack, sizeof(double));
			st->stack->d *= st->acc->ret.d;
			if (st->p->builtin_error != Success) {
				return PO_STEP_ERR_FPMATH;
			}
			return PO_STEP_NEXT;

			/*----------------------------------------------------------------------------
			 * DIVISION
			 *--------------------------------------------------------------------------*/

		case OP_IDIV:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, INT_SIZE);
			if (st->acc->ret.inty == 0) {
				st->err = Err_zero_divide;
				return PO_STEP_TRAP;
			}
			st->stack->inty /= st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_LDIV:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(long));
			if (st->acc->ret.l == 0) {
				st->err = Err_zero_divide;
				return PO_STEP_TRAP;
			}
			st->stack->l /= st->acc->ret.l;
			return PO_STEP_NEXT;
		case OP_DDIV:
			st->acc->ret.d = st->stack->d;
			st->stack = OPTR(st->stack, sizeof(double));
			st->stack->d /= st->acc->ret.d;
			if (st->p->builtin_error != Success) {
				return PO_STEP_ERR_FPMATH;
			}
			return PO_STEP_NEXT;
			/*----------------------------------------------------------------------------
			 * NEGATION
			 *--------------------------------------------------------------------------*/

		case OP_INEG:
			st->stack->inty = -st->stack->inty;
			return PO_STEP_NEXT;
		case OP_LNEG:
			st->stack->l = -st->stack->l;
			return PO_STEP_NEXT;
		case OP_DNEG:
			st->stack->d = -st->stack->d;
			return PO_STEP_NEXT;

			/*----------------------------------------------------------------------------
			 * MODULO
			 *--------------------------------------------------------------------------*/

		case OP_IMOD:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			st->stack->inty %= st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_LMOD:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(st->stack->l));
			st->stack->l %= st->acc->ret.l;
			return PO_STEP_NEXT;
	}
	PO_UNREACHABLE();
}

/*****************************************************************************
 * comparisons
 ****************************************************************************/
PO_STEP_HANDLER po_step_compare(PoRunState* st, int op)
{
	switch (op) {
		case OP_IEQ:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			st->stack->inty = (st->acc->ret.inty == st->stack->inty);
			return PO_STEP_NEXT;
		case OP_LEQ:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(st->stack->l));
			st->acc->ret.inty = (st->acc->ret.l == st->stack->l);
			st->stack = OPTR(st->stack, (sizeof(st->stack->l) - sizeof(st->stack->inty)));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_DEQ:
			st->acc->ret.d = st->stack->d;
			st->stack = OPTR(st->stack, sizeof(st->stack->d));
			st->acc->ret.inty = (st->acc->ret.d == st->stack->d);
			st->stack = OPTR(st->stack, (sizeof(st->stack->d) - sizeof(st->stack->inty)));
			st->stack->inty = st->acc->ret.inty;
			if (st->p->builtin_error != Success) {
				return PO_STEP_ERR_FPMATH;
			}
			return PO_STEP_NEXT;
		case OP_PEQ:
			st->acc->ret.inty =
				(st->stack->ppt.pt == ((Pt_num*)OPTR(st->stack, sizeof(st->stack->ppt)))->ppt.pt);
			st->stack = OPTR(st->stack, 2 * sizeof(st->stack->ppt) - sizeof(st->stack->inty));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;
			/*----------------------------------------------------------------------------
			 * COMPARISONS - NE
			 *--------------------------------------------------------------------------*/

		case OP_INE:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			st->stack->inty = (st->acc->ret.inty != st->stack->inty);
			return PO_STEP_NEXT;
		case OP_LNE:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(st->stack->l));
			st->acc->ret.inty = (st->acc->ret.l != st->stack->l);
			st->stack = OPTR(st->stack, (sizeof(st->stack->l) - sizeof(st->stack->inty)));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_DNE:
			st->acc->ret.d = st->stack->d;
			st->stack = OPTR(st->stack, sizeof(st->stack->d));
			st->acc->ret.inty = (st->acc->ret.d != st->stack->d);
			st->stack = OPTR(st->stack, (sizeof(st->stack->d) - sizeof(st->stack->inty)));
			st->stack->inty = st->acc->ret.inty;
			if (st->p->builtin_error != Success) {
				return PO_STEP_ERR_FPMATH;
			}
			return PO_STEP_NEXT;
		case OP_PNE:
			st->acc->ret.inty =
				(st->stack->ppt.pt != ((Pt_num*)OPTR(st->stack, sizeof(st->stack->ppt)))->ppt.pt);
			st->stack = OPTR(st->stack, 2 * sizeof(st->stack->ppt) - sizeof(st->stack->inty));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;
			/*----------------------------------------------------------------------------
			 * COMPARISONS - GE
			 *--------------------------------------------------------------------------*/

		case OP_IGE:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			st->stack->inty = (st->stack->inty >= st->acc->ret.inty);
			return PO_STEP_NEXT;
		case OP_LGE:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(st->stack->l));
			st->acc->ret.inty = (st->stack->l >= st->acc->ret.l);
			st->stack = OPTR(st->stack, (sizeof(st->stack->l) - sizeof(st->stack->inty)));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_DGE:
			st->acc->ret.d = st->stack->d;
			st->stack = OPTR(st->stack, sizeof(st->stack->d));
			st->acc->ret.inty = (st->stack->d >= st->acc->ret.d);
			st->stack = OPTR(st->stack, (sizeof(st->stack->d) - sizeof(st->stack->inty)));
			st->stack->inty = st->acc->ret.inty;
			if (st->p->builtin_error != Success) {
				return PO_STEP_ERR_FPMATH;
			}
			return PO_STEP_NEXT;
		case OP_PGE:
			st->acc->ret.ppt = st->stack->ppt;
			st->stack = OPTR(st->stack, sizeof(st->stack->ppt));
			st->acc->ret.inty = ((char*)(st->stack->ppt.pt) >= ((char*)st->acc->ret.ppt.pt));
			st->stack = OPTR(st->stack, (sizeof(st->stack->ppt) - sizeof(st->stack->inty)));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;
			/*----------------------------------------------------------------------------
			 * COMPARISONS - GT
			 *--------------------------------------------------------------------------*/

		case OP_IGT:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			st->stack->inty = (st->stack->inty > st->acc->ret.inty);
			return PO_STEP_NEXT;
		case OP_LGT:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(st->stack->l));
			st->acc->ret.inty = (st->stack->l > st->acc->ret.l);
			st->stack = OPTR(st->stack, (sizeof(st->stack->l) - sizeof(st->stack->inty)));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_DGT:
			st->acc->ret.d = st->stack->d;
			st->stack = OPTR(st->stack, sizeof(st->stack->d));
			st->acc->ret.inty = (st->stack->d > st->acc->ret.d);
			st->stack = OPTR(st->stack, (sizeof(st->stack->d) - sizeof(st->stack->inty)));
			st->stack->inty = st->acc->ret.inty;
			if (st->p->builtin_error != Success) {
				return PO_STEP_ERR_FPMATH;
			}
			return PO_STEP_NEXT;
		case OP_PGT:
			st->acc->ret.ppt = st->stack->ppt;
			st->stack = OPTR(st->stack, sizeof(st->stack->ppt));
			st->acc->ret.inty = ((char*)(st->stack->ppt.pt) > ((char*)st->acc->ret.ppt.pt));
			st->stack = OPTR(st->stack, (sizeof(st->stack->ppt) - sizeof(st->stack->inty)));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;
			/*----------------------------------------------------------------------------
			 * COMPARISONS - LE
			 *--------------------------------------------------------------------------*/

		case OP_ILE:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			st->stack->inty = (st->stack->inty <= st->acc->ret.inty);
			return PO_STEP_NEXT;
		case OP_LLE:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(st->stack->l));
			st->acc->ret.inty = (st->stack->l <= st->acc->ret.l);
			st->stack = OPTR(st->stack, (sizeof(st->stack->l) - sizeof(st->stack->inty)));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_DLE:
			st->acc->ret.d = st->stack->d;
			st->stack = OPTR(st->stack, sizeof(st->stack->d));
			st->acc->ret.inty = (st->stack->d <= st->acc->ret.d);
			st->stack = OPTR(st->stack, (sizeof(st->stack->d) - sizeof(st->stack->inty)));
			st->stack->inty = st->acc->ret.inty;
			if (st->p->builtin_error != Success) {
				return PO_STEP_ERR_FPMATH;
			}
			return PO_STEP_NEXT;
		case OP_PLE:
			st->acc->ret.ppt = st->stack->ppt;
			st->stack = OPTR(st->stack, sizeof(st->stack->ppt));
			st->acc->ret.inty = ((char*)(st->stack->ppt.pt) <= ((char*)st->acc->ret.ppt.pt));
			st->stack = OPTR(st->stack, (sizeof(st->stack->ppt) - sizeof(st->stack->inty)));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;
			/*----------------------------------------------------------------------------
			 * COMPARISONS - LT
			 *--------------------------------------------------------------------------*/

		case OP_ILT:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			st->stack->inty = (st->stack->inty < st->acc->ret.inty);
			return PO_STEP_NEXT;
		case OP_LLT:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(st->stack->l));
			st->acc->ret.inty = (st->stack->l < st->acc->ret.l);
			st->stack = OPTR(st->stack, (sizeof(st->stack->l) - sizeof(st->stack->inty)));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_DLT:
			st->acc->ret.d = st->stack->d;
			st->stack = OPTR(st->stack, sizeof(st->stack->d));
			st->acc->ret.inty = (st->stack->d < st->acc->ret.d);
			st->stack = OPTR(st->stack, (sizeof(st->stack->d) - sizeof(st->stack->inty)));
			st->stack->inty = st->acc->ret.inty;
			if (st->p->builtin_error != Success) {
				return PO_STEP_ERR_FPMATH;
			}
			return PO_STEP_NEXT;
		case OP_PLT:
			st->acc->ret.ppt = st->stack->ppt;
			st->stack = OPTR(st->stack, sizeof(st->stack->ppt));
			st->acc->ret.inty = ((char*)(st->stack->ppt.pt) < ((char*)st->acc->ret.ppt.pt));
			st->stack = OPTR(st->stack, (sizeof(st->stack->ppt) - sizeof(st->stack->inty)));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;
	}
	PO_UNREACHABLE();
}

/*****************************************************************************
 * shifts, bitwise and logical operators
 ****************************************************************************/
PO_STEP_HANDLER po_step_bitwise(PoRunState* st, int op)
{
	switch (op) {
		case OP_ILSHIFT:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			st->stack->inty <<= st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_LLSHIFT:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(st->stack->l));
			st->stack->l <<= st->acc->ret.l;
			return PO_STEP_NEXT;

			/*----------------------------------------------------------------------------
			 * SHIFT RIGHT
			 *--------------------------------------------------------------------------*/

		case OP_IRSHIFT:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			st->stack->inty >>= st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_LRSHIFT:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(st->stack->l));
			st->stack->l >>= st->acc->ret.l;
			return PO_STEP_NEXT;

			/*----------------------------------------------------------------------------
			 * BINARY AND
			 *--------------------------------------------------------------------------*/

		case OP_IBAND:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			st->stack->inty = st->stack->inty & st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_LBAND:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(st->stack->l));
			st->stack->l = st->stack->l & st->acc->ret.l;
			return PO_STEP_NEXT;

			/*----------------------------------------------------------------------------
			 * BINARY OR
			 *--------------------------------------------------------------------------*/

		case OP_IBOR:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			st->stack->inty = st->stack->inty | st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_LBOR:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(st->stack->l));
			st->stack->l = st->stack->l | st->acc->ret.l;
			return PO_STEP_NEXT;

			/*----------------------------------------------------------------------------
			 * BINARY XOR
			 *--------------------------------------------------------------------------*/

		case OP_IXOR:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			st->stack->inty = st->stack->inty ^ st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_LXOR:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(st->stack->l));
			st->stack->l = st->stack->l ^ st->acc->ret.l;
			return PO_STEP_NEXT;

			/*----------------------------------------------------------------------------
			 * LOGICAL AND
			 *--------------------------------------------------------------------------*/

		case OP_ILAND:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			st->stack->inty = st->stack->inty && st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_LLAND:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(st->stack->l));
			st->stack->l = st->stack->l && st->acc->ret.l;
			return PO_STEP_NEXT;

			/*----------------------------------------------------------------------------
			 * LOGICAL OR
			 *--------------------------------------------------------------------------*/

		case OP_ILOR:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			st->stack->inty = st->stack->inty || st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_LLOR:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(st->stack->l));
			st->stack->l = st->stack->l || st->acc->ret.l;
			return PO_STEP_NEXT;

			/*----------------------------------------------------------------------------
			 * BINARY NOT
			 *--------------------------------------------------------------------------*/

		case OP_ICOMP:
			st->stack->inty = ~st->stack->inty;
			return PO_STEP_NEXT;
		case OP_LCOMP:
			st->stack->l = ~st->stack->l;
			return PO_STEP_NEXT;

			/*----------------------------------------------------------------------------
			 * LOGICAL NOT
			 *--------------------------------------------------------------------------*/

		case OP_INOT:
			st->stack->inty = !st->stack->inty;
			return PO_STEP_NEXT;
		case OP_LNOT:
			st->stack->l = !st->stack->l;
			return PO_STEP_NEXT;
	}
	PO_UNREACHABLE();
}

/*****************************************************************************
 * accumulator push/pop and stack duplication
 ****************************************************************************/
PO_STEP_HANDLER po_step_stack(PoRunState* st, int op)
{
	switch (op) {
		case OP_IPUSH:
			st->stack = OPTR(st->stack, -sizeof(st->stack->inty));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;
		case OP_LPUSH:
		case OP_CPPUSH:
			st->stack = OPTR(st->stack, -sizeof(st->stack->l));
			st->stack->l = st->acc->ret.l;
			return PO_STEP_NEXT;
		case OP_DPUSH:
			st->stack = OPTR(st->stack, -sizeof(st->stack->d));
			st->stack->d = st->acc->ret.d;
			return PO_STEP_NEXT;
		case OP_PPUSH:
			st->stack = OPTR(st->stack, -sizeof(st->stack->ppt));
			st->stack->ppt = st->acc->ret.ppt;
			return PO_STEP_NEXT;
			/*----------------------------------------------------------------------------
			 * POP STACK TO ACCUMULATOR (RETURN VALUE)
			 *--------------------------------------------------------------------------*/

		case OP_IPOP:
			st->acc->ret.inty = st->stack->inty;
			st->p->result = st->acc->ret;
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			return PO_STEP_NEXT;
		case OP_LPOP:
		case OP_CPPOP:
			st->acc->ret.l = st->stack->l;
			st->p->result = st->acc->ret;
			st->stack = OPTR(st->stack, sizeof(st->stack->l));
			return PO_STEP_NEXT;
		case OP_DPOP:
			st->acc->ret.d = st->stack->d;
			st->p->result = st->acc->ret;
			st->stack = OPTR(st->stack, sizeof(st->stack->d));
			return PO_STEP_NEXT;
		case OP_PPOP:
			st->acc->ret.ppt = st->stack->ppt;
			st->p->result = st->acc->ret;
			st->stack = OPTR(st->stack, sizeof(st->stack->ppt));
			return PO_STEP_NEXT;
			/*----------------------------------------------------------------------------
			 * DUPLICATE TOP-OF-STACK ITEM
			 *--------------------------------------------------------------------------*/

		case OP_IDUPE:
			st->stack = OPTR(st->stack, -sizeof(st->stack->inty));
			st->stack->inty = ((int*)(OPTR(st->stack, sizeof(st->stack->inty))))[0];
			return PO_STEP_NEXT;
		case OP_LDUPE:
			st->stack = OPTR(st->stack, -sizeof(st->stack->l));
			st->stack->l = ((long*)(OPTR(st->stack, sizeof(st->stack->l))))[0];
			return PO_STEP_NEXT;
		case OP_DDUPE:
			st->stack = OPTR(st->stack, -sizeof(st->stack->d));
			st->stack->d = ((double*)(OPTR(st->stack, sizeof(st->stack->d))))[0];
			return PO_STEP_NEXT;
		case OP_PDUPE:
			st->stack = OPTR(st->stack, -sizeof(st->stack->ppt));
			st->stack->ppt = ((Popot*)(OPTR(st->stack, sizeof(st->stack->ppt))))[0];
			return PO_STEP_NEXT;
	}
	PO_UNREACHABLE();
}

/*****************************************************************************
 * pointer arithmetic
 ****************************************************************************/
PO_STEP_HANDLER po_step_ptr_math(PoRunState* st, int op)
{
	switch (op) {
		case OP_ADD_IOFFSET:
			st->acc->ret.inty = st->stack->inty;
			st->stack = OPTR(st->stack, sizeof(st->stack->inty));
			st->stack->ppt.pt = OPTR(st->stack->ppt.pt, st->acc->ret.inty);
			return PO_STEP_NEXT;
		case OP_ADD_LOFFSET:
			st->acc->ret.l = st->stack->l;
			st->stack = OPTR(st->stack, sizeof(st->stack->l));
			st->stack->ppt.pt = OPTR(st->stack->ppt.pt, st->acc->ret.l);
			return PO_STEP_NEXT;
		case OP_PTRDIFF: /* subtract two pointers */
			st->acc->ret.ppt.pt = st->stack->ppt.pt;
			st->stack = OPTR(st->stack, sizeof(st->stack->ppt));
			st->acc->ret.l = (char*)st->stack->ppt.pt - (char*)st->acc->ret.ppt.pt;
			st->stack = OPTR(st->stack, sizeof(st->stack->ppt) - sizeof(long));
			st->stack->l = st->acc->ret.l / st->ip->inty; /* scale result */
			st->ip = OPTR(st->ip, sizeof(st->ip->inty));
			return PO_STEP_NEXT;
	}
	PO_UNREACHABLE();
}

/*****************************************************************************
 * block copy and move
 ****************************************************************************/
PO_STEP_HANDLER po_step_memory(PoRunState* st, int op)
{
	switch (op) {
		case OP_COPY:
			if (st->ip->l < 0) {
				return PO_STEP_ERR_NULL;
			}
			if (!po_copy_span_has_capacity(&st->stack->ppt, (size_t)st->ip->l) ||
				!po_copy_span_has_capacity((Popot*)OPTR(st->stack, sizeof(st->stack->ppt)),
										   (size_t)st->ip->l)) {
				return PO_STEP_ERR_BIG;
			}
			if (!po_registered_pointer_access_is_valid(st->p->pointer_registry, &st->stack->ppt,
													   (size_t)st->ip->l,
													   POCO_POINTER_PERMISSION_READ) ||
				!po_registered_pointer_access_is_valid(
					st->p->pointer_registry, (Popot*)OPTR(st->stack, sizeof(st->stack->ppt)),
					(size_t)st->ip->l, POCO_POINTER_PERMISSION_WRITE)) {
				return PO_STEP_ERR_POINTER_ACCESS;
			}
			poco_copy_bytes(((Popot*)OPTR(st->stack, sizeof(st->stack->ppt)))->pt,
							st->stack->ppt.pt, st->ip->l);
			st->ip = OPTR(st->ip, sizeof(st->ip->l));
			st->stack = OPTR(st->stack, 2 * sizeof(st->stack->ppt));
			return PO_STEP_NEXT;
		case OP_MOVE:
			poco_copy_bytes(st->stack->ppt.pt,
							((Popot*)OPTR(st->stack, sizeof(st->stack->ppt)))->pt, st->ip->l);
			st->ip = OPTR(st->ip, sizeof(st->ip->l));
			st->stack = OPTR(st->stack, 2 * sizeof(st->stack->ppt));
			return PO_STEP_NEXT;
	}
	PO_UNREACHABLE();
}

/*****************************************************************************
 * libffi variadic type recording
 ****************************************************************************/
PO_STEP_HANDLER po_step_ffi_variadic(PoRunState* st, int op)
{
	switch (op) {
		case OP_FFI_POP_ALL:
			po_ffi_variadic_types_reset(&st->p->variadic);
			return PO_STEP_NEXT;

		case OP_FFI_PUSH_POINTER:
			PO_RECORD_VARIADIC_TYPE(st, &ffi_type_pointer);
			return PO_STEP_NEXT;

		case OP_FFI_PUSH_SINT32:
			PO_RECORD_VARIADIC_TYPE(st, &ffi_type_sint32);
			return PO_STEP_NEXT;

		case OP_FFI_PUSH_FLOAT:
			PO_RECORD_VARIADIC_TYPE(st, &ffi_type_float);
			return PO_STEP_NEXT;

		case OP_FFI_PUSH_DOUBLE:
			PO_RECORD_VARIADIC_TYPE(st, &ffi_type_double);
			return PO_STEP_NEXT;

		case OP_FFI_PUSH_UINT8:
			PO_RECORD_VARIADIC_TYPE(st, &ffi_type_uint8);
			return PO_STEP_NEXT;

		case OP_FFI_PUSH_SINT8:
			PO_RECORD_VARIADIC_TYPE(st, &ffi_type_sint8);
			return PO_STEP_NEXT;

		case OP_FFI_PUSH_UINT16:
			PO_RECORD_VARIADIC_TYPE(st, &ffi_type_uint16);
			return PO_STEP_NEXT;

		case OP_FFI_PUSH_SINT16:
			PO_RECORD_VARIADIC_TYPE(st, &ffi_type_sint16);
			return PO_STEP_NEXT;

		case OP_FFI_PUSH_UINT32:
			PO_RECORD_VARIADIC_TYPE(st, &ffi_type_uint32);
			return PO_STEP_NEXT;

		case OP_FFI_PUSH_UINT64:
			PO_RECORD_VARIADIC_TYPE(st, &ffi_type_uint64);
			return PO_STEP_NEXT;

		case OP_FFI_PUSH_SINT64:
			PO_RECORD_VARIADIC_TYPE(st, &ffi_type_sint64);
			return PO_STEP_NEXT;

		case OP_FFI_PUSH_VOID:
		case OP_FFI_PUSH_NULL:
			st->err = Err_poco_ffi_invalid_binding;
			return PO_STEP_ERR_FFI;
	}
	PO_UNREACHABLE();
}

/*****************************************************************************
 * function calls - into C bindings, through a function pointer, or into
 * another Poco function.
 ****************************************************************************/
PO_STEP_HANDLER po_step_call(PoRunState* st, int op)
{
	/*
	 * kiki note:
	 *
	 * I switched how this was working a bit so that po_ffi_call
	 * now returns a Pt_num, making it so we don't have to worry
	 * as much on this side about the type of the return value.
	TODO:
		- if ((st->p->check_abort)(st->p->check_abort_data))
		  goto ABORT;
	*/
	switch (op) {
		case OP_ICCALL:  /* call int valued C function */
		case OP_LCCALL:  /* call long valued C function */
		case OP_DCCALL:  /* call double valued C function */
		case OP_PCCALL:  /* call (popot) pointer valued C function */
		case OP_CPCCALL: /* call C pointer valued C function */
		case OP_CVCCALL: /* call void valued C function */
		{
			Po_FFI* binding;

			if (PO_STACK_OVERFLOW(st, MIN_CCALL_STACK)) {
				st->err = Err_stack;
				return PO_STEP_TRAP;
			}
			binding = po_ffi_find_binding(st->p, st->ip->func);
			if (st->p->builtin_error < Success) {
				return PO_STEP_ERR_LIBROUTINE;
			}

			st->acc->ret = po_ffi_call(binding, st->stack, &st->p->variadic, st->p);
			if (st->p->builtin_error < Success) {
				return PO_STEP_ERR_LIBROUTINE;
			}
			st->ip = OPTR(st->ip, sizeof(st->ip->func));
			return PO_STEP_NEXT;
		}

		case OP_CALLI: /* Call Indirect (via pointer) */
			if ((st->acc->f = st->stack->ppt.pt) == NULL) {
				return PO_STEP_ERR_NULL;
			}
			if (st->acc->f->magic != FUNC_MAGIC) {
				return PO_STEP_ERR_NOTAFUNC;
			}
			switch (st->acc->f->type) {
				case CFF_C:
					if (PO_STACK_OVERFLOW(st, MIN_CCALL_STACK)) {
						st->err = Err_stack;
						return PO_STEP_TRAP;
					}
					st->stack = OPTR(st->stack, sizeof(st->stack->ppt));
					switch (st->acc->f->return_type->ido_type) {
						// #!FIXME: This!
						case IDO_INT:
							//								st->acc->ret.i = IC_call(st->stack,
							// st->acc->f->code_pt);
							st->acc->ret.i = 0;
							break;
						case IDO_LONG:
							//								st->acc->ret.l = LC_call(st->stack,
							// st->acc->f->code_pt);
							st->acc->ret.l = 0;
							break;
						case IDO_DOUBLE:
							//								st->acc->ret.d = DC_call(st->stack,
							// st->acc->f->code_pt);
							st->acc->ret.d = 0.0;
							break;
						case IDO_POINTER:
							//								st->acc->ret.ppt = PC_call(st->stack,
							// st->acc->f->code_pt);
							st->acc->ret.ppt = empty_popot;
							break;
						case IDO_VOID:
							//								VC_call(st->stack, st->acc->f->code_pt);
							break;
						default:
							st->err = Err_unimpl;
							return PO_STEP_TRAP;
					}
					if (st->p->builtin_error < Success) {
						return PO_STEP_ERR_LIBROUTINE;
					}
					break;
				case CFF_POCO:
					if (PO_STACK_OVERFLOW(st, MIN_PCALL_STACK)) {
						st->err = Err_stack;
						return PO_STEP_TRAP;
					}
					st->stack = OPTR(st->stack, sizeof(st->stack->ppt) - sizeof(st->stack->p));
					st->stack->p = st->ip;
					st->ip = (Pt_num*)st->acc->f->code_pt;
					++st->p->debug_call_depth;
					break;
			}
			if ((st->p->check_abort)(st->p->check_abort_data)) {
				return PO_STEP_ABORT;
			}
			return PO_STEP_NEXT;

		case OP_PCALL: /* Call Poco function */
			if (PO_STACK_OVERFLOW(st, MIN_PCALL_STACK)) {
				st->err = Err_stack;
				return PO_STEP_TRAP;
			}
			st->acc->f = st->ip->p;
			st->ip = OPTR(st->ip, sizeof(st->acc->f));
			st->stack = OPTR(st->stack, -sizeof(st->ip));
			st->stack->p = st->ip;
			st->ip = (Pt_num*)st->acc->f->code_pt;
			++st->p->debug_call_depth;
			if ((st->p->check_abort)(st->p->check_abort_data)) {
				return PO_STEP_ABORT;
			}
			return PO_STEP_NEXT;
	}
	PO_UNREACHABLE();
}

#ifdef STRING_EXPERIMENT
/*****************************************************************************
 * the String experiment
 *
 * Every opcode of the String type lives here, in one guard, rather than
 * interleaved with the numeric and pointer opcodes it shadows.  Keeping the
 * experiment in one place is what lets it be read, compiled, or dropped as a
 * unit.  See postring.c and the POCO_STRING_EXPERIMENT option.
 ****************************************************************************/
PO_STEP_HANDLER po_step_string(PoRunState* st, int op)
{
	Po_FFI* binding;

	switch (op) {
		case OP_STRING_TO_CPT:
			po_sr_dec_ref(st->stack->postring); /* dec ref count but
												 * don't deallocate yet */
			st->stack->p = PoStringBuf(&st->stack->postring);
			return PO_STEP_NEXT;

		case OP_CPT_TO_STRING:
			st->err = Err_bad_instruction; /* Right now we don't generate
											* these and so it'd be hard
											* to test the code required.... */
			return PO_STEP_TRAP;
			return PO_STEP_NEXT;

		case OP_STRING_TO_PPT:
			st->acc->ret.postring = st->stack->postring;
			po_sr_dec_ref(st->acc->ret.postring); /* dec ref count but
												   * don't deallocate yet */
			st->stack = OPTR(st->stack, sizeof(st->stack->postring) - sizeof(st->stack->ppt));
			st->stack->ppt = st->acc->ret.postring->string;
			return PO_STEP_NEXT;
		case OP_PPT_TO_STRING:
			st->acc->ret.postring =
				po_sr_new_copy(st->p, st->stack->ppt.pt, (int)Popot_bufsize(&st->stack->ppt));
			if (st->p->builtin_error < Success) {
				return PO_STEP_ERR_LIBROUTINE;
			}
			st->stack = OPTR(st->stack, sizeof(st->stack->ppt) - sizeof(st->stack->postring));
			st->stack->postring = st->acc->ret.postring;
			return PO_STEP_NEXT;

		case OP_STRING_CCALL: /* call string valued C function */
			if (PO_STACK_OVERFLOW(st, MIN_CCALL_STACK)) {
				st->err = Err_stack;
				return PO_STEP_TRAP;
			}
			binding = po_ffi_find_binding(st->p, st->ip->func);
			if (st->p->builtin_error < Success) {
				return PO_STEP_ERR_LIBROUTINE;
			}
			st->acc->ret = po_ffi_call(binding, st->stack, &st->p->variadic, st->p);
			if (st->p->builtin_error < Success) {
				return PO_STEP_ERR_LIBROUTINE;
			}
			st->ip = OPTR(st->ip, sizeof(st->ip->func));
			return PO_STEP_NEXT;

		case OP_GLO_STRING_VAR: /* push a global string onto data stack */
			st->stack = OPTR(st->stack, -sizeof(PoString));
			st->stack->postring = ((PoString*)(OPTR(st->globals, st->ip->doff)))[0];
			po_sr_inc_ref(st->stack->postring);
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;

		case OP_LOC_STRING_VAR: /* push a local string onto data stack */
			st->stack = OPTR(st->stack, -sizeof(PoString));
			st->stack->postring = ((PoString*)(OPTR(st->base, st->ip->doff)))[0];
			po_sr_inc_ref(st->stack->postring);
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;

		case OP_GLO_STRING_ASS: /* top of stack to global string variable */
			po_sr_clean_ref(st->p, (((PoString*)(OPTR(st->globals, st->ip->doff)))[0]));
			((PoString*)(OPTR(st->globals, st->ip->doff)))[0] = st->stack->postring;
			po_sr_inc_ref(st->stack->postring);
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;

		case OP_LOC_STRING_ASS: /* top of stack to local string variable */
			po_sr_clean_ref(st->p, (((PoString*)(OPTR(st->base, st->ip->doff)))[0]));
			((PoString*)(OPTR(st->base, st->ip->doff)))[0] = st->stack->postring;
			po_sr_inc_ref(st->stack->postring);
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;

		case OP_STRING_I_VAR:
			st->acc->ret.ppt = st->stack->ppt;
			if (st->acc->ret.ppt.pt == NULL) {
				return PO_STEP_ERR_NULL;
			}
			if (!po_registered_pointer_access_is_valid(st->p->pointer_registry, &st->acc->ret.ppt,
													   sizeof(PoString),
													   POCO_POINTER_PERMISSION_READ)) {
				return PO_STEP_ERR_POINTER_ACCESS;
			}
			if (st->acc->ret.ppt.pt < st->acc->ret.ppt.min) {
				return PO_STEP_ERR_SMALL;
			}
			if (st->acc->ret.ppt.pt > st->acc->ret.ppt.max) {
				return PO_STEP_ERR_BIG;
			}
			st->stack = OPTR(st->stack, sizeof(st->acc->ret.ppt) - sizeof(st->acc->ret.postring));
			st->stack->postring = *((PoString*)(st->acc->ret.ppt.pt));
			po_sr_inc_ref(st->stack->postring);
			return PO_STEP_NEXT;

		case OP_STRING_I_ASS:
			st->acc->ret.ppt = st->stack->ppt;
			if (st->acc->ret.ppt.pt == NULL) {
				return PO_STEP_ERR_NULL;
			}
			if (!po_registered_pointer_access_is_valid(st->p->pointer_registry, &st->acc->ret.ppt,
													   sizeof(PoString),
													   POCO_POINTER_PERMISSION_WRITE)) {
				return PO_STEP_ERR_POINTER_ACCESS;
			}
			if (st->acc->ret.ppt.pt < st->acc->ret.ppt.min) {
				return PO_STEP_ERR_SMALL;
			}
			if (st->acc->ret.ppt.pt > st->acc->ret.ppt.max) {
				return PO_STEP_ERR_BIG;
			}
			st->stack = OPTR(st->stack, sizeof(st->stack->ppt));
			po_sr_clean_ref(st->p, ((PoString*)(st->acc->ret.ppt.pt))[0]);
			((PoString*)(st->acc->ret.ppt.pt))[0] = st->stack->postring;
			po_sr_inc_ref(st->stack->postring);
			return PO_STEP_NEXT;

		case OP_STRING_CAT: /* Concatenate top two strings */
			st->acc->ret.postring = po_sr_cat_and_clean(
				st->p, ((Pt_num*)OPTR(st->stack, sizeof(st->stack->postring)))->postring,
				st->stack->postring);
			st->stack =
				OPTR(st->stack, 2 * sizeof(st->stack->postring) - sizeof(st->stack->postring));
			st->stack->postring = st->acc->ret.postring;
			return PO_STEP_NEXT;

		case OP_STRING_EQ:
			st->acc->ret.inty = po_sr_eq_and_clean(
				st->p, st->stack->postring,
				((Pt_num*)OPTR(st->stack, sizeof(st->stack->postring)))->postring);
			st->stack = OPTR(st->stack, 2 * sizeof(st->stack->postring) - sizeof(st->stack->inty));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;

		case OP_STRING_NE:
			st->acc->ret.inty = !po_sr_eq_and_clean(
				st->p, st->stack->postring,
				((Pt_num*)OPTR(st->stack, sizeof(st->stack->postring)))->postring);
			st->stack = OPTR(st->stack, 2 * sizeof(st->stack->postring) - sizeof(st->stack->inty));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;

		case OP_STRING_GE:
			st->acc->ret.inty = po_sr_ge_and_clean(
				st->p, ((Pt_num*)OPTR(st->stack, sizeof(st->stack->postring)))->postring,
				st->stack->postring);
			st->stack = OPTR(st->stack, 2 * sizeof(st->stack->postring) - sizeof(st->stack->inty));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;

		case OP_STRING_GT:
			st->acc->ret.inty = !po_sr_le_and_clean(
				st->p, ((Pt_num*)OPTR(st->stack, sizeof(st->stack->postring)))->postring,
				st->stack->postring);
			st->stack = OPTR(st->stack, 2 * sizeof(st->stack->postring) - sizeof(st->stack->inty));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;

		case OP_STRING_LE:
			st->acc->ret.inty = po_sr_le_and_clean(
				st->p, ((Pt_num*)OPTR(st->stack, sizeof(st->stack->postring)))->postring,
				st->stack->postring);
			st->stack = OPTR(st->stack, 2 * sizeof(st->stack->postring) - sizeof(st->stack->inty));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;

		case OP_STRING_LT:
			st->acc->ret.inty = !po_sr_ge_and_clean(
				st->p, ((Pt_num*)OPTR(st->stack, sizeof(st->stack->postring)))->postring,
				st->stack->postring);
			st->stack = OPTR(st->stack, 2 * sizeof(st->stack->postring) - sizeof(st->stack->inty));
			st->stack->inty = st->acc->ret.inty;
			return PO_STEP_NEXT;

		case OP_STRING_PUSH:
			st->stack = OPTR(st->stack, -sizeof(st->stack->postring));
			st->stack->postring = st->acc->ret.postring;
			return PO_STEP_NEXT;

		case OP_STRING_POP:
			st->acc->ret.postring = st->stack->postring;
			st->p->result = st->acc->ret;
			st->stack = OPTR(st->stack, sizeof(st->stack->postring));
			return PO_STEP_NEXT;
		case OP_CLEAN_STRING: /* Pop string and dec reference count */
			st->acc->ret.postring = st->stack->postring;
			po_sr_clean_ref(st->p, st->acc->ret.postring);
			st->p->result = st->acc->ret;
			st->stack = OPTR(st->stack, sizeof(st->stack->postring));
			return PO_STEP_NEXT;

		case OP_FREE_STRING:
			po_sr_clean_ref(st->p, (((PoString*)(OPTR(st->base, st->ip->doff)))[0]));
			st->ip = OPTR(st->ip, INTY_SIZE);
			return PO_STEP_NEXT;
	}
	PO_UNREACHABLE();
}
#endif /* STRING_EXPERIMENT */

/*****************************************************************************
 * decode one instruction and run it.
 *
 * The switch is still the flat 191-label jump table it always was; every arm
 * hands off to the handler for its opcode family, and every handler is
 * force-inlined, so this compiles to the same dispatch as the old monolith.
 ****************************************************************************/
PO_STEP_HANDLER po_step_dispatch(PoRunState* st, int op)
{
	switch (op) {
		default:
			st->err = Err_bad_instruction;
			return PO_STEP_TRAP;

		case OP_END: /* finished instruction stream */
			return PO_STEP_DONE;

		case OP_NOP:
			return PO_STEP_NEXT;

			/* datatype conversions */
			PO_DISPATCH_CASE(OP_INT_TO_LONG, po_step_convert);
			PO_DISPATCH_CASE(OP_LONG_TO_INT, po_step_convert);
			PO_DISPATCH_CASE(OP_INT_TO_DOUBLE, po_step_convert);
			PO_DISPATCH_CASE(OP_LONG_TO_DOUBLE, po_step_convert);
			PO_DISPATCH_CASE(OP_DOUBLE_TO_LONG, po_step_convert);
			PO_DISPATCH_CASE(OP_DOUBLE_TO_INT, po_step_convert);
			PO_DISPATCH_CASE(OP_PPT_TO_CPT, po_step_convert);
			PO_DISPATCH_CASE(OP_CPT_TO_PPT, po_step_convert);

			/* function calls */
			PO_DISPATCH_CASE(OP_ICCALL, po_step_call);
			PO_DISPATCH_CASE(OP_LCCALL, po_step_call);
			PO_DISPATCH_CASE(OP_DCCALL, po_step_call);
			PO_DISPATCH_CASE(OP_PCCALL, po_step_call);
			PO_DISPATCH_CASE(OP_CPCCALL, po_step_call);
			PO_DISPATCH_CASE(OP_CVCCALL, po_step_call);
			PO_DISPATCH_CASE(OP_CALLI, po_step_call);
			PO_DISPATCH_CASE(OP_PCALL, po_step_call);

			/* function entry and exit */
			PO_DISPATCH_CASE(OP_RET, po_step_frame);
			PO_DISPATCH_CASE(OP_ADD_STACK, po_step_frame);
			PO_DISPATCH_CASE(OP_ENTER, po_step_frame);
			PO_DISPATCH_CASE(OP_LEAVE, po_step_frame);

			/* branches */
			PO_DISPATCH_CASE(OP_BRA, po_step_branch);
			PO_DISPATCH_CASE(OP_BEQ, po_step_branch);
			PO_DISPATCH_CASE(OP_BNE, po_step_branch);

			/* constants and effective addresses */
			PO_DISPATCH_CASE(OP_ICON, po_step_push_const);
			PO_DISPATCH_CASE(OP_LCON, po_step_push_const);
			PO_DISPATCH_CASE(OP_DCON, po_step_push_const);
			PO_DISPATCH_CASE(OP_PCON, po_step_push_const);
			PO_DISPATCH_CASE(OP_GLO_ADDRESS, po_step_push_const);
			PO_DISPATCH_CASE(OP_LOC_ADDRESS, po_step_push_const);
			PO_DISPATCH_CASE(OP_CODE_ADDRESS, po_step_push_const);

			/* direct variable loads */
			PO_DISPATCH_CASE(OP_GLO_CVAR, po_step_load_direct);
			PO_DISPATCH_CASE(OP_GLO_SVAR, po_step_load_direct);
			PO_DISPATCH_CASE(OP_GLO_IVAR, po_step_load_direct);
			PO_DISPATCH_CASE(OP_GLO_LVAR, po_step_load_direct);
			PO_DISPATCH_CASE(OP_GLO_PVAR, po_step_load_direct);
			PO_DISPATCH_CASE(OP_GLO_VVAR, po_step_load_direct);
			PO_DISPATCH_CASE(OP_GLO_FVAR, po_step_load_direct);
			PO_DISPATCH_CASE(OP_GLO_DVAR, po_step_load_direct);
			PO_DISPATCH_CASE(OP_LOC_CVAR, po_step_load_direct);
			PO_DISPATCH_CASE(OP_LOC_SVAR, po_step_load_direct);
			PO_DISPATCH_CASE(OP_LOC_IVAR, po_step_load_direct);
			PO_DISPATCH_CASE(OP_LOC_LVAR, po_step_load_direct);
			PO_DISPATCH_CASE(OP_LOC_PVAR, po_step_load_direct);
			PO_DISPATCH_CASE(OP_LOC_FVAR, po_step_load_direct);
			PO_DISPATCH_CASE(OP_LOC_DVAR, po_step_load_direct);

			/* direct variable stores */
			PO_DISPATCH_CASE(OP_GLO_CASS, po_step_store_direct);
			PO_DISPATCH_CASE(OP_GLO_SASS, po_step_store_direct);
			PO_DISPATCH_CASE(OP_GLO_IASS, po_step_store_direct);
			PO_DISPATCH_CASE(OP_GLO_LASS, po_step_store_direct);
			PO_DISPATCH_CASE(OP_GLO_PASS, po_step_store_direct);
			PO_DISPATCH_CASE(OP_GLO_FASS, po_step_store_direct);
			PO_DISPATCH_CASE(OP_GLO_DASS, po_step_store_direct);
			PO_DISPATCH_CASE(OP_LOC_CASS, po_step_store_direct);
			PO_DISPATCH_CASE(OP_LOC_SASS, po_step_store_direct);
			PO_DISPATCH_CASE(OP_LOC_IASS, po_step_store_direct);
			PO_DISPATCH_CASE(OP_LOC_LASS, po_step_store_direct);
			PO_DISPATCH_CASE(OP_LOC_PASS, po_step_store_direct);
			PO_DISPATCH_CASE(OP_LOC_FASS, po_step_store_direct);
			PO_DISPATCH_CASE(OP_LOC_DASS, po_step_store_direct);

			/* indirect variable loads */
			PO_DISPATCH_CASE(OP_CI_VAR, po_step_load_indirect);
			PO_DISPATCH_CASE(OP_SI_VAR, po_step_load_indirect);
			PO_DISPATCH_CASE(OP_II_VAR, po_step_load_indirect);
			PO_DISPATCH_CASE(OP_PI_VAR, po_step_load_indirect);
			PO_DISPATCH_CASE(OP_LI_VAR, po_step_load_indirect);
			PO_DISPATCH_CASE(OP_FI_VAR, po_step_load_indirect);
			PO_DISPATCH_CASE(OP_DI_VAR, po_step_load_indirect);

			/* indirect variable stores */
			PO_DISPATCH_CASE(OP_CI_ASS, po_step_store_indirect);
			PO_DISPATCH_CASE(OP_SI_ASS, po_step_store_indirect);
			PO_DISPATCH_CASE(OP_II_ASS, po_step_store_indirect);
			PO_DISPATCH_CASE(OP_PI_ASS, po_step_store_indirect);
			PO_DISPATCH_CASE(OP_LI_ASS, po_step_store_indirect);
			PO_DISPATCH_CASE(OP_FI_ASS, po_step_store_indirect);
			PO_DISPATCH_CASE(OP_DI_ASS, po_step_store_indirect);

			/* arithmetic, negation and modulo */
			PO_DISPATCH_CASE(OP_IADD, po_step_arith);
			PO_DISPATCH_CASE(OP_LADD, po_step_arith);
			PO_DISPATCH_CASE(OP_DADD, po_step_arith);
			PO_DISPATCH_CASE(OP_PADD, po_step_arith);
			PO_DISPATCH_CASE(OP_ISUB, po_step_arith);
			PO_DISPATCH_CASE(OP_LSUB, po_step_arith);
			PO_DISPATCH_CASE(OP_DSUB, po_step_arith);
			PO_DISPATCH_CASE(OP_PSUB, po_step_arith);
			PO_DISPATCH_CASE(OP_IMUL, po_step_arith);
			PO_DISPATCH_CASE(OP_LMUL, po_step_arith);
			PO_DISPATCH_CASE(OP_DMUL, po_step_arith);
			PO_DISPATCH_CASE(OP_IDIV, po_step_arith);
			PO_DISPATCH_CASE(OP_LDIV, po_step_arith);
			PO_DISPATCH_CASE(OP_DDIV, po_step_arith);
			PO_DISPATCH_CASE(OP_INEG, po_step_arith);
			PO_DISPATCH_CASE(OP_LNEG, po_step_arith);
			PO_DISPATCH_CASE(OP_DNEG, po_step_arith);
			PO_DISPATCH_CASE(OP_IMOD, po_step_arith);
			PO_DISPATCH_CASE(OP_LMOD, po_step_arith);

			/* comparisons */
			PO_DISPATCH_CASE(OP_IEQ, po_step_compare);
			PO_DISPATCH_CASE(OP_LEQ, po_step_compare);
			PO_DISPATCH_CASE(OP_DEQ, po_step_compare);
			PO_DISPATCH_CASE(OP_PEQ, po_step_compare);
			PO_DISPATCH_CASE(OP_INE, po_step_compare);
			PO_DISPATCH_CASE(OP_LNE, po_step_compare);
			PO_DISPATCH_CASE(OP_DNE, po_step_compare);
			PO_DISPATCH_CASE(OP_PNE, po_step_compare);
			PO_DISPATCH_CASE(OP_IGE, po_step_compare);
			PO_DISPATCH_CASE(OP_LGE, po_step_compare);
			PO_DISPATCH_CASE(OP_DGE, po_step_compare);
			PO_DISPATCH_CASE(OP_PGE, po_step_compare);
			PO_DISPATCH_CASE(OP_IGT, po_step_compare);
			PO_DISPATCH_CASE(OP_LGT, po_step_compare);
			PO_DISPATCH_CASE(OP_DGT, po_step_compare);
			PO_DISPATCH_CASE(OP_PGT, po_step_compare);
			PO_DISPATCH_CASE(OP_ILE, po_step_compare);
			PO_DISPATCH_CASE(OP_LLE, po_step_compare);
			PO_DISPATCH_CASE(OP_DLE, po_step_compare);
			PO_DISPATCH_CASE(OP_PLE, po_step_compare);
			PO_DISPATCH_CASE(OP_ILT, po_step_compare);
			PO_DISPATCH_CASE(OP_LLT, po_step_compare);
			PO_DISPATCH_CASE(OP_DLT, po_step_compare);
			PO_DISPATCH_CASE(OP_PLT, po_step_compare);

			/* shifts, bitwise and logical operators */
			PO_DISPATCH_CASE(OP_ILSHIFT, po_step_bitwise);
			PO_DISPATCH_CASE(OP_LLSHIFT, po_step_bitwise);
			PO_DISPATCH_CASE(OP_IRSHIFT, po_step_bitwise);
			PO_DISPATCH_CASE(OP_LRSHIFT, po_step_bitwise);
			PO_DISPATCH_CASE(OP_IBAND, po_step_bitwise);
			PO_DISPATCH_CASE(OP_LBAND, po_step_bitwise);
			PO_DISPATCH_CASE(OP_IBOR, po_step_bitwise);
			PO_DISPATCH_CASE(OP_LBOR, po_step_bitwise);
			PO_DISPATCH_CASE(OP_IXOR, po_step_bitwise);
			PO_DISPATCH_CASE(OP_LXOR, po_step_bitwise);
			PO_DISPATCH_CASE(OP_ILAND, po_step_bitwise);
			PO_DISPATCH_CASE(OP_LLAND, po_step_bitwise);
			PO_DISPATCH_CASE(OP_ILOR, po_step_bitwise);
			PO_DISPATCH_CASE(OP_LLOR, po_step_bitwise);
			PO_DISPATCH_CASE(OP_ICOMP, po_step_bitwise);
			PO_DISPATCH_CASE(OP_LCOMP, po_step_bitwise);
			PO_DISPATCH_CASE(OP_INOT, po_step_bitwise);
			PO_DISPATCH_CASE(OP_LNOT, po_step_bitwise);

			/* accumulator push/pop and stack duplication */
			PO_DISPATCH_CASE(OP_IPUSH, po_step_stack);
			PO_DISPATCH_CASE(OP_LPUSH, po_step_stack);
			PO_DISPATCH_CASE(OP_CPPUSH, po_step_stack);
			PO_DISPATCH_CASE(OP_DPUSH, po_step_stack);
			PO_DISPATCH_CASE(OP_PPUSH, po_step_stack);
			PO_DISPATCH_CASE(OP_IPOP, po_step_stack);
			PO_DISPATCH_CASE(OP_LPOP, po_step_stack);
			PO_DISPATCH_CASE(OP_CPPOP, po_step_stack);
			PO_DISPATCH_CASE(OP_DPOP, po_step_stack);
			PO_DISPATCH_CASE(OP_PPOP, po_step_stack);
			PO_DISPATCH_CASE(OP_IDUPE, po_step_stack);
			PO_DISPATCH_CASE(OP_LDUPE, po_step_stack);
			PO_DISPATCH_CASE(OP_DDUPE, po_step_stack);
			PO_DISPATCH_CASE(OP_PDUPE, po_step_stack);

			/* pointer arithmetic */
			PO_DISPATCH_CASE(OP_ADD_IOFFSET, po_step_ptr_math);
			PO_DISPATCH_CASE(OP_ADD_LOFFSET, po_step_ptr_math);
			PO_DISPATCH_CASE(OP_PTRDIFF, po_step_ptr_math);

			/* block copy and move */
			PO_DISPATCH_CASE(OP_COPY, po_step_memory);
			PO_DISPATCH_CASE(OP_MOVE, po_step_memory);

			/* libffi variadic type recording */
			PO_DISPATCH_CASE(OP_FFI_POP_ALL, po_step_ffi_variadic);
			PO_DISPATCH_CASE(OP_FFI_PUSH_POINTER, po_step_ffi_variadic);
			PO_DISPATCH_CASE(OP_FFI_PUSH_SINT32, po_step_ffi_variadic);
			PO_DISPATCH_CASE(OP_FFI_PUSH_FLOAT, po_step_ffi_variadic);
			PO_DISPATCH_CASE(OP_FFI_PUSH_DOUBLE, po_step_ffi_variadic);
			PO_DISPATCH_CASE(OP_FFI_PUSH_UINT8, po_step_ffi_variadic);
			PO_DISPATCH_CASE(OP_FFI_PUSH_SINT8, po_step_ffi_variadic);
			PO_DISPATCH_CASE(OP_FFI_PUSH_UINT16, po_step_ffi_variadic);
			PO_DISPATCH_CASE(OP_FFI_PUSH_SINT16, po_step_ffi_variadic);
			PO_DISPATCH_CASE(OP_FFI_PUSH_UINT32, po_step_ffi_variadic);
			PO_DISPATCH_CASE(OP_FFI_PUSH_UINT64, po_step_ffi_variadic);
			PO_DISPATCH_CASE(OP_FFI_PUSH_SINT64, po_step_ffi_variadic);
			PO_DISPATCH_CASE(OP_FFI_PUSH_VOID, po_step_ffi_variadic);
			PO_DISPATCH_CASE(OP_FFI_PUSH_NULL, po_step_ffi_variadic);

#ifdef STRING_EXPERIMENT
			/* the String experiment */
			PO_DISPATCH_CASE(OP_STRING_TO_CPT, po_step_string);
			PO_DISPATCH_CASE(OP_CPT_TO_STRING, po_step_string);
			PO_DISPATCH_CASE(OP_STRING_TO_PPT, po_step_string);
			PO_DISPATCH_CASE(OP_PPT_TO_STRING, po_step_string);
			PO_DISPATCH_CASE(OP_STRING_CCALL, po_step_string);
			PO_DISPATCH_CASE(OP_GLO_STRING_VAR, po_step_string);
			PO_DISPATCH_CASE(OP_LOC_STRING_VAR, po_step_string);
			PO_DISPATCH_CASE(OP_GLO_STRING_ASS, po_step_string);
			PO_DISPATCH_CASE(OP_LOC_STRING_ASS, po_step_string);
			PO_DISPATCH_CASE(OP_STRING_I_VAR, po_step_string);
			PO_DISPATCH_CASE(OP_STRING_I_ASS, po_step_string);
			PO_DISPATCH_CASE(OP_STRING_CAT, po_step_string);
			PO_DISPATCH_CASE(OP_STRING_EQ, po_step_string);
			PO_DISPATCH_CASE(OP_STRING_NE, po_step_string);
			PO_DISPATCH_CASE(OP_STRING_GE, po_step_string);
			PO_DISPATCH_CASE(OP_STRING_GT, po_step_string);
			PO_DISPATCH_CASE(OP_STRING_LE, po_step_string);
			PO_DISPATCH_CASE(OP_STRING_LT, po_step_string);
			PO_DISPATCH_CASE(OP_STRING_PUSH, po_step_string);
			PO_DISPATCH_CASE(OP_STRING_POP, po_step_string);
			PO_DISPATCH_CASE(OP_CLEAN_STRING, po_step_string);
			PO_DISPATCH_CASE(OP_FREE_STRING, po_step_string);
#endif /* STRING_EXPERIMENT */
	}
}

/*****************************************************************************
 * copy the caller's argument values onto the interpreter stack.
 ****************************************************************************/
static Errcode po_push_callback_values(void* stack_top, const PocoCallbackValue* values,
									   size_t value_count)
{
	char* argument_data = stack_top;
	size_t value_index;

	for (value_index = 0; value_index < value_count; ++value_index) {
		switch (values[value_index].kind) {
			case POCO_CALLBACK_VALUE_INT:
				memcpy(argument_data, &values[value_index].value.int_value,
					   sizeof(values[value_index].value.int_value));
				argument_data += sizeof(values[value_index].value.int_value);
				break;
			case POCO_CALLBACK_VALUE_LONG:
				memcpy(argument_data, &values[value_index].value.long_value,
					   sizeof(values[value_index].value.long_value));
				argument_data += sizeof(values[value_index].value.long_value);
				break;
			case POCO_CALLBACK_VALUE_DOUBLE:
				memcpy(argument_data, &values[value_index].value.double_value,
					   sizeof(values[value_index].value.double_value));
				argument_data += sizeof(values[value_index].value.double_value);
				break;
			case POCO_CALLBACK_VALUE_POPOT:
				memcpy(argument_data, &values[value_index].value.popot_value,
					   sizeof(values[value_index].value.popot_value));
				argument_data += sizeof(values[value_index].value.popot_value);
				break;
			default:
				return Err_parameter_range;
		}
	}
	return Success;
}

/*****************************************************************************
 * interpret code stream - the heart of the runtime interpreter.
 ****************************************************************************/
static Errcode poco_run_callback(PocoActivation* p, void* code_pt, Pt_num* pret,
								 const PocoCallbackValue* values, size_t value_count)
{
	FILE* tfile = NULL;
	PoRunState state;
	Eax acc;
	PoStep step;
	Errcode err;
	UBYTE* stack_area;
	Pt_num* stack;
	int op;
	int end_op = OP_END;
	size_t argument_bytes = 0;
	size_t value_index;
	bool temporary_stack;
	size_t saved_debug_call_depth;
	size_t debug_depth_floor;

	if (p == NULL || code_pt == NULL || pret == NULL || (value_count != 0 && values == NULL)) {
		return Err_null_ref;
	}

	for (value_index = 0; value_index < value_count; ++value_index) {
		size_t value_size;

		switch (values[value_index].kind) {
			case POCO_CALLBACK_VALUE_INT:
				value_size = sizeof(values[value_index].value.int_value);
				break;
			case POCO_CALLBACK_VALUE_LONG:
				value_size = sizeof(values[value_index].value.long_value);
				break;
			case POCO_CALLBACK_VALUE_DOUBLE:
				value_size = sizeof(values[value_index].value.double_value);
				break;
			case POCO_CALLBACK_VALUE_POPOT:
				value_size = sizeof(values[value_index].value.popot_value);
				break;
			default:
				return Err_parameter_range;
		}
		if (argument_bytes > (size_t)LONG_MAX - value_size) {
			return Err_parameter_range;
		}
		argument_bytes += value_size;
	}
	if (p->stack_size <= (long)sizeof(state.ip) ||
		argument_bytes > (size_t)(p->stack_size - (long)sizeof(state.ip))) {
		return Err_stack;
	}

	state.p = p;
	state.ip = code_pt;
	state.globals = (Pt_num*)(p->data + p->data_size);
	state.acc = &acc;
	state.err = Success;

	saved_debug_call_depth = p->debug_call_depth;
	debug_depth_floor = p->run_depth != 0 ? saved_debug_call_depth + 1 : 0;
	p->debug_call_depth = debug_depth_floor;
	temporary_stack = p->run_depth++ != 0 || p->stack == NULL;
	if (temporary_stack) {
		stack_area = pj_malloc(p->stack_size);
		if (stack_area == NULL) {
			--p->run_depth;
			p->debug_call_depth = saved_debug_call_depth;
			return Err_no_memory;
		}
	} else {
		stack_area = (UBYTE*)p->stack;
	}

	stack = (Pt_num*)(stack_area + p->stack_size);
	state.stack_area = stack_area;

	if (argument_bytes > 0) {
		stack = OPTR(stack, -(long)argument_bytes);
		err = po_push_callback_values(stack, values, value_count);
		if (err != Success) {
			goto DEALLOC_AND_EXIT;
		}
	}

	/* final return address is to an end-op */

	stack = OPTR(stack, -sizeof(state.ip));
	stack->p = &end_op;
	state.stack = stack;
	state.base = stack;
	state.debug_depth_floor = debug_depth_floor;

	/* assume a starting condition of success */

	p->builtin_error = Success;

	/* supply a default abort checker if none provided */
	if (p->check_abort == NULL) {
		p->check_abort = nofunc;
	}

	for (;;) {
		if (p->debug_hook != NULL) {
			/* The hook takes the instruction pointer as a byte address; the
			 * interpreter walks it as Pt_num.  Same address, different view. */
			p->debug_hook(p, (Code*)state.ip, stack_area, state.base);
		}
#ifdef DEVELOPMENT
		/* Trace destination is per-activation (PocoRunOptions::instruction_trace),
		 * so concurrent runs do not share one stream or one on/off switch. */
		if (p->instruction_trace != NULL) {
			po_disasm(p->instruction_trace, state.ip, (C_frame*)p->code->prototypes);
		}
#endif /* DEVELOPMENT */

		op = state.ip->inty;
		state.ip = OPTR(state.ip, OPY_SIZE);

		step = po_step_dispatch(&state, op);
		if (step == PO_STEP_NEXT) {
			continue;
		}

		err = state.err;
		switch (step) {
			case PO_STEP_DONE:
				*pret = acc.ret;
				err = Success;
				goto DEALLOC_AND_EXIT;
			case PO_STEP_ABORT:
				goto ABORT;
			case PO_STEP_TRAP:
				goto DEBUG_TRACE;
			case PO_STEP_ERR_NOTAFUNC:
				goto ERR_NOTAFUNC;
			case PO_STEP_ERR_NULL:
				goto ERR_NULL;
			case PO_STEP_ERR_SMALL:
				goto ERR_SMALL;
			case PO_STEP_ERR_BIG:
				goto ERR_BIG;
			case PO_STEP_ERR_POINTER_ACCESS:
				goto ERR_POINTER_ACCESS;
			case PO_STEP_ERR_FPMATH:
				goto ERR_INLINE_FPMATH;
			case PO_STEP_ERR_LIBROUTINE:
				goto ERR_IN_LIBROUTINE;
			case PO_STEP_ERR_FFI:
				goto ERR_IN_FFI;
			case PO_STEP_NEXT:
				break;
		}
	}

ABORT:
	err = Err_abort;
	goto DEALLOC_AND_EXIT;

ERR_NOTAFUNC:
	err = Err_function_not_found;
	goto DEBUG_TRACE;

ERR_NULL:
	err = Err_null_ref;
	goto DEBUG_TRACE;

ERR_SMALL:
	err = Err_index_small;
	goto DEBUG_TRACE;

ERR_BIG:
	err = Err_index_big;
	goto DEBUG_TRACE;

ERR_POINTER_ACCESS:
	p->builtin_error = err = Err_poco_ffi_bounds;
	goto DEBUG_TRACE;

ERR_INLINE_FPMATH:               // the host has indicated an 80x87 math err happened
								 // while interpreting poco instructions.  we remember
	err = p->builtin_error;      // the status and clear the global status in p->builtin_error.
	p->builtin_error = Success;  // this causes the DEBUG tracing to report the error
	goto DEBUG_TRACE;            // as occurring in the poco code, not a lib routine.

ERR_IN_LIBROUTINE:
	err = p->builtin_error;
	if (err == Err_poco_exit)              // the ONLY thing that can set this
	{                                      // is poco's builtin exit() function,
		p->builtin_error = err = Success;  // invoked with a code >= Success.
		goto DEALLOC_AND_EXIT;             // this gets us out with a good status.
	}

	if (err == Err_early_exit || err == Err_abort) {
		goto DEALLOC_AND_EXIT;
	}

	goto DEBUG_TRACE;

ERR_IN_FFI:
	// ##!TODO: handle error situations

	goto DEBUG_TRACE;

DEBUG_TRACE:
	if (!p->enable_debug_trace) {
		goto DEALLOC_AND_EXIT;
	}

	if (err != Err_in_err_file) {
		char buf[128];

		if (p->trace_file == NULL) {
			tfile = stdout;
		} else if ((tfile = fopen(p->trace_file, "w")) == NULL) {
			goto DEALLOC_AND_EXIT;
		}

		if (tfile != NULL) {
			get_errtext(err, buf);
			fprintf(tfile, "%s ", buf);
			po_print_trace(p, tfile, state.stack, state.base, state.globals, state.ip,
						   p->builtin_error);
			if (tfile != stdout) {
				fclose(tfile);
			}
			err = Err_in_err_file;
			goto DEALLOC_AND_EXIT;
		}
	}

DEALLOC_AND_EXIT:
	if (temporary_stack) {
		pj_free(stack_area);
	}
	--p->run_depth;
	p->debug_call_depth = saved_debug_call_depth;

	return err;
}

Errcode poco_invoke_callback(void* code_pt, Pt_num* pret, const PocoCallbackValue* values,
							 size_t value_count)
{
	Func_frame* function = code_pt;

	if (function == NULL || function->magic != FUNC_MAGIC || function->code_pt == NULL ||
		function->activation == NULL) {
		return Err_function_not_found;
	}
	return poco_run_callback(function->activation, function->code_pt, pret, values, value_count);
}

/*****************************************************************************
 * set up the runtime environment and call the interpreter.
 *
 * this must be called only by the routines in pocoface.c or fold.c.
 * to call from a lib/poe routine back into poco via a pointer passed to
 * you from a Poco program you must enter through poco_invoke_callback().
 ****************************************************************************/
Errcode po_run_ops(PocoActivation* p, Code* code_pt, Pt_num* pret)
{
	Pt_num dummy_ret;

	if (pret == NULL) {
		pret = &dummy_ret;
	}

	return poco_run_callback(p, code_pt, pret, NULL, 0);
}

Errcode po_run_ops_values(PocoActivation* p, Code* code_pt, Pt_num* pret,
						  const PocoCallbackValue* values, size_t value_count)
{
	Pt_num dummy_ret;

	if (pret == NULL) {
		pret = &dummy_ret;
	}
	return poco_run_callback(p, code_pt, pret, values, value_count);
}
