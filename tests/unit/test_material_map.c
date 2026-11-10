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

/* glTF appearance into PRC, checked by reading what pgr_prc_write wrote
   back through nanoPRC's own reader:

     - material colours, converted from linear to sRGB; alpha when the
       material blends; shininess 1 - roughness; emissive; unlit as
       emissive alone; alpha masks drawn opaque, or the primitive left out
       when the mask hides all of it, with base and vertex colour alphas
       and alphaCutoff, and a warning when a texture or corners either side
       of the cutoff leave it undecided;
     - vertex colours, one per corner, converted the same way;
     - base colour textures from a buffer view, a data URI and a file
       beside the glTF: the image's pixels, and texture coordinates with v
       turned over, so that the image sits the right way up; with
       KHR_texture_transform baked in; skipped, with a warning, when the
       image is not PNG or JPEG, uses another TEXCOORD, or the primitive
       has no coordinates; mirrored repeat written as repeat, with a
       warning;
     - --no-colors and --no-texture;

   for compressed and plain tessellations. */

#include "fixtures.h"
#include "pgr_glb.h"
#include "pgr_math.h"
#include "pgr_prc.h"
#include "pgr_test.h"

#include "prc_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_FACES 8
#define MAX_VERTS 64

typedef struct
{
    int is_material, is_texture, has_transparency;
    int tess_is_material;          /* the tessellation's own material, as the reader reports it */
    float tess_diffuse[3];
    float diffuse[3], alpha, emissive[3], shininess;
    uint32_t tex_w, tex_h, tex_channels;
    unsigned char pixels[16];
    uint32_t nv;
    float pos[MAX_VERTS][3], uv[MAX_VERTS][2], color[MAX_VERTS][4];
    int uv_set[MAX_VERTS];
} face_info;

typedef struct
{
    uint32_t nfaces;
    face_info f[MAX_FACES];
} readback;

static void
copy_face(const prc_api_tess *t, const prc_api_face *in, const prc_api_tess_vertex_buffer *vb,
          face_info *out)
{
    size_t i;
    int k;

    memset(out, 0, sizeof(*out));
    out->tess_is_material = t->is_material;
    for (k = 0; k < 3; k++)
        out->tess_diffuse[k] = t->tess_material.diffuse[k];
    out->is_material = in->is_material;
    out->is_texture = in->is_texture;
    out->has_transparency = in->has_transparency;
    for (k = 0; k < 3; k++)
    {
        out->diffuse[k] = in->material.diffuse[k];
        out->emissive[k] = in->material.emissive[k];
    }
    out->alpha = in->material.diffuse_alpha;
    out->shininess = in->material.shininess;
    if (in->is_texture && in->texture.data != NULL)
    {
        out->tex_w = in->texture.width;
        out->tex_h = in->texture.height;
        out->tex_channels = in->texture.num_channels;
        if ((size_t)out->tex_w * out->tex_h * out->tex_channels <= sizeof(out->pixels))
            memcpy(out->pixels, in->texture.data, (size_t)out->tex_w * out->tex_h * out->tex_channels);
    }
    for (i = 0; i < vb->num_vertices && i < MAX_VERTS; i++)
    {
        const prc_api_vertex *v = &vb->vertices[i];

        for (k = 0; k < 3; k++)
            out->pos[i][k] = v->position[k];
        out->uv[i][0] = v->uv[0];
        out->uv[i][1] = v->uv[1];
        out->uv_set[i] = v->uv_set;
        for (k = 0; k < 4; k++)
            out->color[i][k] = v->color[k];
        out->nv++;
    }
}

