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

/* PRC (or a PDF holding PRC) into the neutral scene, through nanoPRC's
   public read API.

   The call sequence is the one nanoPRC's demos use: open, build the model
   tree, count the tessellations, initialise each and fetch its vertices.
   Then the tree is walked, as stl_export walks it:

     - each product becomes a node, with its placement (parent-relative,
       column-major, as glTF's) and name;
     - a product's part, and every representation item nested in it, may
       name a tessellation. The API numbers tessellations once per place a
       part is drawn (part_detail_index), the file once per tessellation
       stored (biased_tess_index); the data is fetched by the first and
       meshes are shared by the second, so a part drawn twice becomes one
       mesh;
     - a surface tessellation becomes one triangle primitive per
       appearance (material, texture, vertex colours). Every face of a
       compressed tessellation gives the whole mesh, so only face 0 is
       read; a plain one's faces each have their own vertices, and faces
       that look the same are joined. Fans and strips are triangulated as
       nanoPRC's own exporters do it;
     - a wire tessellation becomes one primitive of line segments.

   Appearance is the inverse of pgr_prc_write's (see pgr_prc.h).

   Markups and exact geometry are skipped with a warning, and a
   tessellation nanoPRC cannot read is left out with one. */

#include "pgr_prc.h"
#include "pgr_image.h"
#include "pgr_math.h"

#include "prc_api.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    uint32_t *v;
    uint32_t n, cap;
} u32_list;

static int
u32_push(u32_list *l, uint32_t x)
{
    if (l->n == l->cap)
    {
        uint32_t cap;
        uint32_t *v;

        if (l->cap >= 0x7fffffffu)
            return PGR_ERR_NOMEM;
        cap = l->cap ? l->cap * 2 : 64;
        v = (uint32_t *)realloc(l->v, (size_t)cap * sizeof(uint32_t));
        if (v == NULL)
            return PGR_ERR_NOMEM;
        l->v = v;
        l->cap = cap;
    }
    l->v[l->n++] = x;
    return PGR_OK;
}

/* The name nanoPRC's reader gives a product or part that has none stored:
   the API has no flag for "no name", so this exact text is taken as none,
   so that an unnamed glTF node comes back unnamed. */
#define NANOPRC_UNNAMED "Unnamed Product"

static const char *
stored_name(const char *name)
{
    return (name != NULL && strcmp(name, NANOPRC_UNNAMED) != 0) ? name : NULL;
}

/* A mesh made from a list of stored tessellations, kept so that another
   product drawing the same list shares it. */
typedef struct
{
    uint32_t *stored;           /* biased_tess_index of each, in order */
    uint32_t count;
    int mesh;
} mesh_key;

/* A texture image already read: the reader's pixels, valid until the data
   is released, and the scene texture made from them (-1 if none could be). */
typedef struct
{
    const unsigned char *data;
    uint32_t w, h, ch;
    int index;
} tex_key;

typedef struct
{
    prc_context *ctx;
    prc_api_data data;
    prc_api_product *tree;
    prc_api_tess *tess;
    uint8_t *unread;            /* per tessellation: 1 when its vertices could not be read */
    uint32_t nt;
    pgr_scene *s;
    pgr_messages *msg;
    pgr_prc_read_stats *st;
    mesh_key *keys;
    size_t n_keys, cap_keys;
    tex_key *texs;
    size_t n_texs, cap_texs;
} reader;

/* Add the triangles one primitive draws over a buffer of nv vertices,
   offset by base, to out. */
static int
add_triangles(const prc_api_graphic_primitive *gp, uint32_t nv, uint32_t base, u32_list *out)
{
    size_t m;
    int st = PGR_OK;

    for (m = 0; m < gp->num_indices; m++)
        if (gp->indices[m] >= nv)
            return PGR_ERR_PARSE;
    if (gp->type == PRC_API_TRIANGLES)
    {
        for (m = 0; m + 2 < gp->num_indices && st == PGR_OK; m += 3)
        {
            st = u32_push(out, base + gp->indices[m]);
            if (st == PGR_OK)
                st = u32_push(out, base + gp->indices[m + 1]);
            if (st == PGR_OK)
                st = u32_push(out, base + gp->indices[m + 2]);
        }
    }
    else if (gp->type == PRC_API_STRIP || gp->type == PRC_API_FAN)
    {
        for (m = 2; m < gp->num_indices && st == PGR_OK; m++)
        {
            uint32_t a, b, c = gp->indices[m];

            if (gp->type == PRC_API_FAN)
            {
                a = gp->indices[0];
                b = gp->indices[m - 1];
            }
            else if (m % 2 == 0)
            {
                a = gp->indices[m - 2];
                b = gp->indices[m - 1];
            }
            else
            {
                a = gp->indices[m - 1];
                b = gp->indices[m - 2];
            }
            st = u32_push(out, base + a);
            if (st == PGR_OK)
                st = u32_push(out, base + b);
            if (st == PGR_OK)
                st = u32_push(out, base + c);
        }
    }
    return st;
}

static float
unit(float x)
{
    return x >= 0.0f ? (x <= 1.0f ? x : 1.0f) : 0.0f;    /* NaN gives 0 */
}

/* A vertex colour's alpha on 0..1. nanoPRC's reader gave a plain
   tessellation's and a wire's stored alpha as the byte, 0..255, until
   nanoPRC PR #240, and 0..1 after it; a value above 1 can only be a byte. */
static float
unit_alpha(float a)
{
    return unit(a > 1.0f ? a / 255.0f : a);
}

/* PRC's material colours are the sRGB values viewers show; glTF's are
   linear. */
static float
linear(float srgb)
{
    return (float)pgr_srgb_to_linear(unit(srgb));
}

/* glTF's view of a PRC material, the inverse of what pgr_prc_write writes:
   the diffuse colour as the base colour, its alpha (blending when below 1),
   1 - shininess as the roughness, not metallic, and the emissive colour;
   ambient, specular and shininess are kept as PRC's own view. A material
   that is black but for its emissive colour, as pgr_prc_write writes an
   unlit one, is read as unlit in that colour. */
