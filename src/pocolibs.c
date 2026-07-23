/*****************************************************************************
 * pocolib.c - Support for poco's builtin libraries, which also constitutes
 *			   the AA_POCOLIB host library for POE modules.
 ****************************************************************************/
#define REXLIB_INTERNALS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "errcodes.h"
#include "memory.h"
#include "ptrmacro.h"
#include "jimk.h" /* import vb and vs declarations */
#include "rexlib.h"
#include "pocorex.h"
#include "pocolib.h"
#include "pocoface.h"
#include "poly.h"
#include "ani_poco_adapter.h"

extern bool po_check_abort(void* data);
extern Errcode clone_ppoints(Poly* s, Poly* d);  // from polytool.c

/* Forward declarations for Animator's retained native-POE table. */
extern Hostlib _a_a_pocolib; /* from animhost/hostlib_table.c */
extern Porexlib aa_pocolib;  /* defined later in this file */

/*
 * The source tables remain the compatibility definition used by retained
 * Animator-native POE modules.  Animator scripts receive their bindings only
 * when ani_poco_register_libraries() converts these entries to public
 * PocoLibrary registrations on a VM.
 */
typedef struct AniPocoLegacyBinding {
	PocoNativeFunction function;
	char* prototype;
} AniPocoLegacyBinding;

typedef struct AniPocoLibrarySource {
	Poco_lib* library;
	size_t binding_stride;
} AniPocoLibrarySource;

/*
 * Polib* tables are a compatibility ABI of alternating function/prototype
 * pairs.  Generic Lib_proto arrays additionally carry an optional contract.
 * Keep that distinction here instead of treating the direct-table ABI as a
 * Lib_proto array (whose three-field stride would corrupt registrations).
 */
static const AniPocoLibrarySource animator_libraries[] = {
	{&po_user_lib, sizeof(AniPocoLegacyBinding)},
	{&po_draw_lib, sizeof(AniPocoLegacyBinding)},
	{&po_text_lib, sizeof(AniPocoLegacyBinding)},
	{&po_mode_lib, sizeof(AniPocoLegacyBinding)},
	{&po_turtle_lib, sizeof(AniPocoLegacyBinding)},
	{&po_time_lib, sizeof(AniPocoLegacyBinding)},
	{&po_cel_lib, sizeof(AniPocoLegacyBinding)},
	{&po_alt_lib, sizeof(AniPocoLegacyBinding)},
	{&po_optics_lib, sizeof(AniPocoLegacyBinding)},
	{&po_blit_lib, sizeof(AniPocoLegacyBinding)},
	{&po_misc_lib, sizeof(AniPocoLegacyBinding)},
	{&po_load_save_lib, sizeof(AniPocoLegacyBinding)},
	{&po_FILE_lib, sizeof(Lib_proto)},
	{&po_str_lib, sizeof(Lib_proto)},
	{&po_mem_lib, sizeof(Lib_proto)},
	{&po_math_lib, sizeof(Lib_proto)},
	{&po_dos_lib, sizeof(AniPocoLegacyBinding)},
	{&po_globalv_lib, sizeof(AniPocoLegacyBinding)},
	{&po_title_lib, sizeof(AniPocoLegacyBinding)},
	{&po_tween_lib, sizeof(AniPocoLegacyBinding)},
	{&po_flicplay_lib, sizeof(AniPocoLegacyBinding)},
	{&po_picdrive_lib, sizeof(Lib_proto)},
};

static void get_animator_binding(const AniPocoLibrarySource* source, int index,
								 PocoBinding* binding)
{
	char* entry;

	entry = (char*)source->library->lib + (size_t)index * source->binding_stride;
	if (source->binding_stride == sizeof(Lib_proto)) {
		Lib_proto* legacy_binding = (Lib_proto*)entry;

		binding->prototype = legacy_binding->proto;
		binding->function = (PocoNativeFunction)legacy_binding->func;
		binding->contract = legacy_binding->contract;
		binding->flags = legacy_binding->flags;
	} else {
		AniPocoLegacyBinding* legacy_binding = (AniPocoLegacyBinding*)entry;

		binding->prototype = legacy_binding->prototype;
		binding->function = legacy_binding->function;
		binding->contract = NULL;
		binding->flags = 0;
	}
}