static int
read_back(const char *path, readback *rb)
{
    prc_context *ctx = prc_api_new_context(NULL);
    prc_api_data data;
    prc_api_product *tree = NULL;
    prc_api_tess *tess = NULL;
    uint32_t np, nprod, nm, nt = 0, nlt = 0, nx = 0, k;
    size_t j;
    uint8_t has_lines = 0;

    memset(rb, 0, sizeof(*rb));
    if (ctx == NULL)
        return -1;
    data = prc_api_open_contents(ctx, path);
    if (data == NULL)
    {
        prc_api_release_context(ctx);
        return -1;
    }
    if (prc_api_prep_model_tree(ctx, data, &np, &nprod, &nm) != 0
        || prc_api_create_model_tree(ctx, data, &tree, np, nprod, nm) != 0
        || prc_api_get_number_tessellations(ctx, data, tree, &nt, &nlt, &nx, 0) != 0)
    {
        prc_api_release_data(ctx, data, NULL, 0, NULL, 0, NULL, 0, tree);
        prc_api_release_context(ctx);
        return -1;
    }
    tess = (prc_api_tess *)calloc(nt ? nt : 1, sizeof(prc_api_tess));
    for (k = 0; tess != NULL && k < nt; k++)
    {
        prc_api_tess *t = &tess[k];

        t->num_faces = prc_api_get_number_faces(ctx, data, k);
        t->tess_faces = (prc_api_face *)calloc(t->num_faces ? t->num_faces : 1, sizeof(prc_api_face));
        if (prc_api_initialize_tessellation(ctx, data, tree, k, t, NULL, &has_lines) < 0)
            continue;
        if (t->type == PRC_API_TESS_3D_Compressed)
        {
            /* Every face of a compressed tessellation gives the whole mesh. */
            if (t->num_faces > 0 && rb->nfaces < MAX_FACES
                && prc_api_get_tessellation_vertices(ctx, data, k, 0, &t->tess_faces[0], t) >= 0)
                copy_face(t, &t->tess_faces[0], &t->tess_vertices, &rb->f[rb->nfaces++]);
        }
        else if (t->type == PRC_API_TESS_3D)
        {
            for (j = 0; j < t->num_faces && rb->nfaces < MAX_FACES; j++)
                if (prc_api_get_tessellation_vertices(ctx, data, k, j, &t->tess_faces[j], t) >= 0)
                    copy_face(t, &t->tess_faces[j], &t->tess_faces[j].face_vertices, &rb->f[rb->nfaces++]);
        }
        else if (t->type == PRC_API_TESS_3D_Wire)
        {
            /* A wire's points, as pgr_prc_read takes them, as one more
               "face" without a material of its own. */
            prc_api_face none;

            memset(&none, 0, sizeof(none));
            if (rb->nfaces < MAX_FACES && prc_api_get_tessellation_vertices(ctx, data, k, 0, NULL, t) >= 0)
                copy_face(t, &none, &t->tess_vertices, &rb->f[rb->nfaces++]);
        }
    }
    prc_api_release_data(ctx, data, tess, nt, NULL, 0, NULL, 0, tree);
    for (k = 0; tess != NULL && k < nt; k++)
        free(tess[k].tess_faces);
    free(tess);
    return prc_api_release_context(ctx) == 0 ? 0 : -1;
}

static void
quiet(void *user, int is_error, const char *text)
{
    (void)user;
    (void)is_error;
    (void)text;
}

/* Read the glTF at path, write it as PRC with o (its messages captured in
   msg), read the PRC back. The scene as read is left in *scene when it is
   not NULL. */
static int
convert(const char *path, const pgr_prc_write_options *o_in, pgr_messages *msg,
        pgr_prc_write_stats *st, readback *rb, pgr_scene *scene_out)
{
    char prc[300];
    pgr_prc_write_options o = *o_in;
    pgr_glb_read_options ro;
    pgr_scene s;
    int r;

    memset(st, 0, sizeof(*st));
    memset(rb, 0, sizeof(*rb));
    memset(msg, 0, sizeof(*msg));
    msg->fn = quiet;
    o.messages = msg;
    ro.messages = msg;
    snprintf(prc, sizeof(prc), "%s%s.prc", path, o.triangles ? "_tri" : "");
    pgr_scene_init(&s);
    r = pgr_glb_read(path, &ro, &s);
    if (r == PGR_OK)
        r = pgr_prc_write(&s, prc, NULL, &o, st);
    if (scene_out != NULL)
        *scene_out = s;
    else
        pgr_scene_free(&s);
    if (r != PGR_OK)
        return r;
    return read_back(prc, rb);
}

static int
near3(const float *a, double x, double y, double z, double tol)
{
    return fabs(a[0] - x) <= tol && fabs(a[1] - y) <= tol && fabs(a[2] - z) <= tol;
}

static void
dump_face(const char *what, const face_info *f)
{
    uint32_t i;

    fprintf(stderr, "  %s: material %d texture %d transparency %d diffuse (%g, %g, %g) alpha %g "
            "emissive (%g, %g, %g) shininess %g, %u vertices\n", what, f->is_material, f->is_texture,
            f->has_transparency, f->diffuse[0], f->diffuse[1], f->diffuse[2], f->alpha, f->emissive[0],
            f->emissive[1], f->emissive[2], f->shininess, f->nv);
    fprintf(stderr, "    tessellation material %d diffuse (%g, %g, %g)\n", f->tess_is_material,
            f->tess_diffuse[0], f->tess_diffuse[1], f->tess_diffuse[2]);
    for (i = 0; i < f->nv && i < 6; i++)
        fprintf(stderr, "    (%g, %g, %g) colour (%g, %g, %g, %g) uv (%g, %g) set %d\n", f->pos[i][0],
                f->pos[i][1], f->pos[i][2], f->color[i][0], f->color[i][1], f->color[i][2], f->color[i][3],
                f->uv[i][0], f->uv[i][1], f->uv_set[i]);
}

