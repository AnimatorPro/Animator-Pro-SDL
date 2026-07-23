/****************************************************************************
 * main.c - a little testing shell for poco.  Not linked into PJ.
 *
 * Invokes the compiler on the files in the command line.  Also
 * contains the bits of the poco library present in the test shell.
 *
 * MAINTENANCE:
 *	09/06/91	(Jim)	Added -T flag for instruction tracing.
 *
 ***************************************************************************/

#define GENERATE_CTYPE_TABLE

#include <ctype.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aaconfig.h"
#include "cli_debugger.h"
#include "commonst.h"
#include "filepath.h"
#include "poco_errcodes.h"
#include "poco.h"
#include "pocoface.h"
#include "program_internal.h"
#include "ptrmacro.h"

/* Legacy standalone-host state.  The embeddable library keeps this status in
 * Poco_run_env instead. */
Errcode builtin_err;

#ifdef _MSC_VER
#include <float.h>
#endif

#ifndef _MSC_VER
/* _fpreset is a Microsoft CRT operation; standalone Unix builds need no-op. */
static void _fpreset(void)
{
}
#endif

#if defined(IAN) /* Where Ian keeps poco source */
Names incdirs[] = {
	{&incdirs[1], ""},
	{NULL, "\\paa\\resource\\"},
};
#elif defined(JIM) /* Where Jim keeps poco source */
Names incdirs[] = {
	{&incdirs[1], ""},
	{&incdirs[2], "\\paa\\resource\\"},
	{NULL, "c:\\tc\\include\\"},
};
#else
Names incdirs[] = {{&incdirs[1], ""}, {NULL, "\\paa\\resource\\"}};
#endif

/*
 * In PJ this lives in config.c, but since we don't want to pull that file
 * in here for now, declare a local memory space for the config.
 */
AA_config vconfg;

/****************************************************************************
 * some memory management routines...
 * (simulation of the facilities available in PJ)
 ***************************************************************************/

/* other forwards prototypes */
int matherr(void);
Errcode boxf(char* fmt, ...);

int po_puts(Popot s);
int po_printf(char* format, ...);
void po_qtext(char* format, ...);

char* ido_type_to_str(IdoType ido_type);
void dump_func_frame(const char* name, const Func_frame* frame_in);

/****************************************************************************
 *
 ***************************************************************************/
/* real implementations are provided in libpoco (pocoload.c) */

/*****************************************************************************
 * this routine catches div-by-zero and overflows in fp math instructions.
 *	 we just set builtin_err so that the poco interpreter will see an error
 *	 upon completion of the current virtual machine instruction, then we
 *	 re-install ourselves since signal handlers are one-shot by definition.
 * IMPORTANT NOTES:
 *	 watcom calls the floating point signal handler from within its
 *	 interupt handler for 80387 exceptions.  upon entry to this routine,
 *	 the hardware stack (ss:esp) is pointing to a 768-byte interupt stack!
 *	 if this routine is ever modified to take more extensive actions (ie,
 *	 calling an error reporting dialog) it will be necessary to switch to
 *	 a bigger stack.
 *	 despite what the watcom docs say, the 'errno' variable is NOT valid
 *	 upon entry to this routine!
 ****************************************************************************/
static void fpe_handler(int signum)
{
	(void)signum;

	_fpreset();                  /* clear status & re-init chip/emulator */
	builtin_err = Err_float;     /* remember error for poco interpreter */
	signal(SIGFPE, fpe_handler); /* re-install self */
}

/*****************************************************************************
 *
 ****************************************************************************/
int matherr()
{
	_fpreset();
	builtin_err = Err_float;
	return 1;
}

/****************************************************************************
 *
 ***************************************************************************/
/* this puts up a formated textbox for debugging etc */
Errcode boxf(char* fmt, ...)
{
	Errcode err;
	va_list args;

	va_start(args, fmt);
	err = vprintf(fmt, args);
	va_end(args);
	return (err);
}

/****************************************************************************
 *
 ***************************************************************************/
bool check_abort(void* nobody)
{
	// currently a no-op; was checking for a keypress
	(void)nobody;
	return false;
}

/****************************************************************************
 *
 ***************************************************************************/
