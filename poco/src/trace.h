/*
 * trace.c - line-number tables and the run-time function call traceback.
 */
#ifndef POCO_TRACE_H
#define POCO_TRACE_H

#include "poco_internal.h"

Line_data* po_new_line_data(Poco_cb* pcb);
bool po_compress_line_data(Poco_cb* pcb, Line_data* ld);
void po_free_line_data(Line_data* ld);
bool po_add_line_data(Poco_cb* pcb, Line_data* ld, long offset, long line);
void po_print_trace(struct PocoActivation* activation, FILE* tfile, Pt_num* stack, Pt_num* base,
					Pt_num* globals, Pt_num* ip, Errcode cerr);

#endif /* POCO_TRACE_H */
