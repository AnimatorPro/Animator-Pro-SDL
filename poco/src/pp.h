/*
 * pp.c - the preprocessor: #include/#define/#if handling and macro expansion.
 */
#ifndef POCO_PP_H
#define POCO_PP_H

#include "poco_internal.h"

void po_free_pp(Poco_cb* pcb);
bool po_init_pp(Poco_cb* pcb, char* filename);
bool po_init_pp_buffer(Poco_cb* pcb, char* source_name, const char* source, size_t source_length);
char* po_pp_next_line(Poco_cb* pcb);
void pp_say_fatal(Poco_cb* pcb, const char* fmt, ...);

typedef bool (*Poco_use_visitor)(void* context, const char* path, size_t line_number);
bool po_pp_scan_uses(const char* source, size_t source_length, Poco_use_visitor visitor,
					 void* context, char* error, size_t error_capacity);

#endif /* POCO_PP_H */
