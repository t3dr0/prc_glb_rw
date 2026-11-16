# prc_glb_rw

Convert glTF 2.0 (`.glb` and `.gltf`) to PRC / 3D PDF and back, using
[nanoPRC](https://github.com/CascadiaVoxel/nanoPRC).

PRC (ISO 14739) is the 3D format embedded in 3D PDF files. This repository holds two command-line
tools built on nanoPRC's public C API, with glTF read and written through
[cgltf](https://github.com/jkuhlmann/cgltf).

**Status: in development.** Both tools convert geometry, the node hierarchy, materials, vertex
colours and base colour textures, in both directions. Draco mesh compression
(`KHR_draco_mesh_compression`) is read and written by a build with Draco, which is optional (see
[Draco](#draco)).

| Tool | Direction | What it does |
|---|---|---|
| `nano_prc_glb_import` | glTF → PRC and PDF | Reads a glTF scene with its node hierarchy, transforms, materials, textures and vertex colours, and writes a `.prc` file and a minimal 3D PDF |
| `nano_prc_glb_export` | PRC or PDF → glTF | Writes the model tree as a glTF scene with its materials, textures and vertex colours, binary (`.glb`) or JSON with a separate `.bin` (`.gltf`) |

## glb_import

```
nano_prc_glb_import <input.glb|input.gltf> [output.prc] [output.pdf]
                    [--triangles] [--no-normals] [--no-colors] [--no-texture]
                    [--quant-tolerance F] [--units mm|cm|m|in] [--no-axis-convert]
nano_prc_glb_import --version
```

Output paths default to the input's name with `.prc` and `.pdf`.

| glTF | Becomes |
|---|---|
| node, with its matrix or translation, rotation and scale | a PRC node with its placement and name |
| node's mesh | the node's part; a mesh used by several nodes is written once |
| triangle primitive (`TRIANGLES`, `TRIANGLE_STRIP`, `TRIANGLE_FAN`) | a compressed tessellation, or with `--triangles` a plain one; strips and fans are triangulated first |
| line primitive (`LINES`, `LINE_STRIP`, `LINE_LOOP`) | a wire tessellation, connected segments joined into polylines |
| `POSITION`, `NORMAL` | positions and normals; without normals the reader computes them |
| `COLOR_0` | vertex colours, one per triangle corner (or wire point), times the base colour, converted to sRGB (see below) |
| `TEXCOORD_0` | texture coordinates, written only for a textured primitive, with v turned over |
| material | the material of each item that draws the primitive (below) |
| scene | under one root node, named after the scene or the file, that converts the axes and units (below) |

- `--quant-tolerance` sets the compressed encoding's tolerance as a fraction of the bounding-box
  diagonal (default `1e-6`).
- `--no-colors` leaves vertex colours out; `--no-texture` leaves textures and texture coordinates
  out.
- Points, skins, animations, morph targets, cameras and lights are skipped with one warning each.
- A file that requires an extension other than `KHR_texture_transform`, `KHR_materials_unlit` or
  (in a build with Draco) `KHR_draco_mesh_compression` is refused (exit code 3).
- Draco-compressed primitives are decoded in a build with Draco. Without Draco, a primitive that
  also has an uncompressed copy is read from that copy, with a warning; a file whose meshes are only
  in Draco form is refused (exit code 3). See [Draco](#draco).

### Materials, colours and textures

| glTF material | PRC material |
|---|---|
| `baseColorFactor` RGB | the diffuse and ambient colour |
| `baseColorFactor` alpha | the opacity, when `alphaMode` is `BLEND`; otherwise opaque |
| `alphaMode` `MASK` | opaque; a primitive the mask hides entirely is left out (see below) |
| `roughnessFactor` r | shininess 1 − r |
| `metallicFactor` | not used |
| `emissiveFactor` | the emissive colour |
| `KHR_materials_unlit` | black diffuse, ambient and specular, emissive = the base colour; with a texture, an ordinary material and a warning |
| `baseColorTexture` | the PNG or JPEG bytes as the file holds them, multiplied by the base colour as glTF does |
| sampler `REPEAT`, `CLAMP_TO_EDGE` | repeat, clamp |
| sampler `MIRRORED_REPEAT` | repeat, with a warning |
| `KHR_texture_transform` | baked into the texture coordinates |
| no material | nanoPRC's default material |

- **Colours.** glTF colours are linear; PRC viewers show the stored values as they are, so every
  colour (base, emissive, vertex) is converted to sRGB. A linear 0.5 grey is written as 0.735
  (188 of 255).
- **Vertex colours** (`COLOR_0`) become one colour per triangle corner. glTF multiplies them by the
  material's base colour, but Adobe Acrobat and PDF-XChange Editor draw a corner's colour without the
  material colour, so for an untextured material the base colour is multiplied in when converting.
  Their alpha is kept only when `alphaMode` is `BLEND` (times the base colour's alpha), as glTF
  ignores it otherwise.
- **Texture coordinates.** glTF puts v = 0 at the top of the image and PRC at the bottom, so v is
  written as 1 − v. Checked by eye in Adobe Acrobat with a test image carrying a letter F.
- **Textures** come from a buffer view, a data URI, or a file beside the `.gltf` found as below
  (External files). Only base colour textures on `TEXCOORD_0` are read; normal, occlusion, emissive
  and metallic-roughness textures are skipped with a warning. An image is used only if it is a PNG or
  a JPEG of at most 2^28 bytes whose header gives a size of at most 2^28 pixels; otherwise the
  material keeps its colour without the image, with a warning. KTX2 and WebP images are skipped.
- A primitive whose material has a texture but which has no `TEXCOORD_0` is drawn untextured, with a
  warning.
- **Alpha masks.** glTF draws a pixel of a `MASK` material only where its alpha reaches
  `alphaCutoff` (0.5 unless given), and then opaque. PRC has no such test, so it is applied when
  converting. Without a texture the alpha (base colour alpha times any vertex colour alpha) is known
  at every corner: a primitive below the cut-off at every corner is left out, with a warning, and
  one at or above it at every corner is drawn opaque. A textured primitive, or one with corners
  either side of the cut-off, is drawn opaque, with a warning.

## glb_export

```
nano_prc_glb_export <input.pdf|input.prc> <output.glb|output.gltf>
                    [--units mm|cm|m|in] [--no-axis-convert]
                    [--draco] [--draco-qpos N] [--draco-qnorm N] [--draco-quv N]
                    [--draco-qcolor N] [--draco-speed N]
nano_prc_glb_export --version
```

Writes binary glTF for `.glb`, and JSON with a `.bin` of the same name beside it for `.gltf`.
`--draco` compresses the triangle meshes with Draco (see [Draco](#draco)).

| PRC | Becomes |
|---|---|
| product, with its placement and name | a glTF node with its matrix and name |
| part, and the representation items nested in it | the node's mesh; products drawing the same stored tessellations share one mesh |
| plain or compressed tessellation | triangle primitives, one per appearance (material, texture, vertex colours); fans and strips are triangulated |
| wire tessellation | a line primitive (`LINES`) |
| material | a glTF material (below) |
| vertex colours | `COLOR_0`, converted to linear, with a white base colour (below) |
| texture coordinates | `TEXCOORD_0`, as nanoPRC gives them: already in glTF's convention, any texture transform applied |
| texture (one that colours the surface) | `baseColorTexture`, the pixels encoded as PNG, one image for each distinct one |
| the model | a scene, converted to glTF's metres and +Y up (below) |

- A file written by `nano_prc_glb_import` comes back with the nodes it started with: the root node
  the import added to convert units and axes is recognised and removed.
- A product or part with no stored name is read by nanoPRC as "Unnamed Product"; it is written
  unnamed.
- Markups and exact geometry are skipped. A tessellation nanoPRC cannot read is left out, with a
  warning, and the rest of the file is written.

### Materials, colours and textures

The export undoes the import's conversions:

| PRC material | glTF material |
|---|---|
| diffuse colour | `baseColorFactor` RGB, converted from sRGB to linear |
| opacity | `baseColorFactor` alpha; below 1, `alphaMode` `BLEND`. PRC stores it as a byte, so 0.5 comes back as 128/255 |
| shininess s | `roughnessFactor` 1 − s; `metallicFactor` 0 |
| emissive colour | `emissiveFactor`, converted to linear |
| black diffuse, ambient and specular with an emissive colour | `KHR_materials_unlit`, in the emissive colour |
| no material | none; with vertex colours or a texture, a plain white one, since glTF's default material is metallic and draws dark |

- **Vertex colours.** nanoPRC fills every vertex's colour whether or not the file stores any, so they
  are taken as stored when some vertex is not white (or 0, which the reader gives when vertices
  carry their own style). A file whose stored colours are all white draws the same either way.
  Adobe Acrobat draws vertex colours in place of the material colour, so the material's base colour
  is made white and glTF's multiplication changes nothing; the corners' alphas apply when some are
  below 1.
- **Per-triangle styles.** A compressed tessellation whose triangles carry styles of their own
  becomes one primitive per style.
- **Textures.** Only a texture that colours the surface is written; opacity, reflection and other
  kinds are left out with a warning, as are further layers of a layered texture. nanoPRC gives no
  wrap modes, so the sampler repeats, PRC's default. A textured face's material colour multiplies
  the image, as in PRC.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | success |
| 1 | bad command line |
| 2 | the input could not be read or parsed |
| 3 | the input needs something this build does not do (a required extension it does not handle, or Draco in a build without it); also `--draco` in a build without Draco |
| 4 | the output could not be written |

## Units and axes

glTF is defined in metres with +Y up; PRC from CAD is usually millimetres with +Z up. By default
the import places the scene under one root node that scales by 1000 and turns +Y up into +Z up (a
quarter-turn about +X), so a model stands upright in a PDF viewer. The export applies the inverse,
so a model comes back at its original size and the right way up; for a file the import wrote, it
simply removes that root. The coordinates themselves are not changed. In both tools `--units`
chooses the PRC's units (`mm`, `cm`, `m` or `in`), and `--no-axis-convert` leaves the axes as they
are.

## Draco

[Draco](https://github.com/google/draco) is Google's mesh compression library, and
`KHR_draco_mesh_compression` the glTF extension that carries its data. Draco is C++ (Apache-2.0),
so it is optional: configure with `-DPRC_GLB_WITH_DRACO=ON` (see [Building](#building)).
`--version` says whether a build has it, and which version.

**Reading** (`nano_prc_glb_import`). A Draco primitive is decoded, and the decoded points and
triangles replace the primitive's own accessors, since Draco may renumber, merge or split points.
`POSITION`, `NORMAL`, `TEXCOORD_0` and `COLOR_0` are read from the Draco data. When the Draco data
cannot be decoded the file is refused (exit code 2), even if an uncompressed copy is present.

**Writing** (`nano_prc_glb_export --draco`). Each triangle primitive is compressed; line primitives
are left as they are. No uncompressed copy is written, so the extension is listed as required. The
accessors have no buffer view: their counts, and the positions' `min` and `max`, are taken from
decoding the compressed data again, so they state exactly what a reader gets. Draco merges points
whose values are all equal, leaves out points no triangle uses, and leaves out degenerate triangles
(two corners at one point after merging), so a mesh can come back with fewer points and triangles
than it had. A primitive Draco cannot compress is written uncompressed, with a warning.

| Option | Quantisation, in bits (0: none, lossless) | Default |
|---|---|---|
| `--draco-qpos N` | positions, over the primitive's largest extent | 14 |
| `--draco-qnorm N` | normals | 10 |
| `--draco-quv N` | texture coordinates | 12 |
| `--draco-qcolor N` | vertex colours | 8 |
| `--draco-speed N` | encoding and decoding speed, 0 (smallest file) to 10 (fastest) | 5 |

At the defaults a position moves by at most one step of 1/16383 of the primitive's largest extent.

Limits:

- Only `TRIANGLES` primitives are read with Draco; a Draco primitive in another mode is refused
  (exit code 3).
- cgltf reads the extension's attribute ids as accessor indices, so a file whose Draco ids reach
  past its last accessor cannot be read. Draco writers number ids from 0, so in practice this needs
  a file with fewer accessors than attributes in one primitive.
- An accessor without a buffer view, as every Draco accessor is, may state at most 2^24 elements.
- Draco allocates what its data says the mesh holds before it can tell whether the data is sound,
  so a small crafted file can still ask for a large allocation; running out of memory is reported
  as an error rather than a crash.

## External files and untrusted input

- A `.gltf` file's buffers and images are only ever read from the glTF's folder and its
  subfolders. A plain relative path down from the glTF is used as written;
  any other, such as `C:\Users\me\scene.bin` or `../scene.bin`, is reduced to its file name and
  looked for in the glTF's folder and then its subfolders, nearest first (four levels down at most,
  alphabetically within a folder, an exact name before one that differs only in letter case). Each
  substitution is reported. Links are not followed. The same rule, from the same file
  (`common/src/model_path.c`), is used by prc_obj_rw and prc_ply_rw for their texture images.
- Every size a glTF file gives is checked before cgltf uses it, because cgltf v1.15's own checks
  can overflow (CVE-2026-32845); see THIRD_PARTY_NOTICES.md.

## Building

nanoPRC is required. cgltf is a git submodule of this repository, so clone it with its submodule:

```
git clone --recurse-submodules https://github.com/t3dr0/prc_glb_rw
```

or, in an existing clone, `git submodule update --init`. Then build either way:

**Inside a nanoPRC checkout**, where this repository is a submodule, nanoPRC's own CMake build
includes it.

**On its own**, pointing at a nanoPRC checkout:

```
cmake -S prc_glb_rw -B build -DNANOPRC_SOURCE_DIR=/path/to/nanoPRC
cmake --build build --config Release
ctest --test-dir build -C Release
```

The tools and their shared code are C99. Before opening a pull request, run the C99 check and the
tests:

```
python tests/check_c99.py --nanoprc /path/to/nanoPRC
ctest --test-dir build -C Release
```

`PRC_GLB_RW_BUILD_TESTS` (default `ON`) builds the tests.

**With Draco**, add `-DPRC_GLB_WITH_DRACO=ON` (default `OFF`). This enables C++ (C++17) for the
one file that calls Draco, `common/src/draco_shim.cpp`. An installed Draco found by
`find_package(draco)` (vcpkg, apt, Homebrew) is used if there is one; otherwise CMake downloads the
Draco 1.5.9 release from GitHub, checks its SHA-256, and builds it as a static library with its
tools, tests and transcoder off. To build from a copy already on disk, add
`-DFETCHCONTENT_SOURCE_DIR_DRACO=/path/to/draco-1.5.9`. The Draco tests (`test_draco_shim`,
`test_draco_round_trip`) are built only with Draco; `test_draco_common` runs in both kinds of build.

## References

- nanoPRC's public API, [`include/prc_api.h`](https://github.com/CascadiaVoxel/nanoPRC/blob/main/include/prc_api.h),
  the only part of nanoPRC these tools use. Its demos show the patterns followed here:
  - `demos/stl_export/src/stl_export.c`: walking the model tree with each node's transform;
  - `demos/obj_export/src/obj_export.c`: faces, materials and textures;
  - `demos/stl_import/src/stl_import.c`: writing PRC, embedding it in a PDF, reading it back to
    check, and the command line;
  - `demos/teapot_write`: the three levels of the write API, tessellations, representation items
    and tree nodes;
  - `tests/functional/test_stl_roundtrip.c`: the round-trip test style, and `tests/prc_test.h`.
- cgltf: https://github.com/jkuhlmann/cgltf (v1.15). The comments at the top of `cgltf.h` and
  `cgltf_write.h` describe its use.
- Draco: https://github.com/google/draco, in particular `src/draco/compression/decode.h`,
  `encode.h` and `expert_encode.h`.
- `KHR_draco_mesh_compression`:
  https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_draco_mesh_compression/README.md
- glTF 2.0 specification: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
- glTF Validator: https://github.com/KhronosGroup/glTF-Validator
- glTF sample models: https://github.com/KhronosGroup/glTF-Sample-Assets (licences differ from
  model to model; check each one's before use).

## Licence

Copyright (C) 2026 Ian Curington, Thames 3D Research.

prc_glb_rw is licensed under the GNU Affero General Public License v3.0; see [LICENSE](LICENSE).
Third-party components are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

### Commercial licensing

A commercial licence is available on request for uses where the AGPLv3 is not suitable. Contact
info@thames3.org.

prc_glb_rw links nanoPRC, which is licensed separately by its owner. A closed-source product built
with prc_glb_rw also needs a commercial nanoPRC licence; see nanoPRC's README.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Contributions require agreement to [CLA.md](CLA.md).
