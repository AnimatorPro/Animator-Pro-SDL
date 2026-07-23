# Vendored BLAKE3

This directory contains the C implementation from BLAKE3 release `1.8.2`,
commit `df610ddc3b93841ffc59a87e3da659a15910eb46`, downloaded from
<https://github.com/BLAKE3-team/BLAKE3>.

Vendored upstream files:

- `blake3.c`
- `blake3.h`
- `blake3_impl.h`
- `blake3_dispatch.c`
- `blake3_portable.c`
- `blake3_neon.c`

The project selects upstream's CC0-1.0 license option; the complete text is in
`LICENSE_CC0`. `CMakeLists.txt` is local integration code and is not an
upstream file.
