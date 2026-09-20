/*******************************************************************************
 * bytecode_iter.h - One cursor over a run of Poco instructions.
 *
 * Poco bytecode is a flat run of (int opcode, operand blob) pairs whose
 * operand width comes from po_ins_table[opcode].op_size.  The linker, the
 * program-image serializer, the offset/ordinal converters, the constant
 * folder and the disassembler all need to walk that run; this is the only
 * place that reads an opcode, validates it and advances past its operand.
 ******************************************************************************/

#ifndef POCO_BYTECODE_ITER_H
#define POCO_BYTECODE_ITER_H

#include "pocoop.h"

#include <stddef.h>
#include <stdint.h>

typedef enum PoCodeIterStatus {
	/* The cursor reached the end of a bounded run. */
	PO_CODE_ITER_END = 0,
	/* out_ins describes a complete instruction. */
	PO_CODE_ITER_OK = 1,
	/* Fewer bytes remain than an opcode word needs. */
	PO_CODE_ITER_TRUNCATED_OP = -1,
	/* The opcode word is not an index into po_ins_table. */
	PO_CODE_ITER_BAD_OPCODE = -2,
	/* The opcode is valid but its operand runs past the end. */
	PO_CODE_ITER_TRUNCATED_OPERAND = -3
} PoCodeIterStatus;

typedef struct PoCodeIns {
	/* The opcode word as read, whatever it was.  It indexes po_ins_table only
	 * when next() returned PO_CODE_ITER_OK or PO_CODE_ITER_TRUNCATED_OPERAND;
	 * on PO_CODE_ITER_BAD_OPCODE it is the out-of-range word itself, which is
	 * what the disassembler reports.  Zero when no word was read at all. */
	int op;
	/* po_ins_table entry for op; NULL when the opcode is out of range. */
	const Poco_op_table* entry;
	/* First operand byte, writable so the linker can patch in place. */
	void* operand;
	/* Byte offset of the opcode word from the start of the run. */
	size_t offset;
	/* Ordinal of this instruction within the run, counting from zero. */
	uint64_t index;
} PoCodeIns;

typedef struct PoCodeIter {
	uint8_t* start;
	uint8_t* cursor;
	/* End of the run; meaningful only when bounded.  An empty run at a NULL
	 * code pointer is legitimate (a frame with no compiled body), so the end
	 * pointer alone cannot say whether a bound is known. */
	uint8_t* end;
	int bounded;
	uint64_t index;
} PoCodeIter;

/* Walk size bytes of instructions starting at code. */
void po_code_iter_init(PoCodeIter* iter, void* code, long size);

/*
 * Walk instructions from code with no end bound.  For callers that hold an
 * instruction pointer without the length of the run behind it -- the runtime
 * trace hook is the only one -- so truncation cannot be detected.
 */
void po_code_iter_init_unbounded(PoCodeIter* iter, void* code);

/*
 * Decode the instruction at the cursor and advance past it.  On
 * PO_CODE_ITER_OK the cursor sits on the next instruction; on any error it
 * sits just past the opcode word, which is as far as the run can be trusted.
 */
PoCodeIterStatus po_code_iter_next(PoCodeIter* iter, PoCodeIns* out_ins);

/* Byte offset of the cursor from the start of the run. */
size_t po_code_iter_offset(const PoCodeIter* iter);

/* Current cursor position. */
void* po_code_iter_cursor(const PoCodeIter* iter);

/* Number of instructions decoded so far. */
uint64_t po_code_iter_count(const PoCodeIter* iter);

#endif /* POCO_BYTECODE_ITER_H */
