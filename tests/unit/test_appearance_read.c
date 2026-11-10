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

/* PRC appearance into glTF: glTF -> PRC -> glTF, the scene read back by
   pgr_prc_read compared with the one written, for compressed and plain
   tessellations:

     - material colours back in linear values, alpha and blending,
       roughness, emissive; an unlit material back as unlit;
     - vertex colours back in linear values with a white base colour, the
       material colour multiplied in as pgr_prc_write wrote it, and the
       corners' alphas when blending; on lines as on triangles;
     - a texture back as a PNG of the same pixels, with the same texture
       coordinates, and one image for two materials that use it;
     - and the scene so read written as glTF and read again, keeping
       images, samplers, the material's texture and unlit; and written as
       PRC once more, where nanoPRC's reader finds the original pixels. */

#include "fixtures.h"
#include "pgr_glb.h"
#include "pgr_image.h"
#include "pgr_math.h"
#include "pgr_prc.h"
#include "pgr_test.h"

#include "prc_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void
quiet(void *user, int is_error, const char *text)
{
    (void)user;
    if (is_error)
        fprintf(stderr, "error: %s\n", text);
}

static pgr_messages msg = { quiet, NULL, 0, 0, 0 };

/* The glTF at path -> scene a -> PRC (after edit, when not NULL) -> scene b. */
static int
trip(const char *path, int triangles, void (*edit)(pgr_scene *), pgr_scene *a, pgr_scene *b,
     pgr_prc_read_stats *rs)
{
    char prc[256];
    pgr_glb_read_options ro;
    pgr_prc_write_options wo;
    pgr_prc_read_options pro;
    int st;

    memset(rs, 0, sizeof(*rs));
    memset(&ro, 0, sizeof(ro));
    memset(&wo, 0, sizeof(wo));
    memset(&pro, 0, sizeof(pro));
    ro.messages = wo.messages = pro.messages = &msg;
    wo.triangles = triangles;
    snprintf(prc, sizeof(prc), "%s%s.prc", path, triangles ? "_tri" : "");
    pgr_scene_init(a);
    pgr_scene_init(b);
    st = pgr_glb_read(path, &ro, a);
    if (st == PGR_OK && edit != NULL)
        edit(a);
    if (st == PGR_OK)
        st = pgr_prc_write(a, prc, NULL, &wo, NULL);
    if (st == PGR_OK)
        st = pgr_prc_read(prc, &pro, b, rs);
    return st;
}

static int
near(double a, double b, double tol)
{
    return fabs(a - b) <= tol;
}

static int
near4(const float *a, double x, double y, double z, double w, double tol)
{
    return near(a[0], x, tol) && near(a[1], y, tol) && near(a[2], z, tol) && near(a[3], w, tol);
}

/* The primitive of s with a vertex at (x, y, z), and that vertex; 0 if none. */
static int
find_vertex(const pgr_scene *s, double x, double y, double z, const pgr_primitive **p, uint32_t *v)
{
    uint32_t i, j, k;

    for (i = 0; i < s->num_meshes; i++)
        for (j = 0; j < s->meshes[i].num_primitives; j++)
        {
            const pgr_primitive *q = &s->meshes[i].primitives[j];

            for (k = 0; k < q->num_vertices; k++)
                if (near(q->positions[3 * k], x, 1e-4) && near(q->positions[3 * k + 1], y, 1e-4)
                    && near(q->positions[3 * k + 2], z, 1e-4))
                {
                    *p = q;
                    *v = k;
                    return 1;
                }
        }
    return 0;
}

static const pgr_material *
material_of(const pgr_scene *s, const pgr_primitive *p)
{
    return p != NULL && p->material >= 0 ? &s->materials[p->material] : NULL;
}

static void
dump_material(const char *what, const pgr_material *m)
{
    if (m == NULL)
    {
        fprintf(stderr, "  %s: no material\n", what);
        return;
    }
    fprintf(stderr, "  %s: base (%g, %g, %g, %g) mode %d emissive (%g, %g, %g) roughness %g metallic %g "
            "texture %d unlit %d\n", what, m->base_color[0], m->base_color[1], m->base_color[2],
            m->base_color[3], m->alpha_mode, m->emissive[0], m->emissive[1], m->emissive[2], m->roughness,
            m->metallic, m->texture, m->unlit);
}

