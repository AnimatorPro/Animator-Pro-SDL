#define REXLIB_INTERNALS 1
#include "rexlib.h"

#if defined(_WIN32) || defined(__CYGWIN__)
#define ANIMHOST_EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
#define ANIMHOST_EXPORT __attribute__((visibility("default")))
#else
#define ANIMHOST_EXPORT
#endif

/*
 * Canonical definitions of the legacy Hostlib handles.
 * These symbols must exist exactly once in the process so that
 * legacy POE modules and the Animator host share the same addresses.
 */

ANIMHOST_EXPORT Hostlib _a_a_syslib = {NULL, AA_SYSLIB, AA_SYSLIB_VERSION};
ANIMHOST_EXPORT Hostlib _a_a_loadpath = {NULL, AA_LOADPATH, AA_LOADPATH_VERSION};
ANIMHOST_EXPORT Hostlib _a_a_stdiolib = {NULL, AA_STDIOLIB, AA_STDIOLIB_VERSION};
ANIMHOST_EXPORT Hostlib _a_a_gfxlib = {NULL, AA_GFXLIB, AA_GFXLIB_VERSION};
ANIMHOST_EXPORT Hostlib _a_a_pocolib = {NULL, AA_POCOLIB, AA_POCOLIB_VERSION};
ANIMHOST_EXPORT Hostlib _a_a_mathlib = {NULL, AA_MATHLIB, AA_MATHLIB_VERSION};

/*
 * Standalone fallback for the pocolib host table.
 *
 * When the Animator host is running it populates _a_a_pocolib.next with a
 * full Porexlib that gives POE modules access to builtin_err and all the
 * host-side function pointers.  When a POE module is loaded by standalone
 * poco there is no Animator host, so _a_a_pocolib.next stays NULL and any
 * POE function that touches builtin_err would segfault.
 *
 * The stub below provides just enough of the Porexlib layout (Libhead
 * followed by an Errcode pointer) so that builtin_err resolves to valid
 * memory.  Poco core never discovers or invokes this Animator function;
 * the Ani adapter may explicitly select it through PocoModuleHooks when
 * legacy POE compatibility is requested.
 *
 * Host-dependent functions (GetMenuColors, GetPicScreen, etc.) remain NULL
 * in the stub and will crash if called; only pure-computation POE functions
 * should be exercised in standalone mode.
 */

static int _standalone_builtin_err;

static struct {
	Libhead hdr;
	int* pl_builtin_err;
} _standalone_porexlib_stub = {{0, 0, 0}, &_standalone_builtin_err};

ANIMHOST_EXPORT void animhost_ensure_pocolib(void)
{
	if (_a_a_pocolib.next == NULL) {
		_a_a_pocolib.next = &_standalone_porexlib_stub;
	}
}
