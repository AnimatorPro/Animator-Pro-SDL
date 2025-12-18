#ifndef UNDO_REDO_H
#define UNDO_REDO_H

#include "errcodes.h"
#include "rcel.h"
#include "stdtypes.h"
#include "flicel.h"
#include "rcel.h"
#include <stdbool.h>
#include <limits.h>
#include <time.h>


#ifndef LOG
#define LOG(...) fprintf(stderr, "[%s] ", __func__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); fflush(stderr);
#endif


/* Types of operations that can be undone/redone */
typedef enum {
    UNDO_DRAW,           /* Drawing operations */
    UNDO_FRAME,          /* Frame operations */
    UNDO_RCEL,           /* Rcel operations */
    UNDO_PALETTE,        /* Palette operations */
    UNDO_POSITION,       /* Position operations */
    UNDO_COMPOSITE,      /* Combined operations (e.g., picture + palette) */
    UNDO_TIME_SEGMENT    /* Time-based operations affecting multiple frames */
} UndoType;

/* Node in the undo/redo stack */
typedef struct UndoNode {
    char filename[64];   /* Name of file containing the data */
    struct UndoNode* next;
    SHORT start_frame;   /* Starting frame of affected time segment */
    SHORT end_frame;     /* Ending frame of affected time segment */
    UndoType type;       /* Type of undo operation */
} UndoNode;

/* Main undo system structure */
typedef struct {
    char undo_dir[PATH_MAX]; /* Directory for undo files */
    UndoNode* undo_head;     /* Most recent undo operation */
    UndoNode* redo_head;     /* Most recent redo operation */
    uint64_t undo_count;     /* Number of operations in undo stack */
    uint64_t redo_count;     /* Number of operations in redo stack */

    Rcel* undo_cel;          /* temp space to load the cel for copying */
    Cmap* undo_cmap;         /* temp space to load the palette for copying */
} UndoSystem;

/* Initialization and Cleanup */

// Initialize the undo system
Errcode undo_init(void);

// Clean up the undo system
void undo_cleanup(void);

// Reset the undo system when loading a new file
Errcode undo_reset(void);

// Persist undo data to disk
Errcode undo_stack_save_to_disk();

// Load undo data from disk
Errcode undo_stack_load_from_disk();

/* Undo/Redo Operations */

// Create a new undo / redo state in memory and on disk
Errcode undo_push(const UndoType type);
Errcode redo_push(const UndoType type);

// Perform an undo / redo operation
Errcode undo_perform(void);
Errcode redo_perform(void);

// Check if undo / redo is available
bool undo_available(void);
bool redo_available(void);

// pop an operation without adding it to the other stack
// useful for error handling
void undo_pop(void);
void redo_pop(void);

// from quickdat.c
void undo_redo_redraw_buttons(void);

#endif /* UNDO_REDO_H */
