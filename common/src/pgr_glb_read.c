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

/* glTF 2.0 into the neutral scene.

   cgltf parses the file and loads its buffers, from the GLB's BIN chunk,
   from files beside a .gltf, or from data URIs, and checks that accessors
   stay within their buffers. This file turns what cgltf gives into the
   neutral scene:

     - each material's PBR factors (textures come later);
     - each mesh's primitives: positions, and when present NORMAL,
       TEXCOORD_0 and COLOR_0, read through cgltf's accessor unpacking,
       which handles strides, normalised integers and sparse accessors;
       strips and fans triangulated in glTF's own order, line strips and
       loops split into segments, triangles and segments that repeat a
       vertex dropped;
     - each node's local matrix, mesh and children, and the scene's roots.

   Points, skins, animations, morph targets, cameras and lights are skipped
   with one warning per kind. A file that requires an extension this code
   does not handle is refused with PGR_ERR_UNSUPPORTED. */

#include "pgr_glb.h"
#include "pgr_math.h"

#include "cgltf.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Required extensions the reader accepts. Both lose only appearance that
   later steps handle or approximate. */
static const char *const accepted_required[] = {
    "KHR_texture_transform",
    "KHR_materials_unlit"
};

static const char *
result_text(cgltf_result r)
{
    switch (r)
    {
    case cgltf_result_data_too_short:
        return "the file is too short";
    case cgltf_result_unknown_format:
        return "not a glTF file";
    case cgltf_result_invalid_json:
        return "invalid JSON";
    case cgltf_result_invalid_gltf:
        return "invalid glTF";
    case cgltf_result_invalid_options:
        return "invalid options";
    case cgltf_result_file_not_found:
        return "file not found";
    case cgltf_result_io_error:
        return "read error";
    case cgltf_result_out_of_memory:
        return "out of memory";
    case cgltf_result_legacy_gltf:
        return "glTF 1.0, which is not supported";
    default:
        return "unknown error";
    }
}

