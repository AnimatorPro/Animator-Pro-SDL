# Poco internals

Nothing in this directory is public.  An embedding host sees `<poco/poco.h>`
and the `Poco::poco` target and nothing else; `poco/README.md` and
`../docs/poco-embedding.md` cover that side.  This file is for whoever has to
change the compiler or the interpreter, and its job is to answer one question:
given a file in here, what stage does it belong to?

Poco is a two-pass design with a conventional shape — preprocess, tokenize,
parse by recursive descent, emit stack-machine bytecode, link the units
together, interpret.  There is no intermediate representation and no optimizer
pass; code comes out of the parser more or less finished.

## The compile pipeline

```
  source text
      |
      |  chopper.c        physical lines, with continuations spliced
      v
  pp.c / ppeval.c         #include, #define, macro expansion, #if arithmetic
      |
      |  token.c          text -> token type + value      (bcutil.c: symbol chop)
      v
  potokface.c             po_build_token_list(): the token list the parser
      |                   walks, with lookahead and identifier->Symbol binding
      v
  poco.c                  po_compile_file / po_compile_buffer: per-unit driver.
      |                   Sets up the global frame, seeds reserved words, then
      |                   hands the token list to the statement parser.
      v
  statemen.c              po_get_statements(): statement syntax, control flow,
      |                   jump patching, line-number records
      |
      +--> declare.c      declarators and prototypes
      |      +-- pocotype.c    base types, type checking and coercion rules
      |      +-- struct.c      struct/union tags, member offsets
      |      +-- varinit.c     initializer expressions in a definition
      |
      +--> poexpr.c       expression parsing (recursive descent)
             +-- bop.c         binary operators, by operator precedence
             +-- povar.c       variable access, subscripts, member selection
             +-- funccall.c    call sequences, argument checking
             +-- fold.c        constant folding  -- see below
             |
             v
           code.c         the code buffers everything above emits into
      |
      v
  poco.c                  po_compress_func(): frame -> Func_frame
      |
      v
  polink.c                po_link_compiled_units(): cross-unit resolution,
      |                   duplicate/missing diagnostics, operand patching,
      |                   '#pragma use' imports
      v
  runops.c                po_run_ops(): the stack machine
```

`compile_driver.c` sits above all of this: it is what the public API calls.  It
compiles each source in turn (`po_compile_file` / `po_compile_buffer`), links
them, and hands back the `Poco_run_env` the program object is built from.
`use_graph.c` decides what that set of sources is, resolving `#pragma poco use`
into a compile order, and `libproto.c` feeds the preprocessor one library
prototype line at a time.

### fold.c re-enters the interpreter

Worth knowing before you touch either end.  When the expression parser decides
a subexpression is made only of constants, `po_fold_const()` appends an
`OP_END` to the expression's code buffer and runs it on `po_run_ops()` —
during the compile, on a 128-byte stack in a throwaway `PocoActivation` — then
replaces the whole buffer with the resulting constant.  So the interpreter is
live during compilation, and a runtime error can surface with no program
loaded.  Changes to `PocoActivation`, to the stack layout, or to the constant
opcodes have to keep this path working; `fold.c` is small and its two call
sites are the fastest way to see what it depends on.

## After the compile

`vm_api.c` owns the VM, the program and the boundary conversions — turning a
host's `PocoLibrary` binding tables into the type structures the compiler
expects, and turning internal `Errcode`s into `PocoStatus`.  `poco_call.c` is
the host-to-script call path beside it, and `vm_diagnostics.c` holds the VM's
last-error text and the thread-local active-VM slot the older no-VM-parameter
bindings report through.  `poco_lock.c` is the only threading primitive in the
tree.

`activation.c` owns the mutable half of a run.  A compiled program is immutable
and shared; an activation binds one data segment, stack and pointer registry to
it, which is why several activations of one program can run at once.
`porunenv.c` tears down the immutable half (`Poco_run_env`: frames, code, line
data, literals, struct layouts) when the program goes away.

`runops.c` is the machine.  It used to be one function of about 1800 lines
whose registers were locals; the registers now live in a `PoRunState` and each
opcode family is its own handler (`po_step_convert`, `po_step_arith`,
`po_step_call`, `po_step_string`, and a dozen more).  `po_step_dispatch()` is
still the flat jump table it always was, one label per opcode, and every
handler is force-inlined back into it, so the generated dispatch is unchanged.
Handlers return a `PoStep` instead of `goto`-ing an error tail.  If you add an
opcode, add it to `pocoop.c`'s table as well — the disassembler and the
post-mortem stack trace read it from there, and a debug build self-checks that
the table and the enum agree.