static void
from_prc_material(const float diffuse[3], float alpha, const float ambient[3], const float specular[3],
                  const float emissive[3], float shininess, pgr_material *m)
{
    int k, dark = 1, glows = 0;

    memset(m, 0, sizeof(*m));
    for (k = 0; k < 3; k++)
    {
        m->base_color[k] = linear(diffuse[k]);
        m->emissive[k] = linear(emissive[k]);
        m->ambient[k] = unit(ambient[k]);
        m->specular[k] = unit(specular[k]);
        if (unit(diffuse[k]) > 0.0f || m->ambient[k] > 0.0f || m->specular[k] > 0.0f)
            dark = 0;
        if (m->emissive[k] > 0.0f)
            glows = 1;
    }
    m->base_color[3] = unit(alpha);
    m->metallic = 0.0f;
    m->has_phong = 1;
    m->shininess = unit(shininess);
    m->roughness = 1.0f - m->shininess;
    m->texture = -1;
    m->alpha_mode = m->base_color[3] < 1.0f ? PGR_ALPHA_BLEND : PGR_ALPHA_OPAQUE;
    m->alpha_cutoff = 0.5f;
    if (dark && glows)
    {
        for (k = 0; k < 3; k++)
        {
            m->base_color[k] = m->emissive[k];
            m->emissive[k] = 0.0f;
        }
        m->unlit = 1;
    }
}

static int
same_material(const pgr_material *a, const pgr_material *b)
{
    return memcmp(a->base_color, b->base_color, sizeof(a->base_color)) == 0
        && memcmp(a->emissive, b->emissive, sizeof(a->emissive)) == 0
        && a->metallic == b->metallic && a->roughness == b->roughness && a->has_phong == b->has_phong
        && memcmp(a->ambient, b->ambient, sizeof(a->ambient)) == 0
        && memcmp(a->specular, b->specular, sizeof(a->specular)) == 0
        && a->shininess == b->shininess && a->texture == b->texture && a->alpha_mode == b->alpha_mode
        && a->alpha_cutoff == b->alpha_cutoff && a->double_sided == b->double_sided && a->unlit == b->unlit;
}

/* The scene material equal to m, added when there is none yet. */
static int
material_index(reader *r, const pgr_material *m)
{
    pgr_material *out;
    uint32_t i;
    int idx;

    for (i = 0; i < r->s->num_materials; i++)
        if (same_material(&r->s->materials[i], m))
            return (int)i;
    idx = pgr_scene_add_material(r->s, NULL, &out);
    if (idx < 0)
        return idx;
    *out = *m;
    out->name = NULL;
    r->st->materials++;
    return idx;
}

/* The scene texture for a face's texture: its pixels encoded as PNG, once
   for each distinct image. -1 when it is not drawn: a layer that does not
   colour the surface, or an image that cannot be encoded. Further layers
   are left out. nanoPRC's reader gives no wrap modes; glTF's default,
   repeat, is PRC's too. */
static int
texture_index(reader *r, const prc_api_texture *tx)
{
    size_t i, bytes;
    pgr_texture *out;
    uint8_t *png;
    size_t png_size;
    int idx;

    if (tx->kind != PRC_API_TEXTURE_KIND_DIFFUSE)
    {
        pgr_warn_once(r->msg, PGR_WARN_TEXTURE_KIND,
                      "a texture that does not colour the surface (opacity, reflection or other) is left out");
        return -1;
    }
    if (tx->num_layers > 0)
        pgr_warn_once(r->msg, PGR_WARN_TEXTURE_LAYERS,
                      "a texture of several layers is read as its first layer alone");
    if (tx->data == NULL || tx->width == 0 || tx->height == 0 || tx->num_channels < 1
        || tx->num_channels > 4)
    {
        pgr_warn_once(r->msg, PGR_WARN_TEXTURE_IMAGE, "a texture without a usable image is left out");
        return -1;
    }
    bytes = (size_t)tx->width * tx->height * tx->num_channels;
    for (i = 0; i < r->n_texs; i++)
        if (r->texs[i].w == tx->width && r->texs[i].h == tx->height && r->texs[i].ch == tx->num_channels
            && memcmp(r->texs[i].data, tx->data, bytes) == 0)
            return r->texs[i].index;

    idx = -1;
    if (pgr_image_encode_png(tx->data, tx->width, tx->height, tx->num_channels, &png, &png_size) != PGR_OK)
        pgr_warn_once(r->msg, PGR_WARN_TEXTURE_IMAGE, "a texture without a usable image is left out");
    else
    {
        idx = pgr_scene_add_texture(r->s, NULL, &out);
        if (idx < 0)
        {
            free(png);
            return idx;
        }
        out->bytes = png;
        out->size = png_size;
        out->mime = PGR_IMAGE_PNG;
        r->st->textures++;
    }
    if (r->n_texs == r->cap_texs)
    {
        size_t cap = r->cap_texs ? r->cap_texs * 2 : 8;
        tex_key *bigger = (tex_key *)realloc(r->texs, cap * sizeof(tex_key));

        if (bigger == NULL)
            return PGR_ERR_NOMEM;
        r->texs = bigger;
        r->cap_texs = cap;
    }
    r->texs[r->n_texs].data = tx->data;
    r->texs[r->n_texs].w = tx->width;
    r->texs[r->n_texs].h = tx->height;
    r->texs[r->n_texs].ch = tx->num_channels;
    r->texs[r->n_texs++].index = idx;
    return idx;
}

/* 1 when the vertices carry colours of their own, with *translucent set
   when one of them is not opaque, and *uniform when all have one colour.

   nanoPRC's reader fills every vertex's colour whether or not the file
   stores any: with white, with 0 when the vertices carry a style of their
   own, or with the material's diffuse colour (diffuse, when not NULL;
   compressed tessellations of 3dpdf-Aero-Turbine in prc-db, for one). So
   the colours are taken as stored when some vertex has another value. A
   file whose stored colours are all white, or all the material's colour,
   is read as having none, which draws the same. (nanoPRC PR #241 gives a
   flag for this; to be used once it is merged.) */
