#include "undo_redo.h"

#include <flx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "../pj_sdl/pj_sdl.h"
#include "auto.h"
#include "broadcas.h"
#include "errcodes.h"
#include "fli.h"
#include "jimk.h"
#include "memory.h"
#include "palchunk.h"
#include "pentools.h"
#include "pjbasics.h"
#include "rastcall.h"
#include "raster.h"
#include "rectang.h"
#include "xfile.h"
#include "zoom.h"

/* External function declarations */
extern int flx_get_frames(void);
#define Err_no_rcel (Errcode)(-80)

/* Globals */
static UndoSystem undo_system;

/* ======================================================================
 * Forward declarations
 * ====================================================================== */

static char* undo_get_full_path_for_push(const UndoType type, const char* file_name);


/* ======================================================================
 * Helper functions
 * ====================================================================== */

static inline USHORT flx_get_frame_count()
{
	return flix.hdr.frame_count;
}

static inline bool settings_apply_multi_frames()
{
	return vs.multi == 1;
}

static inline bool settings_apply_fill()
{
	return vs.fillp == 1;
}

static inline bool settings_use_key_color()
{
	return vs.zero_clear == 1;
}

static const char* undo_get_prefix_for_type(const UndoType type)
{
	switch (type) {
		case UNDO_DRAW:
			return "draw";
		case UNDO_FRAME:
			return "frame";
		case UNDO_RCEL:
			return "rcel";
		case UNDO_PALETTE:
			return "palette";
		case UNDO_POSITION:
			return "position";
		case UNDO_COMPOSITE:
			return "composite";
		case UNDO_TIME_SEGMENT:
			return "time";
		default:
			return NULL;
	}
}

static const char* undo_get_extension_for_type(const UndoType type)
{
	switch (type) {
		case UNDO_DRAW:
		case UNDO_FRAME:
		case UNDO_RCEL:
		case UNDO_COMPOSITE:
			return "cel";

		case UNDO_PALETTE:
			return "col";

		case UNDO_POSITION:
			return "rect";

		case UNDO_TIME_SEGMENT:
			return "flc";
		default:
			return NULL;
	}
}

static void undo_clear_undo_cell()
{
	if (undo_system.undo_cel) {
		pj_close_raster(undo_system.undo_cel);
		undo_system.undo_cel = NULL;
	}
}

/* Generate a unique filename for undo operations */
static const char* undo_generate_filename_generic(const UndoType type, const char* undo_type_name)
{
	static char file_name[64];

	static int counter = 0;

	int64_t timestamp = time(NULL);

	snprintf(file_name, 64, "%s_%lld_%03d.%s.%s", undo_get_prefix_for_type(type), timestamp,
			 counter % 1000, undo_type_name, undo_get_extension_for_type(type));

	counter += 1;

	return file_name;
}

static const char* undo_generate_filename(const UndoType type)
{
	return undo_generate_filename_generic(type, "undo");
}

static const char* redo_generate_filename(const UndoType type)
{
	return undo_generate_filename_generic(type, "redo");
}

/* Create a new UndoNode */
static UndoNode* undo_create_node(UndoType type, const char* filename)
{
	UndoNode* node = calloc(1, sizeof(UndoNode));
	if (node == NULL) {
		return NULL;
	}

	node->type = type;
	strncpy(node->filename, filename, sizeof(node->filename) - 1);
	node->filename[sizeof(node->filename) - 1] = '\0';

	//!TODO: handle time better
	node->start_frame = node->end_frame = vs.frame_ix;

	return node;
}

/* Free an UndoNode and its resources */
static void undo_free_node(UndoNode* node)
{
	if (node) {
		char full_path[PATH_MAX];

		/* Build the full path to the undo file */
		snprintf(full_path, sizeof(full_path), "%s/%s", undo_system.undo_dir,
				 node->filename);

		/* Remove the undo file */
		pj_delete(full_path);

		/* Free the node */
		free(node);
	}
}

static void undo_clear_redo_stack()
{
	while (undo_system.redo_head != NULL) {
		UndoNode* temp = undo_system.redo_head;
		undo_system.redo_head = temp->next;

		undo_free_node(temp);
	}

	undo_system.redo_count = 0;
}

static void undo_clear_undo_stack()
{
	while (undo_system.undo_head != NULL) {
		UndoNode* temp = undo_system.undo_head;
		undo_system.undo_head = temp->next;

		undo_free_node(temp);
	}

	undo_system.undo_count = 0;
}

/* Add a node to the undo stack */
static Errcode undo_push_node(UndoNode* node)
{
	/* Add the new node to the front of the undo stack */
	node->next = undo_system.undo_head;

	undo_system.undo_head = node;
	undo_system.undo_count++;

	LOG("Pushed %s (undo index %llu)", node->filename, undo_system.undo_count);

	undo_redo_redraw_buttons();
	return Success;
}

