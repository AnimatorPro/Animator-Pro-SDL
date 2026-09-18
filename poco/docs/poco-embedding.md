# Poco embedding and Animator migration

## Audience and boundary

This guide is for external Poco hosts, native-module authors, and Animator
maintainers.  The supported external boundary is intentionally small:

| Need | Supported surface |
|---|---|
| C header | `<poco/poco.h>` |
| CMake target | `Poco::poco` |
| Source integration | `add_subdirectory(poco)` |
| Installed integration | `find_package(Poco CONFIG REQUIRED)` |
| Generic native modules | `poco_add_module()` and `PocoModuleDescriptor` |

No external host should include `pocoface.h`, `pocolib.h`, `pocorex.h`, or an
Animator `src/inc` header.  Those files live in `poco/src/` and are private to
the implementation: they exist only for bounded legacy source compatibility and
are not a second public API.  `poco/include/` holds exactly one installed
header, `poco/poco.h`.

## External host quick start

Configure Poco as either a source subproject or installed CMake package, then
link only `Poco::poco`:

```cmake
# Choose one source of Poco.
add_subdirectory(path/to/poco)
# find_package(Poco CONFIG REQUIRED)

add_executable(my_host main.c)
target_link_libraries(my_host PRIVATE Poco::poco)
```

An `add_subdirectory(poco)` host gets the library and the `poco` CLI only:
`POCO_BUILD_TESTS` defaults to OFF unless Poco is the top-level project, and
the `examples/` targets are built only when `POCO_BUILD_EXAMPLES` is on.

Include only `<poco/poco.h>`.  Create a VM, register the libraries the host
wants to expose, compile a script, run it, then destroy its program before
destroying the VM.  The `poco/test/consumer/main.c` fixture is a runnable
minimal example.

```c
PocoVm *vm = NULL;
PocoProgram *program = NULL;
PocoStatus status = poco_vm_create(NULL, &vm);

if (status == POCO_STATUS_OK)
	status = poco_vm_register_standard_library(vm);
if (status == POCO_STATUS_OK)
	status = poco_vm_register_library(vm, &my_library);
if (status == POCO_STATUS_OK)
	status = poco_vm_compile_file(vm, script_path, &program);
if (status == POCO_STATUS_OK)
	status = poco_vm_run(vm, program, NULL, &result);

poco_program_destroy(program);
poco_vm_destroy(vm);
```

Libraries are explicit.  `poco_vm_register_standard_library()` enables the
host-neutral standard catalog; it does not install Animator libraries.  Poco
copies a registered `PocoLibrary`, its prototype strings, and its contracts
into the VM, and a program receives a private library snapshot when compiled.
The native functions and lifecycle callbacks themselves must remain valid
while that VM/program can use them.  The compiler is not concurrently
re-entrant, so serialize compile and run operations for a VM.

Use `PocoVmOptions` for include paths, diagnostics, verbosity, and module
policy.  Use `PocoRunOptions` for cancellation and tracing.  Diagnostics
include a Poco-owned `PocoStatus`; hosts should map that status to their own
error domain instead of sharing Animator's `Errcode` values.

## Multi-source linking

Poco can link several `.poc` files into one program through either an explicit
source list or source-owned `use` directives.  An explicit list is useful when
the build system owns the program composition:

```sh
poco -c main.poc library.poc -o app.pex
poco main.poc library.poc
```

Embedding hosts use the equivalent `poco_vm_compile_files()` API.  List order
is retained for unit identity and global initialization.  Each listed source
is compiled as a separate translation unit: a caller must see declarations for
functions and globals defined in another listed file, normally through a
shared header.  A function prototype and an `extern` global declaration are
enough; definitions remain in their owning `.poc` file.

A source can instead own its dependencies:

```c
#pragma poco use "library.poc"

int main(void)
{
	return library_add(35);
}
```

Quoted and angle-delimited paths are supported.  Poco searches relative to the
using file first and then through the VM include paths, follows dependencies
transitively, initializes dependencies before their users, and compiles each
canonical file once.  A file that is both `use`d and listed on the command line
is therefore not duplicated, while a `use` cycle is an error.

Unlike a bare source list, `use` automatically makes the immediate dependency's
non-`static` function signatures and globals visible to the using source, so a
header is optional.  A header may still document that interface: declarations
that match the used definitions are accepted, and mismatched declarations are
diagnosed rather than silently changing the linked type.

