# Third-party notices

prc_glb_rw includes or depends on the following third-party components. Draco is used only by a
build configured to use it.

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

## stb_image_write

- Purpose: PNG encoding of the textures `nano_prc_glb_export` reads from PRC.
- Location: `common/src/stb_image_write.h` (v1.16, vendored), compiled in `common/src/pgr_png.c`.
- Author: Sean Barrett.
- Source: https://github.com/nothings/stb
- Licence: public domain (Unlicense) or MIT, at the user's choice. The full text is at the end of the
  header file.
- Known, and unfixed upstream: CVE-2026-102805, unchecked size arithmetic in `stbi_write_png_to_mem`
  and the JPEG and TGA writers. `pgr_image_encode_png` encodes an image only when its width, height
  and channels are sensible and its pixels take at most 2^28 bytes.

## Draco (optional)

- Purpose: decoding and encoding `KHR_draco_mesh_compression` mesh data, only in a build configured
  with `-DPRC_GLB_WITH_DRACO=ON`. The default build neither downloads nor links it.
- Not included in this repository. With the option on, CMake uses an installed Draco, or downloads
  the release below and builds it as a static library, linked into both tools. Only
  `common/src/draco_shim.cpp` calls it.
- Version: 1.5.9, the release archive
  `https://github.com/google/draco/archive/refs/tags/1.5.9.tar.gz`, SHA-256
  `5b84e0e212ccc7b03ace681760a65a66fe2481a5f54df65154cfc8f0cbb87148` (checked by CMake).
- Authors: Google Inc. and other contributors.
- Source: https://github.com/google/draco
- Licence: Apache License 2.0. The full text is Draco's `LICENSE` file, found in the downloaded
  source (`_deps/draco-src/LICENSE` under the build directory). Draco has no `NOTICE` file. Its
  `LICENSE` also covers `docs/assets/js/ASCIIMathML.js` (MIT), which is documentation and is not
  built. The libraries Draco bundles under `third_party` (Eigen, tinygltf, googletest, filesystem)
  serve its transcoder and its tests, both switched off here, and are not built either.
- Binaries built with Draco must be distributed with the Apache 2.0 licence text. Apache 2.0 is
  compatible with the GNU AGPL v3 that covers prc_glb_rw.
- Known: OSV-2021-1082 lists Draco versions up to 1.5.7; 1.5.9 is pinned. Draco data from a file is
  decoded by Draco itself, inside a shim that catches every C++ exception and checks every index
  the decoder returns against the point count.

## nanoPRC

- Purpose: PRC reading and writing; both tools link it.
- Not included in this repository. Supplied as a separate checkout (see README).
- Source: https://github.com/CascadiaVoxel/nanoPRC
- Licence: GNU AGPL v3, with a commercial licence available from its owner.
