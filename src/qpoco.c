/* qpoco.c - programming environment.  Little safe subset of C interpreter.
	Has access to most of the Animator libraries.  Loads source modules
	from resource directory.  Internally is a stack based interpreter
	with a lot of code stolen from my pogo langauge, hence the name. */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "errcodes.h"
#include "filepath.h"
#include "jfile.h"
#include "jimk.h"
#include "textedit.h"
#include "rastcurs.h"
#include "render.h"
#include "resource.h"
#include "commonst.h"
#include "softmenu.h"
#include "xfile.h"

#include <poco/poco.h>

#include "ani_poco_adapter.h"
#include "qpoco.h"
#include "poco_turtle.h"
#include "poco_tween.h"


/* Animator-specific runner policy lives in this adapter-owned source. */
extern void po_init_abort_control(int abortable, void *handler);
extern bool po_check_abort(void *data);


/* the path from which the currently-running poco program was loaded...
 *	this is used below in get_get_poco_include_pathlistlist() as one of the
 *	paths to search for include and library files.	it is also used in
 *	pocodos.c, in the library function GetProgramDir().
 */

char po_current_program_path[PATH_SIZE] = "";

/* the path of a program to be chained to after the current program
 * exits.  before starting a compile/run operation, we blast this down
 * to a null string.  if the program calls PocoChainTo(path), then the
 * specified path is copied into this string.  if the run then ends
 * without error, we automatically loop through a compile/execute cycle
 * again using the new program.
 */

char po_chainto_program_path[PATH_SIZE];

// forward declarations for new compilers
static Errcode qls_poco(char *pbuf,char *prompt, char *button, int qls_mode, int path_type);
static Errcode qload_poco(char *pbuf);
static Errcode qsave_poco(char *pbuf);
Errcode quse_poco();


/*****************************************************************************
 * store the path of the current poco program into the global path var.
 ****************************************************************************/
static void set_current_program_path(char *progpath)
{
if (Success <= get_full_path(progpath, po_current_program_path))
	{
	remove_path_name(po_current_program_path);	/* nuke last name on path str */
	}
else
	po_current_program_path[0] = '\0'; /* oh well */
}

/*****************************************************************************
 * make list of include directories for poco compile.
 *
 *	 the compiler will search the directories in this list for '#include'
 *	 and '#pragma poco library' files.
 *
 *	 currently, we supply a null path, the path the program is in, and the
 *	 system resource directory as the include paths.
 *
 *	 note that the null path MUST be passed to poco to allow specification
 *	 of device/path names in the poco source (eg, "#include "\xyz\temp.h").
 *	 also note that poco expects each pathname (other than the null path)
 *	 to include the trailing backslash.
 ****************************************************************************/
static const char *const *get_poco_include_paths(void)
{
static char  nullpath[] = "";
static char  rbuf[PATH_SIZE] = "";              /* resource dir path buffer */

static const char *pathlist[] = { 					/* list of include paths... */
	po_current_program_path,						/* program's dir first      */
	nullpath,									/* current/specified dir    */
	rbuf,										/* then system resource dir */
	};

if (rbuf[0] == 0)					/* only need to get the resource dir once */
	{
	strcpy(rbuf, resource_dir); 			/* get system resource dir	  */
	}

return pathlist;
}

/*****************************************************************************
 * Remove <cr>'s and truncate string after 5 lines.
 ****************************************************************************/
static void trunc_to_5_lines(char *source)
{
char *dest;
char c;
int count = 0;

dest = source;
while ((c = *source++) != 0)
	{
	switch (c)
		{
		case '\r':
			break;
		case '\n':
			*dest++ = c;
			if (++count >= 5)
				goto OUT;
			break;
		default:
			*dest++ = c;
			break;
		}
	}
OUT:
*dest++ = 0;
}

/*****************************************************************************
 * Put first five lines of a file into a dialog box.
 ****************************************************************************/
void report_err_in_file(char *filename)
{
	char err_buf[512];
	XFILE* f;
	size_t size;

	f = xfopen(filename, XREADONLY);
	if (f == NULL)
	{
		soft_continu_box("no_err_file");
		return;
	}

	size = xfread(err_buf, sizeof(err_buf)-1, 1, f);
	err_buf[size] = 0;
	xfclose(f);
	trunc_to_5_lines(err_buf);
	continu_box(err_buf);
}

/*****************************************************************************
 *
 ****************************************************************************/
static void poco_report_err(char *phase, Errcode err)
{
if (err != Err_early_exit)
	softerr(err, phase);
}

/* Resolve poco_err_name (e.g. "=:AATEMP.ERR") to a real filesystem path
 * so that libpoco's standard fopen() can write to it. */
