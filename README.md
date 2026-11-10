# prc_glb_rw

Convert glTF 2.0 (`.glb` and `.gltf`) to PRC / 3D PDF and back, using
[nanoPRC](https://github.com/CascadiaVoxel/nanoPRC).

PRC (ISO 14739) is the 3D format embedded in 3D PDF files. This repository holds two command-line
tools built on nanoPRC's public C API, with glTF read and written through
[cgltf](https://github.com/jkuhlmann/cgltf).

**Status: in development.** Both tools convert geometry and the node hierarchy, in both directions.
The import also writes materials, vertex colours and base colour textures; the export does not read
them back yet, and Draco is to come.

| Tool | Direction | What it does |
|---|---|---|
| `nano_prc_glb_import` | glTF → PRC and PDF | Reads a glTF scene with its node hierarchy, transforms, materials, textures and vertex colours, and writes a `.prc` file and a minimal 3D PDF |
| `nano_prc_glb_export` | PRC or PDF → glTF | Writes the model tree as a glTF scene, binary (`.glb`) or JSON with a separate `.bin` (`.gltf`) |

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
- A file that requires an extension other than `KHR_texture_transform` or `KHR_materials_unlit`
  is refused (exit code 3), Draco included for now.

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
nano_prc_glb_export --version
```

Writes binary glTF for `.glb`, and JSON with a `.bin` of the same name beside it for `.gltf`.

| PRC | Becomes |
|---|---|
| product, with its placement and name | a glTF node with its matrix and name |
| part, and the representation items nested in it | the node's mesh; products drawing the same stored tessellations share one mesh |
| plain or compressed tessellation | a triangle primitive; fans and strips are triangulated |
| wire tessellation | a line primitive (`LINES`) |
| the model | a scene, converted to glTF's metres and +Y up (below) |

- A file written by `nano_prc_glb_import` comes back with the nodes it started with: the root node
  the import added to convert units and axes is recognised and removed.
- A product or part with no stored name is read by nanoPRC as "Unnamed Product"; it is written
  unnamed.
- Not yet written: materials, vertex colours, texture coordinates and textures. Markups and exact
  geometry are skipped.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | success |
| 1 | bad command line |
| 2 | the input could not be read or parsed |
| 3 | the input needs something this build does not do (a required extension it does not handle) |
| 4 | the output could not be written |

## Units and axes

glTF is defined in metres with +Y up; PRC from CAD is usually millimetres with +Z up. By default
the import places the scene under one root node that scales by 1000 and turns +Y up into +Z up (a
quarter-turn about +X), so a model stands upright in a PDF viewer. The export applies the inverse,
so a model comes back at its original size and the right way up; for a file the import wrote, it
simply removes that root. The coordinates themselves are not changed. In both tools `--units`
chooses the PRC's units (`mm`, `cm`, `m` or `in`), and `--no-axis-convert` leaves the axes as they
are.

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
