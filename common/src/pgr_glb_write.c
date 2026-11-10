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

/* The neutral scene out as glTF 2.0.

   The cgltf_data is built by hand, and cgltf_write turns it into JSON. All
   geometry goes into one buffer: in a .glb its BIN chunk, beside a .gltf a
   .bin file. Each attribute and each index list has its own buffer view,
   starting on a 4-byte boundary, tightly packed. Positions carry their min
   and max, as glTF requires. Indices are 16-bit when every index fits,
   without reaching 65535, and 32-bit otherwise.

   cgltf_write_file is not used: it does not report a failed allocation or
   write. The JSON is made with cgltf_write and the file written here.

   cgltf_write puts strings into the JSON as given, and counts as int. So
   every name is escaped here first (a name from a model file could hold a
   quote and so end the string early), and a primitive with more than
   2^31 - 1 vertices or indices is refused. */

#include "pgr_glb.h"
#include "pgr_info.h"
#include "pgr_math.h"

#include "cgltf_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    unsigned char *data;
    size_t size, cap;
} bin_buffer;

/* Append n bytes at the next multiple of 4; their offset in *offset. */
static int
bin_append(bin_buffer *b, const void *src, size_t n, size_t *offset)
{
    size_t start = (b->size + 3) & ~(size_t)3;
    size_t need = start + n;

    if (need > b->cap)
    {
        size_t cap = b->cap ? b->cap : 4096;
        unsigned char *bigger;

        while (cap < need)
            cap *= 2;
        bigger = (unsigned char *)realloc(b->data, cap);
        if (bigger == NULL)
            return PGR_ERR_NOMEM;
        b->data = bigger;
        b->cap = cap;
    }
    memset(b->data + b->size, 0, start - b->size);
    memcpy(b->data + start, src, n);
    b->size = need;
    *offset = start;
    return PGR_OK;
}

static int
write_all(FILE *f, const void *data, size_t n)
{
    return n == 0 || fwrite(data, 1, n, f) == n;
}

static void
put_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

int
pgr_glb_write_container(const char *path, const void *json, size_t json_size,
                        const void *bin, size_t bin_size)
{
    static const char spaces[3] = { ' ', ' ', ' ' };
    static const char zeros[3] = { 0, 0, 0 };
    size_t json_pad = (4 - json_size % 4) % 4;
    size_t bin_pad = (4 - bin_size % 4) % 4;
    size_t total = 12 + 8 + json_size + json_pad + (bin_size > 0 ? 8 + bin_size + bin_pad : 0);
    unsigned char header[12], chunk[8];
    FILE *f;
    int ok;

    if (total > 0xffffffffu)
        return PGR_ERR_WRITE;
    f = fopen(path, "wb");
    if (f == NULL)
        return PGR_ERR_WRITE;

    put_u32(header, 0x46546C67u);        /* "glTF" */
    put_u32(header + 4, 2);
    put_u32(header + 8, (uint32_t)total);
    ok = write_all(f, header, sizeof(header));

    put_u32(chunk, (uint32_t)(json_size + json_pad));
    put_u32(chunk + 4, 0x4E4F534Au);     /* "JSON" */
    ok = ok && write_all(f, chunk, sizeof(chunk))
            && write_all(f, json, json_size)
            && write_all(f, spaces, json_pad);

    if (bin_size > 0)
    {
        put_u32(chunk, (uint32_t)(bin_size + bin_pad));
        put_u32(chunk + 4, 0x004E4942u); /* "BIN\0" */
        ok = ok && write_all(f, chunk, sizeof(chunk))
                && write_all(f, bin, bin_size)
                && write_all(f, zeros, bin_pad);
    }
    if (fclose(f) != 0)
        ok = 0;
    return ok ? PGR_OK : PGR_ERR_WRITE;
}

/* Everything the cgltf_data points into, allocated once at full size so
   that the pointers between its parts stay valid. */
