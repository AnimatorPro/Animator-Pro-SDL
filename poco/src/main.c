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
#include "commonst.h"
#include "filepath.h"
#include "poco_errcodes.h"
#include "poco.h"
#include "pocoface.h"
#include "ptrmacro.h"

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

/*****************************************************************************
 * this routine fools the PJ lfile library into thinking it is writing to
 * stdout but the stuff really goes into a file.
 ****************************************************************************/
FILE redirection_save;
FILE* f;

static Errcode open_redirect_stdout(char* fname)
{
	if (NULL == (f = fopen(fname, "w"))) { /* create the file */
		return Err_create;
	}

	redirection_save = *stdout; /* save state of stdout */
	*stdout = *f;               /* redirect stdout to file */

	return Success;
}

/*****************************************************************************
 * this un-directs stdout from a file back to the screen.
 ****************************************************************************/
static void close_redirect_stdout(void)
{
	*f = *stdout;               /* update buffer count, etc, in file */
	fclose(f);                  /* close file */
	*stdout = redirection_save; /* restore stdout state */
}

extern Errcode builtin_err; /* Defined by the embeddable Poco runtime. */

#ifdef DEVELOPMENT
/* variables for runops tracing */
extern C_frame* po_run_protos;
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
	printf("poco version %d\n", VRSN_NUM);
}

/****************************************************************************
 *
 ***************************************************************************/
static void usage()
{
	fprintf(stdout, "Usage: poco [options] <file.poc>\n");
	fprintf(stdout, "\nOptions:\n");
	fprintf(stdout, "  -c            Compile only; do not run.\n");
	fprintf(stdout, "  -d            Dump disassembly to <file.dump>.\n");
	fprintf(stdout, "  -o<file>      Redirect stdout to file.\n");
	fprintf(stdout, "  -l            Disable builtin libraries.\n");
#ifdef DEVELOPMENT
	fprintf(stdout, "  -t            Enable instruction trace (development).\n");
#endif
	fprintf(stdout, "  -v            Print version and exit.\n");
	fprintf(stdout, "  -V            Enable verbose debug output.\n");
	fprintf(stdout, "  -g            Launch Poco GUI (if available).\n");

	fflush(stdout);
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
		memcpy(dest, buffer, max(strlen(buffer), max_len - 1));
		dest[(size_t)(dot - buffer)] = '\0';
		snprintf(dest, max_len, "%s.%s", dest, new_ext);
	}
}

/****************************************************************************
 *
 ***************************************************************************/
int main(int argc, char* argv[])
{
	char err_file[PATH_SIZE];
	long err_line;
	int err_char;
	int err = Success;

	void* pexe;
	char* efname = NULL; /* Errors file name.	*/
	char* sfname = NULL; /* Source file name.	*/
	char* dfname = NULL; /* Dump file name.		*/
	bool runflag = true;
	bool verbose = false;
	char* argp;
	int counter;
	Poco_lib* builtin_libs;
	int do_debug_dump = false;
	int gui_mode = false;

	builtin_libs = get_poco_libs();

	init_stdfiles(); /* initialize PJ stdin, stdout, etc */

	signal(SIGFPE, fpe_handler);  // install floating point error trapping

	for (counter = 1; counter < argc; counter++) {
		argp = argv[counter];
		if (*argp == '-') {
			switch (toupper(*++argp)) {
				case 'c': /* Compile-only switch...   */
				case 'C': /* Compile-only switch...   */
					runflag = false;
					break;
#ifdef DEVELOPMENT
				case 't': /* Trace... */
				case 'T': /* Trace... */
					po_trace_flag = true;
					po_trace_file = stdout;
					break;
#endif                    /* DEVELOPMENT */
				case 'd': /* Dump file name...        */
				case 'D': /* Dump file name...        */
					do_debug_dump = true;
					break;
				case 'o': /* Redirection file name... */
				case 'O': /* Redirection file name... */
					if (*++argp != 0) {
						efname = argp;
					} else {
						efname = "stdout.txt";
					}
					break;
				case 'l': /* punt builtin libs...*/
				case 'L': /* punt builtin libs...*/
					builtin_libs = NULL;
					break;
				case 'v':
					print_version();
					return 0;
				case 'V':
					verbose = true;
					break;
				case 'g':
				case 'G':
					gui_mode = true;
					// fprintf(stdout, "Launching Poco GUI...\n");
					fprintf(stdout, "Poco GUI not yet implemented\n");
					return Err_not_implemented;
				default: /* Fat-finger case...		*/
					break;
			}
		} else {
			sfname = argp; /* It's not a switch, must be the source file. */
		}
	}

	if (sfname == NULL) {
		usage();
		return 0;
	}

	if (strchr(sfname, '.') == NULL) {
		/* If no '.' in name, tack on .POC */
		strcat(sfname, ".poc");
	}

	if (efname != NULL) {
		err = open_redirect_stdout(efname);
		if (err != Success) {
			fprintf(stdout, "Error attempting to redirect stdout to '%s'\n", efname);
			exit(-1);
		}
	}

	//	if (gui_mode) {
	//		poco_gui();
	//	}

	const int compile_status = compile_poco(&pexe, sfname, NULL, dfname, builtin_libs, err_file,
											&err_line, &err_char, incdirs, verbose);

	if (compile_status == Success) {
#ifdef DEVELOPMENT
		po_run_protos = (((Poco_run_env*)pexe)->protos); /* for trace */
#endif                                                   /* DEVELOPMENT */

		if (do_debug_dump) {
			po_disassemble_program((Poco_run_env*)pexe, stdout);
			char dump_file_name[FILENAME_MAX];
			replace_file_extension(dump_file_name, sfname, FILENAME_MAX, "dump");
			printf("==> Dumping to %s ...\n", dump_file_name);
			FILE* fp = fopen(dump_file_name, "w");
			if (fp) {
				po_disassemble_program((Poco_run_env*)pexe, fp);
				fclose(fp);
			} else {
				fprintf(stderr, "-- Unable to open dump file for writing: %s\n", dump_file_name);
			}
		}

		if (runflag) {
			err = run_poco(&pexe, NULL, check_abort, NULL, &err_line);
		}

		fprintf(stderr, "Return value: %d\n", ((Poco_run_env*)pexe)->result.i);
		free_poco(&pexe);
	} else {
		/* Propagate compile error to process exit code for test harnesses */
		err = compile_status;
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
				fprintf(stdout, "%s\n", poco_get_error());
				break;
			case Err_no_main:
				fprintf(stdout, "Program does not contain a main() routine.\n");
				break;
			case Err_in_err_file:
				if (poco_get_error()[0] != '\0') {
					fprintf(stdout, "%s", poco_get_error());
				}
				break;
			case Err_abort:
			default:
				break;
		}
		fprintf(stdout, "Error code %d\n", err);
	}

	if (efname != NULL) {
		close_redirect_stdout();
	}

	cleanup_lfiles(); /* cleanup PJ stdin, stdout, etc */

	return err;
}
