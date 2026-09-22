/*******************************************************************************
 * use_graph.c - '#pragma poco use' dependency graph.
 *
 * Resolves a used source to a canonical path, reads it, scans it for further
 * uses and records a post-order that names every unit before the unit that uses
 * it.  A cycle is reported rather than followed.
 ******************************************************************************/

#include "use_graph.h"

#include "activation.h"
#include "filepath.h"
#include "poco_hash.h"
#include "poco_internal.h"
#include "program_internal.h"
#include "pocoface.h"
#include "pp.h"
#include "vm_api.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PocoStatus poco_api_read_source_file(PocoVm* vm, const char* source_name, char** out_source,
											size_t* out_source_length)
{
	FILE* source_file;
	char* source;
	long file_length;
	size_t source_length;
	PocoStatus status;

	if (vm == NULL || source_name == NULL || out_source == NULL || out_source_length == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	*out_source = NULL;
	*out_source_length = 0;

	source_file = fopen(source_name, "rb");
	if (source_file == NULL) {
		status = POCO_STATUS_NO_FILE;
		poco_set_error(vm, "Cannot open source file '%s'", source_name);
		po_vm_report(vm, status, source_name, 0, 0, poco_get_last_error(vm));
		return status;
	}
	if (fseek(source_file, 0, SEEK_END) != 0 || (file_length = ftell(source_file)) < 0 ||
		fseek(source_file, 0, SEEK_SET) != 0) {
		fclose(source_file);
		status = POCO_STATUS_SEEK_FAILED;
		poco_set_error(vm, "Cannot determine source file size for '%s'", source_name);
		po_vm_report(vm, status, source_name, 0, 0, poco_get_last_error(vm));
		return status;
	}
	source_length = (size_t)file_length;
	if ((long)source_length != file_length) {
		fclose(source_file);
		status = POCO_STATUS_OVERFLOW;
		poco_set_error(vm, "Source file '%s' is too large", source_name);
		po_vm_report(vm, status, source_name, 0, 0, poco_get_last_error(vm));
		return status;
	}
	source = malloc(source_length > 0 ? source_length : 1);
	if (source == NULL) {
		fclose(source_file);
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	if (source_length > 0 && fread(source, 1, source_length, source_file) != source_length) {
		free(source);
		fclose(source_file);
		status = POCO_STATUS_READ_FAILED;
		poco_set_error(vm, "Cannot read source file '%s'", source_name);
		po_vm_report(vm, status, source_name, 0, 0, poco_get_last_error(vm));
		return status;
	}
	fclose(source_file);
	*out_source = source;
	*out_source_length = source_length;
	return POCO_STATUS_OK;
}

typedef struct Poco_use_scan_context {
	Poco_use_graph* graph;
	size_t source_index;
} Poco_use_scan_context;

char* po_canonical_source_path(const char* path)
{
	char resolved[PATH_SIZE];

#ifdef _WIN32
	if (_fullpath(resolved, path, sizeof(resolved)) == NULL) {
		return NULL;
	}
#else
	if (realpath(path, resolved) == NULL) {
		return NULL;
	}
#endif
	return po_copy_string(resolved);
}

static char* poco_api_resolve_used_source(Poco_use_graph* graph, const char* using_path,
										  const char* requested)
{
	char candidate[PATH_SIZE];
	char* directory;
	char* canonical;
	Names* include_dir;
	size_t requested_length = strlen(requested);
	bool absolute =
		requested[0] == '/'
#ifdef _WIN32
		|| (requested_length > 2 && isalpha((unsigned char)requested[0]) && requested[1] == ':')
#endif
		;

	if (absolute) {
		return po_canonical_source_path(requested);
	}
	directory = po_source_directory(using_path);
	if (directory == NULL) {
		graph->status = POCO_STATUS_OUT_OF_MEMORY;
		return NULL;
	}
	if (strlen(directory) + requested_length + 1 <= sizeof(candidate)) {
		snprintf(candidate, sizeof(candidate), "%s%s", directory, requested);
		canonical = po_canonical_source_path(candidate);
		if (canonical != NULL) {
			free(directory);
			return canonical;
		}
	}
	free(directory);
	for (include_dir = graph->vm->include_dirs; include_dir != NULL;
		 include_dir = include_dir->next) {
		size_t directory_length = strlen(include_dir->name);
		/* Accept a directory written with or without its trailing separator,
		 * matching the #include search. */
		const char* separator = directory_length == 0 ||
										include_dir->name[directory_length - 1] == '/' ||
										include_dir->name[directory_length - 1] == '\\'
									? ""
									: "/";

		if (directory_length + strlen(separator) + requested_length + 1 > sizeof(candidate)) {
			continue;
		}
		snprintf(candidate, sizeof(candidate), "%s%s%s", include_dir->name, separator, requested);
		canonical = po_canonical_source_path(candidate);
		if (canonical != NULL) {
			return canonical;
		}
	}
	return NULL;
}

static size_t poco_api_use_graph_find(const Poco_use_graph* graph, const char* path)
{
	size_t index;

	for (index = 0; index < graph->source_count; ++index) {
		if (strcmp(graph->sources[index].path, path) == 0) {
			return index;
		}
	}
	return SIZE_MAX;
}

static bool poco_api_use_graph_append(size_t** values, size_t* count, size_t* capacity,
									  size_t value)
{
	size_t* grown;

	if (*count == *capacity) {
		size_t new_capacity = *capacity == 0 ? 4 : *capacity * 2;
		grown = realloc(*values, new_capacity * sizeof(*grown));
		if (grown == NULL) {
			return false;
		}
		*values = grown;
		*capacity = new_capacity;
	}
	(*values)[(*count)++] = value;
	return true;
}

static bool poco_api_collect_use(void* opaque, const char* requested, size_t line_number)
{
	Poco_use_scan_context* context = opaque;
	Poco_use_graph* graph = context->graph;
	const char* using_path = graph->sources[context->source_index].path;
	char* canonical = poco_api_resolve_used_source(graph, using_path, requested);
	size_t used_index;
	size_t index;

	if (canonical == NULL) {
		if (graph->status == POCO_STATUS_OK) {
			graph->status = POCO_STATUS_REPORTED;
			poco_set_error(graph->vm, "Cannot resolve used source '%s' from %s:%zu", requested,
						   using_path, line_number);
		}
		return false;
	}
	if (!po_use_graph_visit(graph, canonical, &used_index)) {
		free(canonical);
		return false;
	}
	free(canonical);
	for (index = 0; index < graph->sources[context->source_index].use_count; ++index) {
		if (graph->sources[context->source_index].uses[index] == used_index) {
			return true;
		}
	}
	if (!poco_api_use_graph_append(&graph->sources[context->source_index].uses,
								   &graph->sources[context->source_index].use_count,
								   &graph->sources[context->source_index].use_capacity,
								   used_index)) {
		graph->status = POCO_STATUS_OUT_OF_MEMORY;
		return false;
	}
	return true;
}

bool po_use_graph_visit(Poco_use_graph* graph, const char* canonical_path, size_t* out_index)
{
	size_t index = poco_api_use_graph_find(graph, canonical_path);
	Poco_use_source* source;
	Poco_use_scan_context scan_context;
	char scan_error[256] = "";

	if (index != SIZE_MAX) {
		if (graph->sources[index].visit_state == 1) {
			graph->status = POCO_STATUS_REPORTED;
			poco_set_error(graph->vm, "#pragma poco use cycle detected at '%s'", canonical_path);
			return false;
		}
		*out_index = index;
		return true;
	}
	if (graph->source_count == graph->source_capacity) {
		size_t new_capacity = graph->source_capacity == 0 ? 8 : graph->source_capacity * 2;
		Poco_use_source* grown = realloc(graph->sources, new_capacity * sizeof(*graph->sources));
		if (grown == NULL) {
			graph->status = POCO_STATUS_OUT_OF_MEMORY;
			return false;
		}
		memset(grown + graph->source_capacity, 0,
			   (new_capacity - graph->source_capacity) * sizeof(*grown));
		graph->sources = grown;
		graph->source_capacity = new_capacity;
	}
	index = graph->source_count++;
	source = &graph->sources[index];
	source->path = po_copy_string(canonical_path);
	source->source_name = po_copy_string(canonical_path);
	if (source->path == NULL || source->source_name == NULL) {
		graph->status = POCO_STATUS_OUT_OF_MEMORY;
		return false;
	}
	graph->status = poco_api_read_source_file(graph->vm, canonical_path, &source->source,
											  &source->source_length);
	if (graph->status != POCO_STATUS_OK) {
		return false;
	}
	source->visit_state = 1;
	scan_context.graph = graph;
	scan_context.source_index = index;
	if (!po_pp_scan_uses(source->source, source->source_length, poco_api_collect_use, &scan_context,
						 scan_error, sizeof(scan_error))) {
		if (graph->status == POCO_STATUS_OK) {
			graph->status = POCO_STATUS_REPORTED;
			poco_set_error(graph->vm, "%s in %s", scan_error, canonical_path);
		}
		return false;
	}
	/* Recursive discovery may have grown the node array. */
	source = &graph->sources[index];
	source->visit_state = 2;
	if (!poco_api_use_graph_append(&graph->order, &graph->order_count, &graph->order_capacity,
								   index)) {
		graph->status = POCO_STATUS_OUT_OF_MEMORY;
		return false;
	}
	*out_index = index;
	return true;
}

void po_use_graph_free(Poco_use_graph* graph)
{
	size_t index;

	for (index = 0; index < graph->source_count; ++index) {
		free(graph->sources[index].uses);
		free(graph->sources[index].source);
		free(graph->sources[index].source_name);
		free(graph->sources[index].path);
	}
	free(graph->sources);
	free(graph->order);
}

PocoStatus poco_vm_compile_files(PocoVm* vm, const char* const* source_names, size_t source_count,
								 PocoProgram** out_program)
{
	Poco_use_graph graph = {0};
	const char** expanded_names = NULL;
	const char** physical_source_paths = NULL;
	const char** sources = NULL;
	size_t* source_lengths = NULL;
	Names* source_directory_entries = NULL;
	Names** include_dirs = NULL;
	size_t** use_indices = NULL;
	size_t* use_counts = NULL;
	size_t expanded_count = 0;
	size_t primary_node_index = SIZE_MAX;
	size_t source_index;
	PocoStatus status = POCO_STATUS_OK;

	if (vm == NULL || source_names == NULL || out_program == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	*out_program = NULL;
	if (vm->destroy_requested || source_count == 0) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	graph.vm = vm;
	graph.status = POCO_STATUS_OK;
	for (source_index = 0; source_index < source_count; ++source_index) {
		char* canonical;
		size_t node_index;

		if (source_names[source_index] == NULL) {
			status = POCO_STATUS_NULL_REFERENCE;
			goto OUT;
		}
		canonical = po_canonical_source_path(source_names[source_index]);
		if (canonical == NULL) {
			status = POCO_STATUS_NO_FILE;
			poco_set_error(vm, "Cannot open source file '%s'", source_names[source_index]);
			goto OUT;
		}
		if (!po_use_graph_visit(&graph, canonical, &node_index)) {
			free(canonical);
			status = graph.status;
			goto OUT;
		}
		if (source_index == 0) {
			primary_node_index = node_index;
		}
		if (strcmp(graph.sources[node_index].source_name, source_names[source_index]) != 0) {
			char* explicit_name = po_copy_string(source_names[source_index]);
			if (explicit_name == NULL) {
				free(canonical);
				status = POCO_STATUS_OUT_OF_MEMORY;
				goto OUT;
			}
			free(graph.sources[node_index].source_name);
			graph.sources[node_index].source_name = explicit_name;
		}
		free(canonical);
	}
	expanded_count = graph.order_count;
	expanded_names = calloc(expanded_count, sizeof(*expanded_names));
	physical_source_paths = calloc(expanded_count, sizeof(*physical_source_paths));
	sources = calloc(expanded_count, sizeof(*sources));
	source_lengths = calloc(expanded_count, sizeof(*source_lengths));
	source_directory_entries = calloc(expanded_count, sizeof(*source_directory_entries));
	include_dirs = calloc(expanded_count, sizeof(*include_dirs));
	use_indices = calloc(expanded_count, sizeof(*use_indices));
	use_counts = calloc(expanded_count, sizeof(*use_counts));
	if (expanded_names == NULL || physical_source_paths == NULL || sources == NULL ||
		source_lengths == NULL || source_directory_entries == NULL || include_dirs == NULL ||
		use_indices == NULL || use_counts == NULL) {
		status = POCO_STATUS_OUT_OF_MEMORY;
		goto OUT;
	}

	for (source_index = 0; source_index < expanded_count; ++source_index) {
		graph.sources[graph.order[source_index]].output_index = source_index;
	}
	for (source_index = 0; source_index < expanded_count; ++source_index) {
		Poco_use_source* graph_source = &graph.sources[graph.order[source_index]];
		char* source_directory;
		size_t use_index;

		expanded_names[source_index] = graph_source->source_name;
		physical_source_paths[source_index] = graph_source->path;
		sources[source_index] = graph_source->source;
		source_lengths[source_index] = graph_source->source_length;
		use_counts[source_index] = graph_source->use_count;
		if (graph_source->use_count != 0) {
			use_indices[source_index] = malloc(graph_source->use_count * sizeof(size_t));
			if (use_indices[source_index] == NULL) {
				status = POCO_STATUS_OUT_OF_MEMORY;
				goto OUT;
			}
			for (use_index = 0; use_index < graph_source->use_count; ++use_index) {
				use_indices[source_index][use_index] =
					graph.sources[graph_source->uses[use_index]].output_index;
			}
		}

		/* Each file gets its own leading include directory; no unit inherits the
		 * filesystem scope of a neighbor in the ordered compile. */
		source_directory = po_source_directory(graph_source->source_name);
		if (source_directory == NULL) {
			status = POCO_STATUS_OUT_OF_MEMORY;
			goto OUT;
		}
		source_directory_entries[source_index].next = vm->include_dirs;
		source_directory_entries[source_index].name = source_directory;
		include_dirs[source_index] = &source_directory_entries[source_index];
	}
	status = po_vm_compile_sources(vm, expanded_names, physical_source_paths, sources,
								   source_lengths, include_dirs, (const size_t* const*)use_indices,
								   use_counts, expanded_count, out_program);
	if (status == POCO_STATUS_OK && primary_node_index != SIZE_MAX &&
		graph.sources[primary_node_index].output_index != 0) {
		const char* primary_name = graph.sources[primary_node_index].source_name;
		const char* separator = strrchr(primary_name, '/');
		const char* alternate_separator = strrchr(primary_name, '\\');
		const char* basename;
		char* stored_name;
		char* stored_path;

		if (alternate_separator != NULL && (separator == NULL || alternate_separator > separator)) {
			separator = alternate_separator;
		}
		basename = separator != NULL && separator[1] != '\0' ? separator + 1 : primary_name;
		stored_name = po_copy_string(basename);
		stored_path = po_copy_string(primary_name);
		if (stored_name == NULL || stored_path == NULL) {
			free(stored_name);
			free(stored_path);
			poco_program_destroy(*out_program);
			*out_program = NULL;
			status = POCO_STATUS_OUT_OF_MEMORY;
			goto OUT;
		}
		free((*out_program)->source_name);
		free((*out_program)->source_path);
		(*out_program)->source_name = stored_name;
		(*out_program)->source_path = stored_path;
		(*out_program)->primary_source_index = graph.sources[primary_node_index].output_index;
		po_blake3_hash(graph.sources[primary_node_index].source,
					   graph.sources[primary_node_index].source_length,
					   (*out_program)->source_hash);
	}

OUT:
	for (source_index = 0; source_index < expanded_count; ++source_index) {
		free(source_directory_entries != NULL ? source_directory_entries[source_index].name : NULL);
		free(use_indices != NULL ? use_indices[source_index] : NULL);
	}
	free(use_counts);
	free(use_indices);
	free(include_dirs);
	free(source_directory_entries);
	free(source_lengths);
	free(sources);
	free(physical_source_paths);
	free(expanded_names);
	po_use_graph_free(&graph);
	return status;
}

PocoStatus poco_vm_compile_file(PocoVm* vm, const char* source_name, PocoProgram** out_program)
{
	const char* source_names[] = {source_name};

	if (source_name == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	return poco_vm_compile_files(vm, source_names, 1, out_program);
}
