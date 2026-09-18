# Poco Menu Notes

## src/mainpull.c
- Errcode init_poco_pull(Menuhdr *mh, SHORT prev_id, SHORT root_id)
  - Where the Poco items are found for the main menu

## Menu Execution

Flow when a Poco menu item is selected:
- `run_pull_poco`, `qrun_pocofile`, `load_soft_pull`, `main_selit`

### Execution Flow

1. Menu selection (`src/menus/pull.c`):
   - `menu_dopull()` (line 347) handles selection
   - On selection, calls `mh->dodata` with the selected menu ID (line 362):
   ```c
   if (mh->dodata != NULL) {
       (*((VFUNC)(mh->dodata)))(mh, selid);
   }
   ```

2. Menu setup (`src/quickdat.c`):
   - `load_soft_pull()` (line 315) sets `mh->dodata = main_selit` (set in `smupull.c` line 37)

3. Main selection handler (`src/vpaint.c`):
   - `main_selit()` (line 558) handles menu selections
   - When the hit ID is in the Poco range (`POC_DOT_PUL + 1` to `POC_DOT_PUL + 10`), it calls `run_pull_poco()`:
   ```c
   if (hitid > POC_DOT_PUL && hitid <= POC_DOT_PUL + 10) /* poco call */
   {
       run_pull_poco(mh, hitid);
   }
   ```

4. Run Poco program (`src/mainpull.c`):
   - `run_pull_poco()` (line 138) constructs the file path and calls `qrun_pocofile()`:
   ```c
   Errcode run_pull_poco(Menuhdr *mh, SHORT id)
   {
       char ppath[PATH_SIZE];
       poco_pull_path(mh, id, ppath);  // Builds path like "resource_dir/PROGRAM.POC"
       return qrun_pocofile(ppath, false);
   }
   ```

5. Execute (`src/qpoco.c`):
   - `qrun_pocofile()` (line 344) calls `qrun_poco()` (line 233)
   - `qrun_poco()` compiles the Poco program via the Ani adapter and `poco_vm_compile_file()`, then runs it via `poco_vm_run()`

In summary: `menu_dopull` → `main_selit` → `run_pull_poco` → `qrun_pocofile` → `qrun_poco` → compiles and executes the Poco program.



---

# Rex Dynamic Library System

## Overview

Rex is a **general-purpose dynamic library loading system** used throughout Animator Pro, not just for Poco. It supports multiple plugin types:
- `REX_VDRIVER` (0x0101) - Video/graphics drivers
- `REX_INK` (0x0201) - Drawing ink modules  
- `REX_IDRIVER` (0x0301) - Input drivers
- `REX_PICDRIVER` (0x0401) - Picture format drivers (GIF, JPEG, etc.)
- `REX_POCO` (0x0501) - Poco extensions (POE modules)
- `REX_TRUEPDR` (0x0601) - True picture drivers

**Why Rex is separate from Poco**: Rex is shared infrastructure used by multiple subsystems. Building it into Poco would create unnecessary dependencies and code duplication across video drivers, picture drivers, inks, etc.

## How the Old Rex System Worked (DOS 32-bit)

### File Format (.REX/.EXP)
- Based on Phar Lap/Watcom 32-bit DOS executable format
- Entry point (`rexentry.asm`) contains:
  - Magic numbers (`PJREX_MAGIC`, `PJREX_MAGIC2`) for validation
  - Pointer to `rexlib_header` structure in loaded code
  - Data end marker (`_end`)

### Loading Process (`src/rexlib/rexhost/rexload.c`)
1. **Read .REX header**: Phar Lap format with code offset, data size, relocation table
2. **Allocate memory**: Load code and initialized data into memory
3. **Apply relocations**: Fix 16/32-bit offsets for actual load address
4. **Locate header**: Extract `rexlib_header` pointer from entry point structure

### Library Structure

Each POE module defines a `Pocorex` structure:
```c
typedef struct pocorex {
    Rexlib hdr;      // Rexlib header (type, version, init, cleanup, hostlibs)
    Poco_lib lib;    // Poco library structure (name, Lib_proto array, count)
} Pocorex;
```

**Rexlib header** contains:
- `type`: Must be `REX_POCO` (0x0501)
- `version`: Version check
- `init`: Optional initialization function (called after loading)
- `cleanup`: Optional cleanup function (called before unloading)
- `first_hostlib`: Linked list of `Hostlib` structures requesting host libraries
- `id_string`: Optional identifier string for validation

**Poco_lib structure** contains:
- `name`: Library name
- `lib`: Array of `Lib_proto` structures
- `count`: Number of functions

