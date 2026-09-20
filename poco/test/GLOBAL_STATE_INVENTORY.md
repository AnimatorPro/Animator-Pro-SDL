# Poco Embeddable Global-State Inventory

Tracks the mutable file-scope state removed from Poco's embeddable library path.

## Automated check: `poco_global_state_inventory`

The inventory started as a hand-run text scan for a list of names. That could
only ever find state someone had already noticed, and it could not see a
function-local `static` at all — which is how `po_strerror`'s shared return
buffer survived three passes of it.

The check is now a CTest, `poco_global_state_inventory`
(`poco/test/verify-global-state.cmake`), and it asks the linker instead of the
text. It runs `nm -m` over the built `poco_core` library and treats every
symbol that landed in `__DATA,__data`, `__DATA,__bss` or `__DATA,__common` as
mutable file-scope state, whatever it is called and wherever it was declared. A
new global therefore fails on the commit that introduces it rather than on the
next audit.

The section names matter: a `const` table holding pointers needs relocation, so
it lands in `__DATA,__const` — read-only once the dynamic linker is done, but
indistinguishable from real data under plain `nm`, which calls both `S`. The
`__common` case matters too, because a tentative definition such as
`FILE* some_stream;` is neither `D` nor `B`. On non-Apple hosts the script
falls back to GNU `nm` symbol types, which already separate `.rodata` (`r`/`R`)
from `.data`, `.bss` and common.

```sh
ctest --test-dir _build --output-on-failure -R poco_global_state_inventory
```

Symbols that must remain writable are listed in `POCO_KNOWN_MUTABLE` in that
script and explained under "Known residue" below. Adding a name to the list is
a deliberate act: update both places together.

The scan sees through renames, since it never matches on a name it was told to
expect, and it catches function-local statics because they reach the symbol
table as `<function>.<variable>`.

## Repeatable declaration scan (human companion)

The old text scan is kept because it names the specific declarations that were
drained, which the symbol scan cannot report once they are gone. Run it from
the repository root; it must produce no output.

```sh
rg -n --pcre2 '^(?:static\s+Poco_run_env\s*\*\s*(?:porunenv|pe)\s*;|static\s+char\s+poco_last_error\s*\[[^]]+\]\s*=|C_frame\s*\*\s*po_run_protos\s*;|POCO_STANDALONE_FALLBACK\s+Errcode\s+builtin_err\s*;|static\s+Mblk_ctl\s*\*\s*mblk_cur\s*;|static\s+char\s*\*\s*(?:lbuf|tok)\s*;|static\s+bool\s+reuse\s*;|static\s+SHORT\s+ttype\s*;|static\s+char\s+missing_rparen\s*\[[^]]*\]\s*=|static\s+char\s+path\s*\[PATH_SIZE\]\s*;|static\s+PocoModuleHooks\s+poco_active_module_hooks\s*;|static\s+char\s+\w+\s*\[\]\s*=|Popot\s+empty_popot\s*=|Poco_op_table\s+po_ins_table\s*\[|int\s+po_ins_table_els\s*=|Op_type\s+po_\w+\s*\[NUM_IDOS\]|Ido_table\s+po_ido_table\s*\[|int\s+po_scoped_address_op\s*\[|static\s+SHORT\s+inv_ido\s*\[|static\s+struct\s+type_table\s+base_type_names\s*\[|static\s+Bop_info\s+bi_table\s*\[|Names\s+incdirs\s*\[)' \
  poco/src/pocoface.c poco/src/runops.c poco/src/pocmemry.c \
  poco/src/ppeval.c poco/src/pp.c poco/src/pocoload.c poco/src/pocolib.c \
  poco/src/poco.c poco/src/pocoop.c poco/src/pocotype.c poco/src/bop.c \
  poco/src/strlib.c poco/src/main.c
```

If a declaration is renamed while it is still file-scope mutable state, update
the pattern and the inventory rather than treating the rename as removal. The
CTest above is the check that cannot be fooled this way.

## In-scope checklist