/* The face whose diffuse colour is near (x, y, z), or NULL. */
static const face_info *
by_diffuse(const readback *rb, double x, double y, double z)
{
    uint32_t i;

    for (i = 0; i < rb->nfaces; i++)
        if (near3(rb->f[i].diffuse, x, y, z, 0.004))
            return &rb->f[i];
    return NULL;
}

/* sRGB of linear 0.5, 0.2, 0.1 and 0.3. */
#define S05 0.735357
#define S02 0.484529
#define S01 0.349190
#define S03 0.583831

static void
test_materials(int triangles)
{
    pgr_prc_write_options o;
    pgr_prc_write_stats st;
    pgr_messages msg;
    readback rb;
    const face_info *red, *blue;

    memset(&o, 0, sizeof(o));
    o.triangles = triangles;
    /* "red" made (0.5, 0.2, 0) linear, so that the conversion shows. */
    PGR_CHECK(fx_edited("mm_materials.gltf", fx_multi_material, "[1,0,0,1]", "[0.5,0.2,0,1]") == 0);
    PGR_CHECK(convert("mm_materials.gltf", &o, &msg, &st, &rb, NULL) == 0);
    PGR_CHECK(st.materials == 2 && st.textured == 0 && rb.nfaces == 2);
    red = by_diffuse(&rb, S05, S02, 0.0);
    blue = by_diffuse(&rb, 0.0, 0.0, 1.0);
    PGR_CHECK(red != NULL && blue != NULL);
    if (red != NULL)
    {
        PGR_CHECK(red->is_material && fabs(red->alpha - 1.0) < 1e-6);
        PGR_CHECK(fabs(red->shininess - 0.75) < 1e-3);
        PGR_CHECK(near3(red->emissive, 0, 0, 0, 1e-6));
    }
    if (blue != NULL)
    {
        /* BLEND keeps the base colour's alpha; the emissive is converted. */
        PGR_CHECK(fabs(blue->alpha - 0.5) < 0.01);
        if (getenv("MM_DUMP"))
            dump_face("blue", blue);
        PGR_CHECK(near3(blue->emissive, S01, S02, S03, 0.004));
    }
    PGR_CHECK(msg.warnings == 0);
}

/* fx_quad_textured with its image in a buffer view, for fx_edited. */
static int
quad_textured_plain(const char *path)
{
    return fx_quad_textured(path, FX_IMAGE_VIEW, "", NULL, 1);
}