static int
vertex_colors(const prc_api_tess_vertex_buffer *b, const float *diffuse, int *translucent, int *uniform)
{
    size_t i;
    int colored = 0;
    const float *first = NULL;

    *translucent = 0;
    *uniform = 1;
    for (i = 0; i < b->num_vertices; i++)
    {
        const float *c = b->vertices[i].color;
        int white = c[0] == 1.0f && c[1] == 1.0f && c[2] == 1.0f && unit_alpha(c[3]) == 1.0f;
        int zero = c[0] == 0.0f && c[1] == 0.0f && c[2] == 0.0f && c[3] == 0.0f;
        int material = diffuse != NULL && c[0] == diffuse[0] && c[1] == diffuse[1] && c[2] == diffuse[2]
                       && unit_alpha(c[3]) == 1.0f;

        if (!white && !zero && !material)
            colored = 1;
        if (!zero && unit_alpha(c[3]) < 1.0f)
            *translucent = 1;
        if (first == NULL)
            first = c;
        else if (memcmp(first, c, 4 * sizeof(float)) != 0)
            *uniform = 0;
    }
    return colored;
}

/* A primitive being built: the triangles (or segments) of one appearance. */
typedef struct
{
    int material;               /* scene material, or -1 */
    int textured, colored;
    float *pos, *nrm, *uv, *col;
    uint32_t count, cap;
    int all_normals;
    u32_list idx;
} group;

typedef struct
{
    group *g;
    size_t n, cap;
} group_list;

static void
groups_free(group_list *gl)
{
    size_t i;

    for (i = 0; i < gl->n; i++)
    {
        free(gl->g[i].pos);
        free(gl->g[i].nrm);
        free(gl->g[i].uv);
        free(gl->g[i].col);
        free(gl->g[i].idx.v);
    }
    free(gl->g);
}

/* The group of this appearance, added when there is none yet; its index,
   or PGR_ERR_NOMEM. */
static int
group_for(group_list *gl, int material, int textured, int colored)
{
    size_t i;
    group *g;

    for (i = 0; i < gl->n; i++)
        if (gl->g[i].material == material && gl->g[i].textured == textured && gl->g[i].colored == colored)
            return (int)i;
    if (gl->n == gl->cap)
    {
        size_t cap = gl->cap ? gl->cap * 2 : 4;
        group *bigger = (group *)realloc(gl->g, cap * sizeof(group));

        if (bigger == NULL)
            return PGR_ERR_NOMEM;
        gl->g = bigger;
        gl->cap = cap;
    }
    g = &gl->g[gl->n];
    memset(g, 0, sizeof(*g));
    g->material = material;
    g->textured = textured;
    g->colored = colored;
    g->all_normals = 1;
    return (int)gl->n++;
}

static int
grow(float **a, uint32_t cap, int per)
{
    float *bigger = (float *)realloc(*a, (size_t)cap * per * sizeof(float));

    if (bigger == NULL)
        return PGR_ERR_NOMEM;
    *a = bigger;
    return PGR_OK;
}

/* Append vertex v to g; its index in g in *out. Colours are converted from
   sRGB to linear, as material colours are. */
static int
group_add_vertex(group *g, const prc_api_vertex *v, uint32_t *out)
{
    uint32_t n = g->count;
    int k;

    if (n == g->cap)
    {
        uint32_t cap;

        if (g->cap >= 0x7fffffffu)
            return PGR_ERR_PARSE;
        cap = g->cap ? g->cap * 2 : 64;
        if (grow(&g->pos, cap, 3) != PGR_OK || grow(&g->nrm, cap, 3) != PGR_OK
            || (g->textured && grow(&g->uv, cap, 2) != PGR_OK)
            || (g->colored && grow(&g->col, cap, 4) != PGR_OK))
            return PGR_ERR_NOMEM;
        g->cap = cap;
    }
    for (k = 0; k < 3; k++)
    {
        g->pos[3 * n + k] = v->position[k];
        g->nrm[3 * n + k] = v->normal[k];
    }
    if (!v->normal_set)
        g->all_normals = 0;
    if (g->textured)
    {
        g->uv[2 * n] = v->uv[0];
        g->uv[2 * n + 1] = v->uv[1];
    }
    if (g->colored)
    {
        for (k = 0; k < 3; k++)
            g->col[4 * n + k] = linear(v->color[k]);
        g->col[4 * n + 3] = unit_alpha(v->color[3]);
    }
    *out = n;
    g->count = n + 1;
    return PGR_OK;
}

/* Where each vertex of the buffer being read went: the group it was last
   added to and its index there. A vertex used by two groups is added to
   each. */
typedef struct
{
    int *group;
    uint32_t *index;
} vertex_map;

static int
vertex_map_init(vertex_map *vm, size_t nv)
{
    size_t i;

    vm->group = (int *)malloc((nv ? nv : 1) * sizeof(int));
    vm->index = (uint32_t *)malloc((nv ? nv : 1) * sizeof(uint32_t));
    if (vm->group == NULL || vm->index == NULL)
        return PGR_ERR_NOMEM;
    for (i = 0; i < nv; i++)
        vm->group[i] = -1;
    return PGR_OK;
}

static void
vertex_map_free(vertex_map *vm)
{
    free(vm->group);
    free(vm->index);
}

/* Add the n corners in corners (indices into b) to group gi of gl. */
static int
add_corners(group_list *gl, int gi, const prc_api_tess_vertex_buffer *b, vertex_map *vm,
            const uint32_t *corners, uint32_t n)
{
    group *g = &gl->g[gi];
    uint32_t c;
    int st = PGR_OK;

    for (c = 0; c < n && st == PGR_OK; c++)
    {
        uint32_t v = corners[c];

        if (vm->group[v] != gi)
        {
            st = group_add_vertex(g, &b->vertices[v], &vm->index[v]);
            if (st != PGR_OK)
                break;
            vm->group[v] = gi;
        }
        st = u32_push(&g->idx, vm->index[v]);
    }
    return st;
}

/* The appearance of a face, as nanoPRC gives it: the face's material, or
   else the tessellation's, and its texture. For a textured face the reader
   puts the material colour in the tessellation's material without setting
   its is_material (src/prc_tri_primitives_api.c, the texture branches of
   the style lookup), with the alpha set to 1. */