void po_qtext(char* format, ...)
{
	va_list args;

	if (format == NULL) {
		builtin_err = Err_null_ref;
		return;
	}

	fputs("------ Qtext --------------------------\n\n", stdout);
	va_start(args, format);
	vfprintf(stdout, format, args);
	va_end(args);

	fputs("\n\n------ Hit any key to Continue --------\n", stdout);
	getch();
}

/****************************************************************************/
static Lib_proto proto_lines[] = {
	/*	{tryme, 	"int ptryme(int (*v)(long a, long b, long c));"}, */
	{puts, "int puts(char *s);"},
	{printf, "int printf(char *format, ...);"},
	{po_qtext, "void Qtext(char *format, ...);"},
};

Poco_lib po_main_lib = {.next = NULL,
						.name = "Poco Library",
						.lib = proto_lines,
						.count = Array_els(proto_lines),
						.init = NULL,
						.cleanup = NULL,
						.local_data = NULL,
						.resources = {NULL, NULL, NULL},
						.rexhead = NULL,
						{0}};

extern Poco_lib po_mem_lib;
extern Poco_lib po_FILE_lib;
extern Poco_lib po_math_lib;
extern Poco_lib po_str_lib;
extern Poco_lib po_dos_standalone_lib;

static Poco_lib* poco_libs[] = {
	&po_main_lib, &po_str_lib, &po_mem_lib, &po_FILE_lib, &po_math_lib, &po_dos_standalone_lib,
};

/****************************************************************************
 *
 ***************************************************************************/
static Poco_lib* get_poco_libs(void)
{
	static Poco_lib* list = NULL;
	int i;

	if (list == NULL) {
		for (i = Array_els(poco_libs); --i >= 0;) {
			poco_libs[i]->next = list;
			list = poco_libs[i];
		}
	}
	return (list);
}

#ifdef DEVELOPMENT
/* variables for runops tracing */
extern FILE* po_trace_file;
extern bool po_trace_flag;
#endif /* DEVELOPMENT */


/****************************************************************************/
char* ido_type_to_str(IdoType ido_type)
{
	switch (ido_type) {
		case IDO_INT:
			return "int";
		case IDO_LONG:
			return "long";
		case IDO_DOUBLE:
			return "double";
		case IDO_POINTER:
			return "pointer";
		case IDO_CPT:
			return "C pointer";
		case IDO_VOID:
			return "void";
		case IDO_VPT:
			return "void*";
		case IDO_STRUCT:
			return "struct";

#ifdef STRING_EXPERIMENT
		case IDO_STRING:
			return "string";
#endif

		default:
			fprintf(stderr, "-- Bad IdoType: %d\n", ido_type);
			return "void";
	}
}

/****************************************************************************/
void dump_func_frame(const char* name, const Func_frame* frame_in)
{
	char msg[16];
	Func_frame* frame = frame_in;

	printf("[ func frames - %s ]\n", name);

	while (frame) {
		if (frame->return_type) {
			sprintf(msg, "%s", ido_type_to_str(frame->return_type->ido_type));
		} else {
			sprintf(msg, "void");
		}

		printf("func: %s (%d params) -> %s\n", frame->name, frame->pcount, msg);

		frame = frame->next;
	}
}

/****************************************************************************
 *
 ***************************************************************************/
static void print_version()
{
	printf("poco version %u.%u.%u\n", POCO_API_VERSION_MAJOR, POCO_API_VERSION_MINOR,
		   POCO_API_VERSION_PATCH);
}

/****************************************************************************
 *
 ***************************************************************************/
static void usage(FILE* stream)
{
	fprintf(stream, "Usage: poco [options] <file> [file ...]\n");
	fprintf(stream, "\nOptions:\n");
	fprintf(stream, "  -c, --compile        Compile only; do not run.\n");
	fprintf(
		stream,
		"  -o, --output <file>  Write compiled output (.pex recommended; implies --compile).\n");
	fprintf(stream, "      --version        Print version and exit.\n");
	fprintf(stream, "      --verbose        Enable verbose debug output.\n");
	fprintf(stream, "      --gui            Launch Poco GUI (not implemented).\n");
	fprintf(stream, "  -g, --debug-info     Emit extended debug information.\n");
	fprintf(stream, "      --debug          Launch the interactive debugger.\n");
	fprintf(stream, "  -d                    Dump disassembly to <file.dump>.\n");
	fprintf(stream, "  -l                    Disable builtin libraries.\n");
#ifdef DEVELOPMENT
	fprintf(stream, "  -t                    Enable instruction trace (development).\n");
#endif

	fflush(stream);
}