static void
test_unlit_and_mask(void)
{
    pgr_prc_write_options o;
    pgr_prc_write_stats st;
    pgr_messages msg;
    readback rb;
    const face_info *red, *blue;
    uint32_t i;

    memset(&o, 0, sizeof(o));
    /* Unlit red: shown by its emissive colour, diffuse black. */
    PGR_CHECK(fx_edited("mm_unlit.gltf", fx_multi_material, "{\"name\":\"red\",",
                        "{\"name\":\"red\",\"extensions\":{\"KHR_materials_unlit\":{}},") == 0);
    PGR_CHECK(convert("mm_unlit.gltf", &o, &msg, &st, &rb, NULL) == 0);
    red = NULL;
    for (i = 0; i < rb.nfaces; i++)
        if (near3(rb.f[i].emissive, 1, 0, 0, 0.004))
            red = &rb.f[i];
    PGR_CHECK(red != NULL && near3(red->diffuse, 0, 0, 0, 0.004));

    /* Alpha masks. Blue's alpha 0.5 is at the default cutoff, 0.5, which
       glTF draws: opaque, and exact, so no warning. */
    PGR_CHECK(fx_edited("mm_mask.gltf", fx_multi_material, "\"alphaMode\":\"BLEND\"", "\"alphaMode\":\"MASK\"") == 0);
    PGR_CHECK(convert("mm_mask.gltf", &o, &msg, &st, &rb, NULL) == 0);
    blue = by_diffuse(&rb, 0.0, 0.0, 1.0);
    PGR_CHECK(blue != NULL && fabs(blue->alpha - 1.0) < 1e-6);
    PGR_CHECK(rb.nfaces == 2 && st.masked_out == 0 && msg.warnings == 0);

    /* Alpha 0.3, below it: glTF draws none of blue, so it is left out,
       with a warning; red stays. */
    PGR_CHECK(fx_edited("mm_mask_low.gltf", fx_multi_material, "[0,0,1,0.5]},\"alphaMode\":\"BLEND\"",
                        "[0,0,1,0.3]},\"alphaMode\":\"MASK\"") == 0);
    PGR_CHECK(convert("mm_mask_low.gltf", &o, &msg, &st, &rb, NULL) == 0);
    PGR_CHECK(rb.nfaces == 1 && st.masked_out == 1 && st.tessellations == 1);
    PGR_CHECK(by_diffuse(&rb, 0.0, 0.0, 1.0) == NULL && by_diffuse(&rb, 1.0, 0.0, 0.0) != NULL);
    PGR_CHECK((msg.warned & PGR_WARN_MASKED_OUT) != 0 && msg.warnings == 1);

    /* The same with alphaCutoff 0.2: drawn. */
    PGR_CHECK(fx_edited("mm_mask_cut.gltf", fx_multi_material, "[0,0,1,0.5]},\"alphaMode\":\"BLEND\"",
                        "[0,0,1,0.3]},\"alphaMode\":\"MASK\",\"alphaCutoff\":0.2") == 0);
    PGR_CHECK(convert("mm_mask_cut.gltf", &o, &msg, &st, &rb, NULL) == 0);
    blue = by_diffuse(&rb, 0.0, 0.0, 1.0);
    PGR_CHECK(blue != NULL && fabs(blue->alpha - 1.0) < 1e-6);
    PGR_CHECK(rb.nfaces == 2 && st.masked_out == 0 && msg.warnings == 0);

    /* Textured: the image's alpha is not known here, so drawn, opaque,
       with a warning, even with a base alpha below the cutoff. */
    PGR_CHECK(fx_edited("mm_mask_tex.gltf", quad_textured_plain, "{\"name\":\"textured\",\"pbrMetallicRoughness\":{"
                        "\"baseColorFactor\":[0.5,0.5,0.5,1]",
                        "{\"name\":\"textured\",\"alphaMode\":\"MASK\",\"pbrMetallicRoughness\":{"
                        "\"baseColorFactor\":[0.5,0.5,0.5,0.3]") == 0);
    PGR_CHECK(convert("mm_mask_tex.gltf", &o, &msg, &st, &rb, NULL) == 0);
    PGR_CHECK(rb.nfaces == 1 && st.textured == 1 && st.masked_out == 0);
    PGR_CHECK((msg.warned & PGR_WARN_ALPHA_MASK) != 0 && (msg.warned & PGR_WARN_MASKED_OUT) == 0);

    /* Vertex colours of alpha 1, 0.5 and 1 times a base alpha a: the
       corners' alphas are a, a/2, a. */
    {
        static const struct { const char *path, *alpha, *cutoff; uint32_t faces, out; unsigned warn; } vc[3] = {
            { "mm_mask_vc_out.gltf", "0.4", "", 0, 1, PGR_WARN_MASKED_OUT },             /* 0.4, 0.2: all below */
            { "mm_mask_vc_mix.gltf", "0.8", "", 1, 0, PGR_WARN_ALPHA_MASK },             /* 0.8, 0.4: either side */
            { "mm_mask_vc_in.gltf", "0.8", ",\"alphaCutoff\":0.3", 1, 0, 0 }             /* all at or above 0.3 */
        };
        char mat[200];

        for (i = 0; i < 3; i++)
        {
            snprintf(mat, sizeof(mat),
                     "},\"material\":0}]}],\"materials\":[{\"alphaMode\":\"MASK\"%s,"
                     "\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,%s]}}]",
                     vc[i].cutoff, vc[i].alpha);
            PGR_CHECK(fx_edited(vc[i].path, fx_tri_vcolors, "}}]}]", mat) == 0);
            PGR_CHECK(convert(vc[i].path, &o, &msg, &st, &rb, NULL) == 0);
            PGR_CHECK(rb.nfaces == vc[i].faces && st.masked_out == vc[i].out);
            PGR_CHECK(msg.warned == vc[i].warn);
        }

        /* --no-colors: the vertex alphas are not written, so the mixed case
           is decided by the base alpha alone, 0.8: drawn, no warning. */
        o.no_colors = 1;
        PGR_CHECK(convert("mm_mask_vc_mix.gltf", &o, &msg, &st, &rb, NULL) == 0);
        PGR_CHECK(rb.nfaces == 1 && st.masked_out == 0 && msg.warned == 0);
        o.no_colors = 0;
    }
}