typedef struct
{
    const prc_api_material *material;   /* NULL: none */
    int texture;                        /* scene texture, or -1 */
    int colored, translucent;
    int has_style_color;                /* 1: no material, and every vertex in style_color */
    float style_color[4];
} appearance;

/* A face with no material whose vertices all have one colour is coloured
   by its style: nanoPRC's reader puts the colour of a style that is a
   colour, not a material, into every vertex of a plain face. Taken as
   vertex colours, that gave 407 of the 916 files of the test corpus vertex
   colours, where 24 store any. It is read as a material of that colour
   instead, which draws the same; so is a wire, or a file that stores one
   colour for every vertex, without a material. */
static void
style_color(const prc_api_tess_vertex_buffer *b, int uniform, appearance *a)
{
    a->has_style_color = 0;
    if (a->colored && uniform && a->material == NULL && a->texture < 0 && b->num_vertices > 0)
    {
        memcpy(a->style_color, b->vertices[0].color, sizeof(a->style_color));
        a->style_color[3] = unit_alpha(a->style_color[3]);
        a->has_style_color = 1;
        a->colored = 0;
    }
}

static int
face_appearance(reader *r, const prc_api_tess *t, const prc_api_face *face,
                const prc_api_tess_vertex_buffer *b, appearance *a)
{
    int uniform;

    a->material = face->is_material ? &face->material
                : (t->is_material || face->is_texture) ? &t->tess_material : NULL;
    a->texture = -1;
    if (face->is_texture)
    {
        a->texture = texture_index(r, &face->texture);
        if (a->texture < -1)
            return a->texture;
    }
    a->colored = vertex_colors(b, a->material != NULL ? a->material->diffuse : NULL, &a->translucent, &uniform);
    style_color(b, uniform, a);
    return PGR_OK;
}

/* The scene material for a group: the PRC material (or, from
   per_vertex, a vertex's own style), its texture when textured, and when
   colored a white base colour, since PRC viewers draw vertex colours in
   place of the material colour, and glTF multiplies them by it. Without a
   PRC material a textured or coloured group gets a plain white one, as
   glTF's default material is metallic and draws dark. -1 for no material:
   none in the file, no texture and no colours. */
static int
group_material(reader *r, const appearance *a, const prc_api_vertex *per_vertex, int textured, int colored)
{
    pgr_material m;
    int k;

    if (per_vertex != NULL)
        from_prc_material(per_vertex->diffuse, per_vertex->alpha, per_vertex->tint, per_vertex->specular,
                          per_vertex->emissive, per_vertex->shininess, &m);
    else if (a->material != NULL)
        from_prc_material(a->material->diffuse, a->material->diffuse_alpha, a->material->ambient,
                          a->material->specular, a->material->emissive, a->material->shininess, &m);
    else if (a->has_style_color)
    {
        static const float black[3] = { 0.0f, 0.0f, 0.0f };

        from_prc_material(a->style_color, a->style_color[3], a->style_color, black, black, 0.0f, &m);
    }
    else if (textured || colored)
    {
        static const float white[3] = { 1.0f, 1.0f, 1.0f }, black[3] = { 0.0f, 0.0f, 0.0f };

        from_prc_material(white, 1.0f, white, black, black, 0.0f, &m);
    }
    else
        return -1;
    if (textured)
        m.texture = a->texture;
    if (colored)
    {
        for (k = 0; k < 3; k++)
            m.base_color[k] = 1.0f;
        m.unlit = 0;
        if (a->translucent)
        {
            /* The corners' own alphas apply, not the material's. */
            m.base_color[3] = 1.0f;
            m.alpha_mode = PGR_ALPHA_BLEND;
        }
    }
    return material_index(r, &m);
}

/* The groups as primitives of mesh. */
static int
emit_groups(reader *r, group_list *gl, int mode, pgr_mesh *mesh)
{
    size_t i;
    pgr_primitive *p;

    for (i = 0; i < gl->n; i++)
    {
        group *g = &gl->g[i];

        if (g->idx.n == 0)
            continue;
        if (pgr_mesh_add_primitive(mesh, mode, &p) < 0)
            return PGR_ERR_NOMEM;
        p->num_vertices = g->count;
        p->positions = g->pos;
        p->normals = mode == PGR_TRIANGLES && g->all_normals ? g->nrm : NULL;
        p->uvs = g->uv;
        p->colors = g->col;
        p->num_indices = g->idx.n;
        p->indices = g->idx.v;
        p->material = g->material;
        if (p->normals == NULL)
            free(g->nrm);
        g->pos = g->nrm = g->uv = g->col = NULL;
        g->idx.v = NULL;
        r->st->primitives++;
        if (p->colors != NULL)
            r->st->coloured++;
        if (p->uvs != NULL)
            r->st->textured++;
        if (mode == PGR_TRIANGLES)
            r->st->triangles += p->num_indices / 3;
        else
            r->st->segments += p->num_indices / 2;
    }
    return PGR_OK;
}

/* A style that is a colour, not a material, as the vertices of a
   compressed tessellation carry it: nanoPRC's reader gives such a vertex
   tri_has_material 0 and the style's colour as its colour (3D_PDF_Blu-ray
   in prc-db has a dozen of them). uniform: every vertex of the style has
   that one colour, so it is the style's, not vertex colours of the file's. */
typedef struct
{
    int32_t style, file;
    float color[4];
    int uniform;
} style_key;

typedef struct
{
    style_key *k;
    size_t n, cap, last;
} style_list;

static style_key *
style_find(style_list *sl, const prc_api_vertex *v)
{
    size_t i;

    if (sl->last < sl->n && sl->k[sl->last].style == v->style_index && sl->k[sl->last].file == v->style_file_index)
        return &sl->k[sl->last];
    for (i = 0; i < sl->n; i++)
        if (sl->k[i].style == v->style_index && sl->k[i].file == v->style_file_index)
        {
            sl->last = i;
            return &sl->k[i];
        }
    return NULL;
}

/* The colour styles of the triangles in tri, each with whether all its
   vertices have one colour. */