/****************************************************************************
 *
 ***************************************************************************/
static void replace_file_extension(char* dest, const char* buffer, size_t max_len,
								   const char* new_ext)
{
	const char* dot = strrchr(buffer, '.');
	if (dot == NULL) {
		// No dot found-- assume file and add the dot at the end
		snprintf(dest, max_len, "%s.%s", buffer, new_ext);
	} else {
		// Dot found-- replace the extension
		memcpy(dest, buffer, min(strlen(buffer), max_len - 1));
		dest[(size_t)(dot - buffer)] = '\0';
		snprintf(dest, max_len, "%s.%s", dest, new_ext);
	}
}

/****************************************************************************
 * Compile a source file through the embedding API and persist the resulting
 * immutable program image.  The serializer owns the canonical source-less
 * archive shape; the CLI only owns opening and closing the destination.
 ***************************************************************************/
static PocoStatus compile_to_binary(PocoVm* vm, const char* const* source_filenames,
									size_t source_count, const char* output_filename,
									bool with_builtin_libs, PocoDebugLevel debug_level)
{
	PocoActivation* activation = NULL;
	PocoCall* main_call = NULL;
	PocoProgram* program = NULL;
	PocoStatus status;
	FILE* output;

	if (with_builtin_libs) {
		status = poco_vm_register_standard_library(vm);
		if (status != POCO_STATUS_OK) {
			return status;
		}
	}
	status = poco_vm_compile_files(vm, source_filenames, source_count, &program);
	if (status != POCO_STATUS_OK) {
		return status;
	}
	/* Writing a binary produces a runnable artifact, so reject library-only
	 * sources here. A bare -c check takes the ordinary compile-only path below and
	 * intentionally does not perform this check. */
	status = poco_activation_acquire(program, &activation);
	if (status == POCO_STATUS_OK) {
		status = poco_call_begin(activation, "main", &main_call);
	}
	if (main_call != NULL) {
		poco_call_end(main_call);
	}
	poco_activation_release(activation);
	if (status == POCO_STATUS_NOT_FOUND) {
		status = (PocoStatus)Err_no_main;
	}
	if (status != POCO_STATUS_OK) {
		poco_program_destroy(program);
		return status;
	}

	output = fopen(output_filename, "wb");
	if (output == NULL) {
		fprintf(stderr, "poco: unable to open output file '%s'\n", output_filename);
		poco_program_destroy(program);
		return POCO_STATUS_CREATE_FAILED;
	}
	status = poco_program_serialize_file(program, debug_level, output);
	if (fclose(output) != 0 && status == POCO_STATUS_OK) {
		status = POCO_STATUS_WRITE_FAILED;
	}
	if (status != POCO_STATUS_OK) {
		fprintf(stderr, "poco: unable to write compiled output '%s'\n", output_filename);
	}
	poco_program_destroy(program);
	return status;
}

/****************************************************************************
 * Inspect only the canonical container magic.  A complete match is
 * authoritative: callers must deserialize the file and report any validation
 * error rather than falling back to the source compiler.
 ***************************************************************************/