All linked units share one flat external namespace.  There may be only one
non-`static` definition of a name across functions and globals; duplicate
functions, duplicate globals, and function/global name collisions are errors.
File-scope `static` functions and globals remain private to their own unit, so
different files may use the same private name without colliding.

A runnable program—and a persisted `.pex` written with `-o`—must define exactly
one `main()`.  A runnable program with no `main()` and any link with multiple
`main()` definitions are rejected.  A bare compile check such as
`poco -c library.poc` intentionally accepts library-only sources with no
`main()`.

Persisted multi-source programs retain a name and content hash for every source
plus file-indexed line information.  Extended debug images (`-g`) also retain
the resolvable paths.  The CLI debugger selects and hash-verifies the source
belonging to each stopped frame, including after stepping into another file;
editing any contributing source makes that debug image fail verification.

## Native bindings and FFI rules

A binding is a Poco prototype string paired with the exact corresponding C
function.  libffi validates the descriptor before it becomes callable and
rejects null functions, unsupported types, duplicate names/addresses, or an
invalid variadic declaration with `POCO_STATUS_FFI_INVALID_BINDING`.

| Prototype component | Supported fixed-call representation |
|---|---|
| Integers | `int`, `long` |
| Floating point | `double` |
| Pointers | a C pointer; Poco script pointers retain `Popot` bounds until final FFI conversion |
| Result | `int`, `long`, `double`, a pointer, or `void` |

`PocoBindingContract` is optional but preferred whenever a native operation's
pointer span or pointer-return ownership is known.  It can require readable or
writable byte/C-string spans and can describe alias, host-borrowed, or
VM-owned pointer results.  A two-field binding initializer remains a trusted
legacy binding; it is not sandboxed once native code receives a raw pointer.

For C variadics, write a normal prototype with at least one fixed argument
before `...`.  Poco owns dynamic metadata for exactly the values passed after
the ellipsis—there are no historical count/byte-size arguments.  Callers must
meet C default promotions: character/short values become `int`, floats become
`double`, and `int`, `long`, `double`, and pointers are supported.  `void`,
unpromoted narrow integers, and unpromoted floats are rejected before native
entry.  A binding implementation's C return type must match the Poco prototype
and its libffi representation exactly; do not use C `bool` for a prototype
whose Poco `Boolean` representation is `int`.

The opt-in standard catalog provides real console, string, managed-memory,
safe-file, math, and portable-path bindings.  It intentionally excludes
Animator and interactive CLI behavior such as `Qtext`.  Prefer `snprintf` to
legacy `sprintf` when a destination capacity is known.

## Native modules

### Generic Poco modules

Generic modules must use `PocoModuleDescriptor`, export
`POCO_MODULE_ENTRY_POINT` (`poco_module_get`), include `<poco/poco.h>`, and
build with the installed `poco_add_module()` helper:

```cmake
find_package(Poco CONFIG REQUIRED)

poco_add_module(my_module
    SOURCES my_module.c
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include
    DEPS my_optional_dependency
    INSTALL_DIR modules)
```

The descriptor contributes exactly one `PocoLibrary`.  Its initialization can
report a diagnostic or request a host-defined, versioned service through
`PocoModuleHost`; it has no Animator headers, globals, resource structures, or
direct function tables.  `poco_add_module()` links only `Poco::poco` plus the
dependencies named by the caller and creates a `.poe` shared module.

The public helper does not prescribe an RPATH or installation layout.  An
external host must install a generic module where its scripts look for it and
ensure the platform loader can locate `libpoco`.  That is intentional: only
the consuming application knows its deployment layout.

### Animator-native compatibility modules

Animator-native modules are different products, not a generic-module option.
They use `ani_add_poe_library()` from
`src/ani_poco/AniPocoPoeLibrary.cmake`, which supplies Animator's include
directory, `animhost`, graphics/raster libraries, resource destination, and
relocatable RPATHs.  `LEGACY_POE` is required only for a retained direct-table
module; it links the Ani adapter and requires the explicit Ani policy host.

`add_poe_library()` is an in-tree generic helper.  It shares the `.poe` file
format with the Animator helper but supplies only `Poco::poco` and
caller-named dependencies.  It is not installed as the external module API;
use `poco_add_module()` for installed-package consumers.

