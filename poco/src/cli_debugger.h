#ifndef POCO_CLI_DEBUGGER_H
#define POCO_CLI_DEBUGGER_H

#include <poco/poco.h>

/* Run one stdin-driven debugger session. Neither vm nor program is consumed. */
PocoStatus poco_cli_debug_program(PocoVm* vm, PocoProgram* program,
								  const char* explicit_source_path, int32_t* out_result,
								  int* out_completed);

#endif /* POCO_CLI_DEBUGGER_H */
