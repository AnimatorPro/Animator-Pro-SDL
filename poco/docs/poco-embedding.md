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

An `add_subdirectory(poco)` host gets the library and the CLI only:
`POCO_BUILD_TESTS` defaults to OFF unless Poco is the top-level project, and
`POCO_BUILD_EXAMPLES` defaults to OFF for everyone -- including a host that
already defines `SDL3::SDL3` -- so the `examples/` targets appear only when the
host asks for them.

Poco claims no generic CMake target names.  The library target is `poco_core`
(aliased `Poco::poco`) and the command-line interpreter is `poco_cli`; a host
remains free to define its own `poco` target.  Both still produce files named
`poco`.  `POCO_INSTALL_CLI` defaults to ON only when Poco is the top-level
project, so `cmake --install` on an embedding host does not deposit the
interpreter in the host's prefix; when it is installed it goes to
`${CMAKE_INSTALL_BINDIR}`, never the prefix root.

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

## Include paths

Three entry points configure the directories an `#include` or a `#pragma poco
use` searches after the using file's own directory.  `PocoVmOptions.include_paths`
seeds the list at `poco_vm_create()`.  `poco_vm_set_include_paths()` replaces
the whole list, discarding anything seeded or appended earlier; passing NULL
with a count of zero clears it.  `poco_vm_add_include_path()` appends one
directory after the existing entries, which is what a host extending a list it
did not build wants, and mirrors `poco_vm_add_library_path()` for `.poe`
modules.  Directories are searched in list order, and each may be written with
or without a trailing separator.

Include paths are read during compilation, so adding one after a program has
compiled is accepted and affects later compiles only.  This differs from
`poco_vm_register_untrusted_expression_library()`, which refuses once a program
exists because it would retroactively change that program's capabilities; an
include path changes nothing about an already-compiled image.

The command line exposes the same list through `-I <dir>`, `--include <dir>`,
and `--include=<dir>`.  The flag is repeatable and accumulates in the order
given:

```sh
poco -c -I engine/include -I game/include main.poc -o game.pex
```

A header present in more than one of those directories resolves to the first
one named, and the source file's own directory is still searched first.

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

Struct, union and enum tags are not part of that flat namespace: each unit
mints its own layout record for every tag it defines, and two units including
one shared header are the ordinary case.  Two units that give the same tag
*different* layouts are an error, reported at the close of the second unit and
naming the tag and both units.  Bodies are compared member by member—member
types and order, and the struct/union/enum kind—rather than by size, so two
eight-byte bodies with different member types are still caught; member *names*
carry no layout and may differ.  Poco can diagnose this only because it
compiles every unit in one process and keeps all the layouts; a C toolchain
sees one unit at a time and cannot.  Two exceptions are worth knowing: a tag
declared but never defined in a unit (`struct vec;`) says nothing that could
disagree, and an enum records no members, so two enums sharing a tag with
different enumerator lists are accepted—only an enum against a struct or union
of the same name is diagnosed.  Redefinition within one unit remains a
separate, pre-existing error.

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

### Declaring a host-provided native

A prototype only becomes a native call site when the compiler is told the host
provides it.  An ordinary `#include`d prototype does not: it compiles to a Poco
function nobody ever defined.  Declaring the function inside a `#pragma poco
native` region says the host provides it, which is what the compiler needs when
the VM that will run the program is not the process compiling it — the
standalone `poco -c` compiler, above all, since it cannot register an
application's binding tables.

```c
#ifdef __POCO__
#pragma poco native begin
#endif
float sys_time(void);
void  sys_log(char *text);
#ifdef __POCO__
#pragma poco native end
#endif
```

The `#ifdef __POCO__` guard matters: `-Wunknown-pragmas` is part of `-Wall` for
both GCC and Clang, so an unguarded pragma warns in the C build that shares the
header.  `__POCO__` is predefined by the Poco compiler.  Every declaration
between `begin` and `end` is host-provided; the region does not nest, does not
continue into an `#include`d file, and must be closed before the file it opened
in ends.

Such a declaration compiles to the same native frame a registered library's
prototype produces, minus the address.  The declared prototype is what argument
checking uses, so calling one with the wrong argument count or types is a
compile error.  A function that is neither defined nor declared this way
remains the error it has always been.