The default Poco loader accepts generic descriptors.  It rejects legacy
`poco_rexlib_get()`/`Pocorex` modules unless an Animator-owned host opts in via
`PocoModuleHooks.allow_legacy_poe` and an `on_load` policy installs the
Animator table.  The opt-in never gives generic modules Animator symbols.

## Artifact and platform behavior

`poco_core` is built as a static archive by default and as a shared library
when `POCO_BUILD_SHARED=ON` (`libpoco` on Unix/macOS, the platform-equivalent
shared library on Windows).  Linkage follows that dedicated switch and not a
parent `BUILD_SHARED_LIBS`, so an embedding project's global choice cannot
silently flip it.  The static archive is built position-independent so it can
be linked into shared hosts such as frameworks and plugins.

Poco's libffi, hashmap, and blake3 dependencies are Poco-private: they are
linked privately, wrapped in `BUILD_INTERFACE`, and neither installed nor
exported.  The package export contains only `Poco::poco`, the canonical header,
CMake package files, and `PocoModule.cmake`.  Do not name those private targets
or link their archives directly.

On macOS, the in-tree generic helper installs modules with
`@loader_path/../lib`; the Animator helper adds `@loader_path/..` for Animator
libraries as well.  On Unix platforms it uses `$ORIGIN/../lib` and
`$ORIGIN/..`, respectively.  The public helper leaves RPATH ownership to the
embedding project.  On Windows, package linking resolves the import library at
build time, while deployment must put the Poco DLL and required host/Animator
DLLs on the normal DLL search path (commonly beside the executable/module or
on `PATH`).

## Migration ledger for Animator maintainers

The following layers are retained only while existing Animator code and
supported POE modules need them.  New work must use the replacement column.

| Retained or removed layer | Current status and replacement | Removal condition |
|---|---|---|
| `compile_poco()`, `run_poco()`, `free_poco()` | Compatibility-only compiler lifecycle. Use `PocoVm`, `PocoProgram`, `poco_vm_compile_file()`, `poco_vm_run()`, and `poco_program_destroy()`. | All retained Animator runner/caller paths use the Ani adapter and public VM lifecycle; the legacy-header compile fixture can be intentionally retired in a separately approved ABI-breaking change. |
| `Poco_lib`, `Lib_proto`, and legacy Poco headers | One Poco-owned compatibility ABI layout remains in the private `poco/src/` headers; do not copy it. Use `PocoLibrary` and `PocoBinding` for all new registration. | The Ani adapter no longer needs to convert retained Animator tables and no supported legacy source caller includes the compatibility headers. |
| `Pocorex` / `poco_rexlib_get()` | Animator native-POE compatibility format only. Migrate portable modules to `PocoModuleDescriptor`; use `ani_add_poe_library(... LEGACY_POE)` only while an Animator-specific direct table is unavoidable. | No supported module exports `poco_rexlib_get()` or relies on direct `Pocorex` layout; then remove the legacy loader policy, fixtures, and headers together. |
| `Polib*`, `PolibUser`, `_plptr`, `_a_a_pocolib` | Animator-only direct native-POE function-table ABI, not Poco/libffi dispatch. Generic modules must never include or receive it. | All retained Animator-native modules have moved to descriptor/service APIs or have been explicitly retired; no module needs the Ani policy to install direct tables. |
| Dummy binding catalog / `dummy_libfunc` | Removed. Standalone Poco now reports Animator-only names as undefined. | No removal work remains; do not reintroduce placeholder functions. Register a real host binding or run the script through Animator. |
| `poco_cont_ops(..., arglength, ...)` | Removed. Existing legacy code-pointer callbacks use typed `PocoCallbackValue` plus `poco_invoke_callback()`; new hosts should use the VM/program API instead. | All compatibility callback users have migrated away from compiler-code pointers; then remove the typed callback compatibility surface in a coordinated ABI change. |
| DOS-era Rex/Poekit batch and include-list metadata | Removed and unsupported. Current module builds use `poco_add_module()`, `add_poe_library()`, or `ani_add_poe_library()`. | No removal work remains; do not restore the artifacts without a new supported build target and compatibility plan. |

Animator's normal runner is already on the public VM lifecycle through
`ani_poco_adapter`.  The adapter alone owns Animator registration,
resource-dependent bindings, diagnostics policy, and retained legacy-POE table
installation.  Keep Poco core host-neutral: it must not regain Animator
headers, source reach-through, symbol probes, or a fallback table installer.