/* Linear 0.5 and 0.2 written as sRGB and read back: within a float's
   rounding of themselves. */
static void
test_materials(int triangles)
{
    pgr_scene a, b;
    pgr_prc_read_stats rs;
    const pgr_primitive *p;
    const pgr_material *m;
    uint32_t v;

    PGR_CHECK(fx_edited("ar_materials.gltf", fx_multi_material, "[1,0,0,1]", "[0.5,0.2,0,1]") == 0);
    PGR_CHECK(trip("ar_materials.gltf", triangles, NULL, &a, &b, &rs) == PGR_OK);
    PGR_CHECK(rs.materials == 2 && b.num_materials == 2 && rs.primitives == 2);

    /* Red, opaque, roughness 0.25, by the vertex only it has. */
    PGR_CHECK(find_vertex(&b, 1, 0, 0, &p, &v));
    m = material_of(&b, p);
    if (getenv("AR_DUMP"))
        dump_material("red", m);
    PGR_CHECK(m != NULL && near4(m->base_color, 0.5, 0.2, 0.0, 1.0, 1e-5));
    PGR_CHECK(m != NULL && m->alpha_mode == PGR_ALPHA_OPAQUE && near(m->roughness, 0.25, 1e-6)
              && m->metallic == 0.0f && !m->unlit && m->texture < 0);
    PGR_CHECK(m != NULL && near(m->emissive[0], 0, 1e-6) && near(m->emissive[1], 0, 1e-6));
    PGR_CHECK(p != NULL && p->colors == NULL && p->uvs == NULL);

    /* Blue, blending at 0.5, emissive (0.1, 0.2, 0.3). PRC stores a
       style's transparency as a byte, so the alpha comes back as 128/255. */
    PGR_CHECK(find_vertex(&b, 0, 1, 0, &p, &v));
    m = material_of(&b, p);
    if (getenv("AR_DUMP"))
        dump_material("blue", m);
    PGR_CHECK(m != NULL && near4(m->base_color, 0, 0, 1, 128.0 / 255.0, 1e-5)
              && m->alpha_mode == PGR_ALPHA_BLEND);
    PGR_CHECK(m != NULL && near(m->emissive[0], 0.1, 1e-5) && near(m->emissive[1], 0.2, 1e-5)
              && near(m->emissive[2], 0.3, 1e-5));
    pgr_scene_free(&a);
    pgr_scene_free(&b);

    /* Unlit red: written black but for its emissive colour, read as unlit
       in that colour. */
    PGR_CHECK(fx_edited("ar_unlit.gltf", fx_multi_material, "{\"name\":\"red\",",
                        "{\"name\":\"red\",\"extensions\":{\"KHR_materials_unlit\":{}},") == 0);
    PGR_CHECK(trip("ar_unlit.gltf", triangles, NULL, &a, &b, &rs) == PGR_OK);
    PGR_CHECK(find_vertex(&b, 1, 0, 0, &p, &v));
    m = material_of(&b, p);
    if (getenv("AR_DUMP"))
        dump_material("unlit", m);
    PGR_CHECK(m != NULL && m->unlit && near4(m->base_color, 1, 0, 0, 1, 1e-5));
    PGR_CHECK(m != NULL && near(m->emissive[0], 0, 1e-6));
    /* Blue is not unlit. */
    PGR_CHECK(find_vertex(&b, 0, 1, 0, &p, &v) && material_of(&b, p) != NULL && !material_of(&b, p)->unlit);

    /* Written as glTF (KHR_materials_unlit) and read again: still unlit. */
    {
        pgr_glb_read_options ro;
        pgr_glb_write_options go;
        pgr_scene c;
        const char *glb = triangles ? "ar_unlit_back_tri.glb" : "ar_unlit_back.glb";

        memset(&ro, 0, sizeof(ro));
        memset(&go, 0, sizeof(go));
        ro.messages = go.messages = &msg;
        pgr_scene_init(&c);
        PGR_CHECK(pgr_glb_write(&b, glb, &go) == PGR_OK && pgr_glb_read(glb, &ro, &c) == PGR_OK);
        PGR_CHECK(find_vertex(&c, 1, 0, 0, &p, &v) && material_of(&c, p) != NULL && material_of(&c, p)->unlit);
        pgr_scene_free(&c);
    }
    pgr_scene_free(&a);
    pgr_scene_free(&b);
}

