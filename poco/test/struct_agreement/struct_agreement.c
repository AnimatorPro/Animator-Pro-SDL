/*
 * struct_agreement - cross-unit struct/union/enum tag agreement.
 *
 * Poco compiles every unit of a program in one process and keeps each unit's
 * layouts in the program's struct table, so it can see something a C
 * toolchain never does: one tag given two different layouts by two units.
 * These cases pin down which disagreements are diagnosed, which spellings are
 * still legal, and that a legal duplicate keeps its own table entry.
 */

#include <poco/poco.h>

#include "poco_internal.h"
#include "program_internal.h"

#include <stdio.h>
#include <string.h>

#ifndef POCO_STRUCT_AGREEMENT_DIR
#error "POCO_STRUCT_AGREEMENT_DIR must name the struct-agreement fixture directory"
#endif

static char path_storage[8][1024];

static int check(int condition, const char* message)
{
	if (!condition) {
		fprintf(stderr, "struct agreement: %s\n", message);
		return 0;
	}
	return 1;
}

static const char* fixture(size_t slot, const char* name)
{
	snprintf(path_storage[slot], sizeof(path_storage[slot]), "%s%s", POCO_STRUCT_AGREEMENT_DIR,
			 name);
	return path_storage[slot];
}

/* How many entries in the compiled program's struct table carry this tag.
 * A legal duplicate must keep a separate entry: the table index is a struct's
 * identity, so merging two matching entries would renumber the table. */
static int count_tag_entries(const PocoProgram* program, const char* tag)
{
	const Poco_run_env* executable = program->executable;
	const Struct_info* entry;
	int count = 0;

	for (entry = executable->struct_infos; entry != NULL; entry = entry->next) {
		if (entry->name != NULL && strcmp(entry->name, tag) == 0) {
			++count;
		}
	}
	return count;
}

static int compiles(PocoVm* vm, const char* const* names, size_t count, const char* tag,
					int expected_entries, const char* message)
{
	PocoProgram* program = NULL;
	PocoStatus status = poco_vm_compile_files(vm, names, count, &program);
	int ok;

	if (status != POCO_STATUS_OK) {
		fprintf(stderr, "struct agreement: %s: %s\n", message, poco_get_last_error(vm));
	}
	ok = check(status == POCO_STATUS_OK && program != NULL, message);
	if (ok && tag != NULL) {
		ok &= check(count_tag_entries(program, tag) == expected_entries,
					"a legal duplicate tag keeps one struct table entry per unit");
	}
	poco_program_destroy(program);
	return ok;
}

static int rejected(PocoVm* vm, const char* const* names, size_t count, const char* message)
{
	PocoProgram* program = NULL;
	PocoStatus status = poco_vm_compile_files(vm, names, count, &program);
	const char* error = poco_get_last_error(vm);
	int ok = check(status == POCO_STATUS_REPORTED && program == NULL, message);

	/* The diagnostic has to name the tag and both units; "struct vec
	 * disagrees somewhere" would send the reader hunting. */
	ok &= check(error != NULL && strstr(error, "vec") != NULL,
				"the disagreement diagnostic names the tag");
	ok &= check(error != NULL && strstr(error, names[0]) != NULL &&
					strstr(error, names[1]) != NULL,
				"the disagreement diagnostic names both units");
	poco_program_destroy(program);
	return ok;
}

int main(void)
{
	PocoVmOptions options = {0};
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	int ok = 1;

	ok &= check(poco_vm_create(&options, &vm) == POCO_STATUS_OK, "create VM");
	ok &= check(poco_vm_register_standard_library(vm) == POCO_STATUS_OK,
				"register standard native prototypes");
	if (!ok) {
		poco_vm_destroy(vm);
		return 1;
	}

	{
		const char* names[2];

		names[0] = fixture(0, "differ_member0.poc");
		names[1] = fixture(1, "differ_member1.poc");
		ok &= rejected(vm, names, 2, "reject one tag given two member types");

		names[0] = fixture(0, "differ_count0.poc");
		names[1] = fixture(1, "differ_count1.poc");
		ok &= rejected(vm, names, 2, "reject one tag given two member counts");

		names[0] = fixture(0, "differ_kind0.poc");
		names[1] = fixture(1, "differ_kind1.poc");
		ok &= rejected(vm, names, 2, "reject one tag used as a struct and as a union");
	}

	{
		const char* names[3];

		/* The shared-header case: identical bodies in two units are the normal
		 * way a multi-file program uses a struct, and stay legal. */
		names[0] = fixture(0, "same0.poc");
		names[1] = fixture(1, "same1.poc");
		ok &= compiles(vm, names, 2, "vec", 2, "accept one tag defined identically in two units");

		/* Member names carry no layout, so renaming a field across units is
		 * not a disagreement. */
		names[0] = fixture(0, "member_names0.poc");
		names[1] = fixture(1, "member_names1.poc");
		ok &= compiles(vm, names, 2, "vec", 2, "accept members that differ only in name");

		/* A tag that refers to itself would hang a naive structural walk. */
		names[0] = fixture(0, "self_ref0.poc");
		names[1] = fixture(1, "self_ref1.poc");
		ok &= compiles(vm, names, 2, "node", 2, "accept a self-referential tag in two units");

		/* An enum records no members - its constants become symbols in the
		 * enclosing frame - so two enums sharing a tag always agree, and
		 * differing enumerator lists are not diagnosed.  The kind comparison
		 * still catches an enum against a struct of the same name. */
		names[0] = fixture(0, "enum0.poc");
		names[1] = fixture(1, "enum1.poc");
		ok &= compiles(vm, names, 2, NULL, 0, "accept two enums sharing a tag");

		names[0] = fixture(0, "header0.poc");
		names[1] = fixture(1, "header1.poc");
		names[2] = fixture(2, "header2.poc");
		ok &= compiles(vm, names, 3, "vec", 3,
					   "accept three units including one header, one entry each");
	}

	/* Redefinition inside a single unit was already an error and stays one,
	 * with its own message. */
	{
		const char* names[1];

		names[0] = fixture(0, "same_unit.poc");
		ok &= check(poco_vm_compile_files(vm, names, 1, &program) == POCO_STATUS_REPORTED,
					"reject redefinition within one unit");
		ok &= check(strstr(poco_get_last_error(vm), "tag redefined") != NULL,
					"same-unit redefinition keeps its existing message");
		poco_program_destroy(program);
		program = NULL;
	}

	poco_vm_destroy(vm);
	return ok ? 0 : 1;
}
