/*******************************************************************************
 * polink.h - Multi-unit link step entry points.
 * po_link_compiled_units is declared in poco_internal.h because the public
 * compile drivers call it; the two entry points the compile driver needs
 * from the link step live here.
 ******************************************************************************/

#ifndef POCO_POLINK_H
#define POCO_POLINK_H

#include "poco_internal.h"

Symbol* po_retain_global_variables(Poco_frame* frame, short* out_count);
bool po_import_used_symbols(Poco_cb* pcb);

#endif /* POCO_POLINK_H */
