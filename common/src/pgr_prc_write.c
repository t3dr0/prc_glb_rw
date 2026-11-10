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

/* The neutral scene out as PRC, and optionally a 3D PDF, through nanoPRC's
   write API (prc_api_write_prc_buffer, prc_api_pdf_embed_prc).

   That API has three levels: tessellations, which hold geometry;
   representation items, which point at a tessellation by its 1-based
   index; and tree nodes, which hold representation items and children.
   The scene maps onto them directly:

     - each primitive with something to draw becomes one tessellation:
       triangles as compressed (or, with the triangles option, plain)
       tessellation, line segments as a wire tessellation whose connected
       segments are joined into polylines;
     - each node becomes a tree node with its matrix and name; a node with
       a mesh gets one representation item per primitive, all pointing at
       that mesh's tessellations, so a mesh used by several nodes is
       written once;
     - the scene's roots go under one more node, the root, whose matrix
       turns the scene into PRC's axes and units (pgr_prc_root_matrix).

   Appearance:

     - a primitive's material goes on each item that draws it: the base
       colour as the diffuse (and ambient) colour, its alpha when the
       material blends, 1 - roughness as the shininess, and the emissive
       colour; an unlit material is shown by its emissive colour alone; a
       masked material is opaque, and a primitive it hides entirely is
       left out (masked_out);
     - glTF colours are linear and PRC viewers show the stored values as
       they are, so every colour is converted to sRGB;
     - vertex colours become one colour per triangle corner (or per wire
       point), multiplied by an untextured material's base colour as glTF
       multiplies them, since viewers draw them without the material
       colour; their alpha is kept only when the material blends;
     - a base colour texture is embedded as the file's own PNG or JPEG
       bytes, multiplied by the base colour as glTF does, with texture
       coordinates written only for primitives that are textured, v turned
       over (v' = 1 - v) because glTF puts v = 0 at the top of the image;
     - a primitive without a material keeps the writer's default. */

#include "pgr_prc.h"
#include "pgr_math.h"

#include "prc_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Everything handed to the write API, freed together. */
typedef struct
{
    prc_api_write_tessellation *tess;
    uint32_t n_tess;
    int *tess_of;                       /* flat (mesh, primitive) -> tessellation, or -1 */
    uint32_t *prim_base;                /* per mesh: its first entry in tess_of */
    uint32_t *tri_counts;               /* per tessellation: its one face's triangles */
    uint8_t *textured;                  /* per tessellation: 1 when it carries texture coordinates */
    prc_api_write_node *nodes;          /* scene nodes, then the root */
    prc_api_write_rep_item *items;
    prc_api_write_node **child_ptrs;
    void **owned;                       /* arrays made for the write API */
    size_t n_owned, cap_owned;
} prc_out;

static void *
own(prc_out *w, size_t bytes)
{
    void *p;

    if (w->n_owned == w->cap_owned)
    {
        size_t cap = w->cap_owned ? w->cap_owned * 2 : 64;
        void **bigger = (void **)realloc(w->owned, cap * sizeof(void *));

        if (bigger == NULL)
            return NULL;
        w->owned = bigger;
        w->cap_owned = cap;
    }
    p = calloc(1, bytes ? bytes : 1);
    if (p != NULL)
        w->owned[w->n_owned++] = p;
    return p;
}

static void
prc_out_free(prc_out *w)
{
    size_t i;

    for (i = 0; i < w->n_owned; i++)
        free(w->owned[i]);
    free(w->owned);
    free(w->tess);
    free(w->tess_of);
    free(w->prim_base);
    free(w->tri_counts);
    free(w->textured);
    free(w->nodes);
    free(w->items);
    free(w->child_ptrs);
}

static double *
to_doubles(prc_out *w, const float *f, size_t n)
{
    double *d = (double *)own(w, n * sizeof(double));
    size_t i;

    if (d != NULL)
        for (i = 0; i < n; i++)
            d[i] = f[i];
    return d;
}

/* The primitive's material, or NULL. */
static const pgr_material *
material_of(const pgr_scene *s, const pgr_primitive *p)
{
    return p->material >= 0 ? &s->materials[p->material] : NULL;
}

