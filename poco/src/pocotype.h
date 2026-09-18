/*
 * pocotype.c - the compiler's type model: construction, comparison, sizing.
 */
#ifndef POCO_POCOTYPE_H
#define POCO_POCOTYPE_H

#include "poco_internal.h"

bool po_check_type_names(Poco_cb* pcb);
Type_info* po_new_type_info(Poco_cb* pcb, Type_info* old, int extras);
bool po_is_num_ido(SHORT ido);
bool po_is_int_ido(SHORT ido);
bool po_is_pointer(Type_info* ti);
bool po_is_array(Type_info* ti);
bool po_is_struct(Type_info* ti);
bool po_is_func(Type_info* ti);
void po_set_ido_type(Type_info* ti);
bool po_append_type(Poco_cb* pcb, Type_info* ti, TypeComp tc, long dim, void* sif);
bool po_set_base_type(Poco_cb* pcb, Type_info* ti, TypeComp tc, long dim, Struct_info* sif);
bool po_copy_type(Poco_cb* pcb, Type_info* s, Type_info* d);
bool po_cat_type(Poco_cb* pcb, Type_info* d, Type_info* s);
bool po_is_void_ptr(Type_info* ti);
bool po_ptypes_same(Type_info* st, Type_info* dt);
bool po_fuf_types_same(Func_frame* sf, Func_frame* df);
bool po_types_same(Type_info* s, Type_info* d, int start);
void po_print_type(Poco_cb* pcb, FILE* f, Type_info* ti);
long po_get_type_size(Type_info* ti);
long po_get_subtype_size(Poco_cb* pcb, Type_info* ti);
bool po_get_base_type(Poco_cb* pcb, Poco_frame* pf, Type_info* ti);
Symbol* po_need_local_symbol(Poco_cb* pcb);
Type_info* po_typi_type(Itypi* tip);

#endif /* POCO_POCOTYPE_H */
