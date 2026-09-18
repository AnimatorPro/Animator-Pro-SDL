/*
 * code.c - code buffer management and bytecode emission.
 */
#ifndef POCO_CODE_H
#define POCO_CODE_H

#include "poco_internal.h"

void po_init_code_buf(Poco_cb* pcb, Code_buf* c);
void po_trash_code_buf(Poco_cb* pcb, Code_buf* c);
bool po_add_op(Poco_cb* pcb, Code_buf* cbuf, int op, void* data, SHORT data_size);
void po_no_code(Poco_cb* pcb, Code_buf* cb);
void po_backup_code(Poco_cb* pcb, Code_buf* cb, int op_size);
long po_cbuf_code_size(Code_buf* c);
bool po_concatenate_code(Poco_cb* pcb, Code_buf* dest, Code_buf* end);
bool po_copy_code(Poco_cb* pcb, Code_buf* source, Code_buf* dest);
void po_code_op(Poco_cb* pcb, Code_buf* cbuf, int op);
void po_add_code_fixup(Poco_cb* pcb, Code_buf* cbuf, int fixup);
void po_code_pop(Poco_cb* pcb, Code_buf* cbuf, int op, int pushop);
void po_code_void_pt(Poco_cb* pcb, Code_buf* cbuf, int op, void* val);
void po_code_double(Poco_cb* pcb, Code_buf* cbuf, int op, double val);
void po_code_long(Poco_cb* pcb, Code_buf* cbuf, int op, long val);
void po_code_address(Poco_cb* pcb, Code_buf* cbuf, int op, int doff, long dsize);
long po_code_int(Poco_cb* pcb, Code_buf* cbuf, int op, int val);
void po_code_popot(Poco_cb* pcb, Code_buf* cbuf, int op, void* min, void* max, void* pt);
void po_int_fixup(Code_buf* cbuf, long fixup_pos, int val);
bool po_compress_func(Poco_cb* pcb, Poco_frame* pf, Func_frame* new_frame);

#endif /* POCO_CODE_H */