static const char* resolved_poco_err_name(void)
{
	static char resolved[PATH_SIZE];
	get_full_path(poco_err_name, resolved);
	return resolved;
}

typedef struct AniPocoDiagnosticState
{
	PocoStatus status;
	char source_name[PATH_SIZE];
	long line;
	int column;
	char message[512];
	char error_file[PATH_SIZE];
} AniPocoDiagnosticState;

static void clear_adapter_error_file(const char *filename)
{
	FILE *file;

	if (filename == NULL)
		return;
	file = fopen(filename, "w");
	if (file != NULL)
		fclose(file);
}

static void capture_adapter_diagnostic(void *user_data,
	const PocoDiagnostic *diagnostic)
{
	AniPocoDiagnosticState *state = user_data;
	FILE *file;

	if (state == NULL || diagnostic == NULL)
		return;
	state->status = diagnostic->status;
	state->line = diagnostic->line;
	state->column = diagnostic->column;
	if (diagnostic->source_name != NULL) {
		strncpy(state->source_name, diagnostic->source_name,
			sizeof(state->source_name) - 1);
		state->source_name[sizeof(state->source_name) - 1] = '\0';
	}
	if (diagnostic->message != NULL) {
		strncpy(state->message, diagnostic->message, sizeof(state->message) - 1);
		state->message[sizeof(state->message) - 1] = '\0';
	}
	if (state->error_file[0] == '\0' || state->message[0] == '\0')
		return;
	file = fopen(state->error_file, "w");
	if (file != NULL) {
		fputs(state->message, file);
		fclose(file);
	}
}

static Errcode animator_status(PocoStatus status,
	const AniPocoDiagnosticState *diagnostic)
{
	if (status == POCO_STATUS_REPORTED && diagnostic != NULL &&
		diagnostic->message[0] != '\0')
		return Err_in_err_file;
	return (Errcode)status;
}

static Errcode compile_animator_program(const char *source_name,
	PocoVm **out_vm, PocoProgram **out_program,
	AniPocoDiagnosticState *diagnostic)
{
	PocoVmOptions options = {0};
	PocoStatus status;

	if (out_vm == NULL || out_program == NULL || diagnostic == NULL)
		return Err_null_ref;
	*out_vm = NULL;
	*out_program = NULL;
	memset(diagnostic, 0, sizeof(*diagnostic));
	strncpy(diagnostic->error_file, resolved_poco_err_name(),
		sizeof(diagnostic->error_file) - 1);
	diagnostic->error_file[sizeof(diagnostic->error_file) - 1] = '\0';
	clear_adapter_error_file(diagnostic->error_file);

	options.include_paths = get_poco_include_paths();
	options.include_path_count = 3;
	options.diagnostic_callback = capture_adapter_diagnostic;
	options.diagnostic_user_data = diagnostic;
	ani_poco_configure_legacy_poe(&options);
	status = poco_vm_create(&options, out_vm);
	if (status == POCO_STATUS_OK)
		status = ani_poco_register_libraries(*out_vm);
	if (status == POCO_STATUS_OK)
		status = poco_vm_compile_file(*out_vm, source_name, out_program);
	if (status != POCO_STATUS_OK) {
		poco_program_destroy(*out_program);
		*out_program = NULL;
		poco_vm_destroy(*out_vm);
		*out_vm = NULL;
	}
	return animator_status(status, diagnostic);
}

static int check_adapter_abort(void *user_data)
{
	return po_check_abort(user_data) ? 1 : 0;
}

static bool poco_text_changed;

/*****************************************************************************
 * Edit poco file and note down that changes have been made.
 ****************************************************************************/
static void qedit_note_changes(long line, int cpos)
{
if (qedit_poco(line, cpos))
	poco_text_changed = true;
}


/*****************************************************************************
 *
 ****************************************************************************/
static Errcode execute_poco(PocoVm *vm, PocoProgram *program,
	AniPocoDiagnosticState *diagnostic)
{
	PocoRunOptions options = {0};
	PocoStatus status;
	void *ocurs;

	po_tur_home();
	make_render_cashes();
	init_poco_tween();
	builtin_err = Success;
	po_init_abort_control(true, NULL);
	ocurs = set_pen_cursor(&plain_ptool_cursor);
	options.cancel_callback = check_adapter_abort;
	options.trace_file = resolved_poco_err_name();
	status = poco_vm_run(vm, program, &options, NULL);
	cleanup_toptext();
	cleanup_poco_tween();
	free_render_cashes();
	set_pen_cursor(ocurs); /* restore old cursor */
	show_mouse();	/* make cursor visible for sure */
	return animator_status(status, diagnostic);
}