Resolution happens **by name, at load**, against the bindings the loading VM has
registered.  It is the same path a serialized native from a registered library
takes: an image naming a function the host does not provide is refused with
`POCO_STATUS_FFI_FUNCTION_NOT_FOUND` before anything runs, and a program that is
compiled and run in one process fails the same way at the call.  There is **no
signature check** at load — the name is the whole contract — so keeping a
shared header and a host's binding table in agreement, and versioning them when
they change, is the host's responsibility.  A host that wants to close that gap
itself can read the declared signature and compare it before running anything;
see [Reading a function's signature](#reading-a-functions-signature).

Binding contracts and `POCO_BINDING_RUN_CONTEXT` live on the host's binding
entry, not on the declaration, and still apply when a call reaches the binding
this way: a contracted native stays bounds-checked.  A function that a
registered library also supplies is bound at compile time as before, and yields
one frame either way.

The opt-in standard catalog provides real console, string, managed-memory,
safe-file, math, and portable-path bindings.  It intentionally excludes
Animator and interactive CLI behavior such as `Qtext`.  Prefer `snprintf` to
legacy `sprintf` when a destination capacity is known.

## Reading a function's signature

A host that loads a `.pex` it did not compile can check an entry point's shape
before calling it:

```c
PocoFunctionSignature signature;
PocoTypeDesc type;
const char* parameter_name;

if (poco_activation_function_signature(activation, "on_tick", &signature) != POCO_STATUS_OK ||
    signature.parameter_count != 1) {
    return reject("script does not provide on_tick(state)");
}
poco_activation_function_parameter(activation, "on_tick", 0, &parameter_name, &type);
if (!type.is_struct || type.struct_size != sizeof(struct HostState)) {
    return reject("script's state layout does not match this host");
}
```

The accessors are read-only and allocate nothing.  An unknown name returns
`POCO_STATUS_NOT_FOUND` and leaves the outputs untouched; a parameter index at
or past `parameter_count` returns `POCO_STATUS_PARAMETER_RANGE`.  Parameters
come back in declaration order, and every returned string is borrowed from the
program for its lifetime, the same ownership rule `poco_get_last_error` uses.

`PocoTypeDesc::kind` is the `PocoCallbackValue` kind a host exchanges for that
type, so it already accounts for the compiler's promotions: a `float` parameter
reports `POCO_CALLBACK_VALUE_DOUBLE`.  A type with no host value — `void`, or a
C pointer that only crosses the native ABI — reports
`POCO_CALLBACK_VALUE_INVALID`.  `is_native` distinguishes a `CFF_C` frame, so a
registered binding and a `#pragma poco native` declaration are both visible
here alongside ordinary Poco functions.

**`struct_name` is descriptive; `struct_id` is the identity.**  Poco compiles
every unit of a program in one process and keeps each unit's layouts
separately, so a tag is neither unique nor stable across a program: every unit
that includes a shared header mints its own entry for the structs in it, and
three units including one header yield three distinct `PocoStructId`s with the
same name and the same size.  That is normal and is not an error.  Compare
`struct_id` when you need to know whether two types are the same type, and
compare `struct_size` against your own packed `sizeof` when you need to know
whether a layout matches.  Poco packs members with no alignment padding, so the
host side of that comparison must be packed too.

`poco_program_struct_member` enumerates a struct's members in declaration order
for a host that wants to verify a layout field by field.  It walks the
program's layouts on every call, so keep it off hot paths.

There is deliberately no structural digest over a layout.  That would bake a
hashing convention into the public API that both sides would have to honour
forever, and a host that controls its own build already has an ABI version for
that purpose.

## Cancelling a running call

`PocoRunOptions.cancel_callback` covers a run started by `poco_vm_run()` or
`poco_activation_run()`.  A host that drives scripts by name —
`poco_call_begin()` / `poco_call_invoke()` for `on_tick`, an event handler, a
posted continuation — passes no run options and so had no way to stop a call
once it had started.  A single unbounded loop in a script hung the host with no
diagnostic.

Cancellation for that path is installed on the activation instead of passed per
call:

```c
static int over_budget(void* user_data)
{
    const struct Frame* frame = user_data;

    return frame->elapsed_seconds > frame->budget_seconds;
}

poco_activation_set_cancel_callback(activation, over_budget, &frame);

status = poco_call_invoke(call, &result);
if (status == POCO_STATUS_ABORTED) {
    /* the script ran too long; the activation is still usable */
}
```

The callback is consulted where `PocoRunOptions.cancel_callback` already is —
on loop back-edges and at function entry — and a non-zero answer ends the call
with `POCO_STATUS_ABORTED`.  The aborted call unwinds its own frames and
releases its borrowed pointer spans, so later calls on the same activation run
normally; nothing about the activation needs to be reset first.

The setting is opt-in and per activation.  With no callback installed nothing
is consulted and no call changes behaviour, which is what an existing embedder
gets.  Passing NULL clears it.  A run started with `PocoRunOptions` still
prefers that run's own `cancel_callback` when it is non-NULL and falls back to
the installed one otherwise.

**Lifetimes.**  The callback and its user data are stored in the activation and
borrowed by the interpreter for the length of each call, so both must stay
valid until they are cleared or the activation is released.  Storing them in
the activation is the point: the interpreter holds its abort hook's context for
the whole of a run, and an earlier implementation installed that context from a
local in `poco_activation_run()`, which left a dangling pointer behind the
moment that function returned.  The setting survives `poco_activation_reset()`,
which clears run state rather than host configuration.

A cancel callback runs inside the interpreter, between two instructions.  Keep
it cheap and side-effect free — read a deadline, check a flag — and in
particular do not call back into Poco from it.

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
linked privately, wrapped in `BUILD_INTERFACE`, and never exported as targets.
The package export contains only `Poco::poco`, the canonical header, CMake
package files, and `PocoModule.cmake`.  Do not name those private targets or
link their archives directly.

A shared `poco_core` absorbs those dependencies at link time, so a shared
install is a single library file.  A static one cannot, so a static install
also ships the three archives in `lib/poco/` and `PocoConfig.cmake` appends
them behind `Poco::poco`.  That directory is an implementation detail of the
package: it is not on any search path, its contents are not exported targets,
and a consumer still links `Poco::poco` alone.

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
| `compile_poco()`, `run_poco()`, `free_poco()` | Removed. Use `PocoVm`, `PocoProgram`, `poco_vm_compile_file()`, `poco_vm_run()`, and `poco_program_destroy()`. The internal `compile_poco_*_with_vm` family and `po_free_executable()` remain private to `poco/src/`; the Animator `src/inc/pocoface.h` shim and the legacy-header compile fixture were removed with them. | No removal work remains; do not reintroduce the entry points or a host-facing `pocoface.h`. |
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