/* A colour component as written, one sRGB byte, read back: within half a
   byte step of linear. */
#define BYTE_TOL 0.003

static void
test_vertex_colors(int triangles)
{
    static const struct { const char *path, *material; double r, g, b, alpha_b; int blend; } vc[3] = {
        { "ar_vcol.gltf", NULL, 1.0, 1.0, 1.0, 1.0, 0 },
        { "ar_vcol_grey.gltf", "\"baseColorFactor\":[0.5,0.2,0.1,1]}}]", 0.5, 0.2, 0.1, 1.0, 0 },
        { "ar_vcol_blend.gltf", "\"baseColorFactor\":[1,1,1,0.5]},\"alphaMode\":\"BLEND\"}]", 1.0, 1.0, 1.0, 0.25, 1 }
    };
    pgr_scene a, b;
    pgr_prc_read_stats rs;
    char mat[200];
    int i;

    /* fx_tri_vcolors' corners are (1,0,0,1), (0,1,0,0.5), (0,0,1,1) at
       (0,0,0), (1,0,0), (0,1,0). pgr_prc_write multiplies in an untextured
       material's base colour, and its alpha only when it blends. */
    for (i = 0; i < 3; i++)
    {
        const pgr_primitive *p;
        const pgr_material *m;
        uint32_t v0, v1, v2;

        if (vc[i].material == NULL)
            PGR_CHECK(fx_tri_vcolors(vc[i].path) == 0);
        else
        {
            snprintf(mat, sizeof(mat), "},\"material\":0}]}],\"materials\":[{\"pbrMetallicRoughness\":{%s",
                     vc[i].material);
            PGR_CHECK(fx_edited(vc[i].path, fx_tri_vcolors, "}}]}]", mat) == 0);
        }
        PGR_CHECK(trip(vc[i].path, triangles, NULL, &a, &b, &rs) == PGR_OK);
        PGR_CHECK(rs.coloured == 1 && rs.primitives == 1);
        if (find_vertex(&b, 0, 0, 0, &p, &v0) && find_vertex(&b, 1, 0, 0, &p, &v1)
            && find_vertex(&b, 0, 1, 0, &p, &v2))
        {
            PGR_CHECK(p->colors != NULL);
            if (p->colors != NULL)
            {
                PGR_CHECK(near4(&p->colors[4 * v0], vc[i].r, 0, 0, vc[i].blend ? 0.5 : 1.0, BYTE_TOL));
                PGR_CHECK(near4(&p->colors[4 * v1], 0, vc[i].g, 0, vc[i].alpha_b, BYTE_TOL));
                PGR_CHECK(near4(&p->colors[4 * v2], 0, 0, vc[i].b, vc[i].blend ? 0.5 : 1.0, BYTE_TOL));
            }
            /* A white base colour, the corners' alphas applying when they
               blend. */
            m = material_of(&b, p);
            if (getenv("AR_DUMP"))
                dump_material(vc[i].path, m);
            PGR_CHECK(m != NULL && near4(m->base_color, 1, 1, 1, 1, 1e-6) && m->metallic == 0.0f);
            PGR_CHECK(m != NULL && m->alpha_mode == (vc[i].blend ? PGR_ALPHA_BLEND : PGR_ALPHA_OPAQUE));
        }
        else
            PGR_CHECK(!"the triangle's corners were not read back");
        pgr_scene_free(&a);
        pgr_scene_free(&b);
    }
}

/* Lines on a material of base (0.5, 0.2, 0.1), red where x = 0 and green
   elsewhere (vary), or red everywhere: written as (0.5, 0, 0) and
   (0, 0.2, 0). */
static int vary;

static void
lines_on_material(pgr_scene *s)
{
    pgr_material *m;
    uint32_t i, j, v;
    int mi = pgr_scene_add_material(s, "lines", &m);

    if (mi < 0)
        return;
    m->base_color[0] = 0.5f;
    m->base_color[1] = 0.2f;
    m->base_color[2] = 0.1f;
    for (i = 0; i < s->num_meshes; i++)
        for (j = 0; j < s->meshes[i].num_primitives; j++)
        {
            pgr_primitive *p = &s->meshes[i].primitives[j];

            p->material = mi;
            free(p->colors);
            p->colors = (float *)malloc((size_t)p->num_vertices * 4 * sizeof(float));
            if (p->colors == NULL)
                return;
            for (v = 0; v < p->num_vertices; v++)
            {
                int red = !vary || p->positions[3 * v] == 0.0f;

                p->colors[4 * v] = red ? 1.0f : 0.0f;
                p->colors[4 * v + 1] = red ? 0.0f : 1.0f;
                p->colors[4 * v + 2] = 0.0f;
                p->colors[4 * v + 3] = 1.0f;
            }
        }
}

