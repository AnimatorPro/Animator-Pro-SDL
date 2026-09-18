/*******************************************************************************
 * polink.c - Multi-unit linker.
 * Resolves cross-unit function and global references after every
 * translation unit has been compiled: duplicate and missing definition
 * diagnostics, operand patching, and the '#pragma use' symbol import that
 * makes another unit's externals visible.  Split out of poco.c.
 ******************************************************************************/

#include "poco_internal.h"
#include "pocmemry.h"
#include "pocoop.h"
#include "pocotype.h"
#include "polink.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is_global_offset_op(int op)
{
	return (op >= OP_GLO_CVAR && op <= OP_GLO_DVAR) || (op >= OP_GLO_CASS && op <= OP_GLO_DASS) ||
		   op == OP_GLO_ADDRESS
#ifdef STRING_EXPERIMENT
		   || op == OP_GLO_STRING_VAR || op == OP_GLO_STRING_ASS
#endif
		;
}

static Func_frame* find_linked_function(Poco_cb* pcb, const Func_frame* reference)
{
	Func_frame* frame;

	for (frame = pcb->run.fff; frame != NULL; frame = frame->next) {
		if (!frame->got_code || frame->type != CFF_POCO ||
			strcmp(frame->name, reference->name) != 0) {
			continue;
		}
		if (reference->is_static) {
			if (frame->unit_index == reference->unit_index) {
				return frame;
			}
		} else if (!frame->is_static) {
			return frame;
		}
	}
	return NULL;
}

static Symbol* find_linked_global(Poco_cb* pcb, const Symbol* reference)
{
	Func_frame* frame;
	Symbol* symbol;

	for (frame = pcb->run.fff; frame != NULL; frame = frame->next) {
		if (frame->got_code) {
			continue;
		}
		for (symbol = frame->parameters; symbol != NULL; symbol = symbol->link) {
			if ((symbol->ti->flags & (TFL_EXTERN | TFL_STATIC)) == 0 &&
				strcmp(symbol->name, reference->name) == 0) {
				return symbol;
			}
		}
	}
	return NULL;
}

static bool patch_link_references(Poco_cb* pcb)
{
	Func_frame* frame;

	for (frame = pcb->run.fff; frame != NULL; frame = frame->next) {
		UBYTE* cursor = frame->code_pt;
		UBYTE* end = cursor + frame->code_size;

		while (cursor < end) {
			int op;
			const Poco_op_table* entry;
			UBYTE* operand;

			if ((size_t)(end - cursor) < sizeof(op)) {
				po_say_internal(pcb, "truncated instruction while linking %s", frame->name);
				return false;
			}
			memcpy(&op, cursor, sizeof(op));
			cursor += sizeof(op);
			if (op < 0 || op >= po_ins_table_els) {
				po_say_internal(pcb, "invalid opcode while linking %s", frame->name);
				return false;
			}
			entry = &po_ins_table[op];
			if ((size_t)(end - cursor) < (size_t)entry->op_size) {
				po_say_internal(pcb, "truncated operand while linking %s", frame->name);
				return false;
			}
			operand = cursor;
			if (entry->op_ext == OEX_FUNCTION) {
				Func_frame* reference;
				Func_frame* definition;

				memcpy(&reference, operand, sizeof(reference));
				if (reference != NULL && reference->type == CFF_POCO) {
					definition = find_linked_function(pcb, reference);
					if (definition == NULL) {
						po_say_fatal(
							pcb,
							"function '%s' referenced by %s has no definition with visible linkage",
							reference->name, frame->unit_name);
						return false;
					}
					if (!po_types_same(reference->return_type, definition->return_type, 0) ||
						!po_fuf_types_same(reference, definition)) {
						po_say_fatal(pcb, "type mismatch linking function '%s' between %s and %s",
									 reference->name, reference->unit_name, definition->unit_name);
						return false;
					}
					memcpy(operand, &definition, sizeof(definition));
				}
			} else if (is_global_offset_op(op)) {
				Func_frame* globals_frame;

				for (globals_frame = pcb->run.fff; globals_frame != NULL;
					 globals_frame = globals_frame->next) {
					Symbol* reference;

					if (globals_frame->got_code) {
						continue;
					}
					for (reference = globals_frame->parameters; reference != NULL;
						 reference = reference->link) {
						int offset;
						Symbol* definition;

						if ((reference->ti->flags & TFL_EXTERN) == 0) {
							continue;
						}
						memcpy(&offset, operand, sizeof(offset));
						if (offset != reference->symval.doff) {
							continue;
						}
						definition = find_linked_global(pcb, reference);
						if (definition == NULL) {
							po_say_fatal(pcb, "global '%s' referenced by %s has no definition",
										 reference->name, frame->unit_name);
							return false;
						}
						if (!po_types_same(reference->ti, definition->ti, 0)) {
							po_say_fatal(pcb, "type mismatch linking global '%s' between %s and %s",
										 reference->name, reference->unit_name,
										 definition->unit_name);
							return false;
						}
						offset = definition->symval.doff;
						memcpy(operand, &offset, sizeof(offset));
						goto patched_global;
					}
				}
			}
patched_global:
			cursor += entry->op_size;
		}
	}
	return true;
}

