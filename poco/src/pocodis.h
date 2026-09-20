/*
 * pocodis.c - bytecode disassembler and dump helpers.
 */
#ifndef POCO_POCODIS_H
#define POCO_POCODIS_H

#include "poco_internal.h"

bool po_check_instr_table(Poco_cb* pcb);
char* find_c_name(C_frame* list, void* fpt);
void* po_disasm(FILE* f, void* code, C_frame* cframes);
void dump_code(Poco_cb* pcb, FILE* file, void* code, long csize);
void po_dump_file(Poco_cb* pcb);
void po_dump_codebuf(Poco_cb* pcb, Code_buf* cbuf);

void po_disassemble_code(Poco_run_env* poco_env, FILE* file, void* code, long csize);
void po_disassemble_program(Poco_run_env* poco_env, FILE* fp);

#endif /* POCO_POCODIS_H */