/* 1 when the primitive is drawn with its material's texture: there is one,
   textures are wanted, and the primitive has coordinates to sample it with. */
static int
is_textured(const pgr_scene *s, const pgr_primitive *p, const pgr_prc_write_options *o,
            pgr_messages *msg)
{
    const pgr_material *m = material_of(s, p);

    if (o->no_textures || p->mode != PGR_TRIANGLES || m == NULL || m->texture < 0)
        return 0;
    if (p->uvs == NULL)
    {
        pgr_warn_once(msg, PGR_WARN_NO_UVS,
                      "a primitive with a textured material has no TEXCOORD_0; it is drawn without the texture");
        return 0;
    }
    return 1;
}

/* 1 when alphaMode MASK hides all of the primitive, which is then left out.

   glTF draws a pixel of a masked material only where its alpha (the base
   colour's, times the texture's and the vertex colour's) is at least
   alphaCutoff, and draws it opaque; PRC has no such test. Without a
   texture the alpha is known at every corner and varies linearly between
   them, so the test can be made here: below the cutoff at every corner,
   nothing is drawn; at or above it at every corner, all of it is, opaque,
   as the item's material is written. Otherwise (a texture, or corners
   either side of the cutoff) the primitive is drawn opaque, with a
   warning. */
static int
masked_out(const pgr_scene *s, const pgr_primitive *p, const pgr_prc_write_options *o,
           pgr_messages *msg)
{
    const pgr_material *m = material_of(s, p);
    float lo, hi;
    uint32_t k;

    if (m == NULL || m->alpha_mode != PGR_ALPHA_MASK)
        return 0;
    if (is_textured(s, p, o, msg))
    {
        pgr_warn_once(msg, PGR_WARN_ALPHA_MASK,
                      "alpha masking (alphaMode MASK) with a texture has no PRC equivalent; it is drawn opaque");
        return 0;
    }
    lo = hi = m->base_color[3];
    if (p->colors != NULL && !o->no_colors)
    {
        lo = 1.0f;
        hi = 0.0f;
        for (k = 0; k < p->num_indices; k++)
        {
            float a = m->base_color[3] * p->colors[4 * p->indices[k] + 3];

            if (a < lo)
                lo = a;
            if (a > hi)
                hi = a;
        }
    }
    if (hi < m->alpha_cutoff)
    {
        pgr_warn_once(msg, PGR_WARN_MASKED_OUT,
                      "a primitive whose alpha is below its material's alphaCutoff everywhere is left out, "
                      "as glTF draws none of it");
        return 1;
    }
    if (lo < m->alpha_cutoff)
        pgr_warn_once(msg, PGR_WARN_ALPHA_MASK,
                      "alpha masking (alphaMode MASK) of part of a primitive has no PRC equivalent; "
                      "it is drawn opaque");
    return 0;
}

/* What vertex colours are multiplied by, in linear RGBA.

   glTF multiplies a vertex colour by the material's base colour. Adobe
   Acrobat and PDF-XChange Editor draw a corner's own colour without the
   item's material colour (checked by eye on 10 November 2026: a triangle
   with a grey material looked the same as one without), so the base colour
   is multiplied in here; not for a textured primitive, whose image the
   material colour already multiplies. glTF uses alpha only when the
   material blends, so otherwise the factor's alpha is 0, meaning opaque. */
static void
color_factor(const pgr_material *m, int textured, float f[4])
{
    int k;

    for (k = 0; k < 4; k++)
        f[k] = m != NULL && !textured ? m->base_color[k] : 1.0f;
    if (m == NULL || m->alpha_mode != PGR_ALPHA_BLEND)
        f[3] = 0.0f;
    else if (textured)
        f[3] = 1.0f;
}

/* Vertex v's colour times f (color_factor) in linear values, then in sRGB;
   its alpha times f's, or 1 when f's alpha is 0. */