/* Lines as triangles: colours that vary back as vertex colours on a white
   base. One colour on every point is read as the colour of the lines: the
   reader gives a wire no material of its own, so it looks like a style
   colour, which draws the same. */
static void
test_line_colors(void)
{
    pgr_scene a, b;
    pgr_prc_read_stats rs;
    uint32_t i, j, v, n = 0;

    PGR_CHECK(fx_lines("ar_lines.gltf") == 0);
    vary = 1;
    PGR_CHECK(trip("ar_lines.gltf", 0, lines_on_material, &a, &b, &rs) == PGR_OK);
    PGR_CHECK(rs.segments > 0 && rs.coloured > 0);
    for (i = 0; i < b.num_meshes; i++)
        for (j = 0; j < b.meshes[i].num_primitives; j++)
        {
            const pgr_primitive *p = &b.meshes[i].primitives[j];
            const pgr_material *m = material_of(&b, p);

            PGR_CHECK(p->mode == PGR_LINES && p->colors != NULL);
            PGR_CHECK(m != NULL && near4(m->base_color, 1, 1, 1, 1, 1e-6));
            for (v = 0; p->colors != NULL && v < p->num_vertices; v++, n++)
            {
                if (p->positions[3 * v] == 0.0f)
                    PGR_CHECK(near4(&p->colors[4 * v], 0.5, 0, 0, 1, BYTE_TOL));
                else
                    PGR_CHECK(near4(&p->colors[4 * v], 0, 0.2, 0, 1, BYTE_TOL));
            }
        }
    PGR_CHECK(n > 0);
    pgr_scene_free(&a);
    pgr_scene_free(&b);

    vary = 0;
    n = 0;
    PGR_CHECK(trip("ar_lines.gltf", 0, lines_on_material, &a, &b, &rs) == PGR_OK);
    PGR_CHECK(rs.segments > 0 && rs.coloured == 0);
    for (i = 0; i < b.num_meshes; i++)
        for (j = 0; j < b.meshes[i].num_primitives; j++, n++)
        {
            const pgr_primitive *p = &b.meshes[i].primitives[j];
            const pgr_material *m = material_of(&b, p);

            if (getenv("AR_DUMP"))
                dump_material("one-colour lines", m);
            PGR_CHECK(p->mode == PGR_LINES && p->colors == NULL);
            PGR_CHECK(m != NULL && near4(m->base_color, 0.5, 0, 0, 1, BYTE_TOL));
        }
    PGR_CHECK(n > 0);
    pgr_scene_free(&a);
    pgr_scene_free(&b);
}

/* fx_png_2x2's pixels: top row red, green; bottom row blue,
   half-transparent white. */
static const unsigned char expect_pixels[16] = {
    255, 0, 0, 255,   0, 255, 0, 255,
    0, 0, 255, 255,   255, 255, 255, 128
};

/* The pixels of the first textured face nanoPRC's reader finds in path,
   copied to px (16 bytes) with the size; 0 when there is none. */
