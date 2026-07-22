# Poco Snake

This example is a small SDL3 host for a game written in Poco. The host owns the
window, renderer, input bridge, and frame pacing. It creates one `PocoVm`,
registers the standard and SDL binding libraries, compiles `snake.poc`, and
keeps one activation alive so the script's global game state persists between
frames.

## Lifecycle contract

The host calls these script functions in order:

1. `init()` initializes game state.
2. `load_assets()` creates any renderer-backed assets.
3. `update()` processes input and advances the game. It returns `0` to keep
   running and a non-zero integer to request that the host quit.
4. `draw()` renders the current frame. The host repeats `update()` and `draw()`
   on the same activation.
5. `shutdown()` releases script-owned resources. After `init()` succeeds, the
   host calls `shutdown()` once even if a later lifecycle call fails.

This keeps platform concerns in C while the game rules and state machine remain
in Poco. The binding table in `sdl_bindings.c` is registered before compilation,
so its native functions are available while Poco resolves the script.

## Building

In the full Animator Pro tree, SDL3 already exists and the Snake target is
enabled automatically:

```sh
pixi run build
cmake --build _build --target snake
```

For a Poco-only source build from the repository, explicitly enable examples.
The example will add the vendored `thirdparty/sdl3` tree because no SDL target
has been provided:

```sh
pixi run cmake -S poco -B _build-poco-examples \
  -DPOCO_BUILD_EXAMPLES=ON \
  -DCMAKE_C_FLAGS=-Wno-incompatible-pointer-types
pixi run cmake --build _build-poco-examples --target snake
```

The compatibility flag is required by the repository's Pixi Clang toolchain
for retained legacy Poco pointer signatures; the full-tree configure applies
the same compatibility setting.

`POCO_SDL3_SOURCE_DIR` can point at a different SDL3 source tree. Embedders that
already define `SDL3::SDL3` can add Poco without adding SDL a second time. A
normal standalone Poco configure leaves `POCO_BUILD_EXAMPLES` off, so Poco's
no-SDL build and test suite are unchanged.

Run `snake` without arguments to use the source-tree `snake.poc`, or pass a
different script path as the first argument.