static void
srgb_color(const float *colors, uint32_t v, const float f[4], float out[4])
{
    int k;

    for (k = 0; k < 3; k++)
        out[k] = (float)pgr_linear_to_srgb(colors[4 * v + k] * f[k]);
    out[3] = f[3] > 0.0f ? colors[4 * v + 3] * f[3] : 1.0f;
}

/* One colour per triangle corner, in sRGB, with alpha only when some corner
   is not opaque. */
static int
corner_colors(prc_out *w, const pgr_primitive *p, const float f[4], prc_api_write_tessellation *t)
{
    uint32_t k, n = p->num_indices;
    int alpha = 0;
    uint8_t *c;

    for (k = 0; k < n; k++)
    {
        float rgba[4];

        srgb_color(p->colors, p->indices[k], f, rgba);
        if (rgba[3] < 1.0f)
            alpha = 1;
    }
    c = (uint8_t *)own(w, (size_t)n * (alpha ? 4 : 3));
    if (c == NULL)
        return PGR_ERR_NOMEM;
    for (k = 0; k < n; k++)
    {
        float rgba[4];
        int j;

        srgb_color(p->colors, p->indices[k], f, rgba);
        for (j = 0; j < (alpha ? 4 : 3); j++)
            c[(size_t)k * (alpha ? 4 : 3) + j] = pgr_unit_to_byte(rgba[j]);
    }
    t->vertex_colors = c;
    t->num_vertex_colors = n;
    t->vertex_colors_have_alpha = alpha;
    return PGR_OK;
}

/* A triangle primitive as a compressed or plain tessellation. */
static int
triangle_tessellation(prc_out *w, const pgr_primitive *p, const pgr_material *m,
                      const pgr_prc_write_options *o, double quant, int textured,
                      prc_api_write_tessellation *t, uint32_t *tri_count)
{
    t->kind = o->triangles ? PRC_API_WRITE_TESS_KIND_TRIANGLES : PRC_API_WRITE_TESS_KIND_COMPRESSED;
    t->positions = to_doubles(w, p->positions, (size_t)p->num_vertices * 3);
    if (t->positions == NULL)
        return PGR_ERR_NOMEM;
    t->num_positions = p->num_vertices;
    t->tri_indices = p->indices;
    t->num_triangles = p->num_indices / 3;
    *tri_count = t->num_triangles;
    t->face_tri_counts = tri_count;
    t->num_faces = 1;
    if (t->kind == PRC_API_WRITE_TESS_KIND_COMPRESSED)
        t->tolerance = prc_write_tol_relative(quant);
    if (p->normals != NULL && !o->no_normals)
    {
        /* One normal per vertex, so the normal indices are the vertex
           indices. */
        t->normals = to_doubles(w, p->normals, (size_t)p->num_vertices * 3);
        if (t->normals == NULL)
            return PGR_ERR_NOMEM;
        t->num_normals = p->num_vertices;
        t->norm_indices = p->indices;
    }
    else if (t->kind == PRC_API_WRITE_TESS_KIND_TRIANGLES)
    {
        /* Without normals a plain tessellation would give the whole face one
           flat normal; have the reader compute one per vertex instead.
           Compressed recomputes them by itself. */
        t->must_calculate_normals = 1;
    }
    if (textured)
    {
        double *tc = (double *)own(w, (size_t)p->num_vertices * 2 * sizeof(double));
        uint32_t v;

        if (tc == NULL)
            return PGR_ERR_NOMEM;
        for (v = 0; v < p->num_vertices; v++)
        {
            tc[2 * v] = p->uvs[2 * v];
            tc[2 * v + 1] = 1.0 - (double)p->uvs[2 * v + 1];
        }
        t->tex_coords = tc;
        t->num_tex_coords = p->num_vertices;
        t->tex_indices = p->indices;
    }
    if (p->colors != NULL && !o->no_colors)
    {
        float f[4];

        color_factor(m, textured, f);
        return corner_colors(w, p, f, t);
    }
    return PGR_OK;
}

/* Point v of a wire: its position, and its colour in sRGB times f
   (color_factor) when cols is not NULL. */