static int
prc_pixels(const char *path, unsigned char px[16], uint32_t *w, uint32_t *h, uint32_t *ch)
{
    prc_context *ctx = prc_api_new_context(NULL);
    prc_api_data data;
    prc_api_product *tree = NULL;
    prc_api_tess *tess = NULL;
    uint32_t np, nprod, nm, nt = 0, nlt = 0, nx = 0, k;
    uint8_t has_lines = 0;
    int found = 0;

    if (ctx == NULL)
        return 0;
    data = prc_api_open_contents(ctx, path);
    if (data == NULL || prc_api_prep_model_tree(ctx, data, &np, &nprod, &nm) != 0
        || prc_api_create_model_tree(ctx, data, &tree, np, nprod, nm) != 0
        || prc_api_get_number_tessellations(ctx, data, tree, &nt, &nlt, &nx, 0) != 0)
    {
        if (data != NULL)
            prc_api_release_data(ctx, data, NULL, 0, NULL, 0, NULL, 0, tree);
        prc_api_release_context(ctx);
        return 0;
    }
    tess = (prc_api_tess *)calloc(nt ? nt : 1, sizeof(prc_api_tess));
    for (k = 0; tess != NULL && k < nt; k++)
    {
        prc_api_tess *t = &tess[k];

        t->num_faces = prc_api_get_number_faces(ctx, data, k);
        t->tess_faces = (prc_api_face *)calloc(t->num_faces ? t->num_faces : 1, sizeof(prc_api_face));
        if (prc_api_initialize_tessellation(ctx, data, tree, k, t, NULL, &has_lines) < 0
            || (t->type != PRC_API_TESS_3D && t->type != PRC_API_TESS_3D_Compressed) || t->num_faces == 0)
            continue;
        if (prc_api_get_tessellation_vertices(ctx, data, k, 0, &t->tess_faces[0], t) >= 0 && !found
            && t->tess_faces[0].is_texture && t->tess_faces[0].texture.data != NULL)
        {
            const prc_api_texture *x = &t->tess_faces[0].texture;

            *w = x->width;
            *h = x->height;
            *ch = x->num_channels;
            if ((size_t)x->width * x->height * x->num_channels <= 16)
                memcpy(px, x->data, (size_t)x->width * x->height * x->num_channels);
            found = 1;
        }
    }
    prc_api_release_data(ctx, data, tess, nt, NULL, 0, NULL, 0, tree);
    for (k = 0; tess != NULL && k < nt; k++)
        free(tess[k].tess_faces);
    free(tess);
    prc_api_release_context(ctx);
    return found;
}

/* A second node drawing a copy of the first primitive with a material of
   its own, base colour (0.2, 0.2, 0.2), and the same texture. */
static void
second_textured_material(pgr_scene *s)
{
    pgr_material *m;
    pgr_mesh *mesh;
    pgr_node *n;
    pgr_primitive *q;
    const pgr_primitive *p;
    int mi, me, ni;

    if (s->num_meshes != 1 || s->meshes[0].num_primitives != 1 || s->num_materials != 1)
        return;
    mi = pgr_scene_add_material(s, "second", &m);
    if (mi < 0)
        return;
    *m = s->materials[0];
    m->name = NULL;
    m->base_color[0] = m->base_color[1] = m->base_color[2] = 0.2f;
    me = pgr_scene_add_mesh(s, "second", &mesh);
    if (me < 0 || pgr_mesh_add_primitive(mesh, PGR_TRIANGLES, &q) < 0)
        return;
    p = &s->meshes[0].primitives[0];
    if (pgr_primitive_alloc(q, p->num_vertices, p->num_indices, p->normals != NULL, p->uvs != NULL, 0)
        != PGR_OK)
        return;
    memcpy(q->positions, p->positions, (size_t)p->num_vertices * 3 * sizeof(float));
    if (p->normals != NULL)
        memcpy(q->normals, p->normals, (size_t)p->num_vertices * 3 * sizeof(float));
    if (p->uvs != NULL)
        memcpy(q->uvs, p->uvs, (size_t)p->num_vertices * 2 * sizeof(float));
    memcpy(q->indices, p->indices, (size_t)p->num_indices * sizeof(uint32_t));
    q->material = mi;
    ni = pgr_scene_add_node(s, "second", &n);
    if (ni < 0)
        return;
    n->mesh = me;
    n->has_matrix = 1;
    pgr_mat4_identity(n->matrix);
    n->matrix[12] = 2.0;
    pgr_scene_add_root(s, (uint32_t)ni);
}