/* Add a node to the redo stack */
static Errcode redo_push_node(UndoNode* node)
{
	/* Add the new node to the front of the undo stack */
	node->next = undo_system.redo_head;
	undo_system.redo_head = node;
	undo_system.redo_count++;

	LOG("Pushed %s (redo index %llu)", node->filename, undo_system.redo_count);

	undo_redo_redraw_buttons();
	return Success;
}

/* Initialize the undo system */
Errcode undo_init(void)
{
	/* Initialize the undo system structure */
	memset(&undo_system, 0, sizeof(UndoSystem));

	/* Set up the undo directory in the user's preferences directory */
	snprintf(undo_system.undo_dir, PATH_MAX, "%s/undo", pj_sdl_preferences_path());

	/* Create the undo directory if it doesn't exist */
	if (!pj_folder_exists(undo_system.undo_dir)) {
		if (mkdir(undo_system.undo_dir, 0755) != 0) {
			return Err_undo_create_dir;
		}
	}

	return Success;
}

/* Clear all undo/redo history */
void undo_clear_all(void)
{
	undo_clear_redo_stack();
	undo_clear_undo_stack();

	// !TODO: clear all files in the undo directory?
}

/* Check if undo is available */
bool undo_available(void)
{
	return undo_system.undo_head != NULL;
}

/* Check if redo is available */
bool redo_available(void)
{
	return undo_system.redo_head != NULL;
}

/*
 * Restoration functions
 */

/* Restore the entire cel and palette from the undo file */
Errcode undo_restore_from_cel(const UndoNode* node)
{
	Rasthdr rastspec;
	Errcode err;

	copy_rasthdr(vb.pencel, &rastspec);
	rastspec.width = vb.pencel->width;
	rastspec.height = vb.pencel->height;
	err = alloc_pencel(&undo_system.undo_cel);
	if (err < Success) {
		LOG("Failed bytemap alloc.");
		return err;
	}

	const char* file_name = undo_get_full_path_for_push(node->type, node->filename);
	err = load_pic(file_name, undo_system.undo_cel, 0, true);
	if (err < Success) {
		LOG("Unable to load undo cel.");
		undo_clear_undo_cell();
		return err;
	}

	// make sure we're on the right frame for the undo or it'll write
	// the undo data to the current frame and possibly break stuff

	if (!node->start_frame == vs.frame_ix) {
		vs.frame_ix = node->start_frame;
		scrub_cur_frame();
	}

	pj_blitrect(undo_system.undo_cel, 0, 0, vb.pencel, 0, 0, vb.pencel->width, vb.pencel->height);

	if (!cmaps_same(undo_system.undo_cel, vb.pencel->cmap)) {
		see_cmap();
		do_color_redraw(NEW_CMAP);
	}

	zoom_it();
	dirties();
	flx_draw_olays();

	undo_clear_undo_cell();

	return Success;
}

/* Perform an undo operation */
Errcode undo_perform(void)
{
	if (!undo_available()) {
		return Err_undo_no_operation;
	}

	Errcode err = Success;
	char full_path[PATH_MAX];

	/* Get the current undo operation */
	UndoNode* node = undo_system.undo_head;

	/* Remove the node from the undo stack */
	undo_system.undo_head = node->next;
	undo_system.undo_count--;

	/* Create redo node */
	redo_push(node->type);

	/* Build the full path to the undo file */
	snprintf(full_path, sizeof(full_path), "%s/%s", undo_system.undo_dir, node->filename);

	/* Peform undo */
	switch (node->type) {
		case UNDO_DRAW:
		case UNDO_FRAME:
		case UNDO_RCEL:
		case UNDO_COMPOSITE:
			err = undo_restore_from_cel(node);
			break;

		case UNDO_PALETTE:
			break;

		case UNDO_POSITION:
			break;

		case UNDO_TIME_SEGMENT:
			break;

		default:
			err = Err_undo_invalid_type;
			break;
	}

	if (err < Success) {
		redo_pop();
		return err;
	}

	LOG("Undo index now: %d", undo_system.undo_count);
	undo_free_node(node);

	return Success;
}