static void
wire_point(const pgr_primitive *p, uint32_t v, const float f[4], float *pts, float *cols, size_t at)
{
    memcpy(pts + 3 * at, &p->positions[3 * v], 3 * sizeof(float));
    if (cols != NULL)
        srgb_color(p->colors, v, f, cols + 4 * at);
}

/* A line primitive as a wire tessellation: each run of segments where one
   ends where the next begins becomes one polyline. */
static int
wire_tessellation(prc_out *w, const pgr_primitive *p, const pgr_material *m,
                  const pgr_prc_write_options *o, prc_api_write_tessellation *t)
{
    uint32_t segs = p->num_indices / 2, k = 0, runs = 0;
    prc_api_write_wire_element *el;
    float *pts, *cols = NULL;
    float f[4];
    size_t used = 0;

    color_factor(m, 0, f);
    /* At most one element per segment, and two points per segment. */
    el = (prc_api_write_wire_element *)own(w, (size_t)segs * sizeof(*el));
    pts = (float *)own(w, (size_t)segs * 6 * sizeof(float));
    if (el == NULL || pts == NULL)
        return PGR_ERR_NOMEM;
    if (p->colors != NULL && !o->no_colors)
    {
        cols = (float *)own(w, (size_t)segs * 8 * sizeof(float));
        if (cols == NULL)
            return PGR_ERR_NOMEM;
    }
    while (k < segs)
    {
        size_t first = used;
        uint32_t n = 0;

        wire_point(p, p->indices[2 * k], f, pts, cols, used++);
        n++;
        do
        {
            wire_point(p, p->indices[2 * k + 1], f, pts, cols, used++);
            n++;
            k++;
        } while (k < segs && p->indices[2 * k] == p->indices[2 * k - 1]);
        el[runs].positions = pts + 3 * first;
        el[runs].colors = cols != NULL ? cols + 4 * first : NULL;
        el[runs].num_vertices = n;
        runs++;
    }
    t->kind = PRC_API_WRITE_TESS_KIND_WIRE;
    t->wire_elements = el;
    t->num_wire_elements = runs;
    return PGR_OK;
}

/* The primitive's material on a representation item. glTF multiplies the
   texture by the base colour, as nanoPRC's default texture function
   (modulate) does, so the base colour stays the item's colour whether or
   not it is textured. */
static void
item_material(const pgr_scene *s, const pgr_primitive *p, int textured, prc_api_write_rep_item *it,
              pgr_messages *msg)
{
    const pgr_material *m = material_of(s, p);
    double rgb[3];
    int k, emits = 0;

    if (m == NULL)
        return;
    it->has_material = 1;
    for (k = 0; k < 3; k++)
    {
        rgb[k] = pgr_linear_to_srgb(m->base_color[k]);
        it->material_color[k] = rgb[k];
        if (m->emissive[k] > 0.0f)
            emits = 1;
    }
    it->material_alpha = 1.0;
    if (m->alpha_mode == PGR_ALPHA_BLEND)
        it->material_alpha = m->base_color[3] < 0.0f ? 0.0 : m->base_color[3] > 1.0f ? 1.0 : m->base_color[3];
    it->material_shininess = m->roughness < 0.0f ? 1.0 : m->roughness > 1.0f ? 0.0 : 1.0 - m->roughness;

    if (m->unlit && textured)
        pgr_warn_once(msg, PGR_WARN_UNLIT,
                      "an unlit material with a texture is written as an ordinary, lit one");
    if (m->unlit && !textured)
    {
        /* Shown by its emissive colour alone, the nearest PRC comes to no
           lighting. */
        it->has_material_slots = 1;
        for (k = 0; k < 3; k++)
        {
            it->material_color[k] = 0.0;
            it->material_ambient[k] = 0.0;
            it->material_specular[k] = 0.0;
            it->material_emissive[k] = rgb[k];
        }
        it->material_shininess = 0.0;
    }
    else if (emits)
    {
        /* Given its own slots, the ambient keeps the base colour and the
           specular nanoPRC's usual dark grey, as without them. */
        it->has_material_slots = 1;
        for (k = 0; k < 3; k++)
        {
            it->material_ambient[k] = rgb[k];
            it->material_specular[k] = 0.2;
            it->material_emissive[k] = pgr_linear_to_srgb(m->emissive[k]);
        }
    }

    if (textured)
    {
        const pgr_texture *tx = &s->textures[m->texture];

        it->has_texture = 1;
        it->texture_image = tx->bytes;
        it->texture_image_size = tx->size;
        it->texture_format = tx->mime == PGR_IMAGE_JPEG ? PRC_API_WRITE_TEXTURE_JPEG : PRC_API_WRITE_TEXTURE_PNG;
        it->texture_function = PRC_API_WRITE_TEXTURE_MODULATE;
        if (tx->wrap_s == PGR_WRAP_MIRROR || tx->wrap_t == PGR_WRAP_MIRROR)
            pgr_warn_once(msg, PGR_WARN_WRAP, "mirrored repeat has no PRC equivalent; it is written as repeat");
        it->texture_wrap_u = tx->wrap_s == PGR_WRAP_CLAMP
                           ? PRC_API_WRITE_TEXTURE_WRAP_CLAMP_TO_BORDER : PRC_API_WRITE_TEXTURE_WRAP_REPEAT;
        it->texture_wrap_v = tx->wrap_t == PGR_WRAP_CLAMP
                           ? PRC_API_WRITE_TEXTURE_WRAP_CLAMP_TO_BORDER : PRC_API_WRITE_TEXTURE_WRAP_REPEAT;
    }
}

