/*
 * runops.c - the bytecode interpreter.
 */
#ifndef POCO_RUNOPS_H
#define POCO_RUNOPS_H

#include "poco_internal.h"

Errcode po_run_ops(struct PocoActivation* activation, Code* code_pt, Pt_num* pret);
Errcode po_run_ops_values(struct PocoActivation* activation, Code* code_pt, Pt_num* pret,
						  const PocoCallbackValue* values, size_t value_count);

#endif /* POCO_RUNOPS_H */