/* The read-back vertex at position (x, y, z), or -1. */
static int
vertex_at(const face_info *f, double x, double y, double z, double tol)
{
    uint32_t i;

    for (i = 0; i < f->nv; i++)
        if (near3(f->pos[i], x, y, z, tol))
            return (int)i;
    return -1;
}

static void
test_vertex_colors(int triangles)
{
    pgr_prc_write_options o;
    pgr_prc_write_stats st;
    pgr_messages msg;
    readback rb;
    int a, b, c;

    memset(&o, 0, sizeof(o));
    o.triangles = triangles;
    /* Float RGBA (1,0,0,1), (0,1,0,0.5), (0,0,1,1), under three materials:
       none, so the colours as they are (the ends of the range convert to
       themselves) and opaque, as glTF ignores alpha unless blending; an
       opaque base (0.5, 0.2, 0.1), multiplied in; and a blending base
       (1, 1, 1, 0.5), whose alpha multiplies the corners'. */
    {
        static const struct { const char *path, *material; double r, g, b, alpha_b; } vc[3] = {
            { "mm_vcol.gltf", NULL, 1.0, 1.0, 1.0, 1.0 },
            { "mm_vcol_grey.gltf", "\"baseColorFactor\":[0.5,0.2,0.1,1]}}]", S05, S02, S01, 1.0 },
            { "mm_vcol_blend.gltf", "\"baseColorFactor\":[1,1,1,0.5]},\"alphaMode\":\"BLEND\"}]", 1.0, 1.0, 1.0, 0.25 }
        };
        char mat[200];
        int i;

        for (i = 0; i < 3; i++)
        {
            if (vc[i].material == NULL)
                PGR_CHECK(fx_tri_vcolors(vc[i].path) == 0);
            else
            {
                snprintf(mat, sizeof(mat), "},\"material\":0}]}],\"materials\":[{\"pbrMetallicRoughness\":{%s",
                         vc[i].material);
                PGR_CHECK(fx_edited(vc[i].path, fx_tri_vcolors, "}}]}]", mat) == 0);
            }
            PGR_CHECK(convert(vc[i].path, &o, &msg, &st, &rb, NULL) == 0);
            PGR_CHECK(st.coloured == 1 && rb.nfaces == 1);
            if (rb.nfaces == 1)
            {
                const face_info *f = &rb.f[0];

                if (getenv("MM_DUMP"))
                    dump_face(vc[i].path, f);
                a = vertex_at(f, 0, 0, 0, 1e-4);
                b = vertex_at(f, 1, 0, 0, 1e-4);
                c = vertex_at(f, 0, 1, 0, 1e-4);
                PGR_CHECK(a >= 0 && b >= 0 && c >= 0);
                if (a >= 0 && b >= 0 && c >= 0)
                {
                    /* nanoPRC's reader gives a plain tessellation's stored
                       vertex alpha as the byte, 0..255, and its colour as
                       0..1 (src/prc_tri_primitives_api.c, the alpha lines of
                       its colour decoding); compressed gives both as 0..1,
                       and both give 1 when no alpha is stored. */
                    double scale = triangles && vc[i].alpha_b < 1.0 ? 255.0 : 1.0;

                    PGR_CHECK(near3(f->color[a], vc[i].r, 0, 0, 0.004));
                    PGR_CHECK(near3(f->color[b], 0, vc[i].g, 0, 0.004));
                    PGR_CHECK(near3(f->color[c], 0, 0, vc[i].b, 0.004));
                    PGR_CHECK(fabs(f->color[b][3] / scale - vc[i].alpha_b) < 0.004);
                    PGR_CHECK(fabs(f->color[a][3] / scale - (vc[i].alpha_b < 1.0 ? 0.5 : 1.0)) < 0.004);
                }
            }
        }
    }

    /* Normalised bytes: 128/255 linear is 0.7373 sRGB, 188 as a byte. */
    PGR_CHECK(fx_u8_colors("mm_u8col.glb") == 0);
    PGR_CHECK(convert("mm_u8col.glb", &o, &msg, &st, &rb, NULL) == 0);
    if (rb.nfaces == 1)
    {
        b = vertex_at(&rb.f[0], 1, 0, 0, 1e-4);
        PGR_CHECK(b >= 0 && near3(rb.f[0].color[b], 0, 188.0 / 255.0, 0, 0.003));
    }

    /* --no-colors: the first corner is no longer red. */
    o.no_colors = 1;
    PGR_CHECK(convert("mm_vcol.gltf", &o, &msg, &st, &rb, NULL) == 0);
    PGR_CHECK(st.coloured == 0 && rb.nfaces == 1);
    if (rb.nfaces == 1)
    {
        a = vertex_at(&rb.f[0], 0, 0, 0, 1e-4);
        PGR_CHECK(a >= 0 && !near3(rb.f[0].color[a], 1, 0, 0, 0.05));
    }
}