static void
box_add(double mn[3], double mx[3], int *has, const double p[3])
{
    int k;

    for (k = 0; k < 3; k++)
    {
        if (!*has || p[k] < mn[k])
            mn[k] = p[k];
        if (!*has || p[k] > mx[k])
            mx[k] = p[k];
    }
    *has = 1;
}

/* Add the box (cmn, cmx), moved by matrix m, to (mn, mx). */
static void
box_add_moved(double mn[3], double mx[3], int *has, const double cmn[3], const double cmx[3],
              const double m[16])
{
    int c;

    for (c = 0; c < 8; c++)
    {
        double corner[3], moved[3];

        corner[0] = (c & 1) ? cmx[0] : cmn[0];
        corner[1] = (c & 2) ? cmx[1] : cmn[1];
        corner[2] = (c & 4) ? cmx[2] : cmn[2];
        pgr_mat4_transform_point(m, corner, moved);
        box_add(mn, mx, has, moved);
    }
}

void
pgr_prc_root_matrix(const pgr_scene *s, const pgr_prc_write_options *o, double out[16])
{
    double scale = (o != NULL && o->units_per_metre > 0.0) ? o->units_per_metre : PGR_UNITS_PER_METRE_MM;
    int turn = s->up_axis == PGR_Y_UP && !(o != NULL && o->no_axis_convert);

    scale *= s->unit_scale_to_m > 0.0 ? s->unit_scale_to_m : 1.0;
    if (turn)
        pgr_mat4_y_up_to_z_up(scale, out);
    else
    {
        pgr_mat4_identity(out);
        out[0] = out[5] = out[10] = scale;
    }
}

/* Each node's box, in its own coordinates: its own geometry, and its
   children's boxes moved by their matrices. Children come after their
   parents in the breadth-first order, so walking that order backwards
   finishes every child before its parent. */
