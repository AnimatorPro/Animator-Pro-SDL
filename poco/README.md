# Poco subproject

Poco is a standalone C embedding library.  An external host needs one public
header, `<poco/poco.h>`, and one CMake target, `Poco::poco`; it never needs an
Animator header, target, or private Poco dependency.  The complete embedding
and Animator-maintainer migration guide is in
[`docs/poco-embedding.md`](../docs/poco-embedding.md).

## Build and install

Build Poco by itself from this directory, or select it from the Animator root:

```sh
cmake -S poco -B build/poco -G Ninja
cmake --build build/poco
cmake --install build/poco --prefix /opt/poco
```

```sh
pixi run cmake -B _build -S . -G Ninja -DWITH_ANI=OFF -DWITH_POCO=ON
pixi run cmake --build _build --target install
```

The distribution is a bundled shared Poco library (`libpoco` on Unix/macOS)
plus the `poco` CLI.  Poco links its vendored libffi and hashmap implementation
targets privately, and exports neither target nor their headers.  `Poco::poco`
is deliberately the only consumer-facing target; no supported install provides
a manually linked or merged static Poco archive.

## Embed a host

Use exactly the same CMake contract whether Poco is a source subproject or an
installed package:

```cmake
# Source tree consumption
add_subdirectory(path/to/poco)

# Or, after installation:
# find_package(Poco CONFIG REQUIRED)

add_executable(host main.c)
target_link_libraries(host PRIVATE Poco::poco)
```

The minimal host registers a real native library before it compiles a script.
The public VM owns the compiled program; destroy the program before the VM.

```c
#include <poco/poco.h>

static int answer(void)
{
	return 42;
}

int main(int argc, char **argv)
{
	static const PocoBinding bindings[] = {
		{"int Answer(void);", (PocoNativeFunction)answer, NULL},
	};
	const PocoLibrary library = {
		"example", bindings, 1, NULL, NULL, NULL,
	};
	PocoVm *vm = NULL;
	PocoProgram *program = NULL;
	PocoStatus status;
	int32_t result = 0;

	if (argc != 2)
		return 2;
	status = poco_vm_create(NULL, &vm);
	if (status == POCO_STATUS_OK)
		status = poco_vm_register_standard_library(vm); /* Explicit opt-in. */
	if (status == POCO_STATUS_OK)
		status = poco_vm_register_library(vm, &library);
	if (status == POCO_STATUS_OK)
		status = poco_vm_compile_file(vm, argv[1], &program);
	if (status == POCO_STATUS_OK)
		status = poco_vm_run(vm, program, NULL, &result);

	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return status == POCO_STATUS_OK && result == 42 ? 0 : 1;
}
```

`PocoVmOptions` supplies copied include paths, diagnostics, verbosity, and
optional module lifecycle policy.  `PocoRunOptions` supplies cancellation and
an optional trace file.  Poco's error values are `PocoStatus`; translate them
at a host adapter boundary instead of mixing them with Animator `Errcode`
values.  A VM's registrations are copied at registration time and snapshotted
into each program at compile time, so later registrations do not alter an
already compiled program.  Compile/run calls on a VM are not concurrently
re-entrant.

## Host-driven invocation

The example above runs a whole script to completion with `poco_vm_run`.  A host
that drives a script over time — calling functions by name, keeping state
between calls, or supplying `argc`/`argv` — acquires an activation and keeps it
alive instead.  `poco_vm_run` is itself a convenience wrapper over this model.

An activation binds one mutable data segment to a program's immutable code.
Acquire it once, run its global initializers, then call into it repeatedly.
Reset discards per-run state but keeps the allocated storage, so a host pool can
reuse an activation without recompiling.  Different activations of one program
may run concurrently; a single activation is owned by one thread at a time.