typedef struct
{
    cgltf_data d;
    cgltf_buffer buffer;
    cgltf_scene scene;
    cgltf_buffer_view *views;
    cgltf_accessor *accessors;
    cgltf_attribute *attributes;
    cgltf_primitive *primitives;
    cgltf_mesh *meshes;
    cgltf_material *materials;
    cgltf_image *images;
    cgltf_sampler *samplers;
    cgltf_texture *textures;
    cgltf_texture **texture_map; /* scene texture -> written texture, or NULL */
    cgltf_node *nodes;
    cgltf_node **child_ptrs;
    cgltf_node **root_ptrs;
    int *mesh_map;           /* scene mesh -> written mesh, or -1 */
    void **scratch;          /* index arrays converted for writing */
    size_t n_scratch;
    char **names;            /* escaped copies of every string written */
    size_t n_names;
    bin_buffer bin;
    char generator[128];
    char *bin_uri;
} writer;

static void
writer_free(writer *w)
{
    size_t i;

    for (i = 0; i < w->n_scratch; i++)
        free(w->scratch[i]);
    free(w->scratch);
    for (i = 0; i < w->n_names; i++)
        free(w->names[i]);
    free(w->names);
    free(w->views);
    free(w->accessors);
    free(w->attributes);
    free(w->primitives);
    free(w->meshes);
    free(w->materials);
    free(w->images);
    free(w->samplers);
    free(w->textures);
    free(w->texture_map);
    free(w->nodes);
    free(w->child_ptrs);
    free(w->root_ptrs);
    free(w->mesh_map);
    free(w->bin.data);
    free(w->bin_uri);
}

/* The length of the well-formed UTF-8 sequence starting at p, or 0 when
   none does: overlong forms, surrogates and code points past U+10FFFF are
   not well formed. */
static size_t
utf8_length(const unsigned char *p)
{
    size_t n, i;
    uint32_t c;

    if (p[0] < 0x80)
        return 1;
    if (p[0] >= 0xC2 && p[0] <= 0xDF)
        n = 2, c = p[0] & 0x1F;
    else if (p[0] >= 0xE0 && p[0] <= 0xEF)
        n = 3, c = p[0] & 0x0F;
    else if (p[0] >= 0xF0 && p[0] <= 0xF4)
        n = 4, c = p[0] & 0x07;
    else
        return 0;
    for (i = 1; i < n; i++)
    {
        if ((p[i] & 0xC0) != 0x80)    /* also stops at the terminating zero */
            return 0;
        c = (c << 6) | (p[i] & 0x3F);
    }
    if ((n == 3 && (c < 0x800 || (c >= 0xD800 && c <= 0xDFFF))) || (n == 4 && (c < 0x10000 || c > 0x10FFFF)))
        return 0;
    return n;
}

/* s as the inside of a JSON string: '"' and '\' backslashed, control
   characters as \u00XX; kept in w->names to be freed. NULL for NULL, and
   *status set when memory runs out.

   glTF's JSON must be UTF-8, and names from a PRC file need not be: real
   files hold Latin-1 ("St\xe4be"). A byte that does not start a
   well-formed UTF-8 sequence is taken as Latin-1 and written as the
   character it stands for. */