static void
test_textures(int triangles)
{
    double tol = triangles ? 1e-6 : 1e-3;
    pgr_scene a, b, c;
    pgr_prc_read_stats rs;
    pgr_glb_read_options ro;
    pgr_glb_write_options go;
    pgr_prc_write_options wo;
    const pgr_primitive *pa, *pb;
    const pgr_material *m;
    unsigned char px[16];
    uint32_t i, v, w = 0, h = 0, ch = 0;
    int mime;
    const char *glb = triangles ? "ar_tex_back_tri.glb" : "ar_tex_back.glb";
    const char *prc = triangles ? "ar_tex_again_tri.prc" : "ar_tex_again.prc";

    PGR_CHECK(fx_quad_textured("ar_tex.gltf", FX_IMAGE_VIEW, "", NULL, 1) == 0);
    PGR_CHECK(trip("ar_tex.gltf", triangles, NULL, &a, &b, &rs) == PGR_OK);
    PGR_CHECK(rs.textures == 1 && rs.textured == 1 && b.num_textures == 1);

    /* The image as a PNG of the same size, and the material's colour. */
    if (b.num_textures == 1)
    {
        PGR_CHECK(b.textures[0].mime == PGR_IMAGE_PNG);
        PGR_CHECK(pgr_image_info(b.textures[0].bytes, b.textures[0].size, &mime, &w, &h) == PGR_OK
                  && mime == PGR_IMAGE_PNG && w == 2 && h == 2);
    }
    pa = &a.meshes[0].primitives[0];
    PGR_CHECK(find_vertex(&b, 0, 0, 0, &pb, &v));
    m = material_of(&b, pb);
    if (getenv("AR_DUMP"))
        dump_material("textured", m);
    PGR_CHECK(m != NULL && m->texture == 0 && near4(m->base_color, 0.5, 0.5, 0.5, 1, 1e-5));

    /* The same texture coordinates at the same positions. */
    PGR_CHECK(pb != NULL && pb->uvs != NULL && pb->num_vertices == pa->num_vertices);
    for (i = 0; pb != NULL && pb->uvs != NULL && i < pa->num_vertices; i++)
    {
        const pgr_primitive *q;

        PGR_CHECK(find_vertex(&b, pa->positions[3 * i], pa->positions[3 * i + 1], pa->positions[3 * i + 2],
                              &q, &v));
        PGR_CHECK(near(q->uvs[2 * v], pa->uvs[2 * i], tol) && near(q->uvs[2 * v + 1], pa->uvs[2 * i + 1], tol));
    }

    /* Written as glTF and read again: image, sampler, texture and the
       material's link to it kept. */
    memset(&go, 0, sizeof(go));
    memset(&ro, 0, sizeof(ro));
    go.messages = ro.messages = &msg;
    PGR_CHECK(pgr_glb_write(&b, glb, &go) == PGR_OK);
    pgr_scene_init(&c);
    PGR_CHECK(pgr_glb_read(glb, &ro, &c) == PGR_OK);
    PGR_CHECK(c.num_textures == 1 && c.num_materials == 1 && c.materials[0].texture == 0);
    if (c.num_textures == 1 && b.num_textures == 1)
        PGR_CHECK(c.textures[0].size == b.textures[0].size
                  && memcmp(c.textures[0].bytes, b.textures[0].bytes, b.textures[0].size) == 0
                  && c.textures[0].wrap_s == PGR_WRAP_REPEAT && c.textures[0].wrap_t == PGR_WRAP_REPEAT);

    /* And as PRC once more: nanoPRC's reader finds the fixture's pixels. */
    memset(&wo, 0, sizeof(wo));
    wo.messages = &msg;
    wo.triangles = triangles;
    PGR_CHECK(pgr_prc_write(&c, prc, NULL, &wo, NULL) == PGR_OK);
    PGR_CHECK(prc_pixels(prc, px, &w, &h, &ch) && w == 2 && h == 2 && ch == 4
              && memcmp(px, expect_pixels, sizeof(expect_pixels)) == 0);
    pgr_scene_free(&a);
    pgr_scene_free(&b);
    pgr_scene_free(&c);

    /* Two materials with the same image: one texture. */
    PGR_CHECK(trip("ar_tex.gltf", triangles, second_textured_material, &a, &b, &rs) == PGR_OK);
    PGR_CHECK(rs.textured == 2 && rs.materials == 2 && rs.textures == 1 && b.num_textures == 1);
    for (i = 0; i < b.num_materials; i++)
        PGR_CHECK(b.materials[i].texture == 0);
    pgr_scene_free(&a);
    pgr_scene_free(&b);
}

int
main(void)
{
    test_materials(0);
    test_materials(1);
    test_vertex_colors(0);
    test_vertex_colors(1);
    test_line_colors();
    test_textures(0);
    test_textures(1);
    return PGR_TEST_RESULT;
}
