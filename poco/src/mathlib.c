
#include "pocolib.h"
#include <math.h>
#include <signal.h>
#include "poco_errcodes.h"
#include "ptrmacro.h"
#include "standard_library.h"

extern Errcode builtin_err;

static Lib_proto mathlib[] = {
	/* Most of the ansi math library (not bits that use pointers) */
	{acos, "double  acos(double x);"},
	{asin, "double  asin(double x);"},
	{atan, "double  atan(double x);"},
	{atan2, "double  atan2(double y, double x);"},
	{ceil, "double  ceil(double x);"},
	{cos, "double  cos(double x);"},
	{cosh, "double  cosh(double x);"},
	{exp, "double  exp(double x);"},
	{fabs, "double  fabs(double x);"},
	{floor, "double  floor(double x);"},
	{fmod, "double  fmod(double x, double y);"},
	{log, "double  log(double x);"},
	{log10, "double  log10(double x);"},
	{pow, "double  pow(double x, double y);"},
	{sin, "double  sin(double x);"},
	{sinh, "double  sinh(double x);"},
	{sqrt, "double  sqrt(double x);"},
	{tan, "double  tan(double x);"},
	{tanh, "double  tanh(double x);"},
};

/*
 * The trusted graph table is registered by embedding hosts, potentially from
 * more than one VM thread.  Keep its public descriptor fully initialized and
 * immutable: a lazy copy from mathlib would allow another thread to observe a
 * partially populated binding array.
 *
 * These functions are the scalar ANSI math surface only.  In particular this
 * table contains no locale, clock, or random-number entry points.
 */
static const PocoBinding standard_math_bindings[] = {
	{"double  acos(double x);", (PocoNativeFunction)acos, NULL, 0},
	{"double  asin(double x);", (PocoNativeFunction)asin, NULL, 0},
	{"double  atan(double x);", (PocoNativeFunction)atan, NULL, 0},
	{"double  atan2(double y, double x);", (PocoNativeFunction)atan2, NULL, 0},
	{"double  ceil(double x);", (PocoNativeFunction)ceil, NULL, 0},
	{"double  cos(double x);", (PocoNativeFunction)cos, NULL, 0},
	{"double  cosh(double x);", (PocoNativeFunction)cosh, NULL, 0},
	{"double  exp(double x);", (PocoNativeFunction)exp, NULL, 0},
	{"double  fabs(double x);", (PocoNativeFunction)fabs, NULL, 0},
	{"double  floor(double x);", (PocoNativeFunction)floor, NULL, 0},
	{"double  fmod(double x, double y);", (PocoNativeFunction)fmod, NULL, 0},
	{"double  log(double x);", (PocoNativeFunction)log, NULL, 0},
	{"double  log10(double x);", (PocoNativeFunction)log10, NULL, 0},
	{"double  pow(double x, double y);", (PocoNativeFunction)pow, NULL, 0},
	{"double  sin(double x);", (PocoNativeFunction)sin, NULL, 0},
	{"double  sinh(double x);", (PocoNativeFunction)sinh, NULL, 0},
	{"double  sqrt(double x);", (PocoNativeFunction)sqrt, NULL, 0},
	{"double  tan(double x);", (PocoNativeFunction)tan, NULL, 0},
	{"double  tanh(double x);", (PocoNativeFunction)tanh, NULL, 0},
};

// int matherr(struct exception *err_info)
///*****************************************************************************
// *
// ****************************************************************************/
//{
//	  builtin_err = Err_float;
//
//	  errline(Err_float, "%s(%f ...) code %d", err_info->name, err_info->arg1,
//		  err_info->type);
//	  raise(SIGFPE);
//	  return(1);
//}

Poco_lib po_math_lib = {
	NULL,
	"(C Standard) Math",
	mathlib,
	Array_els(mathlib),
};

const PocoLibrary* poco_standard_math_library(void)
{
	static const PocoLibrary library = {
		POCO_STANDARD_MATH_LIBRARY_ID,
		standard_math_bindings,
		Array_els(standard_math_bindings),
		NULL,
		NULL,
		NULL,
	};
	return &library;
}