/* Every vertex of p coloured (r, g, b, 1). */
static void
set_colors(pgr_primitive *p, float r, float g, float b)
{
    uint32_t v;

    free(p->colors);
    p->colors = (float *)malloc((size_t)p->num_vertices * 4 * sizeof(float));
    if (p->colors == NULL)
        return;
    for (v = 0; v < p->num_vertices; v++)
    {
        p->colors[4 * v] = r;
        p->colors[4 * v + 1] = g;
        p->colors[4 * v + 2] = b;
        p->colors[4 * v + 3] = 1.0f;
    }
}

/* path read, edit applied to the scene, written as PRC and read back. */
static int
convert_with(const char *path, void (*edit)(pgr_scene *), readback *rb)
{
    char prc[300];
    pgr_glb_read_options ro;
    pgr_prc_write_options o;
    pgr_messages msg;
    pgr_scene s;
    int r;

    memset(&ro, 0, sizeof(ro));
    memset(&o, 0, sizeof(o));
    memset(&msg, 0, sizeof(msg));
    memset(rb, 0, sizeof(*rb));
    msg.fn = quiet;
    ro.messages = &msg;
    o.messages = &msg;
    snprintf(prc, sizeof(prc), "%s.prc", path);
    pgr_scene_init(&s);
    r = pgr_glb_read(path, &ro, &s);
    if (r == PGR_OK)
    {
        edit(&s);
        r = pgr_prc_write(&s, prc, NULL, &o, NULL);
    }
    pgr_scene_free(&s);
    return r != PGR_OK ? r : read_back(prc, rb);
}

/* The textured quad's corners all red. */
static void
red_corners(pgr_scene *s)
{
    if (s->num_meshes == 1 && s->meshes[0].num_primitives == 1)
        set_colors(&s->meshes[0].primitives[0], 1.0f, 0.0f, 0.0f);
}

/* Every line primitive red, with a material of base (0.5, 0.2, 0.1). */
static void
red_lines_on_material(pgr_scene *s)
{
    pgr_material *m;
    uint32_t i, j;
    int mi = pgr_scene_add_material(s, "lines", &m);

    if (mi < 0)
        return;
    m->base_color[0] = 0.5f;
    m->base_color[1] = 0.2f;
    m->base_color[2] = 0.1f;
    for (i = 0; i < s->num_meshes; i++)
        for (j = 0; j < s->meshes[i].num_primitives; j++)
        {
            s->meshes[i].primitives[j].material = mi;
            set_colors(&s->meshes[i].primitives[j], 1.0f, 0.0f, 0.0f);
        }
}

/* Vertex colours on a textured primitive and on lines, added to the scene
   as read since no fixture has them. */
static void
test_vertex_colors_textured_and_lines(void)
{
    readback rb;
    uint32_t i, n = 0;

    /* Textured: the item's material colour already multiplies the image, so
       the corners keep their own colour. */
    PGR_CHECK(fx_quad_textured("mm_vcol_tex.gltf", FX_IMAGE_VIEW, "", NULL, 1) == 0);
    PGR_CHECK(convert_with("mm_vcol_tex.gltf", red_corners, &rb) == 0);
    PGR_CHECK(rb.nfaces == 1 && rb.f[0].is_texture && rb.f[0].nv > 0);
    if (rb.nfaces == 1)
        for (i = 0; i < rb.f[0].nv; i++)
            PGR_CHECK(near3(rb.f[0].color[i], 1, 0, 0, 0.004));

    /* Lines: multiplied by the base colour, as triangles are. */
    PGR_CHECK(fx_lines("mm_vcol_lines.gltf") == 0);
    PGR_CHECK(convert_with("mm_vcol_lines.gltf", red_lines_on_material, &rb) == 0);
    PGR_CHECK(rb.nfaces > 0);
    for (i = 0; i < rb.nfaces; i++)
    {
        uint32_t v;

        if (getenv("MM_DUMP"))
            dump_face("line", &rb.f[i]);
        for (v = 0; v < rb.f[i].nv; v++, n++)
            PGR_CHECK(near3(rb.f[i].color[v], S05, 0, 0, 0.004));
    }
    PGR_CHECK(n > 0);
}

/* The image as fx_png_2x2 holds it: top row red, green; bottom row blue,
   half-transparent white. */
static const unsigned char expect_pixels[16] = {
    255, 0, 0, 255,   0, 255, 0, 255,
    0, 0, 255, 255,   255, 255, 255, 128
};