static Errcode execute_stripped_poco(PocoVm *vm, PocoProgram *program,
	AniPocoDiagnosticState *diagnostic)
{
	PocoRunOptions options = {0};
	PocoStatus status;

	options.cancel_callback = check_adapter_abort;
	options.trace_file = resolved_poco_err_name();
	status = poco_vm_run(vm, program, &options, NULL);
	return animator_status(status, diagnostic);
}

/* Run a poco program that doesn't need much in the way of the
 * poco run time environment (that won't do many ink calls etc. */
Errcode run_poco_stripped_environment(char *source_name)
{
	PocoVm *vm;
	PocoProgram *program;
	AniPocoDiagnosticState diagnostic;
	Errcode err;

	err = compile_animator_program(source_name, &vm, &program, &diagnostic);
	if (err >= Success)
		err = execute_stripped_poco(vm, program, &diagnostic);
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return err;
}


/*****************************************************************************
 * compile, (and if successfull) run a poco program.
 ****************************************************************************/
Errcode qrun_poco(char *sourcename, bool edit_err)
{
	Errcode err;
	char	chainbuf[PATH_SIZE];
	char	*phase;
	PocoVm *vm;
	PocoProgram *program;
	AniPocoDiagnosticState diagnostic;

CHAIN_ANOTHER_PROGRAM:					// loop point for chaining programs
	po_chainto_program_path[0] = '\0';  // start with no chainto program

	phase = "poco_compile";
	err = compile_animator_program(sourcename, &vm, &program, &diagnostic);
	if (err >= Success)
	{
		save_undo();
		phase = "poco_run";
		err = execute_poco(vm, program, &diagnostic);
	}

	if (err < Success)
	{
		po_chainto_program_path[0] = '\0';  // don't allow chaining after error
		if (err == Err_in_err_file) {
			report_err_in_file(resolved_poco_err_name());
			if (edit_err)
				qedit_note_changes(diagnostic.line, diagnostic.column);
		}
		else {
			poco_report_err(phase, err);
		}
		err = Err_reported;
	}

	poco_program_destroy(program);
	poco_vm_destroy(vm);

	if (po_chainto_program_path[0] != '\0')
	{
		strcpy(chainbuf, po_chainto_program_path);
		sourcename = chainbuf;
		set_current_program_path(chainbuf);
		goto CHAIN_ANOTHER_PROGRAM;
	}

	return err;
}

static PocoVm *cl_vm;
static PocoProgram *cl_program;
static AniPocoDiagnosticState cl_diagnostic;

/*****************************************************************************
 * invoke poco (compile only) from the command line
 ****************************************************************************/
Errcode compile_cl_poco(char *name)
{
	set_current_program_path(name); 	/* used by compiler for #include, etc */
	poco_program_destroy(cl_program);
	poco_vm_destroy(cl_vm);
	cl_program = NULL;
	cl_vm = NULL;
	return compile_animator_program(name, &cl_vm, &cl_program, &cl_diagnostic);
}


/*****************************************************************************
 *
 ****************************************************************************/
Errcode do_cl_poco(char *name)
{
Errcode err = Success;
char	chainbuf[PATH_SIZE];

(void)name;

CHAIN_ANOTHER_PROGRAM:

	po_chainto_program_path[0] = '\0';

	if (cl_program != NULL && cl_vm != NULL)
		{
		err = execute_poco(cl_vm, cl_program, &cl_diagnostic);
		if (err < Success)
			{
			po_chainto_program_path[0] = '\0';  // blast chain prog on error
			if (err != Err_in_err_file && err != Err_early_exit)
				poco_report_err("poco_run", err);
			}
		poco_program_destroy(cl_program);
		poco_vm_destroy(cl_vm);
		cl_program = NULL;
		cl_vm = NULL;
		if (po_chainto_program_path[0] != '\0')
			{
			strcpy(chainbuf, po_chainto_program_path);
			if ((err = compile_cl_poco(chainbuf)) >= Success)
				goto CHAIN_ANOTHER_PROGRAM;
			}
		}
	return err;
}

/*****************************************************************************
 * save current poco program path, run program, restore current path.
 *
 *	this allows a 'use' invokation of a poco program to run with the proper
 *	current-program-path info, but ensures that the current path for the
 *	program in the editor (if any) is preserved across the run.
 *	(hey -- kludge is my middle name.)
 ****************************************************************************/
Errcode qrun_pocofile(char *poco_path, bool editable)
{
	Errcode err;
	char save_path[PATH_SIZE];

	strcpy(save_path, po_current_program_path);
	set_current_program_path(poco_path);
	err = qrun_poco(poco_path,editable);
	strcpy(po_current_program_path,save_path);

	return err;
}

/*****************************************************************************
 * Make sure user has a chance to save his changes to the program before
 * he loads in a new program or starts a fresh one.
 ****************************************************************************/