static char *
escaped(writer *w, const char *s, int *status)
{
    static const char hex[] = "0123456789abcdef";
    size_t n = 0, o = 0, len;
    const unsigned char *p;
    char *e;

    if (s == NULL)
        return NULL;
    for (p = (const unsigned char *)s; *p; p += len ? len : 1)
    {
        len = utf8_length(p);
        n += len == 0 ? 2 : (*p == '"' || *p == '\\') ? 2 : *p < 0x20 ? 6 : len;
    }
    e = (char *)malloc(n + 1);
    if (e == NULL)
    {
        *status = PGR_ERR_NOMEM;
        return NULL;
    }
    for (p = (const unsigned char *)s; *p; p += len ? len : 1)
    {
        len = utf8_length(p);
        if (len == 0)
        {
            e[o++] = (char)(0xC0 | (*p >> 6));
            e[o++] = (char)(0x80 | (*p & 0x3F));
        }
        else if (*p == '"' || *p == '\\')
        {
            e[o++] = '\\';
            e[o++] = (char)*p;
        }
        else if (*p < 0x20)
        {
            e[o++] = '\\';
            e[o++] = 'u';
            e[o++] = '0';
            e[o++] = '0';
            e[o++] = hex[*p >> 4];
            e[o++] = hex[*p & 15];
        }
        else
        {
            memcpy(e + o, p, len);
            o += len;
        }
    }
    e[o] = '\0';
    w->names[w->n_names++] = e;
    return e;
}

/* Put n bytes into the buffer as a view of the given kind, and describe
   them with an accessor. */
static cgltf_accessor *
add_accessor(writer *w, const void *src, size_t bytes, cgltf_buffer_view_type kind,
             cgltf_component_type comp, cgltf_type type, cgltf_size count, int *status)
{
    cgltf_buffer_view *v = &w->views[w->d.buffer_views_count];
    cgltf_accessor *a = &w->accessors[w->d.accessors_count];
    size_t offset;

    if (bin_append(&w->bin, src, bytes, &offset) != PGR_OK)
    {
        *status = PGR_ERR_NOMEM;
        return NULL;
    }
    v->buffer = &w->buffer;
    v->offset = offset;
    v->size = bytes;
    v->type = kind;
    a->buffer_view = v;
    a->component_type = comp;
    a->type = type;
    a->count = count;
    a->stride = cgltf_calc_size(type, comp);
    w->d.buffer_views_count++;
    w->d.accessors_count++;
    return a;
}

static int
add_primitive(writer *w, const pgr_primitive *p, cgltf_primitive *cp)
{
    cgltf_attribute *at = &w->attributes[w->d.accessors_count];
    cgltf_accessor *a;
    int st = PGR_OK;
    float mn[3], mx[3];
    void *idx;
    uint32_t i;
    int k;

    cp->type = p->mode == PGR_LINES ? cgltf_primitive_type_lines : cgltf_primitive_type_triangles;
    cp->attributes = at;

    a = add_accessor(w, p->positions, (size_t)p->num_vertices * 3 * sizeof(float),
                     cgltf_buffer_view_type_vertices, cgltf_component_type_r_32f, cgltf_type_vec3,
                     p->num_vertices, &st);
    if (a == NULL)
        return st;
    pgr_primitive_bounds(p, mn, mx);
    a->has_min = a->has_max = 1;
    for (k = 0; k < 3; k++)
    {
        a->min[k] = mn[k];
        a->max[k] = mx[k];
    }
    at[cp->attributes_count].name = (char *)"POSITION";
    at[cp->attributes_count].type = cgltf_attribute_type_position;
    at[cp->attributes_count++].data = a;

    if (p->normals != NULL)
    {
        a = add_accessor(w, p->normals, (size_t)p->num_vertices * 3 * sizeof(float),
                         cgltf_buffer_view_type_vertices, cgltf_component_type_r_32f, cgltf_type_vec3,
                         p->num_vertices, &st);
        if (a == NULL)
            return st;
        at[cp->attributes_count].name = (char *)"NORMAL";
        at[cp->attributes_count].type = cgltf_attribute_type_normal;
        at[cp->attributes_count++].data = a;
    }
    if (p->uvs != NULL)
    {
        a = add_accessor(w, p->uvs, (size_t)p->num_vertices * 2 * sizeof(float),
                         cgltf_buffer_view_type_vertices, cgltf_component_type_r_32f, cgltf_type_vec2,
                         p->num_vertices, &st);
        if (a == NULL)
            return st;
        at[cp->attributes_count].name = (char *)"TEXCOORD_0";
        at[cp->attributes_count].type = cgltf_attribute_type_texcoord;
        at[cp->attributes_count++].data = a;
    }
    if (p->colors != NULL)
    {
        a = add_accessor(w, p->colors, (size_t)p->num_vertices * 4 * sizeof(float),
                         cgltf_buffer_view_type_vertices, cgltf_component_type_r_32f, cgltf_type_vec4,
                         p->num_vertices, &st);
        if (a == NULL)
            return st;
        at[cp->attributes_count].name = (char *)"COLOR_0";
        at[cp->attributes_count].type = cgltf_attribute_type_color;
        at[cp->attributes_count++].data = a;
    }

    /* 16-bit indices when no index can reach 65535, which glTF reserves. */
    if (p->num_vertices <= 65535)
    {
        uint16_t *s = (uint16_t *)malloc(p->num_indices * sizeof(uint16_t));

        if (s == NULL)
            return PGR_ERR_NOMEM;
        for (i = 0; i < p->num_indices; i++)
            s[i] = (uint16_t)p->indices[i];
        idx = s;
        w->scratch[w->n_scratch++] = idx;
        a = add_accessor(w, idx, p->num_indices * sizeof(uint16_t), cgltf_buffer_view_type_indices,
                         cgltf_component_type_r_16u, cgltf_type_scalar, p->num_indices, &st);
    }
    else
        a = add_accessor(w, p->indices, (size_t)p->num_indices * sizeof(uint32_t),
                         cgltf_buffer_view_type_indices, cgltf_component_type_r_32u, cgltf_type_scalar,
                         p->num_indices, &st);
    if (a == NULL)
        return st;
    cp->indices = a;
    return PGR_OK;
}