bool po_link_compiled_units(Poco_cb* pcb)
{
	Func_frame* frame;
	Func_frame* other;
	Symbol* symbol;
	Symbol* other_symbol;
	size_t main_count = 0;
	char main_units[384] = "";

	for (frame = pcb->run.fff; frame != NULL; frame = frame->next) {
		if (frame->got_code && frame->type == CFF_POCO && strcmp(frame->name, "main") == 0) {
			size_t used = strlen(main_units);

			snprintf(main_units + used, sizeof(main_units) - used, "%s%s",
					 main_count == 0 ? "" : ", ", frame->unit_name);
			++main_count;
		}
	}
	if (main_count > 1) {
		po_say_fatal(pcb, "multiple main() definitions in: %s", main_units);
		return false;
	}

	for (frame = pcb->run.fff; frame != NULL; frame = frame->next) {
		if (!frame->got_code || frame->type != CFF_POCO || frame->is_static) {
			continue;
		}
		for (other = frame->next; other != NULL; other = other->next) {
			if (other->got_code && other->type == CFF_POCO && !other->is_static &&
				strcmp(frame->name, other->name) == 0) {
				po_say_fatal(pcb, "duplicate external function '%s' defined in %s and %s",
							 frame->name, frame->unit_name, other->unit_name);
				return false;
			}
		}
		for (other = pcb->run.fff; other != NULL; other = other->next) {
			if (other->got_code) {
				continue;
			}
			for (symbol = other->parameters; symbol != NULL; symbol = symbol->link) {
				if ((symbol->ti->flags & (TFL_EXTERN | TFL_STATIC)) == 0 &&
					strcmp(frame->name, symbol->name) == 0) {
					po_say_fatal(
						pcb,
						"duplicate external symbol '%s' defined as function in %s and global in %s",
						frame->name, frame->unit_name, symbol->unit_name);
					return false;
				}
			}
		}
	}

	for (frame = pcb->run.fff; frame != NULL; frame = frame->next) {
		if (frame->got_code) {
			continue;
		}
		for (symbol = frame->parameters; symbol != NULL; symbol = symbol->link) {
			if ((symbol->ti->flags & (TFL_EXTERN | TFL_STATIC)) != 0) {
				continue;
			}
			for (other = frame->next; other != NULL; other = other->next) {
				if (other->got_code) {
					continue;
				}
				for (other_symbol = other->parameters; other_symbol != NULL;
					 other_symbol = other_symbol->link) {
					if ((other_symbol->ti->flags & (TFL_EXTERN | TFL_STATIC)) == 0 &&
						strcmp(symbol->name, other_symbol->name) == 0) {
						po_say_fatal(pcb, "duplicate external global '%s' defined in %s and %s",
									 symbol->name, symbol->unit_name, other_symbol->unit_name);
						return false;
					}
				}
			}
		}
	}
	return patch_link_references(pcb);
}

/*****************************************************************************
 * Detach root-scope variables from the transient compiler frame so the
 * immutable program retains the symbol metadata needed by embedding hosts.
 * The global initializer Func_frame owns this list after compression.
 ****************************************************************************/
