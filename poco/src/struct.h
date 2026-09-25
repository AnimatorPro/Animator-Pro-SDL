/*
 * struct.c - struct, union and enum declaration parsing.
 */
#ifndef POCO_STRUCT_H
#define POCO_STRUCT_H

#include "poco_internal.h"

void po_free_sif_list(Struct_info** psif);
void po_move_sifs_to_parent(Poco_cb* pcb);
Struct_info* po_get_struct(Poco_cb* pcb, Poco_frame* pf, SHORT ttype);
void po_check_struct_agreement(Poco_cb* pcb, Struct_info* unit_sifs, Struct_info* earlier_sifs);

#endif /* POCO_STRUCT_H */