| Verified | File | Declaration | Before | Resolution |
| --- | --- | --- | --- | --- |
| [x] | `pocoface.c` | `static Poco_run_env* porunenv` | Present | Removed; the active run environment and POE walk cursor are carried per `PocoVm`. |
| [x] | `pocoface.c` | `static char poco_last_error[512]` | Present | Removed; the buffer is stored in `PocoVm` and `poco_get_last_error()` requires that VM. |
| [x] | `runops.c` | `static Poco_run_env* pe` | Present | Removed; interpreter helpers, native calls, and callback handles carry the owning run environment explicitly. |
| [x] | `runops.c` | `C_frame* po_run_protos` | Present under `DEVELOPMENT` | Removed; instruction tracing reads the prototype table from the active `Poco_run_env`. |
| [x] | `pocolib.c` | `Errcode builtin_err` | Discovered by the TSan probe | Removed; the status slot is `PocoActivation::builtin_error`. Poco's own bindings reach it through `poco_vm_builtin_error(vm)`; bindings that take no VM (including all of Animator's, via `src/ani_poco/ani_builtin_err.h`) go through `poco_active_builtin_error()`, which resolves the VM running on *this thread*. `builtin_err` is now an lvalue macro, not an object. |
| [x] | `pocmemry.c` | `static Mblk_ctl* mblk_cur` | Present | Removed; the arena cursor is stored in `Poco_cb`. |
| [x] | `ppeval.c` | `static char *lbuf` | Present | Removed; the evaluator input cursor is stored in `Poco_cb`. |
| [x] | `ppeval.c` | `static char *tok` | Present | Removed; the evaluator token buffer is stored in `Poco_cb`. |
| [x] | `ppeval.c` | `static bool reuse` | Present | Removed; the evaluator token-reuse flag is stored in `Poco_cb`. |
| [x] | `ppeval.c` | `static SHORT ttype` | Present | Removed; the evaluator token type is stored in `Poco_cb`. |
| [x] | `ppeval.c` | `static char missing_rparen[]` | Present as a writable diagnostic array | Removed; the diagnostic is now an immutable string literal. |
| [x] | `pp.c` | `static char path[PATH_SIZE]` | Discovered by the concurrent module-compile check | Removed; each include lookup uses a caller-owned path buffer. |
| [x] | `pocoload.c` | `static PocoModuleHooks poco_active_module_hooks` | Present | Removed; the VM-owned hook table is threaded explicitly through compilation and loading. |
| [x] | `pp.c` | 33 `static char <diagnostic>[]` arrays | Present as writable diagnostic arrays | `const`. `pp_say_fatal()`, `po_say_fatal()`, `po_say_warning()`, `po_say_internal()`, `po_expecting_got()` and `po_expecting_got_str()` now take `const char*`. |
| [x] | `pocotype.c` | `static char signed_and_unsigned[]`, `static char long_and_short[]` | Present, function-local | `const`. Same class as the `pp.c` arrays; only the symbol scan found them. |
| [x] | `pocolib.c` | `Popot empty_popot` | Exported and writable | `const Popot`. Read-only sentinel; the declaration in `poco/compat/poco/poco_legacy.h` is `const` too. |
| [x] | `pocoop.c` | `Poco_op_table po_ins_table[]`, `int po_ins_table_els`, 28 `Op_type po_*_ops[NUM_IDOS]` | Present | `const`. Consumers in `fold.c`, `pocodis.c`, `poco.c` and `program_image.c` hold `const Poco_op_table*`; `plus_equals()`, `get_pre_increment()` and `get_post_increment()` take `const Op_type[]`. |
| [x] | `poco.c` | `Ido_table po_ido_table[]`, `int po_scoped_address_op[2]`, `static SHORT inv_ido[]`, `static struct rwinit rwi[]` | Present | `const`. Pure lookup tables; nothing writes them. |
| [x] | `pocotype.c` | `static struct type_table base_type_names[]` | Present | `const`. |
| [x] | `bop.c` | `static Bop_info bi_table[]` | Present | `const`, and its `ido_ops` field is `const Op_type*`. |
| [x] | `strlib.c` | `static char errmsg[ERRTEXT_SIZE]` in `po_strerror()` | Discovered by the symbol scan | Removed; the buffer is `PocoVm::strerror_text`, reached through `poco_vm_errtext_buffer()`. Two VMs returning error text on two threads no longer overwrite each other's result between the callee's return and the caller's read. |
| [x] | `main.c` | `Names incdirs[]` (IAN / JIM / default variants) | Present, exported, writable | Deleted. Nothing in the repository read it; the three variants were DOS-era personal include paths (`\paa\resource\`, `c:\tc\include\`) behind `IAN`/`JIM` macros that no build defines. |
| [x] | `runops.c` | `FILE* po_trace_file`, `bool po_trace_flag` | Present under `DEVELOPMENT`, exported, writable | Removed; the instruction-trace destination is `PocoActivation::instruction_trace`, set per run through `PocoRunOptions::instruction_trace`. The CLI's `-t` holds its own `FILE*` local in `main()`. Two activations on two threads now trace independently. |
| [x] | `vm_api.c` | `int po_version_number` | Exported and writable | `const int`. `poco/src/pocoface.h:21` and Animator's `src/inc/pocolib.h:39` were changed together so the declarations agree. |
| [x] | `safefile.c`, `strlib.c`, `mathlib.c` | `Poco_lib po_FILE_lib`, `po_mem_lib`, `po_str_lib`, `po_math_lib` | Exported and writable; also the fallback resource list for any VM-less caller | `const Poco_lib`. They are templates a host hands to a VM, which clones them; the clone owns `next`, `resources`, `local_data` and `vm`. The VM-less legacy entry points (`po_malloc`, `po_calloc`, `po_free`, `poco_lmalloc`, `poco_freez`) now resolve through `poco_active_vm()` instead of falling back to the descriptor, so no resource list is reachable without a VM. The CLI chains copies (`main.c:get_poco_libs`) rather than writing `next` into the originals. Gated by `poco_serial_vm_library_state`. |
| [x] | `safefile.c`, `strlib.c`, `mathlib.c`, `path_operations.c` | `Lib_proto filelib[]`, `memlib[]`, `lib[]`, `mathlib[]`, `poco_path_legacy_bindings[]` | Immutable in fact, writable in type | `const`. `Poco_lib::lib` is `const Lib_proto*`, which also made `po_findpoe()`/`Porexlib::pl_findpoe` take `const Lib_proto**` and `print_one_lib()` take `const Lib_proto*`. Animator's sixteen `(Lib_proto*)&po_lib*` casts are now `(const Lib_proto*)`: they still pun a direct-table ABI onto `Lib_proto`, which `src/pocolibs.c` handles with its own stride, but they no longer launder `const` away. |

`po_run_protos` was compiled only in development builds, but remained in scope
because it was mutable file-scope state whenever that configuration was
enabled. `builtin_err` was absent from the original checklist; a two-thread
TSan probe showed that every interpreter entry writes this weak process-global
status, so it had to be drained. The preprocessor path buffer was likewise
absent from the baseline; repeated concurrent module compiles showed that one
VM could otherwise open the other VM's source file. `po_strerror`'s buffer was
absent from every earlier pass because it is a function-local `static`, which
no text scan over declarations was ever going to catch.

## Known residue

These are the symbols in `POCO_KNOWN_MUTABLE`. They are writable on purpose or
because draining them is a larger job than this inventory owns.

- **Lazy standard-library binding caches** — `poco_standard_file_library.*`,
  `poco_standard_memory_library.*`, `poco_standard_path_library.*`,
  `poco_standard_string_library.*`. One-shot derivations of the `const
  Lib_proto` tables into `PocoBinding` form, held in function-local `static`s.

  Two VMs running in sequence on one thread share these, and the second one
  does observe what the first one wrote. That is the question this inventory
  now asks, and the answer here is that the observation is harmless: every
  write stores a value computed from read-only data, so the cache the second VM
  inherits is byte-for-byte the one it would have built itself. Nothing per-VM
  — no resource list, no activation, no VM pointer — reaches them. They are a
  shared cache of immutable data, not shared state.

  Draining them outright is possible and is deliberately not done here. It
  needs each binding list to be written once as a macro and expanded into both
  the `Lib_proto` and the `PocoBinding` form, or `Lib_proto` reordered to be
  layout-compatible with `PocoBinding` and cast. The first duplicates the
  expansion of roughly seventy bindings across four files; the second puns a
  public ABI struct onto a compatibility one. Neither is worth the regression
  risk for state that carries nothing.

## Explicitly out of scope

- CLI-console terminal state in `poco/src/poco_unix.c` (`static struct termios
  old, new`). This file belongs to the standalone CLI surface, not the
  embeddable library concurrency boundary.
- Read-only registration and descriptor tables that are genuinely `const`.
  Examples include the `const` binding/library descriptors in
  `poco/src/standard_library.c`. These contain no per-run or per-compile
  activation state, and being `const` they never reach a writable section, so
  the symbol scan does not see them.

Out of scope does not mean that new mutable state may be added to these
categories. Reclassify any table that begins changing at runtime and add it to
this inventory if it enters the embeddable concurrency boundary.

## Manual verification

- [x] Run `poco_global_state_inventory` and confirm it reports no undocumented
  mutable state. Proved non-vacuous: a planted `char _gate_probe_global[8]` in
  `poco/src/fold.c` fails the test, and removing it passes again.
- [x] Run the declaration scan and confirm it prints no remaining in-scope
  declaration.
- [x] Walk every inventory row and confirm the resolution in the implementation;
  a rename or thread-local replacement does not satisfy the checklist.
- [x] Confirm the CLI-console statics remain outside the embeddable target and
  registration tables remain read-only descriptors.
