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

The example needs SDL3 and is opt-in. Enable `POCO_BUILD_EXAMPLES` on a Poco
configure and build the `snake` target:

```sh
cmake -S <poco-dir> -B build-examples \
  -DPOCO_BUILD_EXAMPLES=ON \
  -DCMAKE_C_FLAGS=-Wno-incompatible-pointer-types
cmake --build build-examples --target snake
```

The compatibility flag suppresses warnings from retained legacy Poco pointer
signatures under Clang; a host tree that already builds Poco normally applies
the same setting.

If the enclosing project already defines an `SDL3::SDL3` target, the example
uses it and does not add SDL a second time. Otherwise it adds an SDL3 source
tree: set `POCO_SDL3_SOURCE_DIR` to that tree (the default points at the
Animator Pro repository's vendored `thirdparty/sdl3`, and the configure fails
with an explicit message when neither is available). A Poco configure with
`POCO_BUILD_EXAMPLES` off needs no SDL at all, so the library build and test
suite stay dependency-free.

Run `snake` without arguments to use the source-tree `snake.poc`, or pass a
different script path as the first argument.
