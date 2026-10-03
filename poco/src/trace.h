/*
 * trace.c - line-number tables and the run-time function call traceback.
 */
#ifndef POCO_TRACE_H
#define POCO_TRACE_H

#include <poco/poco.h>

#include "poco_internal.h"

Line_data* po_new_line_data(Poco_cb* pcb);
bool po_compress_line_data(Poco_cb* pcb, Line_data* ld);
void po_free_line_data(Line_data* ld);
bool po_add_line_data(Poco_cb* pcb, Line_data* ld, long offset, long line);
void po_print_trace(struct PocoActivation* activation, FILE* tfile, Pt_num* stack, Pt_num* base,
					Pt_num* globals, Pt_num* ip, Errcode cerr);

/* Forget the last recorded fault; every public entry point does this first. */
void po_clear_fault(struct PocoActivation* activation);

/* Remember where a run-time error happened unless a fault is already recorded.
 * cerr is the library routine's error when one detected it, Success otherwise. */
void po_record_fault(struct PocoActivation* activation, Pt_num* ip, Errcode err, Errcode cerr,
					 bool outside_vm_region);

/*
 * Report a failed run through the VM's diagnostic callback.  A recorded fault
 * is reported as its specific message ("Attempt to divide by zero (in f)") at
 * the line of the function's own source unit, named from the program's source
 * table; without one the generic message and line are reported unchanged.
 */
void po_report_run_failure(struct PocoActivation* activation, PocoStatus status, long line,
						   const char* generic_message);

#endif /* POCO_TRACE_H */