static int
node_boxes(const pgr_scene *s, prc_out *w)
{
    uint32_t *order, *placed;
    int *has;
    double (*mn)[3], (*mx)[3];
    uint32_t n = 0, head = 0, i, j, r;
    int st = PGR_OK;

    order = (uint32_t *)calloc(s->num_nodes + 1, sizeof(uint32_t));
    placed = (uint32_t *)calloc(s->num_nodes + 1, sizeof(uint32_t));
    has = (int *)calloc(s->num_nodes + 1, sizeof(int));
    mn = (double (*)[3])calloc(s->num_nodes + 1, sizeof(*mn));
    mx = (double (*)[3])calloc(s->num_nodes + 1, sizeof(*mx));
    if (order == NULL || placed == NULL || has == NULL || mn == NULL || mx == NULL)
        st = PGR_ERR_NOMEM;
    for (r = 0; st == PGR_OK && r < s->num_roots; r++)
        if (!placed[s->roots[r]])
        {
            placed[s->roots[r]] = 1;
            order[n++] = s->roots[r];
        }
    while (st == PGR_OK && head < n)
    {
        const pgr_node *nd = &s->nodes[order[head++]];

        for (j = 0; j < nd->num_children; j++)
            if (!placed[nd->children[j]])
            {
                placed[nd->children[j]] = 1;
                order[n++] = nd->children[j];
            }
    }
    for (i = n; st == PGR_OK && i-- > 0;)
    {
        uint32_t id = order[i];
        const pgr_node *nd = &s->nodes[id];

        if (nd->mesh >= 0)
        {
            const pgr_mesh *m = &s->meshes[nd->mesh];

            for (j = 0; j < m->num_primitives; j++)
            {
                float fmn[3], fmx[3];

                if (w->tess_of[w->prim_base[nd->mesh] + j] >= 0
                    && pgr_primitive_bounds(&m->primitives[j], fmn, fmx))
                {
                    double a[3], b[3];
                    int k;

                    for (k = 0; k < 3; k++)
                    {
                        a[k] = fmn[k];
                        b[k] = fmx[k];
                    }
                    box_add(mn[id], mx[id], &has[id], a);
                    box_add(mn[id], mx[id], &has[id], b);
                }
            }
        }
        for (j = 0; j < nd->num_children; j++)
        {
            uint32_t c = nd->children[j];

            if (has[c])
                box_add_moved(mn[id], mx[id], &has[id], mn[c], mx[c], s->nodes[c].matrix);
        }
        memcpy(w->nodes[id].bbox_min, mn[id], sizeof(mn[id]));
        memcpy(w->nodes[id].bbox_max, mx[id], sizeof(mx[id]));
    }
    /* The root's box: its children, the scene's roots, as placed. */
    if (st == PGR_OK)
    {
        prc_api_write_node *root = &w->nodes[s->num_nodes];
        double rmn[3] = { 0, 0, 0 }, rmx[3] = { 0, 0, 0 };
        int rhas = 0;

        for (r = 0; r < s->num_roots; r++)
            if (has[s->roots[r]])
                box_add_moved(rmn, rmx, &rhas, mn[s->roots[r]], mx[s->roots[r]],
                              s->nodes[s->roots[r]].matrix);
        memcpy(root->bbox_min, rmn, sizeof(rmn));
        memcpy(root->bbox_max, rmx, sizeof(rmx));
    }
    free(order);
    free(placed);
    free(has);
    free(mn);
    free(mx);
    return st;
}

static int
write_file(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    int ok;

    if (f == NULL)
        return PGR_ERR_WRITE;
    ok = fwrite(data, 1, size, f) == size;
    if (fclose(f) != 0)
        ok = 0;
    return ok ? PGR_OK : PGR_ERR_WRITE;
}