/* Every vertex's texture coordinate against (fu, fv) of its position. */
static int
uvs_are(const face_info *f, double (*fu)(double, double), double (*fv)(double, double), double tol)
{
    uint32_t i;

    if (f->nv == 0)
        return 0;
    for (i = 0; i < f->nv; i++)
        if (!f->uv_set[i] || fabs(f->uv[i][0] - fu(f->pos[i][0], f->pos[i][1])) > tol
            || fabs(f->uv[i][1] - fv(f->pos[i][0], f->pos[i][1])) > tol)
        {
            fprintf(stderr, "  vertex %u at (%g, %g, %g): uv (%g, %g) set %d, expected (%g, %g)\n",
                    i, f->pos[i][0], f->pos[i][1], f->pos[i][2], f->uv[i][0], f->uv[i][1], f->uv_set[i],
                    fu(f->pos[i][0], f->pos[i][1]), fv(f->pos[i][0], f->pos[i][1]));
            return 0;
        }
    return 1;
}

/* The file stores v turned over, v' = 1 - v, so that the image's top row
   lies along y = 1, the right way up in Adobe Acrobat (checked by eye on 7
   November 2026). nanoPRC's read API turns v over again
   (src/prc_tri_primitives_api.c, "V-flip must happen AFTER transform"), so
   a correct file reads back as glTF's own coordinates; a file written
   without the turn would read back with v = y.

   No transform: glTF (x, 1 - y). */
static double ux(double x, double y) { (void)y; return x; }
static double vy(double x, double y) { (void)x; return 1.0 - y; }
/* offset (0.5, 0.25), scale 2: glTF (2x + 0.5, 2 (1 - y) + 0.25). */
static double u_os(double x, double y) { (void)y; return 2.0 * x + 0.5; }
static double v_os(double x, double y) { (void)x; return 2.0 * (1.0 - y) + 0.25; }
/* rotation a quarter-turn: glTF (1 - y, -x). */
static double u_rot(double x, double y) { (void)x; return 1.0 - y; }
static double v_rot(double x, double y) { (void)y; return -x; }

static void
test_textures(int triangles)
{
    static const char *names[3] = { "mm_tex_view", "mm_tex_uri", "mm_tex_file" };
    static const int modes[3] = { FX_IMAGE_VIEW, FX_IMAGE_DATA_URI, FX_IMAGE_FILE };
    double tol = triangles ? 1e-6 : 1e-3;
    pgr_prc_write_options o;
    pgr_prc_write_stats st;
    pgr_messages msg;
    readback rb;
    pgr_scene s;
    char path[64];
    int i;

    memset(&o, 0, sizeof(o));
    o.triangles = triangles;
    for (i = 0; i < 3; i++)
    {
        snprintf(path, sizeof(path), "%s.gltf", names[i]);
        PGR_CHECK(fx_quad_textured(path, modes[i], "", NULL, 1) == 0);
        PGR_CHECK(convert(path, &o, &msg, &st, &rb, &s) == 0);
        /* The scene holds the PNG as the file did. */
        PGR_CHECK(s.num_textures == 1 && s.num_materials == 1 && s.materials[0].texture == 0);
        if (s.num_textures == 1)
            PGR_CHECK(s.textures[0].mime == PGR_IMAGE_PNG && s.textures[0].size == fx_png_2x2_size
                      && memcmp(s.textures[0].bytes, fx_png_2x2, fx_png_2x2_size) == 0);
        pgr_scene_free(&s);
        PGR_CHECK(st.textured == 1 && rb.nfaces == 1 && msg.warnings == 0);
        if (rb.nfaces == 1)
        {
            const face_info *f = &rb.f[0];

            if (getenv("MM_DUMP"))
                dump_face(path, f);
            PGR_CHECK(f->is_texture && f->tex_w == 2 && f->tex_h == 2 && f->tex_channels == 4);
            PGR_CHECK(memcmp(f->pixels, expect_pixels, sizeof(expect_pixels)) == 0);
            /* The base colour multiplies the image, as in glTF. nanoPRC's
               reader reports a textured face's material colour on the
               tessellation, not on the face. */
            PGR_CHECK(near3(f->tess_diffuse, S05, S05, S05, 0.004));
            PGR_CHECK(f->nv >= 4 && uvs_are(f, ux, vy, tol));
        }
    }

    /* The same in a .glb. */
    PGR_CHECK(fx_quad_textured("mm_tex.glb", FX_IMAGE_VIEW, "", NULL, 1) == 0);
    PGR_CHECK(convert("mm_tex.glb", &o, &msg, &st, &rb, NULL) == 0);
    PGR_CHECK(rb.nfaces == 1 && rb.f[0].is_texture && uvs_are(&rb.f[0], ux, vy, tol));

    /* KHR_texture_transform, baked into the coordinates. */
    PGR_CHECK(fx_quad_textured("mm_tex_os.gltf", FX_IMAGE_VIEW,
                               ",\"extensions\":{\"KHR_texture_transform\":{\"offset\":[0.5,0.25],\"scale\":[2,2]}}",
                               NULL, 1) == 0);
    PGR_CHECK(convert("mm_tex_os.gltf", &o, &msg, &st, &rb, NULL) == 0);
    PGR_CHECK(rb.nfaces == 1 && uvs_are(&rb.f[0], u_os, v_os, tol));
    PGR_CHECK(fx_quad_textured("mm_tex_rot.gltf", FX_IMAGE_VIEW,
                               ",\"extensions\":{\"KHR_texture_transform\":{\"rotation\":1.5707963267948966}}",
                               NULL, 1) == 0);
    PGR_CHECK(convert("mm_tex_rot.gltf", &o, &msg, &st, &rb, NULL) == 0);
    PGR_CHECK(rb.nfaces == 1 && uvs_are(&rb.f[0], u_rot, v_rot, tol));

    /* Clamp and mirrored repeat: written, mirror with a warning. */
    PGR_CHECK(fx_quad_textured("mm_tex_wrap.gltf", FX_IMAGE_VIEW, "", "{\"wrapS\":33071,\"wrapT\":33648}", 1) == 0);
    PGR_CHECK(convert("mm_tex_wrap.gltf", &o, &msg, &st, &rb, NULL) == 0);
    PGR_CHECK(st.textured == 1 && rb.nfaces == 1 && rb.f[0].is_texture);
    PGR_CHECK((msg.warned & PGR_WARN_WRAP) != 0);
}

