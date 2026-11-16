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

   With options->draco, each triangle primitive is written in
   KHR_draco_mesh_compression form instead (add_draco_primitive): its Draco
   data in one buffer view, accessors without buffer views, and the
   extension listed as required (require_draco).

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
    cgltf_attribute *draco_attributes; /* each Draco primitive's id map */
    size_t draco_ids_needed;  /* accessors the largest Draco id needs */
    size_t draco_primitives;
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
    free(w->draco_attributes);
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

/* An accessor with no buffer view: it states the type and count of data
   that the Draco extension holds. */
static cgltf_accessor *
add_viewless_accessor(writer *w, cgltf_component_type comp, cgltf_type type, cgltf_size count)
{
    cgltf_accessor *a = &w->accessors[w->d.accessors_count++];

    a->component_type = comp;
    a->type = type;
    a->count = count;
    return a;
}

/* A triangle primitive as KHR_draco_mesh_compression: the Draco data in a
   buffer view of its own, and accessors without buffer views stating what
   the data decodes to. No uncompressed copy is written.

   The counts and the positions' min and max come from decoding the data
   again here, so they describe exactly what a reader gets: Draco merges
   equal points, can split others, and quantises the values. That decode
   also proves the data readable before it is written.

   Returns PGR_ERR_UNSUPPORTED, writing nothing, when Draco cannot encode
   the primitive; the caller then writes it uncompressed. */
static int
add_draco_primitive(writer *w, const pgr_primitive *p, cgltf_primitive *cp,
                    const ds_encode_options *o, pgr_messages *msg)
{
    static const char *const names[4] = { "POSITION", "NORMAL", "TEXCOORD_0", "COLOR_0" };
    static const cgltf_attribute_type types[4] = { cgltf_attribute_type_position, cgltf_attribute_type_normal,
                                                   cgltf_attribute_type_texcoord, cgltf_attribute_type_color };
    static const cgltf_type shapes[4] = { cgltf_type_vec3, cgltf_type_vec3, cgltf_type_vec2, cgltf_type_vec4 };
    ds_attribute in_att[4];
    int slot[4], ids[4];
    ds_mesh in, out;
    void *blob = NULL;
    size_t blob_size = 0, offset;
    uint32_t n = 0, k, i;
    cgltf_attribute *at = &w->attributes[w->d.accessors_count];
    cgltf_attribute *dat = &w->draco_attributes[w->d.accessors_count];
    cgltf_buffer_view *v;
    int r;

    memset(in_att, 0, sizeof(in_att));
    in_att[n].semantic = DS_POSITION, in_att[n].num_components = 3, in_att[n].data = p->positions, slot[n++] = 0;
    if (p->normals != NULL)
        in_att[n].semantic = DS_NORMAL, in_att[n].num_components = 3, in_att[n].data = p->normals, slot[n++] = 1;
    if (p->uvs != NULL)
        in_att[n].semantic = DS_TEXCOORD, in_att[n].num_components = 2, in_att[n].data = p->uvs, slot[n++] = 2;
    if (p->colors != NULL)
        in_att[n].semantic = DS_COLOR, in_att[n].num_components = 4, in_att[n].data = p->colors, slot[n++] = 3;
    in.num_points = p->num_vertices;
    in.num_faces = p->num_indices / 3;
    in.indices = p->indices;
    in.num_attributes = n;
    in.attributes = in_att;

    r = ds_encode(&in, o, &blob, &blob_size, ids);
    if (r == DS_ERR_NOMEM)
        return PGR_ERR_NOMEM;
    if (r != DS_OK)
    {
        pgr_warn_once(msg, PGR_WARN_DRACO, "Draco could not compress a primitive; it is written uncompressed");
        return PGR_ERR_UNSUPPORTED;
    }
    r = ds_decode(blob, blob_size, ids, n, &out);
    if (r == DS_OK)
        for (k = 0; k < n; k++)
            if (ids[k] < 0 || out.attributes[k].data == NULL
                || out.attributes[k].num_components != in_att[k].num_components)
                r = DS_ERR_DECODE;
    if (r == DS_OK && (out.num_points == 0 || out.num_points > 0x7fffffffu || out.num_faces > 0x7fffffffu / 3))
        r = DS_ERR_DECODE;
    if (r != DS_OK)
    {
        ds_buffer_free(blob);
        ds_mesh_free(&out);
        if (r == DS_ERR_NOMEM)
            return PGR_ERR_NOMEM;
        pgr_warn_once(msg, PGR_WARN_DRACO,
                      "Draco's data for a primitive did not decode as written; it is written uncompressed");
        return PGR_ERR_UNSUPPORTED;
    }

    r = bin_append(&w->bin, blob, blob_size, &offset);
    ds_buffer_free(blob);
    if (r != PGR_OK)
    {
        ds_mesh_free(&out);
        return PGR_ERR_NOMEM;
    }
    v = &w->views[w->d.buffer_views_count++];
    v->buffer = &w->buffer;
    v->offset = offset;
    v->size = blob_size;

    cp->type = cgltf_primitive_type_triangles;
    cp->attributes = at;
    cp->has_draco_mesh_compression = 1;
    cp->draco_mesh_compression.buffer_view = v;
    cp->draco_mesh_compression.attributes = dat;
    for (k = 0; k < n; k++)
    {
        cgltf_accessor *a = add_viewless_accessor(w, cgltf_component_type_r_32f, shapes[slot[k]], out.num_points);

        if (slot[k] == 0)
        {
            const float *pos = out.attributes[k].data;
            int c;

            a->has_min = a->has_max = 1;
            for (c = 0; c < 3; c++)
                a->min[c] = a->max[c] = pos[c];
            for (i = 1; i < out.num_points; i++)
                for (c = 0; c < 3; c++)
                {
                    if (pos[3 * i + c] < a->min[c])
                        a->min[c] = pos[3 * i + c];
                    if (pos[3 * i + c] > a->max[c])
                        a->max[c] = pos[3 * i + c];
                }
        }
        at[k].name = (char *)names[slot[k]];
        at[k].type = types[slot[k]];
        at[k].data = a;
        /* The id goes into the JSON as an accessor index: see
           pgr_draco_attribute_id. pgr_glb_write checks it is in range. */
        dat[k].name = (char *)names[slot[k]];
        dat[k].type = types[slot[k]];
        dat[k].data = &w->accessors[ids[k]];
        if ((size_t)ids[k] + 1 > w->draco_ids_needed)
            w->draco_ids_needed = (size_t)ids[k] + 1;
    }
    cp->attributes_count = n;
    cp->draco_mesh_compression.attributes_count = n;
    cp->indices = add_viewless_accessor(w, out.num_points <= 65535 ? cgltf_component_type_r_16u
                                                                   : cgltf_component_type_r_32u,
                                        cgltf_type_scalar, (cgltf_size)out.num_faces * 3);
    w->draco_primitives++;
    ds_mesh_free(&out);
    return PGR_OK;
}