```c
PocoActivation *activation = NULL;
PocoCallbackValue value;

if (poco_activation_acquire(program, &activation) != POCO_STATUS_OK)
	return 1;
if (poco_activation_init(activation) != POCO_STATUS_OK) /* run global inits */
	return 1;

/* Seed a global the script reads: int difficulty; */
PocoCallbackValue seed = { POCO_CALLBACK_VALUE_INT, { .int_value = 3 } };
poco_activation_set_global(activation, "difficulty", seed);

/* Call a script function by name: int update(int frame); */
PocoCall *call = NULL;
if (poco_call_begin(activation, "update", &call) == POCO_STATUS_OK) {
	poco_call_push_int(call, 0);
	if (poco_call_invoke(call, &value) == POCO_STATUS_OK &&
	    value.kind == POCO_CALLBACK_VALUE_INT)
		printf("update -> %d\n", value.value.int_value);
}
poco_call_end(call); /* one-shot; end it whether invoke succeeded or not */

/* Read a global back out: int score; */
value = poco_activation_get_global(activation, "score");
if (value.kind == POCO_CALLBACK_VALUE_INT)
	printf("score = %d\n", value.value.int_value);

poco_activation_release(activation);
```

Numeric arguments are coerced to the declared parameter types at invoke time.
`poco_call_push_pointer()` borrows an exact host span with requested read/write
permissions; the host keeps that memory alive across `poco_call_invoke()`.  To
run a conventional entry point instead of individual functions, call
`poco_activation_run_main(activation, argc, argv, &result)` after
`poco_activation_init()`.

`poco_vm_compile_files()` links several `.poc` sources into one program — a flat
namespace with per-file `static` privacy — which then acquires activations the
same way.  `poco/examples/snake` is a complete host that drives a Poco game by
name (`init`, `load_assets`, `update`, `draw`, `shutdown`) over SDL3 using this
model.

## Native bindings and standard library

Each `PocoBinding` pairs a Poco prototype string with the exact C function
signature called by libffi.  The stable fixed-call ABI supports `int`, `long`,
`double`, `void` results, and C pointers.  Script pointers carry a `Popot`
across the Poco boundary; use an optional `PocoBindingContract` to describe
read/write spans and pointer-return ownership.  A two-field binding initializer
is intentionally trusted compatibility behavior, not a bounds contract.

Standard bindings are opt-in through `poco_vm_register_standard_library()`.
They provide host-neutral console output, string and bounded-formatting
functions, managed memory, safe `FILE` handles, math, and portable path
operations.  They do not provide Animator APIs or the interactive CLI-only
`Qtext` binding.  Prefer `snprintf` when an output buffer has a known capacity;
the historical `sprintf` binding remains explicitly trusted because its
prototype cannot describe that capacity.

### C variadic bindings

Variadic declarations must have at least one fixed parameter before `...`.
Poco records only the actual variadic values and passes them through libffi;
there are no hidden count or byte-size arguments.  Each variadic argument must
already satisfy the C default promotions:

- `char` and `short` values are passed as `int`.
- `float` values are passed as `double`.
- Native-width `int`, `long`, `double`, and C-pointer values are supported.
- `void`, unpromoted narrow integer values, and unpromoted `float` values are
  rejected with `POCO_STATUS_FFI_INVALID_BINDING` before a native call.

## Building native modules

The following CMake helpers are the only supported module build paths:

- New, host-neutral modules should use the installed `poco_add_module()`
  helper with `<poco/poco.h>` and `Poco::poco`.
- In-tree generic `.poe` targets use `add_poe_library()` from
  `poco/cmake/PocoPoeLibrary.cmake`.  It links only `Poco::poco` and explicit
  caller dependencies; it never adds Animator headers or libraries.
- Animator-native compatibility modules use `ani_add_poe_library()` from
  `src/ani_poco/AniPocoPoeLibrary.cmake`.  This Animator-private helper owns
  the Animator includes, resource destination, RPATHs, and explicit
  `LEGACY_POE` policy.

DOS-era batch, include-list, and archive-packaging flows are retired.  Do not
use them to build or distribute modules.

Generic modules export `poco_module_get()` and return a
`PocoModuleDescriptor` built from the types in `<poco/poco.h>`.  They receive
only `PocoModuleHost` diagnostics and explicitly requested versioned services;
they must not include Animator headers or expect Animator globals.  A default
Poco VM also rejects the legacy `poco_rexlib_get()` module ABI.

