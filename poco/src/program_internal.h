/* PocoProgram construction helpers shared by the API and private image codec. */
#ifndef POCO_PROGRAM_INTERNAL_H
#define POCO_PROGRAM_INTERNAL_H

#include "activation.h"
#include "poco_hash.h"

typedef struct PocoProgramSource {
	char* name;
	char* path;
	uint8_t hash[POCO_BLAKE3_HASH_SIZE];
} PocoProgramSource;

struct PocoProgram {
	PocoVm* vm;
	void* executable;
	Poco_program_library* libraries;
	Poco_program_code code;
	char* source_name;
	char* source_path;
	PocoProgramSource* sources;
	size_t source_count;
	size_t primary_source_index;
	PocoDebugLevel debug_level;
	size_t minimal_struct_count;
	int minimal_struct_count_known;
	uint8_t source_hash[POCO_BLAKE3_HASH_SIZE];
};

PocoStatus po_program_adopt_decoded(PocoVm* vm, Poco_run_env* executable,
									PocoProgram** out_program);
int po_vm_resolve_serialized_binding(PocoVm* vm, const Poco_lib* loaded_libraries, const char* name,
									 void** out_function, const PocoBindingContract** out_contract,
									 uint32_t* out_flags);
const PocoDebugLocal* po_program_debug_local_lookup(const Func_frame* frame, const char* name,
													long bytecode_offset, short scope);

#endif /* POCO_PROGRAM_INTERNAL_H */
