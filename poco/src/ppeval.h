/*
 * ppeval.c - the limited constant expression evaluator for #if.
 */
#ifndef POCO_PPEVAL_H
#define POCO_PPEVAL_H

#include "poco_internal.h"

long po_pp_eval(Poco_cb* pcb, char* line, char* buf);

#endif /* POCO_PPEVAL_H */