static int
style_colors(const prc_api_tess_vertex_buffer *b, const u32_list *tri, style_list *sl)
{
    uint32_t m;
    int c;

    sl->n = sl->last = 0;
    for (m = 0; m + 2 < tri->n; m += 3)
    {
        const prc_api_vertex *v0 = &b->vertices[tri->v[m]];
        style_key *sk;

        if (v0->tri_has_material)
            continue;
        sk = style_find(sl, v0);
        if (sk == NULL)
        {
            if (sl->n == sl->cap)
            {
                size_t cap = sl->cap ? sl->cap * 2 : 8;
                style_key *bigger = (style_key *)realloc(sl->k, cap * sizeof(style_key));

                if (bigger == NULL)
                    return PGR_ERR_NOMEM;
                sl->k = bigger;
                sl->cap = cap;
            }
            sk = &sl->k[sl->n];
            sk->style = v0->style_index;
            sk->file = v0->style_file_index;
            memcpy(sk->color, v0->color, sizeof(sk->color));
            sk->color[3] = unit_alpha(sk->color[3]);
            sk->uniform = 1;
            sl->last = sl->n++;
        }
        for (c = 0; c < 3; c++)
        {
            const float *col = b->vertices[tri->v[m + c]].color;

            if (col[0] != sk->color[0] || col[1] != sk->color[1] || col[2] != sk->color[2]
                || unit_alpha(col[3]) != sk->color[3])
                sk->uniform = 0;
        }
    }
    return PGR_OK;
}

/* 1 when all three corners' colours are 0, as the reader gives a vertex
   whose style is a material. */
static int
corners_zero(const prc_api_tess_vertex_buffer *b, const uint32_t *corners)
{
    int c, k;

    for (c = 0; c < 3; c++)
        for (k = 0; k < 4; k++)
            if (b->vertices[corners[c]].color[k] != 0.0f)
                return 0;
    return 1;
}

/* One surface tessellation as triangle primitives of mesh, one for each
   appearance. Every face of a compressed tessellation gives the whole
   mesh, so only face 0 is read; whose vertices may each carry a style of
   their own (vertices_have_style), each triangle then taking its first
   corner's: a material, or a colour style (style_colors). A plain
   tessellation's faces each have their own vertices and
   appearance; faces that look the same share a primitive. A triangle is
   textured when its face is and all its corners have coordinates. */
static int
add_surface(reader *r, uint32_t k, pgr_mesh *mesh)
{
    prc_api_tess *t = &r->tess[k];
    int compressed = t->type == PRC_API_TESS_3D_Compressed;
    size_t faces = compressed ? (t->num_faces > 0 ? 1 : 0) : t->num_faces;
    group_list gl = { NULL, 0, 0 };
    u32_list tri = { NULL, 0, 0 };
    style_list sl = { NULL, 0, 0, 0 };
    size_t f, j;
    uint32_t m;
    int st = PGR_OK;

    for (f = 0; f < faces && st == PGR_OK; f++)
    {
        const prc_api_face *face = &t->tess_faces[f];
        const prc_api_tess_vertex_buffer *b = compressed ? &t->tess_vertices : &face->face_vertices;
        appearance a;
        vertex_map vm = { NULL, NULL };
        int per_vertex = compressed && face->vertices_have_style;
        int plain_group[2][2] = { { -1, -1 }, { -1, -1 } };    /* [textured][colored] */
        int32_t last_style = -1, last_file = -1;
        int last_textured = -1, last_colored = -1, last_group = -1;

        if (face->disable_face)
            continue;
        tri.n = 0;
        for (j = 0; j < face->num_graphic_primitives && st == PGR_OK; j++)
        {
            prc_api_graphic_primitive gp;

            if (prc_api_get_graphics_primitive(r->ctx, r->data, t, (uint32_t)f, j, &gp) < 0)
                continue;
            st = add_triangles(&gp, (uint32_t)b->num_vertices, 0, &tri);
        }
        if (st != PGR_OK || tri.n == 0)
            continue;
        st = face_appearance(r, t, face, b, &a);
        if (st == PGR_OK)
            st = vertex_map_init(&vm, b->num_vertices);

        if (st == PGR_OK && per_vertex)
            st = style_colors(b, &tri, &sl);

        for (m = 0; m + 2 < tri.n && st == PGR_OK; m += 3)
        {
            const prc_api_vertex *v0 = &b->vertices[tri.v[m]];
            int textured = a.texture >= 0 && v0->uv_set && b->vertices[tri.v[m + 1]].uv_set
                           && b->vertices[tri.v[m + 2]].uv_set;
            const style_key *sk = per_vertex && !v0->tri_has_material ? style_find(&sl, v0) : NULL;
            int by_style = per_vertex && (v0->tri_has_material || (sk != NULL && sk->uniform));
            /* A triangle whose corners are 0 has a material of its own,
               not colours; one in a style colour is drawn in that. */
            int colored = a.colored && !corners_zero(b, &tri.v[m]) && !(sk != NULL && sk->uniform);
            int gi;

            if (by_style)
            {
                /* Triangles of one style come in runs; look up the group
                   only when the style or the kind of triangle changes. */
                if (v0->style_index != last_style || v0->style_file_index != last_file
                    || textured != last_textured || colored != last_colored || last_group < 0)
                {
                    int mat;

                    if (v0->tri_has_material)
                        mat = group_material(r, &a, v0, textured, colored);
                    else
                    {
                        appearance s = a;

                        s.material = NULL;
                        s.has_style_color = 1;
                        memcpy(s.style_color, sk->color, sizeof(s.style_color));
                        mat = group_material(r, &s, NULL, textured, 0);
                    }
                    if (mat < -1)
                    {
                        st = mat;
                        break;
                    }
                    last_style = v0->style_index;
                    last_file = v0->style_file_index;
                    last_textured = textured;
                    last_colored = colored;
                    last_group = group_for(&gl, mat, textured, colored);
                }
                gi = last_group;
            }
            else
            {
                if (plain_group[textured][colored] < 0)
                {
                    int mat = group_material(r, &a, NULL, textured, colored);

                    if (mat < -1)
                    {
                        st = mat;
                        break;
                    }
                    plain_group[textured][colored] = group_for(&gl, mat, textured, colored);
                }
                gi = plain_group[textured][colored];
            }
            if (gi < 0)
            {
                st = gi;
                break;
            }
            st = add_corners(&gl, gi, b, &vm, &tri.v[m], 3);
        }
        vertex_map_free(&vm);
    }
    if (st == PGR_OK)
        st = emit_groups(r, &gl, PGR_TRIANGLES, mesh);
    if (st == PGR_ERR_PARSE)
        pgr_warn(r->msg, "tessellation %u has an index past its vertices; it is left out", (unsigned)k);
    groups_free(&gl);
    free(sl.k);
    free(tri.v);
    return st;
}