static cgltf_wrap_mode
wrap_mode(int wrap)
{
    return wrap == PGR_WRAP_CLAMP ? cgltf_wrap_mode_clamp_to_edge
         : wrap == PGR_WRAP_MIRROR ? cgltf_wrap_mode_mirrored_repeat
         : cgltf_wrap_mode_repeat;
}

/* Each scene texture with an image as a glTF image in the buffer, a
   sampler with its wrap modes, and a texture joining them. */
static int
add_textures(writer *w, const pgr_scene *s)
{
    uint32_t i;
    int st = PGR_OK;

    for (i = 0; i < s->num_textures && st == PGR_OK; i++)
    {
        const pgr_texture *tx = &s->textures[i];
        cgltf_buffer_view *v = &w->views[w->d.buffer_views_count];
        cgltf_image *im = &w->images[w->d.images_count];
        cgltf_sampler *sm = &w->samplers[w->d.samplers_count];
        cgltf_texture *t = &w->textures[w->d.textures_count];
        size_t offset;

        if (tx->bytes == NULL || tx->size == 0)
            continue;
        if (bin_append(&w->bin, tx->bytes, tx->size, &offset) != PGR_OK)
            return PGR_ERR_NOMEM;
        v->buffer = &w->buffer;
        v->offset = offset;
        v->size = tx->size;
        w->d.buffer_views_count++;
        im->buffer_view = v;
        im->mime_type = (char *)(tx->mime == PGR_IMAGE_JPEG ? "image/jpeg" : "image/png");
        w->d.images_count++;
        sm->wrap_s = wrap_mode(tx->wrap_s);
        sm->wrap_t = wrap_mode(tx->wrap_t);
        w->d.samplers_count++;
        t->name = escaped(w, tx->name, &st);
        t->image = im;
        t->sampler = sm;
        w->texture_map[i] = t;
        w->d.textures_count++;
    }
    return st;
}