/* The file name without its directory or extension, into buf. */
static void
file_stem(const char *path, char *buf, size_t size)
{
    const char *base = path;
    const char *p;
    size_t n;

    for (p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    n = strlen(base);
    p = strrchr(base, '.');
    if (p != NULL && p != base)
        n = (size_t)(p - base);
    if (n >= size)
        n = size - 1;
    memcpy(buf, base, n);
    buf[n] = '\0';
}

static int
check_required_extensions(const cgltf_data *data, pgr_messages *msg)
{
    cgltf_size i, k;

    for (i = 0; i < data->extensions_required_count; i++)
    {
        const char *ext = data->extensions_required[i];
        int accepted = 0;

        if (strcmp(ext, "KHR_draco_mesh_compression") == 0)
        {
            pgr_error(msg, "the file uses KHR_draco_mesh_compression; "
                      "rebuild with -DPRC_GLB_WITH_DRACO=ON to read it");
            return PGR_ERR_UNSUPPORTED;
        }
        for (k = 0; k < sizeof(accepted_required) / sizeof(accepted_required[0]); k++)
            if (strcmp(ext, accepted_required[k]) == 0)
                accepted = 1;
        if (!accepted)
        {
            pgr_error(msg, "the file requires the extension %s, which is not supported", ext);
            return PGR_ERR_UNSUPPORTED;
        }
    }
    return PGR_OK;
}

static int
read_materials(const cgltf_data *data, pgr_scene *scene, pgr_messages *msg)
{
    cgltf_size i;

    for (i = 0; i < data->materials_count; i++)
    {
        const cgltf_material *cm = &data->materials[i];
        pgr_material *m;
        int k;

        if (pgr_scene_add_material(scene, cm->name, &m) < 0)
            return PGR_ERR_NOMEM;
        if (cm->has_pbr_metallic_roughness)
        {
            for (k = 0; k < 4; k++)
                m->base_color[k] = cm->pbr_metallic_roughness.base_color_factor[k];
            m->metallic = cm->pbr_metallic_roughness.metallic_factor;
            m->roughness = cm->pbr_metallic_roughness.roughness_factor;
        }
        else if (cm->has_pbr_specular_glossiness)
        {
            for (k = 0; k < 4; k++)
                m->base_color[k] = cm->pbr_specular_glossiness.diffuse_factor[k];
            m->metallic = 0.0f;
            m->roughness = 1.0f - cm->pbr_specular_glossiness.glossiness_factor;
        }
        for (k = 0; k < 3; k++)
            m->emissive[k] = cm->emissive_factor[k];
        m->alpha_mode = cm->alpha_mode == cgltf_alpha_mode_mask ? PGR_ALPHA_MASK
                      : cm->alpha_mode == cgltf_alpha_mode_blend ? PGR_ALPHA_BLEND
                      : PGR_ALPHA_OPAQUE;
        m->alpha_cutoff = cm->alpha_cutoff;
        m->double_sided = cm->double_sided ? 1 : 0;
        if (cm->pbr_metallic_roughness.base_color_texture.texture != NULL
            || cm->pbr_specular_glossiness.diffuse_texture.texture != NULL
            || cm->normal_texture.texture != NULL
            || cm->occlusion_texture.texture != NULL
            || cm->emissive_texture.texture != NULL)
            pgr_warn_once(msg, PGR_WARN_TEXTURES, "textures are not read yet; they are skipped");
    }
    return PGR_OK;
}

/* Unpack accessor a, of components floats per element, into a new array
   of a->count * out_components floats. A three-component colour gains an
   alpha of 1 when out_components is 4. */
static int
unpack(const cgltf_accessor *a, cgltf_size out_components, float **out)
{
    cgltf_size comps = cgltf_num_components(a->type);
    cgltf_size n = a->count * comps;
    float *f;

    *out = NULL;
    f = (float *)malloc((n ? n : 1) * sizeof(float));
    if (f == NULL)
        return PGR_ERR_NOMEM;
    if (n > 0 && cgltf_accessor_unpack_floats(a, f, n) != n)
    {
        free(f);
        return PGR_ERR_PARSE;
    }
    if (comps == 3 && out_components == 4)
    {
        float *g = (float *)malloc((a->count ? a->count : 1) * 4 * sizeof(float));
        cgltf_size i;

        if (g == NULL)
        {
            free(f);
            return PGR_ERR_NOMEM;
        }
        for (i = 0; i < a->count; i++)
        {
            g[4 * i] = f[3 * i];
            g[4 * i + 1] = f[3 * i + 1];
            g[4 * i + 2] = f[3 * i + 2];
            g[4 * i + 3] = 1.0f;
        }
        free(f);
        f = g;
    }
    *out = f;
    return PGR_OK;
}

/* An attribute is used only when it has the type glTF requires and one
   element per vertex; otherwise it is skipped with a warning. */
static const cgltf_accessor *
usable(const cgltf_accessor *a, cgltf_size vertices, cgltf_type t1, cgltf_type t2,
       const char *what, pgr_messages *msg)
{
    if (a == NULL)
        return NULL;
    if ((a->type != t1 && a->type != t2) || a->count != vertices)
    {
        pgr_warn_once(msg, PGR_WARN_ATTRIBUTE,
                      "a primitive's %s does not match its positions; it is skipped", what);
        return NULL;
    }
    return a;
}

typedef struct
{
    uint32_t *v;
    uint32_t n, cap;
} index_list;

static int
push(index_list *l, uint32_t a)
{
    if (l->n == l->cap)
    {
        uint32_t cap = l->cap ? l->cap * 2 : 64;
        uint32_t *v = (uint32_t *)realloc(l->v, cap * sizeof(uint32_t));

        if (v == NULL || cap < l->cap)
            return PGR_ERR_NOMEM;
        l->v = v;
        l->cap = cap;
    }
    l->v[l->n++] = a;
    return PGR_OK;
}

static int
push_triangle(index_list *l, uint32_t a, uint32_t b, uint32_t c, uint32_t *dropped)
{
    if (a == b || b == c || a == c)
    {
        (*dropped)++;
        return PGR_OK;
    }
    if (push(l, a) != PGR_OK || push(l, b) != PGR_OK || push(l, c) != PGR_OK)
        return PGR_ERR_NOMEM;
    return PGR_OK;
}

static int
push_segment(index_list *l, uint32_t a, uint32_t b, uint32_t *dropped)
{
    if (a == b)
    {
        (*dropped)++;
        return PGR_OK;
    }
    if (push(l, a) != PGR_OK || push(l, b) != PGR_OK)
        return PGR_ERR_NOMEM;
    return PGR_OK;
}

/* Turn the primitive's index stream src (n entries) into triangles or
   segments, in glTF's order for strips and fans. */
static int
assemble(cgltf_primitive_type type, const uint32_t *src, uint32_t n, index_list *out,
         uint32_t *dropped)
{
    uint32_t i;
    int st = PGR_OK;

    switch (type)
    {
    case cgltf_primitive_type_triangles:
        for (i = 0; i + 2 < n && st == PGR_OK; i += 3)
            st = push_triangle(out, src[i], src[i + 1], src[i + 2], dropped);
        break;
    case cgltf_primitive_type_triangle_strip:
        /* Triangle i is (v[i], v[i + 1 + i % 2], v[i + 2 - i % 2]). */
        for (i = 0; i + 2 < n && st == PGR_OK; i++)
        {
            if (i % 2 == 0)
                st = push_triangle(out, src[i], src[i + 1], src[i + 2], dropped);
            else
                st = push_triangle(out, src[i], src[i + 2], src[i + 1], dropped);
        }
        break;
    case cgltf_primitive_type_triangle_fan:
        /* Triangle i is (v[i + 1], v[i + 2], v[0]). */
        for (i = 0; i + 2 < n && st == PGR_OK; i++)
            st = push_triangle(out, src[i + 1], src[i + 2], src[0], dropped);
        break;
    case cgltf_primitive_type_lines:
        for (i = 0; i + 1 < n && st == PGR_OK; i += 2)
            st = push_segment(out, src[i], src[i + 1], dropped);
        break;
    case cgltf_primitive_type_line_strip:
    case cgltf_primitive_type_line_loop:
        for (i = 0; i + 1 < n && st == PGR_OK; i++)
            st = push_segment(out, src[i], src[i + 1], dropped);
        if (type == cgltf_primitive_type_line_loop && n >= 2 && st == PGR_OK)
            st = push_segment(out, src[n - 1], src[0], dropped);
        break;
    default:
        break;
    }
    return st;
}

/* Normalise every normal; 0 when one has no direction, so that the set is
   dropped and normals are computed later instead. */
static int
normalise(float *n, uint32_t count)
{
    uint32_t i;

    for (i = 0; i < count; i++)
    {
        float *v = &n[3 * i];
        double len = sqrt((double)v[0] * v[0] + (double)v[1] * v[1] + (double)v[2] * v[2]);

        if (!(len > 1e-12) || !isfinite(len))
            return 0;
        v[0] = (float)(v[0] / len);
        v[1] = (float)(v[1] / len);
        v[2] = (float)(v[2] / len);
    }
    return 1;
}

static int
read_primitive(const cgltf_data *data, const cgltf_primitive *cp, pgr_mesh *mesh,
               pgr_messages *msg)
{
    const cgltf_accessor *pos = NULL, *nrm = NULL, *tex = NULL, *col = NULL;
    float *positions = NULL, *normals = NULL, *uvs = NULL, *colors = NULL;
    uint32_t *src = NULL;
    index_list out = { NULL, 0, 0 };
    uint32_t nv, n_src = 0, i, dropped = 0;
    int mode, st = PGR_OK;
    cgltf_size a;
    pgr_primitive *p;

    switch (cp->type)
    {
    case cgltf_primitive_type_points:
        pgr_warn_once(msg, PGR_WARN_POINTS, "point primitives are not supported; they are skipped");
        return PGR_OK;
    case cgltf_primitive_type_lines:
    case cgltf_primitive_type_line_loop:
    case cgltf_primitive_type_line_strip:
        mode = PGR_LINES;
        break;
    case cgltf_primitive_type_triangles:
    case cgltf_primitive_type_triangle_strip:
    case cgltf_primitive_type_triangle_fan:
        mode = PGR_TRIANGLES;
        break;
    default:
        pgr_error(msg, "a primitive has an unknown mode");
        return PGR_ERR_PARSE;
    }
    if (cp->targets_count > 0)
        pgr_warn_once(msg, PGR_WARN_MORPH_TARGETS, "morph targets are not supported; the base shape is used");

    for (a = 0; a < cp->attributes_count; a++)
    {
        const cgltf_attribute *at = &cp->attributes[a];

        if (at->type == cgltf_attribute_type_position && at->index == 0)
            pos = at->data;
        else if (at->type == cgltf_attribute_type_normal && at->index == 0)
            nrm = at->data;
        else if (at->type == cgltf_attribute_type_texcoord && at->index == 0)
            tex = at->data;
        else if (at->type == cgltf_attribute_type_color && at->index == 0)
            col = at->data;
    }
    if (pos == NULL || pos->count == 0)
    {
        pgr_warn_once(msg, PGR_WARN_NO_POSITION, "a primitive without positions is skipped");
        return PGR_OK;
    }
    if (cp->has_draco_mesh_compression && pos->buffer_view == NULL && !pos->is_sparse)
    {
        pgr_error(msg, "a primitive is stored only with KHR_draco_mesh_compression; "
                  "rebuild with -DPRC_GLB_WITH_DRACO=ON to read it");
        return PGR_ERR_UNSUPPORTED;
    }
    if (pos->type != cgltf_type_vec3 || pos->count > 0xffffffffu)
    {
        pgr_warn_once(msg, PGR_WARN_NO_POSITION, "a primitive without usable positions is skipped");
        return PGR_OK;
    }
    nv = (uint32_t)pos->count;
    nrm = usable(nrm, nv, cgltf_type_vec3, cgltf_type_vec3, "NORMAL", msg);
    tex = usable(tex, nv, cgltf_type_vec2, cgltf_type_vec2, "TEXCOORD_0", msg);
    col = usable(col, nv, cgltf_type_vec3, cgltf_type_vec4, "COLOR_0", msg);

    st = unpack(pos, 3, &positions);
    if (st == PGR_OK && nrm != NULL)
        st = unpack(nrm, 3, &normals);
    if (st == PGR_OK && tex != NULL)
        st = unpack(tex, 2, &uvs);
    if (st == PGR_OK && col != NULL)
        st = unpack(col, 4, &colors);
    if (st == PGR_OK && normals != NULL && !normalise(normals, nv))
    {
        pgr_warn_once(msg, PGR_WARN_ZERO_NORMAL,
                      "a primitive has a normal of zero length; its normals are dropped and will be computed");
        free(normals);
        normals = NULL;
    }

    /* The index stream: the primitive's own, or 0 .. nv - 1. */
    if (st == PGR_OK)
    {
        if (cp->indices != NULL)
        {
            const cgltf_accessor *ia = cp->indices;

            if (ia->type != cgltf_type_scalar || ia->is_sparse || ia->buffer_view == NULL
                || ia->count > 0xffffffffu)
            {
                pgr_error(msg, "a primitive's indices are not a plain scalar accessor");
                st = PGR_ERR_PARSE;
            }
            else
            {
                n_src = (uint32_t)ia->count;
                src = (uint32_t *)malloc((n_src ? n_src : 1) * sizeof(uint32_t));
                if (src == NULL)
                    st = PGR_ERR_NOMEM;
                else if (n_src > 0 && cgltf_accessor_unpack_indices(ia, src, sizeof(uint32_t), n_src) != n_src)
                {
                    pgr_error(msg, "a primitive's indices could not be read");
                    st = PGR_ERR_PARSE;
                }
            }
        }
        else
        {
            n_src = nv;
            src = (uint32_t *)malloc(n_src * sizeof(uint32_t));
            if (src == NULL)
                st = PGR_ERR_NOMEM;
            else
                for (i = 0; i < n_src; i++)
                    src[i] = i;
        }
    }
    for (i = 0; st == PGR_OK && i < n_src; i++)
    {
        if (src[i] >= nv)
        {
            pgr_error(msg, "a primitive's index %u is out of range (%u vertices)", src[i], nv);
            st = PGR_ERR_PARSE;
        }
    }
    if (st == PGR_OK)
        st = assemble(cp->type, src, n_src, &out, &dropped);
    if (st == PGR_OK && dropped > 0)
        pgr_warn_once(msg, PGR_WARN_DEGENERATE,
                      "triangles or segments that repeat a vertex are dropped (%u in one primitive)", dropped);

    if (st == PGR_OK && out.n == 0)
    {
        pgr_warn_once(msg, PGR_WARN_EMPTY_MESH, "a primitive with nothing to draw is skipped");
    }
    else if (st == PGR_OK)
    {
        if (pgr_mesh_add_primitive(mesh, mode, &p) < 0)
            st = PGR_ERR_NOMEM;
        else
        {
            p->num_vertices = nv;
            p->positions = positions;
            p->normals = normals;
            p->uvs = uvs;
            p->colors = colors;
            p->num_indices = out.n;
            p->indices = out.v;
            p->material = cp->material != NULL ? (int)cgltf_material_index(data, cp->material) : -1;
            positions = normals = uvs = colors = NULL;
            out.v = NULL;
        }
    }
    free(positions);
    free(normals);
    free(uvs);
    free(colors);
    free(src);
    free(out.v);
    return st;
}

static int
read_meshes(const cgltf_data *data, pgr_scene *scene, pgr_messages *msg)
{
    cgltf_size i, j;
    int st;

    for (i = 0; i < data->meshes_count; i++)
    {
        const cgltf_mesh *cm = &data->meshes[i];
        int index = pgr_scene_add_mesh(scene, cm->name, NULL);

        if (index < 0)
            return PGR_ERR_NOMEM;
        for (j = 0; j < cm->primitives_count; j++)
        {
            st = read_primitive(data, &cm->primitives[j], &scene->meshes[index], msg);
            if (st != PGR_OK)
                return st;
        }
    }
    return PGR_OK;
}

static int
read_nodes(const cgltf_data *data, pgr_scene *scene, pgr_messages *msg)
{
    cgltf_size i, c;
    int k;

    for (i = 0; i < data->nodes_count; i++)
    {
        const cgltf_node *cn = &data->nodes[i];
        pgr_node *n;
        cgltf_float local[16];

        if (pgr_scene_add_node(scene, cn->name, &n) < 0)
            return PGR_ERR_NOMEM;
        cgltf_node_transform_local(cn, local);
        for (k = 0; k < 16; k++)
            n->matrix[k] = local[k];
        n->has_matrix = !pgr_mat4_is_identity(n->matrix, 0.0);
        n->mesh = cn->mesh != NULL ? (int)cgltf_mesh_index(data, cn->mesh) : -1;
        if (cn->skin != NULL)
            pgr_warn_once(msg, PGR_WARN_SKINS, "skins are not supported; meshes are kept in their bind pose");
        if (cn->camera != NULL)
            pgr_warn_once(msg, PGR_WARN_CAMERAS, "cameras are not supported; they are skipped");
        if (cn->light != NULL)
            pgr_warn_once(msg, PGR_WARN_LIGHTS, "lights are not supported; they are skipped");
        if (cn->has_mesh_gpu_instancing)
            pgr_warn_once(msg, PGR_WARN_EXTENSION,
                          "EXT_mesh_gpu_instancing is not supported; each mesh is placed once");
    }
    for (i = 0; i < data->nodes_count; i++)
    {
        const cgltf_node *cn = &data->nodes[i];

        for (c = 0; c < cn->children_count; c++)
            if (pgr_node_add_child(scene, (uint32_t)i, (uint32_t)cgltf_node_index(data, cn->children[c])) < 0)
                return PGR_ERR_NOMEM;
    }
    return PGR_OK;
}

static int
read_roots(const cgltf_data *data, const cgltf_scene *chosen, pgr_scene *scene)
{
    cgltf_size i;

    if (chosen != NULL)
    {
        for (i = 0; i < chosen->nodes_count; i++)
            if (pgr_scene_add_root(scene, (uint32_t)cgltf_node_index(data, chosen->nodes[i])) < 0)
                return PGR_ERR_NOMEM;
        return PGR_OK;
    }
    /* No scene: every node without a parent is a root. */
    for (i = 0; i < data->nodes_count; i++)
        if (data->nodes[i].parent == NULL)
            if (pgr_scene_add_root(scene, (uint32_t)i) < 0)
                return PGR_ERR_NOMEM;
    return PGR_OK;
}

int
pgr_glb_read(const char *path, const pgr_glb_read_options *options, pgr_scene *scene)
{
    pgr_messages *msg = options != NULL ? options->messages : NULL;
    cgltf_options copt;
    cgltf_data *data = NULL;
    const cgltf_scene *chosen;
    cgltf_result r;
    char stem[256];
    int st;

    if (path == NULL || scene == NULL)
        return PGR_ERR_ARG;
    memset(&copt, 0, sizeof(copt));

    r = cgltf_parse_file(&copt, path, &data);
    if (r != cgltf_result_success)
    {
        pgr_error(msg, "%s: %s", path, result_text(r));
        return r == cgltf_result_out_of_memory ? PGR_ERR_NOMEM : PGR_ERR_PARSE;
    }
    st = check_required_extensions(data, msg);
    if (st == PGR_OK)
    {
        r = cgltf_load_buffers(&copt, data, path);
        if (r == cgltf_result_success)
            r = cgltf_validate(data);
        if (r != cgltf_result_success)
        {
            pgr_error(msg, "%s: %s", path, result_text(r));
            st = r == cgltf_result_out_of_memory ? PGR_ERR_NOMEM : PGR_ERR_PARSE;
        }
    }
    if (st == PGR_OK && data->animations_count > 0)
        pgr_warn_once(msg, PGR_WARN_ANIMATIONS, "animations are not supported; they are skipped");

    chosen = data->scene != NULL ? data->scene : data->scenes_count > 0 ? &data->scenes[0] : NULL;
    if (st == PGR_OK && data->scenes_count > 1)
        pgr_warn_once(msg, PGR_WARN_MULTIPLE_SCENES, "the file has %u scenes; only scene %u is read",
                      (unsigned)data->scenes_count, (unsigned)(chosen - data->scenes));

    if (st == PGR_OK)
        st = read_materials(data, scene, msg);
    if (st == PGR_OK)
        st = read_meshes(data, scene, msg);
    if (st == PGR_OK)
        st = read_nodes(data, scene, msg);
    if (st == PGR_OK)
        st = read_roots(data, chosen, scene);
    if (st == PGR_OK)
    {
        if (chosen != NULL && chosen->name != NULL && chosen->name[0] != '\0')
            scene->model_name = pgr_strdup(chosen->name);
        else
        {
            file_stem(path, stem, sizeof(stem));
            scene->model_name = pgr_strdup(stem);
        }
        if (scene->model_name == NULL)
            st = PGR_ERR_NOMEM;
    }
    if (st == PGR_OK)
    {
        scene->up_axis = PGR_Y_UP;
        scene->unit_scale_to_m = 1.0;
        if (pgr_scene_check(scene) != PGR_OK)
        {
            pgr_error(msg, "%s: the node hierarchy is not a tree", path);
            st = PGR_ERR_PARSE;
        }
    }
    cgltf_free(data);
    return st;
}
