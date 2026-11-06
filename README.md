# prc_glb_rw

Convert glTF 2.0 (`.glb` and `.gltf`) to PRC / 3D PDF and back, using
[nanoPRC](https://github.com/CascadiaVoxel/nanoPRC).

PRC (ISO 14739) is the 3D format embedded in 3D PDF files. This repository holds two command-line
tools built on nanoPRC's public C API, with glTF read and written through
[cgltf](https://github.com/jkuhlmann/cgltf).

**Status: skeleton.** Both tools build, report their versions and check their command lines, but
do not convert yet. What follows describes the intended behaviour, and is updated as each part
lands.

| Tool | Direction | What it will do |
|---|---|---|
| `nano_prc_glb_import` | glTF → PRC and PDF | Reads a glTF scene with its node hierarchy, transforms, materials, textures and vertex colours, and writes a `.prc` file and a minimal 3D PDF |
| `nano_prc_glb_export` | PRC or PDF → glTF | Writes the model tree as a glTF scene, binary (`.glb`) or JSON with a separate `.bin` (`.gltf`) |

## glb_import

```
nano_prc_glb_import <input.glb|input.gltf> [output.prc] [output.pdf]
nano_prc_glb_import --version
```

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
| 3 | the input needs something this build does not do (in the skeleton, any conversion) |
| 4 | the output could not be written |

## Units and axes

glTF is defined in metres with +Y up; PRC from CAD is usually millimetres with +Z up. By default
the import will place the scene under one root node that scales by 1000 and turns +Y up into +Z
up, and the export will undo it, so a model stands upright in a PDF viewer and comes back at its
original size. The coordinates themselves are not changed.

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