static void insure_changes(char *poco_file, char *poco_path)
{
if (!poco_text_changed)
	return;
if (poco_file[0] == 0)
	{
	if (soft_yes_no_box("save_changes_first"))
		{
		qsave_poco(poco_path);
		}
	}
else
	{
	if (soft_yes_no_box("!%s", "save_changes_to_first", poco_file))
		{
		pj_copyfile(poco_source_name, poco_path);
		}
	}
}

/*****************************************************************************
 *  The main loop for the Poco programming menu.
 ****************************************************************************/
void go_pgmn(void)
{
	int choice;
	char pbuf[PATH_SIZE];
	char *poco_file;
	USHORT mdis[9];


	for (;;)
	{
		/* set up asterisks and disables */
		clear_mem(mdis, sizeof(mdis));

		if (!pj_exists(poco_source_name)) {
			mdis[1] = mdis[3] = mdis[4] = QCF_DISABLED;
		}

		vset_get_path(POCO_PATH, pbuf);

		poco_file = pj_get_path_name(pbuf);

		choice = soft_qchoice(mdis, "!%.18s", "poco_program", poco_file );

		switch (choice)
		{
			case 0:
				qedit_note_changes(-1L,-1);
				break;
			case 1:
				qrun_poco(poco_source_name,true);
				break;
			case 2:
				insure_changes(poco_file, pbuf);
				qload_poco(pbuf);
				break;
			case 3: 	/* save */
				if (poco_file[0] == 0) {
					// perform save as
					qsave_poco(pbuf);
					break;
				}
				pj_copyfile(poco_source_name, pbuf);
				poco_text_changed = false;
				break;
			case 4: 	/* save as */
				qsave_poco(pbuf);
				break;
			case 5: 	/* new	*/
				insure_changes(poco_file, pbuf);
				pj_delete(poco_source_name);
				poco_text_changed = false;
				poco_file[0] = 0;
				vset_set_path(POCO_PATH,pbuf);
				break;
			case 6:
				ani_poco_write_library_list("pocolib.txt");
				break;

			default:
				return;
		}
	}
}

#define QLS_LOAD 0
#define QLS_SAVE 1
#define QLS_USE 2

/*****************************************************************************
 * Put up a file requestor to load, save, or use a poco program (depending
 * on qls_mode variable).	Then take appropriate load/save/use action.
 ****************************************************************************/
static Errcode qls_poco(char *pbuf,char *prompt, char *button, int qls_mode, int path_type)
{
	bool got_it = false;
	Errcode err = Success;
	char poco_path[PATH_SIZE];

	if(vset_get_filename(prompt,".POC;.H",button,path_type, poco_path,1) != NULL)
	{
		switch (qls_mode)
		{
			case QLS_LOAD:
			{
				if (!pj_exists(poco_path))
				{
					cant_find(poco_path);
					err = Err_no_file;
				}
				else
				{
					vs.ped_cursor_p = vs.ped_yoff = 0;
					set_current_program_path(poco_path);
					if ((err = pj_copyfile(poco_path,poco_source_name)) >= Success)
						poco_text_changed = false;
				}
			}
			break;

			case QLS_USE:
				qrun_pocofile(poco_path,false);
				break;
			case QLS_SAVE:
				{
				if (overwrite_old(poco_path))
					{
					if ((err = pj_copyfile(poco_source_name,poco_path)) >= Success)
						poco_text_changed = false;
					}
				else
					err = Err_extant;
				}
				break;
		}
	}
	else {
		err = Err_abort;
	}

	if(err >= Success)
	{
		strcpy(pbuf,poco_path);
	}
	else
	{
		pj_get_path_name(pbuf)[0] = 0;
	}
	return err;
}

/*****************************************************************************
 *
 ****************************************************************************/
static Errcode qload_poco(char *pbuf)
{
char sbuf[50];

return(qls_poco(pbuf,stack_string("load_poco",sbuf),
	load_str, QLS_LOAD, POCO_PATH));
}

/*****************************************************************************
 *
 ****************************************************************************/
static Errcode qsave_poco(char *pbuf)
{
char sbuf[50];

return(qls_poco(pbuf,stack_string("save_poco",sbuf),
	save_str, QLS_SAVE, POCO_PATH));
}

/*****************************************************************************
 *
 ****************************************************************************/
Errcode quse_poco()
{
	char pbuf[PATH_SIZE];
	char sbuf[50];
	char ubuf[16];

	return qls_poco(pbuf,stack_string("use_poco",sbuf),
	stack_string("use_str",ubuf), QLS_USE, POCO_USE_PATH);
}

#undef QLS_LOAD
#undef QLS_SAVE
#undef QLS_USE