`poco_add_module()` creates a `.poe` module but deliberately does not impose
an installation layout or loader RPATH.  An embedding application must install
the module where its script lookup expects it and arrange its platform dynamic
loader search path for `libpoco`.  The in-tree helpers do specify relocatable
RPATHs: `add_poe_library()` uses `@loader_path/../lib` on macOS and
`$ORIGIN/../lib` on other Unix platforms; `ani_add_poe_library()` also adds
the Animator-root entry (`@loader_path/..` or `$ORIGIN/..`).  On Windows, put
the Poco DLL and any Animator DLLs where the normal Windows loader can find
them (typically beside the host/module or on `PATH`).

## Native pointer contracts

`PocoBinding` may optionally carry a `PocoBindingContract`.  A contract names
the readable and/or writable byte span for each fixed pointer parameter and
defines whether a pointer result aliases an input, is VM-owned, or is borrowed
from a host-registered span.  Poco validates contracted spans before it calls
native code through libffi; failures return `POCO_STATUS_FFI_BOUNDS` and do not
enter the target function.  Existing two-field `{ prototype, function }`
bindings remain supported as explicitly trusted bindings.

Hosts register the lifetime and permissions of externally owned memory with
`poco_vm_register_borrowed_span()` and revoke it with
`poco_vm_unregister_borrowed_span()`.  VM teardown releases contract-owned
returns and discards borrowed-span metadata.  Poco keeps tombstones for
unregistered spans until teardown so a retained stale pointer is rejected.

Contracts are a boundary check, not a sandbox: trusted native code still
receives a raw C pointer and can index past it after libffi has invoked the
function.  Use a contract wherever the operation's span is known; prefer the
built-in `snprintf` over legacy `sprintf`, whose declaration has no destination
capacity and therefore remains intentionally trusted.

## Standard binding catalog

`poco_vm_register_standard_library()` explicitly opts a VM into Poco's
host-neutral catalog: standard console output (`puts`, `printf`), strings and
bounded formatting (`snprintf`), managed memory, safe C `FILE` handles, math,
and portable `fnsplit`/`fnmerge` path operations.  The catalog is registered
through `PocoLibrary` descriptors, so an embedding host needs no Animator
headers, globals, or file APIs.

Managed allocations and open safe-file handles belong to the program's
library snapshots and are released after either a successful or failed run.
The interactive CLI-only `Qtext` binding is deliberately not part of this
catalog.  For formatting into a known-capacity destination, use `snprintf`;
legacy `sprintf` remains available only as an explicitly trusted compatibility
binding because its historical signature cannot express output capacity.

## Native module compatibility

New native modules use `<poco/poco.h>`, `PocoModuleDescriptor`, and
`poco_add_module()`.  They receive only the host-neutral module services in
that API; Poco never gives them Animator globals, `_a_a_pocolib`, or `Polib*`
function tables.

The `Pocorex`/`poco_rexlib_get` format is a deprecated Animator native-POE
compatibility ABI, not script-to-C/libffi binding dispatch.  A default Poco VM
rejects it.  An Animator-owned host may temporarily opt in with
`PocoModuleHooks.allow_legacy_poe` and an `on_load` callback that installs its
native table.  Existing POE modules should retain their field order and direct
calls while they migrate to `PocoModuleDescriptor`; new modules must not use
`Polib*` or `_plptr`.

## Compatibility status

`pocoface.h`, `pocolib.h`, `pocorex.h`, `compile_poco()`, `run_poco()`,
`free_poco()`, `Poco_lib`, and `Pocorex` are retained only for current Animator
and legacy-POE source compatibility.  They are not an alternate embedding API.
`poco_cont_ops()` and the old dummy binding catalog are already removed; a
standalone host must register a real binding or receive an undefined-API
diagnostic.  See the migration table in
[`docs/poco-embedding.md`](../docs/poco-embedding.md) for each compatibility
layer, its replacement, and the condition for removing it.