### Host Library System

POE modules request host libraries via `Hostlib` structures:
```c
// In POE source code:
#define HLIB_TYPE_1 AA_POCOLIB
#define HLIB_TYPE_2 AA_SYSLIB  
#include <hliblist.h>  // Builds linked list automatically
```

During loading (`src/rexlib/rexhost/rexlib.c`):
1. Host receives `Hostlib` list from loaded module
2. Matches requested libraries with host-provided `Libhead` structures
3. Replaces `Hostlib.next` pointers with pointers to host `Libhead` structures
4. Module code can then call host functions through these pointers

**What is passed to libraries:**
- Host library pointers (via `Hostlib.next` fields)
- Program ID and version (to `init()` function)
- Optional user data (to `init()`)

**What is retrieved from libraries:**
- `Rexlib` header (type, version, init/cleanup pointers)
- `Poco_lib` structure containing:
  - `func`: Function pointer (used at runtime)
  - `proto`: Prototype string (used during compilation)

## Where `#pragma poco library` is Handled

### Location
- **Parser**: `poco/src/pp.c`, function `pp_pragma()` (lines 1070-1196)
- **Case 3** (line 1149): Handles `#pragma poco library "filename.poe"`

### Flow
1. Parser recognizes `#pragma poco library "filename.poe"`
2. Resolves file path via include directories (line 1157)
3. Calls `po_open_library()` (`poco/src/libproto.c`)
4. `po_open_library()` calls `pj_load_pocorex()` (`poco/src/pocoload.c`)
5. `pj_load_pocorex()` calls `pj_rexlib_load()` which:
   - Loads binary via `pj_rex_load()`
   - Verifies type and version
   - Resolves host libraries
   - Calls `pj_rexlib_init()` for initialization
6. Returns `Poco_lib*` added to `pcb->run.loaded_libs`
7. Preprocessor treats library as file stack entry (line 1185)

### During Compilation
- `po_get_libproto_line()` (`libproto.c`) returns prototype strings from loaded libraries
- Each prototype pairs with function pointer (`pcb->libfunc`)
- `func_proto()` (`declare.c:385`) associates function pointer with prototype during parsing

## Modern Rex System Design

### Challenges with Old System
1. **DOS executable format**: `.REX` files are DOS 32-bit executables, not portable
2. **Manual relocation**: Custom relocation code is OS/architecture-specific
3. **Binary format**: Tightly coupled to Phar Lap/Watcom toolchain

### Recommended Approach: Platform-Native Shared Libraries

Use OS-native shared libraries:
- **Linux**: `.so` files via `dlopen()`/`dlsym()`
- **macOS**: `.dylib` files via `dlopen()`/`dlsym()`
- **Windows**: `.dll` files via `LoadLibrary()`/`GetProcAddress()`

### Implementation Steps

1. **Library Structure** (keep same):
   ```c
   // Export entry function
   Pocorex* poco_rexlib_get(void) {
       return &rexlib_header;
   }
   ```

2. **Host Loading** (`poco/src/pocoload.c`):
   ```c
   Errcode pj_load_pocorex_modern(Poco_lib **lib, char *name, char *id_string) {
       void *handle = dlopen(name, RTLD_LAZY);
       Pocorex* (*get_rexlib)(void) = dlsym(handle, "poco_rexlib_get");
       Pocorex *exe = get_rexlib();
       // ... rest of initialization same as before
   }
   ```

3. **Build System**:
   ```bash
   # Build as shared library
   gcc -shared -fPIC -o pstamp.poe pstamp.c -lpoco
   ```

### Benefits
- Works with standard C toolchains
- OS handles linking and relocation automatically
- Standardized binary format per platform
- Easier debugging and maintenance

### What to Keep
- `Pocorex`/`Rexlib`/`Hostlib` structures (they're portable)
- `#pragma poco library` interface (already works)
- Host library resolution system
- Version checking and initialization protocol

## Key Files

- **Rex loader**: `src/rexlib/rexhost/rexload.c` - Low-level binary loading
- **Rex library manager**: `src/rexlib/rexhost/rexlib.c` - High-level loading and hostlib resolution
- **Module loader**: `poco/src/pocoload.c` - the one .poe loader; Animator adds only its resource directory, through `poco_vm_add_library_path()`
- **Pragma handler**: `poco/src/pp.c` - Parses `#pragma poco library`
- **Library opening**: `poco/src/libproto.c` - `po_open_library()` function
- **Header definitions**: `src/rexlib/inc/rexlib.h`, `src/rexlib/inc/pocorex.h`