static void
fill_material(const writer *w, const pgr_scene *s, const pgr_material *m, cgltf_material *cm)
{
    int k;

    cm->has_pbr_metallic_roughness = 1;
    for (k = 0; k < 4; k++)
        cm->pbr_metallic_roughness.base_color_factor[k] = m->base_color[k];
    if (m->texture >= 0 && (uint32_t)m->texture < s->num_textures)
        cm->pbr_metallic_roughness.base_color_texture.texture = w->texture_map[m->texture];
    cm->unlit = m->unlit ? 1 : 0;
    cm->pbr_metallic_roughness.metallic_factor = m->metallic;
    cm->pbr_metallic_roughness.roughness_factor = m->roughness;
    for (k = 0; k < 3; k++)
        cm->emissive_factor[k] = m->emissive[k];
    cm->alpha_mode = m->alpha_mode == PGR_ALPHA_MASK ? cgltf_alpha_mode_mask
                   : m->alpha_mode == PGR_ALPHA_BLEND ? cgltf_alpha_mode_blend
                   : cgltf_alpha_mode_opaque;
    cm->alpha_cutoff = m->alpha_cutoff;
    cm->double_sided = m->double_sided ? 1 : 0;
}

/* path with its extension replaced by .bin, and that file's name alone. */
static char *
bin_path_for(const char *path, const char **name)
{
    size_t n = strlen(path);
    const char *dot = strrchr(path, '.');
    const char *slash1 = strrchr(path, '/');
    const char *slash2 = strrchr(path, '\\');
    const char *slash = slash1 > slash2 ? slash1 : slash2;
    char *out;

    if (dot != NULL && (slash == NULL || dot > slash))
        n = (size_t)(dot - path);
    out = (char *)malloc(n + 5);
    if (out == NULL)
        return NULL;
    memcpy(out, path, n);
    memcpy(out + n, ".bin", 5);
    *name = out + (slash != NULL ? (size_t)(slash - path) + 1 : 0);
    return out;
}

