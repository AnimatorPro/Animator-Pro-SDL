#ifndef POCO_DEBUG_INTERNAL_H
#define POCO_DEBUG_INTERNAL_H

#include "activation.h"

void po_debug_instruction_hook(PocoActivation* activation, Code* instruction, void* stack_area,
							   void* frame_base);
void po_debug_activation_reset(PocoActivation* activation);
void po_debug_activation_destroy(PocoActivation* activation);

#endif /* POCO_DEBUG_INTERNAL_H */