/* One wire tessellation as a primitive of line segments, with the
   tessellation's material and its points' colours. */
static int
add_wire(reader *r, uint32_t k, pgr_mesh *mesh)
{
    prc_api_tess *t = &r->tess[k];
    const prc_api_tess_vertex_buffer *b = &t->tess_vertices;
    group_list gl = { NULL, 0, 0 };
    u32_list seg = { NULL, 0, 0 };
    vertex_map vm = { NULL, NULL };
    appearance a;
    uint32_t nv = (uint32_t)b->num_vertices;
    size_t j, m;
    int st = PGR_OK, gi, mat;

    for (j = 0; j < t->num_line_primitives && st == PGR_OK; j++)
    {
        prc_api_graphic_primitive gp;

        if (prc_api_get_graphics_primitive(r->ctx, r->data, t, 0, j, &gp) < 0)
            continue;
        for (m = 0; m < gp.num_indices; m++)
            if (gp.indices[m] >= nv)
                st = PGR_ERR_PARSE;
        if (st != PGR_OK)
            break;
        if (gp.type == PRC_API_LINE)
        {
            for (m = 0; m + 1 < gp.num_indices && st == PGR_OK; m += 2)
                if ((st = u32_push(&seg, gp.indices[m])) == PGR_OK)
                    st = u32_push(&seg, gp.indices[m + 1]);
        }
        else if (gp.type == PRC_API_LINE_STRIP || gp.type == PRC_API_LINE_LOOP)
        {
            for (m = 0; m + 1 < gp.num_indices && st == PGR_OK; m++)
                if ((st = u32_push(&seg, gp.indices[m])) == PGR_OK)
                    st = u32_push(&seg, gp.indices[m + 1]);
            if (gp.type == PRC_API_LINE_LOOP && gp.num_indices >= 2 && st == PGR_OK)
                if ((st = u32_push(&seg, gp.indices[gp.num_indices - 1])) == PGR_OK)
                    st = u32_push(&seg, gp.indices[0]);
        }
    }
    if (st == PGR_OK && seg.n > 0)
    {
        int uniform;

        a.material = t->is_material ? &t->tess_material : NULL;
        a.texture = -1;
        a.colored = vertex_colors(b, a.material != NULL ? a.material->diffuse : NULL, &a.translucent, &uniform);
        style_color(b, uniform, &a);
        mat = group_material(r, &a, NULL, 0, a.colored);
        gi = mat < -1 ? mat : group_for(&gl, mat, 0, a.colored);
        st = gi < 0 ? gi : vertex_map_init(&vm, nv);
        if (st == PGR_OK)
            st = add_corners(&gl, gi, b, &vm, seg.v, seg.n);
        if (st == PGR_OK)
            st = emit_groups(r, &gl, PGR_LINES, mesh);
        vertex_map_free(&vm);
    }
    if (st == PGR_ERR_PARSE)
        pgr_warn(r->msg, "wire tessellation %u has an index past its vertices; it is left out", (unsigned)k);
    groups_free(&gl);
    free(seg.v);
    return st;
}

/* The tessellations a part and its nested items draw: API index and stored
   index, depth first, as stl_export visits them. */
static int
part_tessellations(const reader *r, const prc_api_part *part, u32_list *api, u32_list *stored)
{
    const prc_api_part **stack;
    size_t depth = 0, cap = 16, i;
    int st = PGR_OK;

    stack = (const prc_api_part **)malloc(cap * sizeof(*stack));
    if (stack == NULL)
        return PGR_ERR_NOMEM;
    stack[depth++] = part;
    while (depth > 0 && st == PGR_OK)
    {
        const prc_api_part *item = stack[--depth];

        if (item->biased_tess_index > 0 && item->part_detail_index < r->nt)
        {
            st = u32_push(api, item->part_detail_index);
            if (st == PGR_OK)
                st = u32_push(stored, item->biased_tess_index);
        }
        for (i = item->num_rep_items; i > 0 && st == PGR_OK; i--)
        {
            if (depth == cap)
            {
                const prc_api_part **bigger =
                    (const prc_api_part **)realloc((void *)stack, cap * 2 * sizeof(*stack));

                if (bigger == NULL)
                {
                    st = PGR_ERR_NOMEM;
                    break;
                }
                stack = bigger;
                cap *= 2;
            }
            stack[depth++] = &item->rep_items[i - 1];
        }
    }
    free((void *)stack);
    return st;
}

/* The mesh for a part: the one already made for the same stored
   tessellations, or a new one. -1 when the part draws nothing. */
