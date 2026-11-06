# prc_glb_rw

Convert glTF 2.0 (`.glb` and `.gltf`) to PRC / 3D PDF and back, using
[nanoPRC](https://github.com/CascadiaVoxel/nanoPRC).

PRC (ISO 14739) is the 3D format embedded in 3D PDF files. This repository holds two command-line
tools built on nanoPRC's public C API, with glTF read and written through
[cgltf](https://github.com/jkuhlmann/cgltf).

**Status: in development.** `nano_prc_glb_import` converts geometry and the node hierarchy;
materials, vertex colours, textures and Draco are to come. `nano_prc_glb_export` checks its command
line but does not convert yet. What follows describes the intended behaviour where a part has not
landed.

| Tool | Direction | What it will do |
|---|---|---|
| `nano_prc_glb_import` | glTF → PRC and PDF | Reads a glTF scene with its node hierarchy, transforms, materials, textures and vertex colours, and writes a `.prc` file and a minimal 3D PDF |
| `nano_prc_glb_export` | PRC or PDF → glTF | Writes the model tree as a glTF scene, binary (`.glb`) or JSON with a separate `.bin` (`.gltf`) |

## glb_import

```
nano_prc_glb_import <input.glb|input.gltf> [output.prc] [output.pdf]
                    [--triangles] [--no-normals] [--quant-tolerance F]
                    [--units mm|cm|m|in] [--no-axis-convert]
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
| scene | under one root node, named after the scene or the file, that converts the axes and units (below) |

- `--quant-tolerance` sets the compressed encoding's tolerance as a fraction of the bounding-box
  diagonal (default `1e-6`).
- Not yet written: materials (every part gets nanoPRC's default material), vertex colours,
  texture coordinates and textures. Points, skins, animations, morph targets, cameras and lights are
  skipped with one warning each.
- A file that requires an extension other than `KHR_texture_transform` or `KHR_materials_unlit`
  is refused (exit code 3), Draco included for now.

## glb_export

```
nano_prc_glb_export <input.pdf|input.prc> <output.glb|output.gltf>
nano_prc_glb_export --version
```

## Exit codes

| Code | Meaning |
|---|---|
| 0 | success |
| 1 | bad command line |
| 2 | the input could not be read or parsed |
| 3 | the input needs something this build does not do (a required extension it does not handle; for now, any export) |
| 4 | the output could not be written |

## Units and axes

glTF is defined in metres with +Y up; PRC from CAD is usually millimetres with +Z up. By default
the import places the scene under one root node that scales by 1000 and turns +Y up into +Z up (a
quarter-turn about +X), so a model stands upright in a PDF viewer; the export will undo it, so a
model comes back at its original size. The coordinates themselves are not changed. `--units`
chooses the PRC's units (`mm`, `cm`, `m` or `in`), and `--no-axis-convert` leaves the axes as they
are.

## External files and untrusted input

- A `.gltf` file's buffers (and, once textures are read, its images) are only ever read from the
  glTF's folder and its subfolders. A plain relative path down from the glTF is used as written;
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
