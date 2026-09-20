/*******************************************************************************
 * bytecode_iter.c - The single loop step that advances a bytecode cursor.
 ******************************************************************************/

#include "bytecode_iter.h"

#include <string.h>

void po_code_iter_init(PoCodeIter* iter, void* code, long size)
{
	iter->start = (uint8_t*)code;
	iter->cursor = (uint8_t*)code;
	iter->end = (uint8_t*)code + (size > 0 ? (size_t)size : 0);
	iter->bounded = 1;
	iter->index = 0;
}

void po_code_iter_init_unbounded(PoCodeIter* iter, void* code)
{
	iter->start = (uint8_t*)code;
	iter->cursor = (uint8_t*)code;
	iter->end = NULL;
	iter->bounded = 0;
	iter->index = 0;
}

size_t po_code_iter_offset(const PoCodeIter* iter)
{
	return (size_t)(iter->cursor - iter->start);
}

void* po_code_iter_cursor(const PoCodeIter* iter)
{
	return iter->cursor;
}

uint64_t po_code_iter_count(const PoCodeIter* iter)
{
	return iter->index;
}

PoCodeIterStatus po_code_iter_next(PoCodeIter* iter, PoCodeIns* out_ins)
{
	int op;

	out_ins->op = 0;
	out_ins->entry = NULL;
	out_ins->operand = NULL;
	out_ins->offset = po_code_iter_offset(iter);
	out_ins->index = iter->index;

	if (iter->bounded && iter->cursor >= iter->end) {
		return PO_CODE_ITER_END;
	}
	if (iter->bounded && (size_t)(iter->end - iter->cursor) < sizeof(op)) {
		iter->cursor = iter->end;
		return PO_CODE_ITER_TRUNCATED_OP;
	}
	memcpy(&op, iter->cursor, sizeof(op));
	iter->cursor += sizeof(op);
	/* Report the word even when it is out of range: it is the whole content of
	 * the disassembler's wild-opcode diagnostic. */
	out_ins->op = op;
	if (op < 0 || op >= po_ins_table_els) {
		return PO_CODE_ITER_BAD_OPCODE;
	}
	out_ins->entry = &po_ins_table[op];
	if (iter->bounded && (size_t)(iter->end - iter->cursor) < (size_t)po_ins_table[op].op_size) {
		return PO_CODE_ITER_TRUNCATED_OPERAND;
	}
	out_ins->operand = iter->cursor;
	iter->cursor += po_ins_table[op].op_size;
	++iter->index;
	return PO_CODE_ITER_OK;
}