static void
test_textures_skipped(void)
{
    pgr_prc_write_options o;
    pgr_prc_write_stats st;
    pgr_messages msg;
    readback rb;

    memset(&o, 0, sizeof(o));
    /* Not a PNG or JPEG: the material keeps its colour, without the image. */
    PGR_CHECK(fx_quad_textured("mm_tex_gif.gltf", FX_IMAGE_NOT_PNG, "", NULL, 1) == 0);
    PGR_CHECK(convert("mm_tex_gif.gltf", &o, &msg, &st, &rb, NULL) == 0);
    PGR_CHECK(st.textured == 0 && st.materials == 1 && msg.warnings == 1);
    PGR_CHECK(rb.nfaces == 1 && !rb.f[0].is_texture && near3(rb.f[0].diffuse, S05, S05, S05, 0.004));

    /* Another TEXCOORD. */
    PGR_CHECK(fx_quad_textured("mm_tex_tc1.gltf", FX_IMAGE_VIEW, ",\"texCoord\":1", NULL, 1) == 0);
    PGR_CHECK(convert("mm_tex_tc1.gltf", &o, &msg, &st, &rb, NULL) == 0);
    PGR_CHECK(st.textured == 0 && (msg.warned & PGR_WARN_TEXCOORD) != 0);

    /* No coordinates at all. */
    PGR_CHECK(fx_quad_textured("mm_tex_nouv.gltf", FX_IMAGE_VIEW, "", NULL, 0) == 0);
    PGR_CHECK(convert("mm_tex_nouv.gltf", &o, &msg, &st, &rb, NULL) == 0);
    PGR_CHECK(st.textured == 0 && (msg.warned & PGR_WARN_NO_UVS) != 0);
    PGR_CHECK(rb.nfaces == 1 && !rb.f[0].is_texture);

    /* --no-texture: no image and no coordinates. */
    o.no_textures = 1;
    PGR_CHECK(fx_quad_textured("mm_tex_off.gltf", FX_IMAGE_VIEW, "", NULL, 1) == 0);
    PGR_CHECK(convert("mm_tex_off.gltf", &o, &msg, &st, &rb, NULL) == 0);
    PGR_CHECK(st.textured == 0 && msg.warnings == 0);
    PGR_CHECK(rb.nfaces == 1 && !rb.f[0].is_texture && rb.f[0].nv > 0 && !rb.f[0].uv_set[0]);
}

int
main(void)
{
    test_materials(0);
    test_materials(1);
    test_unlit_and_mask();
    test_vertex_colors(0);
    test_vertex_colors(1);
    test_vertex_colors_textured_and_lines();
    test_textures(0);
    test_textures(1);
    test_textures_skipped();
    return PGR_TEST_RESULT;
}