int
pgr_glb_write(const pgr_scene *s, const char *path, const pgr_glb_write_options *options)
{
    pgr_messages *msg = options != NULL ? options->messages : NULL;
    int glb = pgr_has_extension(path, ".glb");
    writer w;
    cgltf_options copt;
    size_t n_prims = 0, n_children = 0, json_size;
    char *json = NULL, *bin_path = NULL;
    const char *bin_name = NULL;
    uint32_t i, j, c;
    int st = PGR_OK;

    if (s == NULL || path == NULL)
        return PGR_ERR_ARG;
    if (pgr_scene_check(s) != PGR_OK)
    {
        pgr_error(msg, "the scene to write is inconsistent");
        return PGR_ERR_ARG;
    }
    memset(&w, 0, sizeof(w));
    memset(&copt, 0, sizeof(copt));

    for (i = 0; i < s->num_meshes; i++)
    {
        n_prims += s->meshes[i].num_primitives;
        for (j = 0; j < s->meshes[i].num_primitives; j++)
        {
            const pgr_primitive *p = &s->meshes[i].primitives[j];

            if (p->num_vertices > 0x7fffffffu || p->num_indices > 0x7fffffffu)
            {
                pgr_error(msg, "a primitive is too large for glTF as written here "
                          "(over 2^31 - 1 vertices or indices)");
                return PGR_ERR_ARG;
            }
        }
    }
    for (i = 0; i < s->num_nodes; i++)
        n_children += s->nodes[i].num_children;

    /* At most four attributes and one index list per primitive, and one
       image per texture. */
    w.views = (cgltf_buffer_view *)calloc(5 * n_prims + s->num_textures + 1, sizeof(cgltf_buffer_view));
    w.images = (cgltf_image *)calloc(s->num_textures + 1, sizeof(cgltf_image));
    w.samplers = (cgltf_sampler *)calloc(s->num_textures + 1, sizeof(cgltf_sampler));
    w.textures = (cgltf_texture *)calloc(s->num_textures + 1, sizeof(cgltf_texture));
    w.texture_map = (cgltf_texture **)calloc(s->num_textures + 1, sizeof(cgltf_texture *));
    w.accessors = (cgltf_accessor *)calloc(5 * n_prims + 1, sizeof(cgltf_accessor));
    w.attributes = (cgltf_attribute *)calloc(5 * n_prims + 1, sizeof(cgltf_attribute));
    w.primitives = (cgltf_primitive *)calloc(n_prims + 1, sizeof(cgltf_primitive));
    w.meshes = (cgltf_mesh *)calloc(s->num_meshes + 1, sizeof(cgltf_mesh));
    w.materials = (cgltf_material *)calloc(s->num_materials + 1, sizeof(cgltf_material));
    w.nodes = (cgltf_node *)calloc(s->num_nodes + 1, sizeof(cgltf_node));
    w.child_ptrs = (cgltf_node **)calloc(n_children + 1, sizeof(cgltf_node *));
    w.root_ptrs = (cgltf_node **)calloc(s->num_roots + 1, sizeof(cgltf_node *));
    w.mesh_map = (int *)calloc(s->num_meshes + 1, sizeof(int));
    w.scratch = (void **)calloc(n_prims + 1, sizeof(void *));
    /* One escaped string per node, mesh, material and texture, the scene's
       name and the generator. */
    w.names = (char **)calloc((size_t)s->num_nodes + s->num_meshes + s->num_materials + s->num_textures + 2,
                              sizeof(char *));
    if (w.views == NULL || w.accessors == NULL || w.attributes == NULL || w.primitives == NULL
        || w.meshes == NULL || w.materials == NULL || w.images == NULL || w.samplers == NULL
        || w.textures == NULL || w.texture_map == NULL || w.nodes == NULL || w.child_ptrs == NULL
        || w.root_ptrs == NULL || w.mesh_map == NULL || w.scratch == NULL || w.names == NULL)
    {
        writer_free(&w);
        return PGR_ERR_NOMEM;
    }

    w.d.buffer_views = w.views;
    w.d.accessors = w.accessors;
    w.d.images = w.images;
    w.d.samplers = w.samplers;
    w.d.textures = w.textures;
    st = add_textures(&w, s);
    w.d.materials = w.materials;
    w.d.materials_count = s->num_materials;
    for (i = 0; i < s->num_materials; i++)
    {
        fill_material(&w, s, &s->materials[i], &w.materials[i]);
        w.materials[i].name = escaped(&w, s->materials[i].name, &st);
    }

    /* Meshes; one with no primitive left to draw is not written, since glTF
       requires at least one. */
    {
        cgltf_primitive *next = w.primitives;

        for (i = 0; i < s->num_meshes && st == PGR_OK; i++)
        {
            const pgr_mesh *m = &s->meshes[i];
            cgltf_mesh *cm = &w.meshes[w.d.meshes_count];

            cm->primitives = next;
            for (j = 0; j < m->num_primitives && st == PGR_OK; j++)
            {
                const pgr_primitive *p = &m->primitives[j];

                if (p->num_vertices == 0 || p->num_indices == 0)
                    continue;
                st = add_primitive(&w, p, &cm->primitives[cm->primitives_count]);
                if (st == PGR_OK)
                {
                    cm->primitives[cm->primitives_count].material =
                        p->material >= 0 ? &w.materials[p->material] : NULL;
                    cm->primitives_count++;
                }
            }
            if (cm->primitives_count == 0)
            {
                w.mesh_map[i] = -1;
                pgr_warn_once(msg, PGR_WARN_EMPTY_MESH, "a mesh with nothing to draw is not written");
                continue;
            }
            cm->name = escaped(&w, m->name, &st);
            next += cm->primitives_count;
            w.mesh_map[i] = (int)w.d.meshes_count++;
        }
    }
    w.d.meshes = w.meshes;

    /* Nodes, children and roots. */
    {
        cgltf_node **next = w.child_ptrs;

        for (i = 0; i < s->num_nodes; i++)
        {
            const pgr_node *n = &s->nodes[i];
            cgltf_node *cn = &w.nodes[i];
            int k;

            cn->name = escaped(&w, n->name, &st);
            if (n->mesh >= 0 && w.mesh_map[n->mesh] >= 0)
                cn->mesh = &w.meshes[w.mesh_map[n->mesh]];
            if (n->has_matrix && !pgr_mat4_is_identity(n->matrix, 0.0))
            {
                cn->has_matrix = 1;
                for (k = 0; k < 16; k++)
                    cn->matrix[k] = (cgltf_float)n->matrix[k];
            }
            cn->children = next;
            cn->children_count = n->num_children;
            for (c = 0; c < n->num_children; c++)
            {
                next[c] = &w.nodes[n->children[c]];
                w.nodes[n->children[c]].parent = cn;
            }
            next += n->num_children;
        }
        for (i = 0; i < s->num_roots; i++)
            w.root_ptrs[i] = &w.nodes[s->roots[i]];
    }
    w.d.nodes = w.nodes;
    w.d.nodes_count = s->num_nodes;
    w.scene.name = escaped(&w, s->model_name, &st);
    w.scene.nodes = w.root_ptrs;
    w.scene.nodes_count = s->num_roots;
    w.d.scenes = &w.scene;
    w.d.scenes_count = 1;
    w.d.scene = &w.scene;

    if (options != NULL && options->generator != NULL)
        snprintf(w.generator, sizeof(w.generator), "%s", options->generator);
    else
        snprintf(w.generator, sizeof(w.generator), "prc_glb_rw %s", pgr_version());
    w.d.asset.version = (char *)"2.0";
    w.d.asset.generator = escaped(&w, w.generator, &st);

    if (st == PGR_OK && w.bin.size > 0)
    {
        w.buffer.size = w.bin.size;
        if (!glb)
        {
            bin_path = bin_path_for(path, &bin_name);
            if (bin_path == NULL)
                st = PGR_ERR_NOMEM;
            else
            {
                w.bin_uri = pgr_strdup(bin_name);
                if (w.bin_uri == NULL)
                    st = PGR_ERR_NOMEM;
                w.buffer.uri = w.bin_uri;
            }
        }
        w.d.buffers = &w.buffer;
        w.d.buffers_count = 1;
    }

    if (st == PGR_OK)
    {
        copt.type = glb ? cgltf_file_type_glb : cgltf_file_type_gltf;
        json_size = cgltf_write(&copt, NULL, 0, &w.d);
        json = (char *)malloc(json_size ? json_size : 1);
        if (json == NULL)
            st = PGR_ERR_NOMEM;
        else if (cgltf_write(&copt, json, json_size, &w.d) != json_size || json_size < 1)
            st = PGR_ERR_WRITE;
    }
    if (st == PGR_OK)
    {
        /* cgltf_write's size counts a terminating zero, not part of the file. */
        json_size--;
        if (glb)
            st = pgr_glb_write_container(path, json, json_size, w.bin.data, w.bin.size);
        else
        {
            FILE *f;

            if (bin_path != NULL)
            {
                f = fopen(bin_path, "wb");
                if (f == NULL)
                    st = PGR_ERR_WRITE;
                else
                {
                    if (!write_all(f, w.bin.data, w.bin.size))
                        st = PGR_ERR_WRITE;
                    if (fclose(f) != 0)
                        st = PGR_ERR_WRITE;
                }
            }
            if (st == PGR_OK)
            {
                f = fopen(path, "wb");
                if (f == NULL)
                    st = PGR_ERR_WRITE;
                else
                {
                    if (!write_all(f, json, json_size))
                        st = PGR_ERR_WRITE;
                    if (fclose(f) != 0)
                        st = PGR_ERR_WRITE;
                }
            }
        }
        if (st == PGR_ERR_WRITE)
            pgr_error(msg, "cannot write %s", path);
    }

    free(json);
    free(bin_path);
    writer_free(&w);
    return st;
}