static PocoStatus inspect_input_file(const char* filename, FILE** out_file, bool* out_is_binary)
{
	static const unsigned char bytecode_magic[8] = {'P', 'O', 'C', 'O', 'B', 'C', 0x0d, 0x0a};
	unsigned char leading_bytes[sizeof(bytecode_magic)];
	size_t bytes_read;
	FILE* input;

	if (filename == NULL || out_file == NULL || out_is_binary == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	*out_file = NULL;
	*out_is_binary = false;
	input = fopen(filename, "rb");
	if (input == NULL) {
		return POCO_STATUS_NO_FILE;
	}
	bytes_read = fread(leading_bytes, 1, sizeof(leading_bytes), input);
	if (bytes_read < sizeof(leading_bytes) && ferror(input)) {
		fclose(input);
		return POCO_STATUS_READ_FAILED;
	}
	if (fseek(input, 0, SEEK_SET) != 0) {
		fclose(input);
		return POCO_STATUS_SEEK_FAILED;
	}
	*out_is_binary = bytes_read == sizeof(leading_bytes) &&
					 memcmp(leading_bytes, bytecode_magic, sizeof(bytecode_magic)) == 0;
	*out_file = input;
	return POCO_STATUS_OK;
}

static void report_binary_load_error(PocoVm* vm, PocoStatus status)
{
	switch (status) {
		case POCO_STATUS_IMAGE_TRUNCATED:
			fprintf(stderr, "poco: compiled binary is truncated\n");
			break;
		case POCO_STATUS_IMAGE_CORRUPT:
			fprintf(stderr, "poco: compiled binary is corrupt (hash or layout mismatch)\n");
			break;
		case POCO_STATUS_IMAGE_VERSION_MISMATCH:
			fprintf(stderr, "poco: compiled binary version mismatch\n");
			break;
		case POCO_STATUS_MODULE_NOT_FOUND:
			fprintf(stderr, "poco: required external library was not found\n");
			break;
		case POCO_STATUS_MODULE_LOAD_FAILED:
			fprintf(stderr, "poco: external library load failed: %s\n", poco_get_last_error(vm));
			break;
		case POCO_STATUS_MODULE_NO_ENTRY:
			fprintf(stderr, "poco: external library entry point is missing\n");
			break;
		case POCO_STATUS_MODULE_VERSION:
			fprintf(stderr, "poco: external library ABI version mismatch\n");
			break;
		case POCO_STATUS_FFI_FUNCTION_NOT_FOUND:
			fprintf(stderr, "poco: unknown external library binding\n");
			break;
		case POCO_STATUS_MODULE_INVALID:
			fprintf(stderr, "poco: external library is invalid\n");
			break;
		case POCO_STATUS_MODULE_EMPTY:
			fprintf(stderr, "poco: external library contains no bindings\n");
			break;
		default:
			fprintf(stderr, "poco: unable to load compiled binary\n");
			break;
	}
}

/****************************************************************************
 * Load and optionally run a validated source-less program.  Image validation
 * statuses have CLI-specific diagnostics because they are otherwise outside
 * the legacy compiler error domain handled by report_status below.
 ***************************************************************************/
static PocoStatus run_binary(PocoVm* vm, FILE* input, bool run_program, bool with_builtin_libs)
{
	PocoProgram* program = NULL;
	PocoStatus status;
	int32_t result = 0;

	if (with_builtin_libs) {
		status = poco_vm_register_standard_library(vm);
		if (status != POCO_STATUS_OK) {
			return status;
		}
	}
	status = poco_vm_deserialize_file(vm, input, &program);
	if (status != POCO_STATUS_OK) {
		report_binary_load_error(vm, status);
		return status;
	}
	if (run_program) {
		status = poco_vm_run(vm, program, NULL, &result);
		if (status == POCO_STATUS_OK) {
			fprintf(stderr, "Return value: %d\n", (int)result);
		}
	}
	poco_program_destroy(program);
	return status;
}

/****************************************************************************
 * Compile or load a program through the embedding API and attach the CLI
 * debugger. Source compiles retain their physical path in memory; binaries
 * must carry the extended path metadata emitted by -g.
 ***************************************************************************/
static PocoStatus run_debugger(PocoVm* vm, const char* const* filenames, size_t source_count,
							   FILE* input, bool is_binary, bool with_builtin_libs)
{
	PocoProgram* program = NULL;
	PocoStatus status;
	int32_t result = 0;
	int completed = 0;

	if (with_builtin_libs) {
		status = poco_vm_register_standard_library(vm);
		if (status != POCO_STATUS_OK) {
			return status;
		}
	}
	status = is_binary ? poco_vm_deserialize_file(vm, input, &program)
					   : poco_vm_compile_files(vm, filenames, source_count, &program);
	if (status != POCO_STATUS_OK) {
		if (is_binary) {
			report_binary_load_error(vm, status);
		} else {
			fprintf(stderr, "poco: unable to compile debugger input '%s'\n", filenames[0]);
		}
		return status;
	}
	status =
		poco_cli_debug_program(vm, program, is_binary ? NULL : filenames[0], &result, &completed);
	if (status == POCO_STATUS_OK && completed) {
		fprintf(stderr, "Return value: %d\n", (int)result);
	}
	poco_program_destroy(program);
	return status;
}

static PocoStatus run_source_files(PocoVm* vm, const char* const* filenames, size_t source_count,
								   bool run_program, bool with_builtin_libs, bool debug_dump)
{
	PocoProgram* program = NULL;
	PocoStatus status;
	int32_t result = 0;

	if (with_builtin_libs) {
		status = poco_vm_register_standard_library(vm);
		if (status != POCO_STATUS_OK) {
			return status;
		}
	}
	status = poco_vm_compile_files(vm, filenames, source_count, &program);
	if (status == POCO_STATUS_OK && debug_dump) {
		char dump_file_name[FILENAME_MAX];
		FILE* dump_file;

		po_disassemble_program((Poco_run_env*)program->executable, stdout);
		replace_file_extension(dump_file_name, filenames[0], FILENAME_MAX, "dump");
		printf("==> Dumping to %s ...\n", dump_file_name);
		dump_file = fopen(dump_file_name, "w");
		if (dump_file != NULL) {
			po_disassemble_program((Poco_run_env*)program->executable, dump_file);
			fclose(dump_file);
		} else {
			fprintf(stderr, "-- Unable to open dump file for writing: %s\n", dump_file_name);
		}
	}
	if (status == POCO_STATUS_OK && run_program) {
		status = poco_vm_run(vm, program, NULL, &result);
		if (status == POCO_STATUS_OK) {
			fprintf(stderr, "Return value: %d\n", (int)result);
		}
	}
	poco_program_destroy(program);
	/* Preserve the legacy CLI's detailed compiler diagnostic path. */
	if (status == POCO_STATUS_REPORTED) {
		status = (PocoStatus)Err_in_err_file;
	}
	return status;
}

/****************************************************************************
 *
 ***************************************************************************/
int main(int argc, char* argv[])
{
	int err = Success;

	char* sfname = NULL; /* First input name, retained for legacy diagnostics. */
	char** input_filenames = argv + 1;
	size_t input_count = 0;
	const char* output_filename = NULL;
	bool runflag = true;
	bool verbose = false;
	bool emit_debug_info = false;
	bool debug_mode = false;
	bool parse_options = true;
	const char* argp;
	int counter;
	Poco_lib* builtin_libs;
	PocoVm* vm = NULL;
	PocoVmOptions vm_options = {0};
	int do_debug_dump = false;
	FILE* input_file = NULL;
	bool input_is_binary = false;
	PocoStatus status;

	builtin_libs = get_poco_libs();

	for (counter = 1; counter < argc; counter++) {
		argp = argv[counter];
		if (parse_options && strcmp(argp, "--") == 0) {
			parse_options = false;
		} else if (parse_options && strcmp(argp, "-c") == 0) {
			runflag = false;
		} else if (parse_options && strcmp(argp, "--compile") == 0) {
			runflag = false;
		} else if (parse_options && strcmp(argp, "-o") == 0) {
			if (++counter >= argc) {
				fprintf(stderr, "poco: option '-o' requires an argument\n");
				usage(stderr);
				return EXIT_FAILURE;
			}
			output_filename = argv[counter];
			runflag = false;
		} else if (parse_options && strcmp(argp, "--output") == 0) {
			if (++counter >= argc) {
				fprintf(stderr, "poco: option '--output' requires an argument\n");
				usage(stderr);
				return EXIT_FAILURE;
			}
			output_filename = argv[counter];
			runflag = false;
		} else if (parse_options && strncmp(argp, "--output=", 9) == 0) {
			if (argp[9] == '\0') {
				fprintf(stderr, "poco: option '--output' requires an argument\n");
				usage(stderr);
				return EXIT_FAILURE;
			}
			output_filename = argp + 9;
			runflag = false;
		} else if (parse_options && strcmp(argp, "--version") == 0) {
			print_version();
			return EXIT_SUCCESS;
		} else if (parse_options && strcmp(argp, "--verbose") == 0) {
			verbose = true;
		} else if (parse_options && strcmp(argp, "--gui") == 0) {
			fprintf(stdout, "Poco GUI not yet implemented\n");
			return Err_not_implemented;
		} else if (parse_options &&
				   (strcmp(argp, "-g") == 0 || strcmp(argp, "--debug-info") == 0)) {
			emit_debug_info = true;
		} else if (parse_options && strcmp(argp, "--debug") == 0) {
			debug_mode = true;
		} else if (parse_options && strcmp(argp, "-d") == 0) {
			do_debug_dump = true;
		} else if (parse_options && strcmp(argp, "-l") == 0) {
			builtin_libs = NULL;
#ifdef DEVELOPMENT
		} else if (parse_options && strcmp(argp, "-t") == 0) {
			po_trace_flag = true;
			po_trace_file = stdout;
#endif /* DEVELOPMENT */
		} else if (parse_options && argp[0] == '-' && argp[1] != '\0') {
			fprintf(stderr, "poco: unknown option '%s'\n", argp);
			usage(stderr);
			return EXIT_FAILURE;
		} else {
			/* Compact positionals into argv's already-consumed prefix. This keeps
			 * their command-line order without allocating or mutating the strings. */
			input_filenames[input_count++] = (char*)argp;
		}
	}

	if (input_count == 0) {
		usage(stdout);
		return 0;
	}
	sfname = input_filenames[0];

	init_stdfiles(); /* initialize PJ stdin, stdout, etc */

	signal(SIGFPE, fpe_handler);  // install floating point error trapping

	vm_options.verbose = verbose;
	if (poco_vm_create(&vm_options, &vm) != POCO_STATUS_OK) {
		return Err_no_memory;
	}
	for (size_t input_index = 0; input_index < input_count; ++input_index) {
		FILE* inspected_file = NULL;
		bool inspected_is_binary = false;

		status =
			inspect_input_file(input_filenames[input_index], &inspected_file, &inspected_is_binary);
		if (status != POCO_STATUS_OK) {
			sfname = input_filenames[input_index];
			err = (int)status;
			goto report_status;
		}
		if (input_count > 1 && inspected_is_binary) {
			fprintf(stderr,
					"poco: compiled binary '%s' cannot be mixed with additional source inputs\n",
					input_filenames[input_index]);
			fclose(inspected_file);
			err = EXIT_FAILURE;
			goto report_status;
		}
		if (input_count == 1) {
			input_file = inspected_file;
			input_is_binary = inspected_is_binary;
		} else {
			fclose(inspected_file);
		}
	}
	if (input_is_binary) {
		if (output_filename != NULL) {
			fprintf(stderr, "poco: '-o' cannot recompile an already-compiled binary '%s'\n",
					sfname);
			fclose(input_file);
			input_file = NULL;
			err = EXIT_FAILURE;
			goto report_status;
		}
		err = (int)(debug_mode ? run_debugger(vm, input_filenames, 1, input_file, true,
											  builtin_libs != NULL)
							   : run_binary(vm, input_file, runflag, builtin_libs != NULL));
		fclose(input_file);
		input_file = NULL;
		goto report_status;
	}
	if (input_file != NULL) {
		fclose(input_file);
	}
	input_file = NULL;
	if (debug_mode) {
		err = (int)run_debugger(vm, (const char* const*)input_filenames, input_count, NULL, false,
								builtin_libs != NULL);
		goto report_status;
	}
	if (output_filename != NULL) {
		err = (int)compile_to_binary(
			vm, (const char* const*)input_filenames, input_count, output_filename,
			builtin_libs != NULL,
			emit_debug_info ? POCO_DEBUG_LEVEL_EXTENDED : POCO_DEBUG_LEVEL_MINIMAL);
		goto report_status;
	}
	err = (int)run_source_files(vm, (const char* const*)input_filenames, input_count, runflag,
								builtin_libs != NULL, do_debug_dump);

report_status:
	if (input_file != NULL) {
		fclose(input_file);
	}
	if (err < Success) {
		switch (err) {
			case Err_no_memory:
				fprintf(stdout, "Out of memory\n");
				break;
			case Err_no_file:
				fprintf(stdout, "Couldn't find %s\n", sfname);
				break;
			case Err_create:
				fprintf(stdout, "Couldn't create error/dump/trace files (disk full?)\n");
				break;
			case Err_syntax:
				fprintf(stdout, "Poco C syntax error.\n");
				break;
			case Err_poco_internal:
				fprintf(stdout, "Poco compiler failed self-check.\n");
				break;
			case Err_poco_ffi_invalid_binding:
				fprintf(stdout, "%s\n", poco_get_last_error(vm));
				break;
			case Err_no_main:
				fprintf(stdout, "Program does not contain a main() routine.\n");
				break;
			case Err_in_err_file:
				fprintf(stdout, "%s", poco_get_last_error(vm));
				break;
			case Err_abort:
			default:
				break;
		}
		fprintf(stdout, "Error code %d\n", err);
	}

	cleanup_lfiles(); /* cleanup PJ stdin, stdout, etc */
	poco_vm_destroy(vm);

	return err;
}