static int
mesh_for_part(reader *r, const prc_api_part *part, int *mesh_out)
{
    u32_list api = { NULL, 0, 0 }, stored = { NULL, 0, 0 };
    size_t i;
    uint32_t j;
    int st, m = -1;

    *mesh_out = -1;
    st = part_tessellations(r, part, &api, &stored);
    if (st != PGR_OK || api.n == 0)
    {
        free(api.v);
        free(stored.v);
        return st;
    }
    for (i = 0; i < r->n_keys; i++)
        if (r->keys[i].count == stored.n
            && memcmp(r->keys[i].stored, stored.v, stored.n * sizeof(uint32_t)) == 0)
        {
            *mesh_out = r->keys[i].mesh;
            free(api.v);
            free(stored.v);
            return PGR_OK;
        }

    m = pgr_scene_add_mesh(r->s, stored_name(part->name), NULL);
    if (m < 0)
        st = PGR_ERR_NOMEM;
    for (j = 0; j < api.n && st == PGR_OK; j++)
    {
        prc_api_tess *t = &r->tess[api.v[j]];
        pgr_mesh *mesh = &r->s->meshes[m];

        if (r->unread[api.v[j]])
            continue;
        if (t->type == PRC_API_TESS_3D || t->type == PRC_API_TESS_3D_Compressed)
            st = add_surface(r, api.v[j], mesh);
        else if (t->type == PRC_API_TESS_3D_Wire)
            st = add_wire(r, api.v[j], mesh);
        else
        {
            r->st->skipped++;
            pgr_warn_once(r->msg, PGR_WARN_EXTENSION,
                          "markups and exact geometry are not read; they are skipped");
        }
        if (st == PGR_ERR_PARSE)
        {
            /* Already reported, and nothing of it added: left out, as a
               tessellation nanoPRC cannot read is, and not again. */
            r->unread[api.v[j]] = 1;
            r->st->unread++;
            st = PGR_OK;
        }
    }
    if (st == PGR_OK)
    {
        if (r->n_keys == r->cap_keys)
        {
            size_t cap = r->cap_keys ? r->cap_keys * 2 : 16;
            mesh_key *bigger = (mesh_key *)realloc(r->keys, cap * sizeof(mesh_key));

            if (bigger == NULL)
                st = PGR_ERR_NOMEM;
            else
            {
                r->keys = bigger;
                r->cap_keys = cap;
            }
        }
        if (st == PGR_OK)
        {
            r->keys[r->n_keys].stored = stored.v;
            r->keys[r->n_keys].count = stored.n;
            r->keys[r->n_keys].mesh = m;
            r->n_keys++;
            stored.v = NULL;
            r->st->meshes++;
            *mesh_out = m;
        }
    }
    free(api.v);
    free(stored.v);
    return st;
}

/* A product's own matrix: its location, or the identity. */
static void
location_of(const prc_api_product *p, double out[16])
{
    if (p->location_set && !p->location.is_identity)
        memcpy(out, p->location.matrix, 16 * sizeof(double));
    else
        pgr_mat4_identity(out);
}

static int
part_draws_nothing(const reader *r, const prc_api_part *part)
{
    u32_list api = { NULL, 0, 0 }, stored = { NULL, 0, 0 };
    int none;

    if (part == NULL)
        return 1;
    if (part_tessellations(r, part, &api, &stored) != PGR_OK)
        none = 0;
    else
        none = api.n == 0;
    free(api.v);
    free(stored.v);
    return none;
}

/* A product still to place: its parent node (-1 for a root), and whether
   it is placed under the conversion matrix (a root of the PRC) or as it
   stands (a child, or a root that was under pgr_prc_write's own root). */
typedef struct
{
    const prc_api_product *p;
    int parent;
    int convert;
} pending;

static int
walk_tree(reader *r, const double root_m[16], const double conv[16], double tol)
{
    pending *stack;
    size_t depth = 0, cap, c;
    const prc_api_product *top = r->tree;
    int st = PGR_OK;

    /* Room for every root at once; the walk grows it as needed after. */
    cap = top->num_children + 64;
    stack = (pending *)malloc(cap * sizeof(*stack));
    if (stack == NULL)
        return PGR_ERR_NOMEM;
    /* The API's root is the model; its children are the file's roots. */
    if (top->is_model)
    {
        for (c = top->num_children; c > 0; c--)
        {
            stack[depth].p = &top->children[c - 1];
            stack[depth].parent = -1;
            stack[depth++].convert = 1;
        }
    }
    else
    {
        stack[depth].p = top;
        stack[depth].parent = -1;
        stack[depth++].convert = 1;
    }

    while (depth > 0 && st == PGR_OK)
    {
        pending e = stack[--depth];
        double loc[16];
        int id, mesh = -1, keep_as_is = 0;
        pgr_node *n;

        location_of(e.p, loc);
        /* pgr_prc_write's own root: placed exactly by the conversion, with
           nothing of its own to draw. Leave it out; its children are the
           scene's roots, already in glTF's terms. */
        if (e.parent < 0 && e.convert && pgr_mat4_equal(loc, root_m, tol)
            && part_draws_nothing(r, e.p->part))
        {
            if (!r->st->collapsed_root && stored_name(e.p->name) != NULL)
            {
                free(r->s->model_name);
                r->s->model_name = pgr_strdup(e.p->name);
            }
            r->st->collapsed_root = 1;
            keep_as_is = 1;
        }
        if (!keep_as_is)
        {
            if (e.p->part != NULL)
                st = mesh_for_part(r, e.p->part, &mesh);
            if (st != PGR_OK)
                break;
            id = pgr_scene_add_node(r->s, stored_name(e.p->name), &n);
            if (id < 0)
            {
                st = PGR_ERR_NOMEM;
                break;
            }
            n->mesh = mesh;
            if (e.convert)
                pgr_mat4_mul(conv, loc, n->matrix);
            else
                memcpy(n->matrix, loc, sizeof(loc));
            n->has_matrix = !pgr_mat4_is_identity(n->matrix, 0.0);
            st = e.parent < 0 ? pgr_scene_add_root(r->s, (uint32_t)id)
                              : pgr_node_add_child(r->s, (uint32_t)e.parent, (uint32_t)id);
            if (st < 0)
                break;
            st = PGR_OK;
        }
        else
            id = -1;

        /* Children, pushed in reverse so they are visited in order. */
        for (c = e.p->num_children; c > 0; c--)
        {
            if (depth == cap)
            {
                pending *bigger = (pending *)realloc(stack, cap * 2 * sizeof(*stack));

                if (bigger == NULL)
                {
                    st = PGR_ERR_NOMEM;
                    break;
                }
                stack = bigger;
                cap *= 2;
            }
            stack[depth].p = &e.p->children[c - 1];
            stack[depth].parent = id;
            stack[depth++].convert = 0;
        }
    }
    free(stack);
    return st;
}