/* cgltf_write lists KHR_draco_mesh_compression as required only for a
   primitive with no attributes or no indices. Every Draco primitive here
   has both, but none has an uncompressed copy, so a reader without Draco
   could draw nothing: add the extension to extensionsRequired. *json is
   cgltf_write's output with its terminating zero, *size counting it; on
   success both describe a new block and the old one is freed. */
static int
require_draco(char **json, size_t *size)
{
    static const char ext[] = "\"KHR_draco_mesh_compression\"";
    static const char key[] = "\"extensionsRequired\"";
    static const char add_list[] = ",\n  \"extensionsRequired\": [\n    \"KHR_draco_mesh_compression\"\n  ]";
    static const char add_item[] = "\n    \"KHR_draco_mesh_compression\",";
    char *s = *json, *list, *at, *out;
    const char *insert;
    size_t n = *size - 1, pos, len;

    list = strstr(s, key);
    if (list != NULL)
    {
        char *open = strchr(list, '[');
        char *close = open != NULL ? strchr(open, ']') : NULL;
        char *found = strstr(list, ext);

        if (open == NULL || close == NULL)
            return PGR_ERR_WRITE;
        if (found != NULL && found < close)
            return PGR_OK;
        at = open + 1;
        insert = add_item;
    }
    else
    {
        /* Just after the last value of the root object. */
        at = s + n;
        while (at > s && at[-1] != '}')
            at--;
        if (at == s)
            return PGR_ERR_WRITE;
        at--;
        while (at > s && (at[-1] == ' ' || at[-1] == '\n' || at[-1] == '\r' || at[-1] == '\t'))
            at--;
        insert = add_list;
    }
    pos = (size_t)(at - s);
    len = strlen(insert);
    out = (char *)malloc(n + len + 1);
    if (out == NULL)
        return PGR_ERR_NOMEM;
    memcpy(out, s, pos);
    memcpy(out + pos, insert, len);
    memcpy(out + pos + len, s + pos, n - pos + 1);
    free(s);
    *json = out;
    *size = n + len + 1;
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
    int draco = options != NULL && options->draco;
    const ds_encode_options *draco_options = options != NULL ? options->draco_options : NULL;
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
    if (draco && !ds_available())
    {
        pgr_error(msg, "Draco compression was asked for, but this build has no Draco: "
                  "configure prc_glb_rw with -DPRC_GLB_WITH_DRACO=ON");
        return PGR_ERR_UNSUPPORTED;
    }

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
    w.draco_attributes = (cgltf_attribute *)calloc(5 * n_prims + 1, sizeof(cgltf_attribute));
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
    if (w.views == NULL || w.accessors == NULL || w.attributes == NULL || w.draco_attributes == NULL
        || w.primitives == NULL
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
                st = PGR_ERR_UNSUPPORTED;
                if (draco && p->mode == PGR_TRIANGLES)
                    st = add_draco_primitive(&w, p, &cm->primitives[cm->primitives_count], draco_options, msg);
                if (st == PGR_ERR_UNSUPPORTED)
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
        if (st == PGR_OK && w.draco_primitives > 0)
        {
            if (w.draco_ids_needed > w.d.accessors_count)
            {
                pgr_error(msg, "a Draco attribute id is past the last accessor, which glTF readers "
                          "built on cgltf reject");
                st = PGR_ERR_WRITE;
            }
            else
                st = require_draco(&json, &json_size);
        }
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