/* Perform a redo operation */
Errcode redo_perform(void)
{
	if (!redo_available()) {
		return Err_undo_no_operation;
	}

	Errcode err = Success;
	char full_path[PATH_MAX];

	/* Get the current undo operation */
	UndoNode* node = undo_system.redo_head;

	/* Remove the node from the undo stack */
	undo_system.redo_head = node->next;
	undo_system.redo_count--;

	/* Save the current state before applying the redo */
	/* Create undo node */
	undo_push(node->type);

	/* Build the full path to the undo file */
	snprintf(full_path, sizeof(full_path), "%s/%s", undo_system.undo_dir, node->filename);

	/* Peform redo */
	switch (node->type) {
		case UNDO_DRAW:
		case UNDO_FRAME:
		case UNDO_RCEL:
		case UNDO_COMPOSITE:
			err = undo_restore_from_cel(node);
		break;

		case UNDO_PALETTE:
			break;

		case UNDO_POSITION:
			break;

		case UNDO_TIME_SEGMENT:
			break;

		default:
			err = Err_undo_invalid_type;
		break;
	}

	if (err < Success) {
		undo_pop();
		return err;
	}

	undo_free_node(node);
	return Success;
}

/*
 * Undo Pushes
 *
 * The Undo system may save different things depending on the
 * type of operation and on whether time-based editing is
 * active.
 */

static char* undo_get_full_path_for_push(const UndoType type, const char* file_name)
{
	static char full_path[PATH_MAX];

	snprintf(full_path, PATH_MAX, "%s/%s", undo_system.undo_dir, file_name);

	return full_path;
}

/* Persist the current vb.pencel to temp folder on disk-- used for undo and redo. */
static UndoNode* undo_save_cel(const UndoType type, const char* file_name)
{
	char* full_name = undo_get_full_path_for_push(type, file_name);

	Errcode result = save_pic(full_name, vb.pencel, 0, true);

	if (result < Success) {
		LOG("Unable to save undo cel to %s", full_name);
		return NULL;
	}

	UndoNode* node = undo_create_node(type, file_name);
	if (node == NULL) {
		LOG("Unable to allocate undo node");
		return NULL;
	}

	return node;
}

static Errcode undo_push_cel(const UndoType type)
{
	const char* file_name = undo_generate_filename(type);
	UndoNode* node = undo_save_cel(type, file_name);
	if (!node) {
		return Err_undo_no_node;
	}
	return undo_push_node(node);
}

static Errcode redo_push_cel(const UndoType type)
{
	const char* file_name = redo_generate_filename(type);
	UndoNode* node = undo_save_cel(type, file_name);
	if (!node) {
		return Err_undo_no_node;
	}
	return redo_push_node(node);
}

static Errcode undo_push_palette()
{
	return Success;
}

static Errcode redo_push_palette()
{
	return Success;
}

static Errcode undo_push_rect()
{
	return Success;
}

static Errcode redo_push_rect()
{
	return Success;
}

static Errcode undo_push_flic()
{
	return Success;
}

static Errcode redo_push_flic()
{
	return Success;
}

Errcode undo_push(const UndoType type)
{
	LOG(undo_get_prefix_for_type(type));

	switch (type) {
		case UNDO_DRAW:
		case UNDO_FRAME:
		case UNDO_RCEL:
		case UNDO_COMPOSITE:
			if (undo_push_cel(type) != Success) {
				return Err_undo_save_state;
			};
			break;

		case UNDO_PALETTE:
			if (undo_push_palette() != Success) {
				return Err_undo_save_state;
			};
			break;

		case UNDO_POSITION:
			if (undo_push_rect() != Success) {
				return Err_undo_save_state;
			};
			break;

		case UNDO_TIME_SEGMENT:
			if (undo_push_flic() != Success) {
				return Err_undo_save_state;
			};
			break;

		default:
			return Err_undo_invalid_type;
	}

	return Success;
}


Errcode redo_push(const UndoType type)
{
	LOG(undo_get_prefix_for_type(type));

	switch (type) {
		case UNDO_DRAW:
		case UNDO_FRAME:
		case UNDO_RCEL:
		case UNDO_COMPOSITE:
			if (redo_push_cel(type) != Success) {
				return Err_undo_save_state;
			};
			break;

		case UNDO_PALETTE:
			if (redo_push_palette() != Success) {
				return Err_undo_save_state;
			};
			break;

		case UNDO_POSITION:
			if (redo_push_rect() != Success) {
				return Err_undo_save_state;
			};
			break;

		case UNDO_TIME_SEGMENT:
			if (redo_push_flic() != Success) {
				return Err_undo_save_state;
			};
			break;

		default:
			return Err_undo_invalid_type;
	}

	return Success;
}

void undo_pop()
{
	if (!undo_available()) {
		return;
	}

	UndoNode* node = undo_system.undo_head;
	undo_system.undo_head = node->next;
	undo_free_node(node);
}

void redo_pop()
{
	if (!redo_available()) {
		return;
	}

	UndoNode* node = undo_system.redo_head;
	undo_system.redo_head = node->next;
	undo_free_node(node);
}

