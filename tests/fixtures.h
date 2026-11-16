/* Copyright (C) 2026 Ian Curington, Thames 3D Research

    prc_glb_rw is free software: you can redistribute it and/or modify it
    under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or (at
    your option) any later version.

    prc_glb_rw is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
    General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with prc_glb_rw. If not, see <https://www.gnu.org/licenses/>.
*/

/* Test glTF files, made by the tests themselves so that none is committed.

   Each is written from its own JSON text and binary data, not through
   pgr_glb_write, so that the reader is tested against files the writer did
   not make. A path ending in .glb gives a binary glTF; any other path a
   .gltf whose buffer is a base64 data URI. Each function returns 0 on
   success. */

#ifndef FIXTURES_H
#define FIXTURES_H

#include <stddef.h>

/* A unit cube centred on the origin, each face with its own four vertices
   and normal: 24 vertices, 12 triangles, 16-bit indices. */
int fx_cube_flat(const char *path);

/* The same cube with its 8 corners shared and no normals; 8-bit indices. */
int fx_cube_smooth(const char *path);

/* One triangle without indices, with float RGBA vertex colours
   (1,0,0,1), (0,1,0,0.5), (0,0,1,1). */
int fx_tri_vcolors(const char *path);

/* One triangle whose COLOR_0 is normalised unsigned bytes, RGB, with a
   byte stride of 4: (255,0,0), (0,128,0), (0,0,255). */
int fx_u8_colors(const char *path);

/* One triangle mesh used by two nodes, translated by (0,0,0) and (2,0,0). */
int fx_two_instances(const char *path);

/* root (translation 1,2,3) -> middle (a quarter-turn about +Z) -> leaf
   (scale 2, with the mesh); and root -> matrix_node, given as a matrix
   with translation (4,5,6). */
int fx_hierarchy3(const char *path);

/* One mesh of two primitives with two materials: "red" (base colour
   1,0,0,1, metallic 0, roughness 0.25) and "blue" (0,0,1,0.5, BLEND,
   double-sided, emissive 0.1,0.2,0.3). */
int fx_multi_material(const char *path);

/* Four vertices drawn as LINES (2 segments), LINE_STRIP (3) and LINE_LOOP
   (4). */
int fx_lines(const char *path);

/* A TRIANGLE_STRIP of five vertices without indices, and a TRIANGLE_FAN
   of five vertices with indices 0..4: three triangles each. */
int fx_strip_fan(const char *path);

/* Three positions, the second replaced by a sparse accessor with (5,5,5). */
int fx_sparse_positions(const char *path);

/* A mesh whose only primitive is POINTS. */
int fx_points(const char *path);

/* Two triangles, the second repeating a vertex: (0,1,2), (0,0,1). */
int fx_degenerate(const char *path);

/* A triangle whose second normal is (0,0,0). */
int fx_zero_normal(const char *path);

/* A triangle in a file that requires the named extension. */
int fx_requires(const char *path, const char *extension);

/* A triangle with an index of 7 into 3 vertices. */
int fx_bad_index(const char *path);

/* Two nodes with no scene: the first the parent of the second. */
int fx_no_scene(const char *path);

/* make's .gltf with the first occurrence of `old` in its JSON replaced by
   `replacement`: sizes no reader should believe. path must end in .gltf. */
int fx_edited(const char *path, int (*make)(const char *), const char *old, const char *replacement);

/* A triangle whose buffer is the file bin_path, named in the JSON as uri. */
int fx_external_buffer(const char *gltf_path, const char *uri, const char *bin_path);

/* A one-primitive mesh in KHR_draco_mesh_compression form: blob, the
   Draco data, in a buffer view of its own, and the extension's map giving
   POSITION the Draco id position_id, and NORMAL normal_id unless that is
   negative. Without fallback, the primitive's accessors have no buffer
   view and state count vertices and index_count indices, and the file
   lists the extension as required. With fallback, they hold an
   uncompressed triangle instead (count and index_count are not used, nor
   is NORMAL), and the extension is only used. */
int fx_draco(const char *path, const void *blob, size_t blob_size, unsigned count, unsigned index_count,
             int position_id, int normal_id, int fallback);

/* A 2 x 2 RGBA PNG: red, green on the top row, blue, half-transparent
   white below, stored top row first as PNG does. */
extern const unsigned char fx_png_2x2[];
extern const unsigned fx_png_2x2_size;

/* fx_quad_textured's image_mode: where the image is. */
#define FX_IMAGE_VIEW     0   /* in a buffer view */
#define FX_IMAGE_DATA_URI 1   /* its own data URI */
#define FX_IMAGE_FILE     2   /* a file beside the glTF: its name with .png added */
#define FX_IMAGE_NOT_PNG  3   /* bytes that are neither PNG nor JPEG, in a buffer view */

/* The unit square in z = 0, corners (0,0) to (1,1), normals +Z, two
   triangles, textured with fx_png_2x2 by a material "textured" of base
   colour (0.5, 0.5, 0.5, 1). Its TEXCOORD_0 puts the image the right way
   up on the square as glTF defines it: (u, v) = (x, 1 - y), v = 0 at the
   top of the image. tex_info_extra is more JSON members for the
   baseColorTexture object, starting with a comma, or ""; sampler a sampler
   object's JSON, or NULL for none; with_uvs 0 leaves TEXCOORD_0 out. */
int fx_quad_textured(const char *path, int image_mode, const char *tex_info_extra,
                     const char *sampler, int with_uvs);

#endif