`bytecode_iter.c` is the one place that walks that opcode stream.  Reading an
opcode word, validating it against `po_ins_table` and stepping over its operand
used to be copy-pasted into the linker, the serializer, the offset/ordinal
converters, the constant folder and the disassembler; they now share
`po_code_iter_next()`.  Nothing else advances a bytecode cursor.

`poco_ffi.c` is the other direction: calling native code from a script.  It
builds libffi call descriptors from parsed `Func_frame`s and enforces the
pointer contracts.  It replaced the original hand-written `runcall.asm`.

## Persistence

```
  PocoProgram  <--->  program_image.c  <--->  bytecode_container.c  <--->  file
                      (sections)              (header, integrity digest)
```

`serialization.c` is the public face of this (`poco_program_serialize_*`,
`poco_vm_deserialize_*`); `program_image.c` flattens and rebuilds the image;
`bytecode_container.c` owns the envelope and knows nothing of its contents;
`poco_hash.c` supplies the one BLAKE3 digest used for both image integrity and
source-content addressing.

Compiled programs carry no source.  Debug information is DWARF-shaped: line
tables and live ranges keyed to the opcode stream, not embedded text.
`debug.c` maps activations to source locations and holds breakpoints,
`trace.c` associates code with the source that produced it and prints stack
traces, `pocodis.c` disassembles, and `cli_debugger.c` drives all three from
stdin for the standalone `poco` binary.

## Native bindings

`pocolib.c` holds the machinery every binding shares, and `libproto.c` is how
the compiler sees a binding at all: it feeds the preprocessor one prototype
line at a time out of the builtin and loaded library lists.  The host-neutral
catalog that `poco_vm_register_standard_library()` installs is split by area —
`standard_library.c` (console), `strlib.c`, `mathlib.c`, `safefile.c`,
`path_operations.c` — and `standard_library.h` lists the whole set in one
place.  `pocoload.c` loads `.poe` modules named by `#pragma poco library`,
handling both `PocoModuleDescriptor` modules and the deprecated
`poco_rexlib_get()` ABI.

`postring.c` holds the 1991 `String` type, which never shipped and is retained
behind the `POCO_STRING_EXPERIMENT` option.  Everything belonging to it is in
that file deliberately, so the experiment does not shape the layout of the
compiler or the interpreter.

## Support

`pocmemry.c` is the compile-time allocator and the frame/code-buffer caches.
`posymbol.c` is the symbol table.  `podiag.c` formats every warning, fatal and
internal error, including the `expected X, got Y` family the parser leans on.
`port.c` is the portability layer the legacy sources reach through their
historical `pj_*` spellings; `poco_unix.c` supplies the few DOS console
routines Unix lacks.  `main.c`, `cli_debugger.c` and `pocodos_standalone.c`
build the `poco` CLI and are not part of the library.

## Headers

Every module declares itself in its own header — `bop.h`, `code.h`,
`declare.h`, `statemen.h` and so on — so an include line tells you what a file
actually depends on.

`poco_internal.h` is the compiler's control block and little else now:
`Poco_cb`, `Poco_run_env`, the abort and token macros, and the handful of
prototypes that have no better home.  The shared vocabulary underneath it was
pulled out so those headers can be included on their own:

| Header | Holds |
|---|---|
| `poco_limits.h` | fixed sizes, build tweakables, the one-line helpers |
| `poco_typemodel.h` | `TypeComp`, `Ido`, `Type_info`, `Symbol`, struct/enum layouts |
| `poco_frames.h` | code buffers, expression/loop/scope frames, `Func_frame` |
| `poco_pp_state.h` | `PreprocessorState` and the token stack, embedded in `Poco_cb` |

The preprocessor's state type is `PreprocessorState`; it was called `struct
token` for thirty years, which described the one field somebody happened to be
looking at rather than the structure.

## A note on the file banners

Most of the 1990-92 files open with a dated maintenance log.  Those logs
predate version control and predate the splits described above — `poco.c`'s
log, for instance, still claims work that now lives in `poexpr.c`, `povar.c`,
`polink.c` and four other files, and `compile_driver.c` carries the log for
the whole of what used to be `pocoface.c`.  Read the first line of a banner
for the file's role and take the log as history; `git log` and `git blame` are
authoritative for anything after 2022.

## Source order

`POCO_CORE_SOURCES` in `../CMakeLists.txt` is listed in pipeline order —
front end, parser, back end, linker, interpreter, persistence, bindings,
support — rather than alphabetically, so the list reads as the table of
contents for this file.  Add a new source to the stage it belongs to.