/* The file name without its folder or extension. */
static char *
file_stem(const char *path)
{
    const char *base = path, *p, *dot;
    size_t n;
    char *out;

    for (p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    dot = strrchr(base, '.');
    n = dot != NULL && dot != base ? (size_t)(dot - base) : strlen(base);
    out = (char *)malloc(n + 1);
    if (out != NULL)
    {
        memcpy(out, base, n);
        out[n] = '\0';
    }
    return out;
}

int
pgr_prc_read(const char *path, const pgr_prc_read_options *options, pgr_scene *s,
             pgr_prc_read_stats *stats)
{
    pgr_prc_read_stats st_local;
    pgr_prc_write_options wo;
    pgr_scene unit;
    reader r;
    double root_m[16], conv[16], tol;
    uint32_t np = 0, nprod = 0, nm = 0, nlt = 0, nx = 0, k;
    uint8_t has_lines = 0;
    size_t i, f;
    int st = PGR_OK;

    if (stats == NULL)
        stats = &st_local;
    memset(stats, 0, sizeof(*stats));
    if (path == NULL || s == NULL)
        return PGR_ERR_ARG;
    memset(&r, 0, sizeof(r));
    r.s = s;
    r.st = stats;
    r.msg = options != NULL ? options->messages : NULL;

    /* The matrix pgr_prc_write gives its root under the same options, and
       its inverse, which takes the PRC's units and axes to glTF's. */
    memset(&wo, 0, sizeof(wo));
    if (options != NULL)
    {
        wo.no_axis_convert = options->no_axis_convert;
        wo.units_per_metre = options->units_per_metre;
    }
    pgr_scene_init(&unit);
    pgr_prc_root_matrix(&unit, &wo, root_m);
    if (!pgr_mat4_invert(root_m, conv))
        return PGR_ERR_ARG;
    tol = 1e-9 * (fabs(root_m[0]) + fabs(root_m[6]) + fabs(root_m[9]) + fabs(root_m[5]));

    r.ctx = prc_api_new_context(NULL);
    if (r.ctx == NULL)
        return PGR_ERR_NOMEM;
    r.data = prc_api_open_contents(r.ctx, path);
    if (r.data == NULL)
    {
        pgr_error(r.msg, "%s: no PRC could be read", path);
        prc_api_release_context(r.ctx);
        return PGR_ERR_PARSE;
    }
    if (prc_api_prep_model_tree(r.ctx, r.data, &np, &nprod, &nm) != 0
        || prc_api_create_model_tree(r.ctx, r.data, &r.tree, np, nprod, nm) != 0
        || r.tree == NULL
        || prc_api_get_number_tessellations(r.ctx, r.data, r.tree, &r.nt, &nlt, &nx, 0) != 0)
    {
        pgr_error(r.msg, "%s: the model tree could not be read", path);
        prc_api_print_error_stack(r.ctx);
        st = PGR_ERR_PARSE;
    }

    if (st == PGR_OK)
    {
        r.tess = (prc_api_tess *)calloc(r.nt ? r.nt : 1, sizeof(prc_api_tess));
        r.unread = (uint8_t *)calloc(r.nt ? r.nt : 1, 1);
        if (r.tess == NULL || r.unread == NULL)
            st = PGR_ERR_NOMEM;
    }
    for (k = 0; st == PGR_OK && k < r.nt; k++)
    {
        prc_api_tess *t = &r.tess[k];

        t->num_faces = prc_api_get_number_faces(r.ctx, r.data, k);
        t->tess_faces = (prc_api_face *)calloc(t->num_faces ? t->num_faces : 1, sizeof(prc_api_face));
        if (t->tess_faces == NULL)
        {
            st = PGR_ERR_NOMEM;
            break;
        }
        if (prc_api_initialize_tessellation(r.ctx, r.data, r.tree, k, t, NULL, &has_lines) < 0)
        {
            t->type = PRC_API_TESS_UNKNOWN;
            continue;
        }
        if (t->type == PRC_API_TESS_3D_Wire)
        {
            if (prc_api_get_tessellation_vertices(r.ctx, r.data, k, 0, NULL, t) < 0)
                r.unread[k] = 1;
        }
        else if (t->type == PRC_API_TESS_3D || t->type == PRC_API_TESS_3D_Compressed)
        {
            for (f = 0; f < t->num_faces; f++)
                if (prc_api_get_tessellation_vertices(r.ctx, r.data, k, (uint32_t)f, &t->tess_faces[f], t) < 0)
                {
                    r.unread[k] = 1;
                    break;
                }
        }
        if (r.unread[k])
        {
            /* Left out, not the whole file: nanoPRC's reader refuses some
               tessellations that other readers draw. */
            pgr_warn(r.msg, "%s: tessellation %u could not be read; it is left out", path, (unsigned)k);
            stats->unread++;
        }
    }

    if (st == PGR_OK)
        st = walk_tree(&r, root_m, conv, tol);
    if (st == PGR_OK && s->model_name == NULL)
    {
        s->model_name = file_stem(path);
        if (s->model_name == NULL)
            st = PGR_ERR_NOMEM;
    }
    s->up_axis = (options != NULL && options->no_axis_convert) ? PGR_Z_UP : PGR_Y_UP;
    s->unit_scale_to_m = 1.0;
    stats->nodes = s->num_nodes;

    if (r.data != NULL)
        prc_api_release_data(r.ctx, r.data, r.tess, r.tess != NULL ? r.nt : 0, NULL, 0, NULL, 0, r.tree);
    if (r.tess != NULL)
    {
        /* release_data frees what the reader allocated inside each
           tessellation, not the face arrays allocated here. */
        for (k = 0; k < r.nt; k++)
            free(r.tess[k].tess_faces);
        free(r.tess);
    }
    free(r.unread);
    free(r.texs);
    prc_api_release_context(r.ctx);
    for (i = 0; i < r.n_keys; i++)
        free(r.keys[i].stored);
    free(r.keys);
    if (st == PGR_OK && pgr_scene_check(s) != PGR_OK)
    {
        pgr_error(r.msg, "%s: the model tree is not a tree", path);
        st = PGR_ERR_PARSE;
    }
    return st;
}
