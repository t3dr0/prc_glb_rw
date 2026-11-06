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

/* The neutral scene both directions pass through.

     .glb/.gltf --pgr_glb_read--> pgr_scene --(PRC writer)--> .prc/.pdf
     .glb/.gltf <-pgr_glb_write-- pgr_scene <-(PRC reader)--- .prc/.pdf

   It follows glTF's model: one index stream addresses every attribute of a
   vertex, a node has one parent-relative matrix, column-major, and a mesh
   is a list of primitives with one material each. Strips and fans are
   already triangulated, and line strips and loops already split into
   segments, so a primitive is either triangles or line segments.

   Everything is allocated with malloc and freed by pgr_scene_free. */

#ifndef PGR_SCENE_H
#define PGR_SCENE_H

#include <stddef.h>
#include <stdint.h>

/* pgr_primitive.mode */
#define PGR_TRIANGLES 0   /* 3 indices per triangle */
#define PGR_LINES     1   /* 2 indices per segment */

/* pgr_material.alpha_mode */
#define PGR_ALPHA_OPAQUE 0
#define PGR_ALPHA_MASK   1
#define PGR_ALPHA_BLEND  2

/* pgr_texture.mime */
#define PGR_IMAGE_PNG  1
#define PGR_IMAGE_JPEG 2

/* pgr_texture.wrap_s, wrap_t */
#define PGR_WRAP_REPEAT 0
#define PGR_WRAP_CLAMP  1
#define PGR_WRAP_MIRROR 2

/* pgr_scene.up_axis */
#define PGR_Y_UP 0   /* glTF */
#define PGR_Z_UP 1   /* PRC, as CAD usually is */

typedef struct pgr_material
{
    char *name;
    float base_color[4];     /* linear RGBA */
    float emissive[3];
    float metallic;
    float roughness;
    /* PRC's own view of the material, kept so that PRC to glTF to PRC loses
       nothing glTF cannot hold. */
    int has_phong;
    float ambient[3];
    float specular[3];
    float shininess;         /* 0..1 */
    int texture;             /* index into pgr_scene.textures, or -1 */
    int alpha_mode;          /* PGR_ALPHA_* */
    float alpha_cutoff;
    int double_sided;
} pgr_material;

typedef struct pgr_texture
{
    char *name;
    uint8_t *bytes;          /* the encoded image, PNG or JPEG */
    size_t size;
    int mime;                /* PGR_IMAGE_* */
    int wrap_s, wrap_t;      /* PGR_WRAP_* */
    float uv_offset[2];
    float uv_scale[2];
} pgr_texture;

typedef struct pgr_primitive
{
    int mode;                /* PGR_TRIANGLES or PGR_LINES */
    uint32_t num_vertices;
    float *positions;        /* 3 per vertex */
    float *normals;          /* 3 per vertex, unit length, or NULL */
    float *uvs;              /* 2 per vertex, glTF convention (v down), or NULL */
    float *colors;           /* 4 per vertex, linear RGBA, or NULL */
    uint32_t num_indices;
    uint32_t *indices;       /* each < num_vertices */
    int material;            /* index into pgr_scene.materials, or -1 */
} pgr_primitive;

typedef struct pgr_mesh
{
    char *name;
    uint32_t num_primitives;
    pgr_primitive *primitives;
} pgr_mesh;

typedef struct pgr_node
{
    char *name;
    double matrix[16];       /* parent-relative, column-major */
    int has_matrix;          /* 0: matrix is the identity */
    int mesh;                /* index into pgr_scene.meshes, or -1 */
    uint32_t num_children;
    uint32_t *children;      /* indices into pgr_scene.nodes */
} pgr_node;

typedef struct pgr_scene
{
    pgr_node *nodes;
    uint32_t num_nodes;
    uint32_t *roots;         /* indices into nodes */
    uint32_t num_roots;
    pgr_mesh *meshes;
    uint32_t num_meshes;
    pgr_material *materials;
    uint32_t num_materials;
    pgr_texture *textures;
    uint32_t num_textures;
    int up_axis;             /* PGR_Y_UP or PGR_Z_UP */
    double unit_scale_to_m;  /* 1 for metres, 0.001 for millimetres */
    char *model_name;
} pgr_scene;

void pgr_scene_init(pgr_scene *s);
void pgr_scene_free(pgr_scene *s);

/* A copy of str made with malloc, or NULL when str is NULL or memory runs out. */
char *pgr_strdup(const char *str);

/* Each add function returns the new element's index, or a negative
   PGR_ERR_* status. A pointer returned through *out stays valid only until
   the next add to the same array. */
int pgr_scene_add_node(pgr_scene *s, const char *name, pgr_node **out);
int pgr_scene_add_mesh(pgr_scene *s, const char *name, pgr_mesh **out);
int pgr_scene_add_material(pgr_scene *s, const char *name, pgr_material **out);
int pgr_mesh_add_primitive(pgr_mesh *m, int mode, pgr_primitive **out);
int pgr_node_add_child(pgr_scene *s, uint32_t parent, uint32_t child);
int pgr_scene_add_root(pgr_scene *s, uint32_t node);

/* Give a primitive room for its vertices and indices: positions always,
   normals, uvs and colors when asked. Returns PGR_OK or PGR_ERR_NOMEM. */
int pgr_primitive_alloc(pgr_primitive *p, uint32_t num_vertices, uint32_t num_indices,
                        int with_normals, int with_uvs, int with_colors);

/* PGR_OK when every index is in range, every node has at most one parent,
   roots have none, and there is no cycle; else PGR_ERR_ARG. */
int pgr_scene_check(const pgr_scene *s);

/* The bounding box of a primitive's positions; 0 when it has none. */
int pgr_primitive_bounds(const pgr_primitive *p, float min[3], float max[3]);

typedef struct pgr_scene_stats
{
    uint32_t nodes, meshes, primitives;
    uint32_t triangles, segments;     /* summed over meshes, not instances */
    uint32_t vertices;
} pgr_scene_stats;

void pgr_scene_get_stats(const pgr_scene *s, pgr_scene_stats *st);

#endif