int
pgr_prc_write(const pgr_scene *s, const char *prc_path, const char *pdf_path,
              const pgr_prc_write_options *options, pgr_prc_write_stats *stats)
{
    pgr_prc_write_options defaults;
    const pgr_prc_write_options *o = options;
    pgr_messages *msg;
    prc_out w;
    prc_context *ctx = NULL;
    uint8_t *buf = NULL;
    size_t size = 0, n_prims = 0, n_items = 0, n_children = 0;
    double quant;
    pgr_prc_write_stats st_local;
    uint32_t i, j;
    int st = PGR_OK;

    if (o == NULL)
    {
        memset(&defaults, 0, sizeof(defaults));
        o = &defaults;
    }
    msg = o->messages;
    quant = o->quant_tolerance > 0.0 ? o->quant_tolerance : PGR_DEFAULT_QUANT_TOLERANCE;
    if (stats == NULL)
        stats = &st_local;
    memset(stats, 0, sizeof(*stats));
    if (s == NULL || prc_path == NULL)
        return PGR_ERR_ARG;
    if (pgr_scene_check(s) != PGR_OK)
    {
        pgr_error(msg, "the scene to write is inconsistent");
        return PGR_ERR_ARG;
    }

    memset(&w, 0, sizeof(w));
    for (i = 0; i < s->num_meshes; i++)
        n_prims += s->meshes[i].num_primitives;
    for (i = 0; i < s->num_nodes; i++)
    {
        n_children += s->nodes[i].num_children;
        if (s->nodes[i].mesh >= 0)
            n_items += s->meshes[s->nodes[i].mesh].num_primitives;
    }
    w.tess = (prc_api_write_tessellation *)calloc(n_prims + 1, sizeof(*w.tess));
    w.tess_of = (int *)malloc((n_prims + 1) * sizeof(int));
    w.prim_base = (uint32_t *)calloc(s->num_meshes + 1, sizeof(uint32_t));
    w.tri_counts = (uint32_t *)calloc(n_prims + 1, sizeof(uint32_t));
    w.textured = (uint8_t *)calloc(n_prims + 1, sizeof(uint8_t));
    w.nodes = (prc_api_write_node *)calloc((size_t)s->num_nodes + 1, sizeof(*w.nodes));
    w.items = (prc_api_write_rep_item *)calloc(n_items + 1, sizeof(*w.items));
    w.child_ptrs = (prc_api_write_node **)calloc(n_children + s->num_roots + 1, sizeof(*w.child_ptrs));
    if (w.tess == NULL || w.tess_of == NULL || w.prim_base == NULL || w.tri_counts == NULL
        || w.textured == NULL || w.nodes == NULL || w.items == NULL || w.child_ptrs == NULL)
    {
        prc_out_free(&w);
        return PGR_ERR_NOMEM;
    }

    /* Tessellations, one per primitive with something to draw. */
    {
        uint32_t base = 0;

        for (i = 0; i < s->num_meshes && st == PGR_OK; i++)
        {
            const pgr_mesh *m = &s->meshes[i];

            w.prim_base[i] = base;
            for (j = 0; j < m->num_primitives && st == PGR_OK; j++)
            {
                const pgr_primitive *p = &m->primitives[j];
                prc_api_write_tessellation *t = &w.tess[w.n_tess];

                w.tess_of[base + j] = -1;
                if (p->num_vertices == 0 || p->num_indices == 0)
                    continue;
                if (masked_out(s, p, o, msg))
                {
                    stats->masked_out++;
                    continue;
                }
                if (p->mode == PGR_LINES)
                {
                    st = wire_tessellation(&w, p, material_of(s, p), o, t);
                    stats->wires++;
                    stats->segments += p->num_indices / 2;
                }
                else
                {
                    w.textured[w.n_tess] = (uint8_t)is_textured(s, p, o, msg);
                    st = triangle_tessellation(&w, p, material_of(s, p), o, quant, w.textured[w.n_tess], t,
                                               &w.tri_counts[w.n_tess]);
                    if (t->kind == PRC_API_WRITE_TESS_KIND_COMPRESSED)
                        stats->compressed++;
                    stats->triangles += p->num_indices / 3;
                }
                if (p->colors != NULL && !o->no_colors)
                    stats->coloured++;
                w.tess_of[base + j] = (int)w.n_tess++;
            }
            base += m->num_primitives;
        }
    }

    /* Nodes, their representation items and children, and the root. */
    if (st == PGR_OK)
    {
        prc_api_write_rep_item *next_item = w.items;
        prc_api_write_node **next_child = w.child_ptrs;
        prc_api_write_node *root = &w.nodes[s->num_nodes];

        for (i = 0; i < s->num_nodes; i++)
        {
            const pgr_node *nd = &s->nodes[i];
            prc_api_write_node *wn = &w.nodes[i];

            wn->name = nd->name;
            if (nd->has_matrix && !pgr_mat4_is_identity(nd->matrix, 0.0))
            {
                wn->has_transform = 1;
                memcpy(wn->transform, nd->matrix, sizeof(wn->transform));
            }
            if (nd->mesh >= 0)
            {
                const pgr_mesh *m = &s->meshes[nd->mesh];

                wn->rep_items = next_item;
                wn->part_name = m->name;
                for (j = 0; j < m->num_primitives; j++)
                {
                    int t = w.tess_of[w.prim_base[nd->mesh] + j];

                    if (t < 0)
                        continue;
                    next_item->kind = w.tess[t].kind == PRC_API_WRITE_TESS_KIND_WIRE
                        ? PRC_API_WRITE_RI_WIRE : PRC_API_WRITE_RI_SURFACE;
                    next_item->biased_tessellation_index = (uint32_t)t + 1;
                    item_material(s, &m->primitives[j], w.textured[t], next_item, msg);
                    if (next_item->has_material)
                        stats->materials++;
                    if (next_item->has_texture)
                        stats->textured++;
                    next_item++;
                    wn->num_rep_items++;
                }
            }
            if (nd->num_children > 0)
            {
                wn->children = next_child;
                wn->num_children = nd->num_children;
                for (j = 0; j < nd->num_children; j++)
                    *next_child++ = &w.nodes[nd->children[j]];
                /* An assembly level gets an empty part, as real producers
                   give one. */
                if (wn->num_rep_items == 0)
                    wn->has_empty_part = 1;
            }
        }
        root->name = s->model_name;
        root->has_empty_part = 1;
        pgr_prc_root_matrix(s, o, root->transform);
        root->has_transform = !pgr_mat4_is_identity(root->transform, 0.0);
        root->children = next_child;
        root->num_children = s->num_roots;
        for (i = 0; i < s->num_roots; i++)
            *next_child++ = &w.nodes[s->roots[i]];
        stats->nodes = s->num_nodes + 1;
        stats->tessellations = w.n_tess;
        st = node_boxes(s, &w);
    }

    if (st == PGR_OK)
    {
        ctx = prc_api_new_context(NULL);
        if (ctx == NULL)
            st = PGR_ERR_NOMEM;
    }
    if (st == PGR_OK && prc_api_write_prc_buffer(ctx, s->model_name, &w.nodes[s->num_nodes],
                                                 w.tess, w.n_tess, &buf, &size) != 0)
    {
        pgr_error(msg, "nanoPRC refused to write the PRC");
        prc_api_print_error_stack(ctx);
        st = PGR_ERR_WRITE;
    }
    if (st == PGR_OK)
    {
        stats->prc_bytes = size;
        st = write_file(prc_path, buf, size);
        if (st != PGR_OK)
            pgr_error(msg, "cannot write %s", prc_path);
    }
    if (st == PGR_OK && pdf_path != NULL)
    {
        /* A default view aimed at the model as placed: without one Adobe
           Acrobat often shows nothing until the model is fitted by hand. */
        double bmin[3] = { -1, -1, -1 }, bmax[3] = { 1, 1, 1 };
        uint8_t has_bounds = 0;
        prc_pdf_view_spec view;
        prc_pdf_write_options pdf;

        if (prc_api_write_bounds(ctx, &w.nodes[s->num_nodes], w.tess, w.n_tess,
                                 bmin, bmax, &has_bounds) != 0)
        {
            bmin[0] = bmin[1] = bmin[2] = -1.0;
            bmax[0] = bmax[1] = bmax[2] = 1.0;
        }
        prc_api_pdf_view_from_bounds(bmin, bmax, &view);
        memset(&pdf, 0, sizeof(pdf));
        pdf.views = &view;
        pdf.num_views = 1;
        if (prc_api_pdf_embed_prc(ctx, pdf_path, buf, size, &pdf) != 0)
        {
            pgr_error(msg, "cannot write %s", pdf_path);
            prc_api_print_error_stack(ctx);
            st = PGR_ERR_WRITE;
        }
    }

    if (ctx != NULL)
    {
        if (buf != NULL)
            prc_api_write_prc_buffer_free(ctx, buf);
        prc_api_release_context(ctx);
    }
    prc_out_free(&w);
    return st;
}