/* Public descriptors require a function pointer even for a typedef line. */
static void ani_poco_typedef_placeholder(void)
{
}

/*
 * Duplicate a source/POE prototype string for public registration.
 *
 * The Poco parser handles any pointer depth in a prototype (a declarator's
 * stars are counted and each becomes a TYPE_POINTER; see declare.c:dcl).
 * Multi-level spellings such as "char **choices" (Qmenu, Qchoice, ...) must
 * therefore be registered verbatim: collapsing "**" to "*" makes the
 * registered parameter a shallower pointer than the char*[] arguments callers
 * pass, which the compiler rejects as a pointer type mismatch.
 */
static char* copy_animator_prototype(const char* prototype)
{
	char* copy;

	if (prototype == NULL) {
		return NULL;
	}
	copy = malloc(strlen(prototype) + 1);
	if (copy == NULL) {
		return NULL;
	}
	strcpy(copy, prototype);
	return copy;
}

static void free_animator_binding_prototypes(PocoBinding* bindings, int binding_count)
{
	int index;

	for (index = 0; index < binding_count; ++index) {
		free((void*)bindings[index].prototype);
	}
}

static PocoStatus initialize_animator_library(PocoLibrary* library)
{
	Poco_lib* legacy_library = library != NULL ? library->user_data : NULL;

	if (legacy_library == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	return (PocoStatus)po_init_libs(legacy_library);
}

static void cleanup_animator_library(PocoLibrary* library)
{
	Poco_lib* legacy_library = library != NULL ? library->user_data : NULL;

	if (legacy_library != NULL) {
		po_cleanup_libs(legacy_library);
	}
}

static void ani_poco_install_legacy_poe_table(void)
{
	_a_a_pocolib.next = &aa_pocolib;
}

static PocoStatus register_animator_library(PocoVm* vm, const AniPocoLibrarySource* source)
{
	PocoBinding* bindings;
	PocoLibrary library;
	PocoStatus status;
	Poco_lib* legacy_library;
	int index;
	int binding_count = 0;
	int binding_index = 0;

	if (source == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	legacy_library = source->library;
	if (vm == NULL || legacy_library == NULL || legacy_library->name == NULL ||
		legacy_library->lib == NULL || legacy_library->count <= 0) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	for (index = 0; index < legacy_library->count; ++index) {
		PocoBinding binding;

		get_animator_binding(source, index, &binding);
		if (binding.function == NULL && binding.prototype != NULL &&
			strncmp(binding.prototype, "typedef", strlen("typedef")) == 0) {
			binding.function = ani_poco_typedef_placeholder;
		}
		if (binding.function != NULL) {
			++binding_count;
		}
	}
	if (binding_count == 0) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	bindings = calloc((size_t)binding_count, sizeof(*bindings));
	if (bindings == NULL) {
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	for (index = 0; index < legacy_library->count; ++index) {
		PocoBinding binding;

		/* Typedef lines use a placeholder because public descriptors require a function. */
		get_animator_binding(source, index, &binding);
		if (binding.function == NULL && binding.prototype != NULL &&
			strncmp(binding.prototype, "typedef", strlen("typedef")) == 0) {
			binding.function = ani_poco_typedef_placeholder;
		}
		if (binding.function == NULL) {
			continue;
		}
		binding.prototype = copy_animator_prototype(binding.prototype);
		if (binding.prototype == NULL) {
			free_animator_binding_prototypes(bindings, binding_index);
			free(bindings);
			return POCO_STATUS_OUT_OF_MEMORY;
		}
		bindings[binding_index] = binding;
		++binding_index;
	}
	library.identity = legacy_library->name;
	library.bindings = bindings;
	library.binding_count = (size_t)binding_count;
	library.initialize = initialize_animator_library;
	library.cleanup = cleanup_animator_library;
	library.user_data = legacy_library;
	status = poco_vm_register_library(vm, &library);
	free_animator_binding_prototypes(bindings, binding_count);
	free(bindings);
	return status;
}

PocoStatus ani_poco_register_libraries(PocoVm* vm)
{
	size_t index;
	PocoStatus status;

	if (vm == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	ani_poco_install_legacy_poe_table();
	for (index = 0; index < Array_els(animator_libraries); ++index) {
		status = register_animator_library(vm, &animator_libraries[index]);
		if (status != POCO_STATUS_OK) {
			return status;
		}
	}
	return POCO_STATUS_OK;
}

PocoStatus ani_poco_write_library_list(const char* filename)
{
	FILE* file;
	size_t library_index;
	int binding_index;

	if (filename == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	file = fopen(filename, "w");
	if (file == NULL) {
		return POCO_STATUS_CREATE_FAILED;
	}
	for (library_index = 0; library_index < Array_els(animator_libraries); ++library_index) {
		const AniPocoLibrarySource* source = &animator_libraries[library_index];
		Poco_lib* library = source->library;

		for (binding_index = 0; binding_index < library->count; ++binding_index) {
			PocoBinding binding;

			get_animator_binding(source, binding_index, &binding);
			if (binding.prototype != NULL) {
				fprintf(file, "%s\n", binding.prototype);
			}
		}
	}
	if (fclose(file) != 0) {
		return POCO_STATUS_WRITE_FAILED;
	}
	return POCO_STATUS_OK;
}

PocoStatus ani_poco_write_library_inventory(const char* filename)
{
	FILE* file;
	size_t library_index;

	if (filename == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	file = fopen(filename, "w");
	if (file == NULL) {
		return POCO_STATUS_CREATE_FAILED;
	}
	for (library_index = 0; library_index < Array_els(animator_libraries); ++library_index) {
		Poco_lib* library = animator_libraries[library_index].library;

		if (library == NULL || library->name == NULL || fprintf(file, "%s\n", library->name) < 0) {
			fclose(file);
			return POCO_STATUS_WRITE_FAILED;
		}
	}
	if (fclose(file) != 0) {
		return POCO_STATUS_WRITE_FAILED;
	}
	return POCO_STATUS_OK;
}

/*****************************************************************************
 * some routines used by more than one poco library...
 ****************************************************************************/

extern Popot poco_lmalloc(long size);
extern void po_free(void* pt);

/*****************************************************************************
 * Convert a polygon to two int arrays
 ****************************************************************************/
static void poly_to_arrays(Poly* p, int* x, int* y)
{
	int i;
	LLpoint* pt;

	i = p->pt_count;
	pt = p->clipped_list;
	while (--i >= 0) {
		*x++ = pt->x;
		*y++ = pt->y;
		pt = pt->next;
	}
}

/*****************************************************************************
 * Convert a polygon to two poco int arrays
 ****************************************************************************/
Errcode po_poly_to_arrays(Poly* p, Popot* px, Popot* py)
{
	Popot x, y;
	long acount;

	acount = p->pt_count * sizeof(int);
	x = poco_lmalloc(acount);
	if (x.pt == NULL) {
		return Err_no_memory;
	}
	y = poco_lmalloc(acount);
	if (y.pt == NULL) {
		po_free(x.pt);
		return Err_no_memory;
	}
	poly_to_arrays(p, x.pt, y.pt);
	*px = x;
	*py = y;
	return Success;
}

/* Make sure that 2 poco arrays are big enough to hold ptcount ints. */
Errcode po_2_arrays_check(int ptcount, Popot* px, Popot* py)
{
	int bsize;

	if (px->pt == NULL || py->pt == NULL) {
		return builtin_err = Err_null_ref;
	}
	bsize = ptcount * sizeof(int);
	if (Popot_bufsize(px) < bsize || Popot_bufsize(py) < bsize) {
		return builtin_err = Err_index_big;
	}
	return Success;
}

/*****************************************************************************
 * This creates a Poly quickly from Poco array representation.
 * However this poly needs to be disposed with free(p->clipped_list)
 * rather than freeing each point individually.
 ****************************************************************************/
Errcode po_arrays_to_poly(Poly* p, int ptcount, Popot* px, Popot* py)
{
	LLpoint* list;
	int i;
	int* x;
	int* y;
	Errcode err = po_2_arrays_check(ptcount, px, py);

	if (err < Success) {
		return err;
	}
	x = px->pt;
	y = py->pt;
	list = p->clipped_list = begmem(ptcount * sizeof(LLpoint));
	if (list == NULL) {
		return Err_no_memory;
	}
	i = p->pt_count = ptcount;
	while (--i >= 0) {
		list->x = *x++;
		list->y = *y++;
		list++;
	}
	p->polymagic = POLYMAGIC;
	linkup_poly(p);
	return Success;
}

/*****************************************************************************
 * This creates a Poly quickly from Poco array representation.
 * It creates each point individually.
 ****************************************************************************/
Errcode po_arrays_to_ll_poly(Poly* poly, int ptcount, Popot* px, Popot* py)
{
	Poly lpoly;
	Errcode err = po_arrays_to_poly(&lpoly, ptcount, px, py);
	if (err < Success) {
		return err;
	}
	err = clone_ppoints(&lpoly, poly);
	poly->polymagic = POLYMAGIC;
	pj_free(lpoly.clipped_list);
}

/*****************************************************************************
 *
 ****************************************************************************/
static void* get_pic_screen(void)
{
	return vb.pencel;
}

/*****************************************************************************
 *
 ****************************************************************************/
static int get_menu_colors(Pixel** indicies, Rgb3** lastrgbs, Rgb3** idealrgbs)
{
	if (indicies != NULL) {
		*indicies = &vb.screen->mc_colors;
	}
	if (lastrgbs != NULL) {
		*lastrgbs = &vb.screen->mc_lastrgbs;
	}
	if (idealrgbs != NULL) {
		*idealrgbs = vb.screen->mc_ideals;
	}
	return NUM_MUCOLORS;
}

/*****************************************************************************
 *
 ****************************************************************************/
Popot po_ptr2ppt(void* ptr, int bytes)
{
	Popot p = {ptr, ptr, bytes > 0 ? (char*)ptr + bytes - 1 : ptr};
	return p;
}

void* po_ppt2ptr(Popot ppt)
{
	return ppt.pt;
}

/*****************************************************************************
 *
 ****************************************************************************/
Porexlib aa_pocolib = {
	/* header */
	{sizeof(Porexlib), AA_POCOLIB, AA_POCOLIB_VERSION},
	&builtin_err,
	get_pic_screen,
	po_ppt2ptr,
	po_ptr2ppt,
	get_menu_colors,
	po_findpoe,
	po_poe_overtime,
	po_check_abort,
	po_poe_oversegment,
	po_poe_overall,
	&vb,
	&vs,
	{0, 0, 0, 0}, /* reserved1[4] */
	&po_libuser,
	&po_liboptics,
	&po_libswap,
	&po_libscreen,
	&po_libcel,
	&po_libdos,
	&po_libdraw,
	&po_libaafile,
	&po_libmisc,
	&po_libmode,
	&po_libtext,
	&po_libtime,
	&po_libturtle,
	&po_libglobalv,
	&po_libtitle,
	&po_libtween,
	&po_libflicplay,
};
