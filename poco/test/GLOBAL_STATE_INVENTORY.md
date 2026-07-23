# Poco Embeddable Global-State Inventory

Tracks the mutable file-scope state removed from Poco's embeddable library path.
This is a manual check: do not add it to CTest or CI.

## Repeatable declaration scan

Run this command from the repository root. It matches only the known
file-scope declarations, so context fields and local variables with the same
names do not count as remaining globals.

```sh
rg -n --pcre2 '^(?:static\s+Poco_run_env\s*\*\s*(?:porunenv|pe)\s*;|static\s+char\s+poco_last_error\s*\[[^]]+\]\s*=|C_frame\s*\*\s*po_run_protos\s*;|POCO_STANDALONE_FALLBACK\s+Errcode\s+builtin_err\s*;|static\s+Mblk_ctl\s*\*\s*mblk_cur\s*;|static\s+char\s*\*\s*(?:lbuf|tok)\s*;|static\s+bool\s+reuse\s*;|static\s+SHORT\s+ttype\s*;|static\s+char\s+missing_rparen\s*\[[^]]*\]\s*=|static\s+char\s+path\s*\[PATH_SIZE\]\s*;|static\s+PocoModuleHooks\s+poco_active_module_hooks\s*;)' \
  poco/src/pocoface.c poco/src/runops.c poco/src/pocmemry.c \
  poco/src/ppeval.c poco/src/pp.c poco/src/pocoload.c poco/src/pocolib.c
```

The scan originally reported the eleven rows in the inventory below. A two-thread
runtime probe later exposed a twelfth global, `builtin_err`, and a concurrent
module-compile check exposed a thirteenth, the preprocessor's shared source-path
buffer. Both were added to the scan and inventory. The command must now produce
no output. If a declaration is renamed while it is still file-scope mutable
state, update both the pattern and inventory rather than treating the rename as
removal.

## In-scope checklist

| Verified | File | Declaration | Before | Resolution |
| --- | --- | --- | --- | --- |
| [x] | `pocoface.c` | `static Poco_run_env* porunenv` | Present | Removed; the active run environment and POE walk cursor are carried per `PocoVm`. |
| [x] | `pocoface.c` | `static char poco_last_error[512]` | Present | Removed; the buffer is stored in `PocoVm` and `poco_get_last_error()` requires that VM. |
| [x] | `runops.c` | `static Poco_run_env* pe` | Present | Removed; interpreter helpers, native calls, and callback handles carry the owning run environment explicitly. |
| [x] | `runops.c` | `C_frame* po_run_protos` | Present under `DEVELOPMENT` | Removed; instruction tracing reads the prototype table from the active `Poco_run_env`. |
| [x] | `pocolib.c` | `Errcode builtin_err` | Discovered by the TSan probe | Removed; native/interpreter status is stored in `Poco_run_env` and context-aware standard bindings update the active VM's run. |
| [x] | `pocmemry.c` | `static Mblk_ctl* mblk_cur` | Present | Removed; the arena cursor is stored in `Poco_cb`. |
| [x] | `ppeval.c` | `static char *lbuf` | Present | Removed; the evaluator input cursor is stored in `Poco_cb`. |
| [x] | `ppeval.c` | `static char *tok` | Present | Removed; the evaluator token buffer is stored in `Poco_cb`. |
| [x] | `ppeval.c` | `static bool reuse` | Present | Removed; the evaluator token-reuse flag is stored in `Poco_cb`. |
| [x] | `ppeval.c` | `static SHORT ttype` | Present | Removed; the evaluator token type is stored in `Poco_cb`. |
| [x] | `ppeval.c` | `static char missing_rparen[]` | Present as a writable diagnostic array | Removed; the diagnostic is now an immutable string literal. |
| [x] | `pp.c` | `static char path[PATH_SIZE]` | Discovered by the concurrent module-compile check | Removed; each include lookup uses a caller-owned path buffer. |
| [x] | `pocoload.c` | `static PocoModuleHooks poco_active_module_hooks` | Present | Removed; the VM-owned hook table is threaded explicitly through compilation and loading. |

`po_run_protos` was compiled only in development builds, but remained in scope
because it was mutable file-scope state whenever that configuration was
enabled. `builtin_err` was absent from the original checklist; a two-thread
TSan probe showed that every interpreter entry writes this weak process-global
status, so it had to be drained. The preprocessor path buffer was likewise
absent from the baseline; repeated concurrent module compiles showed that one
VM could otherwise open the other VM's source file.

## Explicitly out of scope

- CLI-console terminal state in `poco/src/poco_unix.c` (`static struct termios
  old, new`). This file belongs to the standalone CLI surface, not the
  embeddable library concurrency boundary.
- Read-only registration and descriptor tables. Examples include the `const`
  binding/library descriptors in `poco/src/standard_library.c` and the legacy
  native-function registration tables consumed as immutable descriptors.
  These tables contain no per-run or per-compile activation state.

Out of scope does not mean that new mutable state may be added to these
categories. Reclassify any table that begins changing at runtime and add it to
this inventory if it enters the embeddable concurrency boundary.

## Manual verification

- [x] Run the declaration scan and confirm it prints no remaining in-scope
  declaration.
- [x] Walk every inventory row and confirm the resolution in the implementation;
  a rename or thread-local replacement does not satisfy the checklist.
- [x] Confirm the CLI-console statics remain outside the embeddable target and
  registration tables remain read-only descriptors.