Symbol* po_retain_global_variables(Poco_frame* frame, short* out_count)
{
	Symbol* retained = NULL;
	Symbol** retained_tail = &retained;
	Symbol** cursor = &frame->symbols;
	short count = 0;

	while (*cursor != NULL) {
		Symbol* symbol = *cursor;

		if (symbol->tok_type != PTOK_VAR || symbol->storage_scope != SCOPE_GLOBAL ||
			po_is_func(symbol->ti)) {
			cursor = &symbol->link;
			continue;
		}
		*cursor = symbol->link;
		symbol->link = NULL;
		*retained_tail = symbol;
		retained_tail = &symbol->link;
		++count;
	}
	*out_count = count;
	return retained;
}

static bool unit_is_directly_used(const Poco_cb* pcb, size_t unit_index)
{
	size_t index;

	for (index = 0; index < pcb->current_use_count; ++index) {
		if (pcb->current_use_indices[index] == unit_index) {
			return true;
		}
	}
	return false;
}

static Type_info* clone_import_type(Poco_cb* pcb, Type_info* source)
{
	Itypi storage;
	Type_info* temporary = po_typi_type(&storage);

	if (!po_copy_type(pcb, source, temporary)) {
		return NULL;
	}
	temporary->flags = source->flags;
	return po_new_type_info(pcb, temporary, 0);
}

static Symbol* clone_import_parameter(Poco_cb* pcb, const Symbol* source)
{
	Symbol* clone = po_memzalloc(pcb, sizeof(*clone) + strlen(source->name) + 1);

	clone->name = (char*)(clone + 1);
	strcpy(clone->name, source->name);
	clone->unit_name = pcb->current_unit_name;
	clone->unit_index = pcb->current_unit_index;
	clone->tok_type = source->tok_type;
	clone->scope = source->scope;
	clone->storage_scope = source->storage_scope;
	clone->flags = source->flags;
	clone->ti = clone_import_type(pcb, source->ti);
	return clone;
}

static Func_frame* make_import_function(Poco_cb* pcb, const Func_frame* definition)
{
	Func_frame* reference = po_memzalloc(pcb, sizeof(*reference));
	const Symbol* parameter;
	Symbol** tail = &reference->parameters;

	reference->name = po_clone_string(pcb, definition->name);
	reference->pcount = definition->pcount;
	reference->type = CFF_POCO;
	reference->unit_name = pcb->current_unit_name;
	reference->unit_index = pcb->current_unit_index;
	reference->return_type = clone_import_type(pcb, definition->return_type);
	for (parameter = definition->parameters; parameter != NULL; parameter = parameter->link) {
		*tail = clone_import_parameter(pcb, parameter);
		tail = &(*tail)->link;
	}
	reference->mlink = pcb->run.protos;
	pcb->run.protos = reference;
	return reference;
}

bool po_import_used_symbols(Poco_cb* pcb)
{
	Func_frame* frame;

	for (frame = pcb->run.fff; frame != NULL; frame = frame->next) {
		if (!unit_is_directly_used(pcb, frame->unit_index)) {
			continue;
		}
		if (frame->got_code) {
			Func_frame* reference;
			Symbol* symbol;
			Itypi storage;
			Type_info* function_type;

			if (frame->type != CFF_POCO || frame->is_static) {
				continue;
			}
			reference = make_import_function(pcb, frame);
			function_type = po_typi_type(&storage);
			if (!po_copy_type(pcb, reference->return_type, function_type) ||
				!po_append_type(pcb, function_type, TYPE_FUNCTION, 0, reference)) {
				return false;
			}
			symbol = po_new_symbol(pcb, frame->name);
			symbol->tok_type = PTOK_VAR;
			symbol->storage_scope = SCOPE_GLOBAL;
			symbol->ti = po_new_type_info(pcb, function_type, 0);
			continue;
		}
		{
			const Symbol* definition;

			for (definition = frame->parameters; definition != NULL;
				 definition = definition->link) {
				Symbol* symbol;

				if (definition->tok_type != PTOK_VAR ||
					(definition->ti->flags & (TFL_EXTERN | TFL_STATIC)) != 0) {
					continue;
				}
				symbol = po_new_symbol(pcb, definition->name);
				symbol->tok_type = PTOK_VAR;
				symbol->storage_scope = SCOPE_GLOBAL;
				symbol->symval = definition->symval;
				symbol->ti = clone_import_type(pcb, definition->ti);
				symbol->ti->flags |= TFL_EXTERN;
			}
		}
	}
	return true;
}
