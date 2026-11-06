# Third-party notices

prc_glb_rw includes or depends on the following third-party components.

## cgltf

- Purpose: glTF 2.0 reading and writing, binary (`.glb`) and JSON (`.gltf`), used by both tools.
- Location: `thirdparty/cgltf`, a git submodule.
- Version: 1.15, the `v1.15` tag (commit `360db1a95480fe102ae9c69b27c5d101167ff5ba`).
- Author: Johannes Kuhlmann.
- Source: https://github.com/jkuhlmann/cgltf
- Licence: MIT

```
Copyright (c) 2018-2021 Johannes Kuhlmann

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

The text above is cgltf's `LICENSE` file, rewrapped; the file in the submodule is the original.

Known, and unfixed upstream as of 6 November 2026: CVE-2026-32845. `cgltf_validate` sums accessor,
sparse-accessor and buffer-view sizes without checking for overflow, and a crafted file can then make
it read far outside a buffer. `cgltf_load_buffer_base64` also allocates a buffer's declared length
before decoding it, and `cgltf_write` writes strings unescaped. `pgr_glb_read` checks every size with
sums that cannot wrap before cgltf loads or validates anything, and `pgr_glb_write` escapes every
string it hands to cgltf; cgltf itself is left unchanged.

## nanoPRC

- Purpose: PRC reading and writing; both tools link it.
- Not included in this repository. Supplied as a separate checkout (see README).
- Source: https://github.com/CascadiaVoxel/nanoPRC
- Licence: GNU AGPL v3, with a commercial licence available from its owner.
