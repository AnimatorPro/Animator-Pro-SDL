/*******************************************************************************
 * use_graph.h - the '#pragma poco use' dependency graph.
 *
 * poco_vm_compile_files() builds one of these, then compiles graph.order in
 * order: every unit is compiled after the units it uses.
 ******************************************************************************/

#ifndef POCO_USE_GRAPH_H
#define POCO_USE_GRAPH_H

#include <poco/poco.h>

#include <stdbool.h>
#include <stddef.h>

typedef struct Poco_use_source {
	char* path;
	char* source_name;
	char* source;
	size_t source_length;
	size_t* uses;
	size_t use_count;
	size_t use_capacity;
	size_t output_index;
	int visit_state;
} Poco_use_source;

typedef struct Poco_use_graph {
	PocoVm* vm;
	Poco_use_source* sources;
	size_t source_count;
	size_t source_capacity;
	size_t* order;
	size_t order_count;
	size_t order_capacity;
	PocoStatus status;
} Poco_use_graph;

/* malloc'd absolute path, or NULL when the file does not exist. */
char* po_canonical_source_path(const char* path);

/* Add canonical_path and everything it uses to the graph.  False on failure,
 * with graph->status carrying the reason. */
bool po_use_graph_visit(Poco_use_graph* graph, const char* canonical_path, size_t* out_index);

void po_use_graph_free(Poco_use_graph* graph);

#endif /* POCO_USE_GRAPH_H */
