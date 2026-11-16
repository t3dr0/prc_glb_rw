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

/* KHR_draco_mesh_compression end to end, in builds with Draco:

     reader  a Draco cube made by the fixtures straight from the shim, not
             by pgr_glb_write, reads back as the uncompressed cube;
     RT4     glTF -> scene -> glTF with --draco -> scene: every triangle
             within the position quantisation, the same nodes, lines left
             uncompressed, vertex colours and texture coordinates within
             theirs; the file lists the extension as required and its Draco
             accessors have no buffer view. Lossless options give the
             positions back exactly;
     RT5     glTF (Draco) -> PRC -> glTF (Draco) -> PRC: the second PRC
             has as many triangles as the first, and its bounding box agrees
             within the sum of the tolerances. */

#include "cgltf.h"
#include "fixtures.h"
#include "pgr_glb.h"
#include "pgr_prc.h"
#include "pgr_test.h"
#include "scene_compare.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
quiet(void *user, int is_error, const char *text)
{
    (void)user;
    if (is_error)
        fprintf(stderr, "error: %s\n", text);
}

static pgr_messages msg = { quiet, NULL, 0, 0, 0 };

static int
read_glb(const char *path, pgr_scene *s)
{
    pgr_glb_read_options o;

    o.messages = &msg;
    pgr_scene_init(s);
    return pgr_glb_read(path, &o, s);
}

static int
write_draco(const pgr_scene *s, const char *path, const ds_encode_options *dopt)
{
    pgr_glb_write_options o;

    memset(&o, 0, sizeof(o));
    o.messages = &msg;
    o.draco = 1;
    o.draco_options = dopt;
    return pgr_glb_write(s, path, &o);
}

/* A Draco primitive's accessors against its data, decoded here: each
   attribute's count is the decoded point count, the indices' count three
   per decoded face, 16-bit exactly when every index fits, and POSITION's
   min and max exactly those of the decoded positions. */
static void
check_against_decoded(const cgltf_data *data, const cgltf_primitive *p)
{
    const cgltf_buffer_view *v = p->draco_mesh_compression.buffer_view;
    int ids[8];
    cgltf_size k, n = p->draco_mesh_compression.attributes_count;
    ds_mesh m;
    uint32_t i;
    int c;

    if (v == NULL || v->buffer->data == NULL || n > 8)
    {
        PGR_CHECK(0);
        return;
    }
    for (k = 0; k < n; k++)
        ids[k] = pgr_draco_attribute_id(data, &p->draco_mesh_compression.attributes[k]);
    PGR_CHECK(ds_decode((const unsigned char *)v->buffer->data + v->offset, v->size, ids, (uint32_t)n, &m) == DS_OK);
    PGR_CHECK(p->indices->count == (cgltf_size)m.num_faces * 3);
    PGR_CHECK(p->indices->component_type
              == (m.num_points <= 65535 ? cgltf_component_type_r_16u : cgltf_component_type_r_32u));
    for (k = 0; k < p->attributes_count; k++)
    {
        const cgltf_accessor *a = p->attributes[k].data;

        PGR_CHECK(a->count == m.num_points);
        if (p->attributes[k].type == cgltf_attribute_type_position && m.attributes != NULL)
        {
            const float *pos = NULL;
            cgltf_size j;

            for (j = 0; j < n; j++)
                if (p->draco_mesh_compression.attributes[j].type == cgltf_attribute_type_position)
                    pos = m.attributes[j].data;
            PGR_CHECK(pos != NULL);
            for (c = 0; c < 3 && pos != NULL; c++)
            {
                float lo = pos[c], hi = pos[c];

                for (i = 1; i < m.num_points; i++)
                {
                    lo = pos[3 * i + c] < lo ? pos[3 * i + c] : lo;
                    hi = pos[3 * i + c] > hi ? pos[3 * i + c] : hi;
                }
                PGR_CHECK(a->min[c] == lo && a->max[c] == hi);
            }
        }
    }
    ds_mesh_free(&m);
}

/* The file as written: every triangle primitive in Draco form with
   accessors that have no buffer view and POSITION's min and max, every
   line primitive not; the extension used and required. Returns the number
   of Draco primitives. */
static unsigned
check_file(const char *path)
{
    cgltf_options o;
    cgltf_data *data = NULL;
    cgltf_size i, j, k;
    unsigned n = 0;
    int required = 0, used = 0;

    memset(&o, 0, sizeof(o));
    PGR_CHECK(cgltf_parse_file(&o, path, &data) == cgltf_result_success);
    if (data == NULL)
        return 0;
    PGR_CHECK(cgltf_load_buffers(&o, data, path) == cgltf_result_success);
    for (i = 0; i < data->extensions_used_count; i++)
        used |= strcmp(data->extensions_used[i], "KHR_draco_mesh_compression") == 0;
    for (i = 0; i < data->extensions_required_count; i++)
        required |= strcmp(data->extensions_required[i], "KHR_draco_mesh_compression") == 0;
    for (i = 0; i < data->meshes_count; i++)
        for (j = 0; j < data->meshes[i].primitives_count; j++)
        {
            const cgltf_primitive *p = &data->meshes[i].primitives[j];

            if (p->type == cgltf_primitive_type_lines)
            {
                PGR_CHECK(!p->has_draco_mesh_compression);
                continue;
            }
            PGR_CHECK(p->has_draco_mesh_compression);
            if (!p->has_draco_mesh_compression)
                continue;
            n++;
            PGR_CHECK(p->draco_mesh_compression.buffer_view != NULL);
            PGR_CHECK(p->draco_mesh_compression.attributes_count == p->attributes_count);
            PGR_CHECK(p->indices != NULL && p->indices->buffer_view == NULL);
            for (k = 0; k < p->attributes_count; k++)
            {
                PGR_CHECK(p->attributes[k].data->buffer_view == NULL);
                if (p->attributes[k].type == cgltf_attribute_type_position)
                    PGR_CHECK(p->attributes[k].data->has_min && p->attributes[k].data->has_max);
            }
            check_against_decoded(data, p);
        }
    PGR_CHECK(n == 0 || (used && required));
    cgltf_free(data);
    return n;
}

/* Every vertex of b's primitives with colours or texture coordinates has
   a vertex of a's at the same place (within pos_tol) whose colour and
   texture coordinates are within tol of its own. */
static void
check_attributes(const pgr_scene *a, const pgr_scene *b, double pos_tol, double tol)
{
    uint32_t m, p, i, j, q;

    PGR_CHECK(a->num_meshes == b->num_meshes);
    for (m = 0; m < a->num_meshes && m < b->num_meshes; m++)
        for (p = 0; p < b->meshes[m].num_primitives && p < a->meshes[m].num_primitives; p++)
        {
            const pgr_primitive *pa = &a->meshes[m].primitives[p], *pb = &b->meshes[m].primitives[p];

            PGR_CHECK((pa->colors != NULL) == (pb->colors != NULL));
            PGR_CHECK((pa->uvs != NULL) == (pb->uvs != NULL));
            PGR_CHECK((pa->normals != NULL) == (pb->normals != NULL));
            PGR_CHECK(pa->material == pb->material);
            if (pb->colors == NULL && pb->uvs == NULL)
                continue;
            for (i = 0; i < pb->num_vertices; i++)
            {
                double best = 1e30;

                for (j = 0; j < pa->num_vertices; j++)
                {
                    double d = 0, e = 0;

                    for (q = 0; q < 3; q++)
                        d += fabs((double)pb->positions[3 * i + q] - pa->positions[3 * j + q]);
                    if (d > pos_tol)
                        continue;
                    if (pb->colors)
                        for (q = 0; q < 4; q++)
                            e = fmax(e, fabs((double)pb->colors[4 * i + q] - pa->colors[4 * j + q]));
                    if (pb->uvs)
                        for (q = 0; q < 2; q++)
                            e = fmax(e, fabs((double)pb->uvs[2 * i + q] - pa->uvs[2 * j + q]));
                    best = e < best ? e : best;
                }
                PGR_CHECK(best <= tol);
            }
        }
}

/* RT4 on one fixture: the scene, written with Draco and read back, draws
   the same, within tol of the diagonal. */
static void
rt4(const char *name, int (*make)(const char *), const char *ext, const ds_encode_options *dopt, double tol)
{
    char src[256], out[256];
    pgr_scene a, b;
    scene_diff d;
    int ok;

    snprintf(src, sizeof(src), "rt4_%s_src.glb", name);
    snprintf(out, sizeof(out), "rt4_%s%s", name, ext);
    PGR_CHECK(make(src) == 0);
    PGR_CHECK(read_glb(src, &a) == PGR_OK);
    PGR_CHECK(write_draco(&a, out, dopt) == PGR_OK);
    check_file(out);
    PGR_CHECK(read_glb(out, &b) == PGR_OK);
    ok = scene_compare(&a, &b, tol, &d) == 0;
    if (!ok)
        fprintf(stderr, "RT4 %s: triangles %u -> %u, segments %u -> %u, unmatched %u, max error %g\n",
                name, d.triangles_a, d.triangles_b, d.segments_a, d.segments_b, d.unmatched, d.max_error);
    else
        printf("RT4 %s: %u triangles, %u segments, max error %.3g of diagonal %.3g\n", name, d.triangles_a,
               d.segments_a, d.max_error, d.diagonal);
    PGR_CHECK(ok);
    PGR_CHECK(scene_same_names(&a, &b));
    /* Colour 8 bits, texture coordinates 12, both over 0..1. */
    check_attributes(&a, &b, 1e-3, dopt != NULL && dopt->qbits_color == 0 ? 1e-6 : 1.5 / 255);
    pgr_scene_free(&a);
    pgr_scene_free(&b);
}

/* A scene of one node drawing one triangle primitive, from the given
   positions and indices. */
static void
one_primitive_scene(pgr_scene *s, const float *pos, uint32_t nv, const uint32_t *idx, uint32_t ni)
{
    pgr_mesh *m;
    pgr_primitive *p;
    pgr_node *n;

    pgr_scene_init(s);
    s->up_axis = PGR_Y_UP;
    s->unit_scale_to_m = 1.0;
    PGR_CHECK(pgr_scene_add_mesh(s, "made", &m) == 0);
    PGR_CHECK(pgr_mesh_add_primitive(m, PGR_TRIANGLES, &p) == 0);
    PGR_CHECK(pgr_primitive_alloc(p, nv, ni, 0, 0, 0) == PGR_OK);
    memcpy(p->positions, pos, (size_t)nv * 3 * sizeof(float));
    memcpy(p->indices, idx, (size_t)ni * sizeof(uint32_t));
    p->material = -1;
    PGR_CHECK(pgr_scene_add_node(s, "node", &n) == 0);
    n->mesh = 0;
    PGR_CHECK(pgr_scene_add_root(s, 0) == PGR_OK);
}

/* RT4 on a scene made here rather than from a fixture file. scene_compare
   matches triangles pairwise, too slowly for a large mesh: with compare 0
   only the triangle count and the bounding box (within tol of the
   diagonal) are compared. */
static void
rt4_scene(const char *name, const pgr_scene *a, double tol, uint32_t expect_points, int compare)
{
    char out[256];
    pgr_scene b;
    scene_diff d;
    pgr_scene_stats sa, sb;
    double mna[3], mxa[3], mnb[3], mxb[3], diag = 0;
    int k;

    snprintf(out, sizeof(out), "rt4_%s.glb", name);
    PGR_CHECK(write_draco(a, out, NULL) == PGR_OK);
    PGR_CHECK(check_file(out) == 1);
    PGR_CHECK(read_glb(out, &b) == PGR_OK);
    if (compare)
    {
        PGR_CHECK(scene_compare(a, &b, tol, &d) == 0);
        printf("RT4 %s: %u triangles, max error %.3g of diagonal %.3g\n", name, d.triangles_a, d.max_error,
               d.diagonal);
    }
    pgr_scene_get_stats(a, &sa);
    pgr_scene_get_stats(&b, &sb);
    PGR_CHECK(sa.triangles == sb.triangles);
    if (!scene_bounds(a, mna, mxa) || !scene_bounds(&b, mnb, mxb))
    {
        PGR_CHECK(0);
        pgr_scene_free(&b);
        return;
    }
    for (k = 0; k < 3; k++)
        diag += (mxa[k] - mna[k]) * (mxa[k] - mna[k]);
    diag = sqrt(diag);
    for (k = 0; k < 3; k++)
        PGR_CHECK(fabs(mna[k] - mnb[k]) <= tol * diag && fabs(mxa[k] - mxb[k]) <= tol * diag);
    if (!compare)
        printf("RT4 %s: %u triangles, %u points\n", name, sb.triangles, sb.vertices);
    if (b.num_meshes == 1 && b.meshes[0].num_primitives == 1)
        PGR_CHECK(b.meshes[0].primitives[0].num_vertices == expect_points);
    else
        PGR_CHECK(0);
    pgr_scene_free(&b);
}

/* A cube as 12 separate triangles, 36 corners on 8 places: Draco merges
   them, so the file must state 8 points, not 36. */
static void
rt4_soup(void)
{
    static const float corner[8][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 },
                                        { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } };
    static const uint32_t tri[36] = { 0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                                      1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7 };
    float pos[108];
    uint32_t idx[36], i;
    pgr_scene s;

    for (i = 0; i < 36; i++)
    {
        memcpy(pos + 3 * i, corner[tri[i]], 3 * sizeof(float));
        idx[i] = i;
    }
    one_primitive_scene(&s, pos, 36, idx, 36);
    rt4_scene("soup", &s, 2e-4, 8, 1);
    pgr_scene_free(&s);
}

/* A sphere of over 65535 points, so that the indices are 32-bit. */
static void
rt4_large(void)
{
    const uint32_t slices = 300, stacks = 230;
    uint32_t nv = (slices + 1) * (stacks + 1), nf = 2 * slices * (stacks - 1), i, j, f = 0;
    float *pos = (float *)malloc((size_t)nv * 3 * sizeof(float));
    uint32_t *idx = (uint32_t *)malloc((size_t)nf * 3 * sizeof(uint32_t));
    pgr_scene s;

    for (i = 0; i <= stacks; i++)
        for (j = 0; j <= slices; j++)
        {
            /* The seam column is moved off the first by a hair, so that
               no two points coincide and Draco keeps all of them. */
            double th = 3.14159265358979 * (0.001 + 0.998 * i / stacks);
            double ph = 2 * 3.14159265358979 * j / (slices + 0.5);
            uint32_t v = i * (slices + 1) + j;

            pos[3 * v] = (float)(sin(th) * cos(ph));
            pos[3 * v + 1] = (float)(sin(th) * sin(ph));
            pos[3 * v + 2] = (float)cos(th);
        }
    for (i = 0; i < stacks; i++)
        for (j = 0; j < slices; j++)
        {
            uint32_t a = i * (slices + 1) + j, b = a + 1, c = a + slices + 1, d = c + 1;

            if (i != 0)
                idx[3 * f] = a, idx[3 * f + 1] = c, idx[3 * f + 2] = b, f++;
            if (i != stacks - 1)
                idx[3 * f] = b, idx[3 * f + 1] = c, idx[3 * f + 2] = d, f++;
        }
    PGR_CHECK(nv > 65535 && f == nf);
    one_primitive_scene(&s, pos, nv, idx, 3 * nf);
    free(pos);
    free(idx);
    /* Two points are in no triangle (the first of the top row, the last
       of the bottom one), and Draco leaves such points out. */
    rt4_scene("large", &s, 2e-4, nv - 2, 0);
    pgr_scene_free(&s);
}

static int
make_quad(const char *path)
{
    return fx_quad_textured(path, FX_IMAGE_VIEW, "", NULL, 1);
}

/* The reader on Draco data the writer did not make: the fixture cube's
   triangles encoded by the shim and put in a file by fixtures.c. */
static void
reader_on_fixture(void)
{
    pgr_scene a, b;
    const pgr_primitive *p;
    ds_attribute att[2];
    ds_mesh in, dec;
    void *blob = NULL;
    size_t size = 0;
    int ids[2];
    scene_diff d;

    PGR_CHECK(fx_cube_flat("rd_cube_src.glb") == 0);
    PGR_CHECK(read_glb("rd_cube_src.glb", &a) == PGR_OK);
    if (a.num_meshes != 1 || a.meshes[0].num_primitives != 1)
    {
        PGR_CHECK(0);
        pgr_scene_free(&a);
        return;
    }
    p = &a.meshes[0].primitives[0];
    memset(att, 0, sizeof(att));
    att[0].semantic = DS_POSITION, att[0].num_components = 3, att[0].data = p->positions;
    att[1].semantic = DS_NORMAL, att[1].num_components = 3, att[1].data = p->normals;
    in.num_points = p->num_vertices;
    in.num_faces = p->num_indices / 3;
    in.indices = p->indices;
    in.num_attributes = 2;
    in.attributes = att;
    PGR_CHECK(ds_encode(&in, NULL, &blob, &size, ids) == DS_OK);
    PGR_CHECK(ds_decode(blob, size, ids, 2, &dec) == DS_OK);
    /* The accessors carry Draco's own point count, as a writer should. */
    PGR_CHECK(fx_draco("rd_cube.glb", blob, size, dec.num_points, dec.num_faces * 3, ids[0], ids[1], 0) == 0);
    /* And a file whose accessors state 5 points too many: the decoded
       count is what is read, with a warning. */
    PGR_CHECK(fx_draco("rd_cube_miscount.glb", blob, size, dec.num_points + 5, dec.num_faces * 3, ids[0], ids[1],
                       0) == 0);
    ds_buffer_free(blob);

    PGR_CHECK(read_glb("rd_cube.glb", &b) == PGR_OK);
    PGR_CHECK(scene_compare(&a, &b, 1e-4, &d) == 0);
    PGR_CHECK(d.triangles_b == 12);
    PGR_CHECK(b.num_meshes == 1 && b.meshes[0].num_primitives == 1 && b.meshes[0].primitives[0].normals != NULL);
    pgr_scene_free(&b);

    {
        pgr_messages counted = { NULL, NULL, 0, 0, 0 };
        pgr_glb_read_options o;

        counted.fn = quiet;
        o.messages = &counted;
        pgr_scene_init(&b);
        PGR_CHECK(pgr_glb_read("rd_cube_miscount.glb", &o, &b) == PGR_OK);
        PGR_CHECK(counted.warnings == 1);
        PGR_CHECK(b.num_meshes == 1 && b.meshes[0].num_primitives == 1
                  && b.meshes[0].primitives[0].num_vertices == dec.num_points);
        PGR_CHECK(scene_compare(&a, &b, 1e-4, &d) == 0);
        pgr_scene_free(&b);
    }
    ds_mesh_free(&dec);
    pgr_scene_free(&a);
}

/* A Draco primitive's texture coordinates get the material's
   KHR_texture_transform, as an uncompressed one's do: the textured quad
   written with Draco as .gltf, then its JSON given an offset of
   (0.5, 0.25), reads back with every u and v moved by that much. */
static void
texture_transform(void)
{
    static const char ins[] = "\"extensions\":{\"KHR_texture_transform\":{\"offset\":[0.5,0.25]}},";
    pgr_scene a, b, c;
    FILE *f;
    char *json = NULL, *at, *edited;
    long n;
    uint32_t i;

    PGR_CHECK(make_quad("tt_src.glb") == 0);
    PGR_CHECK(read_glb("tt_src.glb", &a) == PGR_OK);
    PGR_CHECK(write_draco(&a, "tt.gltf", NULL) == PGR_OK);
    PGR_CHECK(read_glb("tt.gltf", &b) == PGR_OK);

    f = fopen("tt.gltf", "rb");
    PGR_CHECK(f != NULL);
    if (f == NULL)
        return;
    n = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    if (n <= 0 || fseek(f, 0, SEEK_SET) != 0)
    {
        PGR_CHECK(0);
        fclose(f);
        return;
    }
    json = (char *)calloc((size_t)n + 1, 1);
    PGR_CHECK(json != NULL && fread(json, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    at = strstr(json, "\"baseColorTexture\"");
    at = at != NULL ? strchr(at, '{') : NULL;
    PGR_CHECK(at != NULL);
    if (at == NULL)
    {
        free(json);
        return;
    }
    edited = (char *)malloc((size_t)n + sizeof(ins));
    memcpy(edited, json, (size_t)(at + 1 - json));
    memcpy(edited + (at + 1 - json), ins, sizeof(ins) - 1);
    memcpy(edited + (at + 1 - json) + sizeof(ins) - 1, at + 1, (size_t)n - (size_t)(at + 1 - json));
    f = fopen("tt_offset.gltf", "wb");
    PGR_CHECK(f != NULL && fwrite(edited, 1, (size_t)n + sizeof(ins) - 1, f) == (size_t)n + sizeof(ins) - 1);
    if (f != NULL)
        fclose(f);
    free(json);
    free(edited);

    /* The .bin beside tt.gltf is found from tt_offset.gltf's folder too. */
    PGR_CHECK(read_glb("tt_offset.gltf", &c) == PGR_OK);
    if (b.num_meshes == 1 && c.num_meshes == 1 && b.meshes[0].num_primitives == 1
        && c.meshes[0].num_primitives == 1 && b.meshes[0].primitives[0].uvs != NULL
        && c.meshes[0].primitives[0].uvs != NULL
        && b.meshes[0].primitives[0].num_vertices == c.meshes[0].primitives[0].num_vertices)
    {
        const pgr_primitive *pb = &b.meshes[0].primitives[0], *pc = &c.meshes[0].primitives[0];

        for (i = 0; i < pb->num_vertices; i++)
        {
            PGR_CHECK(fabs(pc->uvs[2 * i] - (pb->uvs[2 * i] + 0.5)) < 1e-6);
            PGR_CHECK(fabs(pc->uvs[2 * i + 1] - (pb->uvs[2 * i + 1] + 0.25)) < 1e-6);
        }
    }
    else
        PGR_CHECK(0);
    pgr_scene_free(&a);
    pgr_scene_free(&b);
    pgr_scene_free(&c);
}

/* Draco's Edgebreaker leaves out degenerate triangles: the cube's twelve
   with a thirteenth whose corners are distinct vertices but two of them in
   one place, which Draco merges. The file must say 36 indices, not 39, and
   twelve triangles come back. */
static void
rt4_degenerate(void)
{
    static const float pos[15] = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0 };
    static const uint32_t idx[9] = { 0, 1, 2, 0, 2, 4, 0, 2, 3 };
    pgr_scene s, b;
    pgr_scene_stats st;

    one_primitive_scene(&s, pos, 5, idx, 9);
    PGR_CHECK(write_draco(&s, "rt4_degenerate.glb", NULL) == PGR_OK);
    PGR_CHECK(check_file("rt4_degenerate.glb") == 1);
    PGR_CHECK(read_glb("rt4_degenerate.glb", &b) == PGR_OK);
    pgr_scene_get_stats(&b, &st);
    PGR_CHECK(st.triangles == 2);
    pgr_scene_free(&s);
    pgr_scene_free(&b);
}

/* RT5 on one fixture: Draco glTF -> PRC -> scene -> Draco glTF -> scene ->
   PRC -> scene. */
static void
rt5(const char *name, int (*make)(const char *))
{
    char g0[256], g1[256], g2[256], p1[256], p2[256];
    pgr_scene s0, s1, s2, s3, s4;
    pgr_prc_write_options wo;
    pgr_prc_read_options ro;
    pgr_prc_read_stats rs1, rs2;
    pgr_scene_stats t1, t2;
    double mn1[3], mx1[3], mn2[3], mx2[3], diag = 0, worst = 0;
    int k;

    snprintf(g0, sizeof(g0), "rt5_%s_src.glb", name);
    snprintf(g1, sizeof(g1), "rt5_%s_1.glb", name);
    snprintf(g2, sizeof(g2), "rt5_%s_2.glb", name);
    snprintf(p1, sizeof(p1), "rt5_%s_1.prc", name);
    snprintf(p2, sizeof(p2), "rt5_%s_2.prc", name);
    memset(&wo, 0, sizeof(wo));
    memset(&ro, 0, sizeof(ro));
    wo.messages = ro.messages = &msg;

    PGR_CHECK(make(g0) == 0);
    PGR_CHECK(read_glb(g0, &s0) == PGR_OK);
    PGR_CHECK(write_draco(&s0, g1, NULL) == PGR_OK);
    PGR_CHECK(read_glb(g1, &s1) == PGR_OK);
    PGR_CHECK(pgr_prc_write(&s1, p1, NULL, &wo, NULL) == PGR_OK);
    pgr_scene_init(&s2);
    PGR_CHECK(pgr_prc_read(p1, &ro, &s2, &rs1) == PGR_OK);
    PGR_CHECK(write_draco(&s2, g2, NULL) == PGR_OK);
    PGR_CHECK(read_glb(g2, &s3) == PGR_OK);
    PGR_CHECK(pgr_prc_write(&s3, p2, NULL, &wo, NULL) == PGR_OK);
    pgr_scene_init(&s4);
    PGR_CHECK(pgr_prc_read(p2, &ro, &s4, &rs2) == PGR_OK);

    pgr_scene_get_stats(&s2, &t1);
    pgr_scene_get_stats(&s4, &t2);
    PGR_CHECK(rs1.triangles == rs2.triangles);
    PGR_CHECK(t1.triangles == t2.triangles && t1.triangles > 0);
    if (!scene_bounds(&s2, mn1, mx1) || !scene_bounds(&s4, mn2, mx2))
    {
        PGR_CHECK(0);
        for (k = 0; k < 3; k++)
            mn1[k] = mx1[k] = mn2[k] = mx2[k] = 0;
    }
    for (k = 0; k < 3; k++)
    {
        diag += (mx1[k] - mn1[k]) * (mx1[k] - mn1[k]);
        worst = fmax(worst, fmax(fabs(mn1[k] - mn2[k]), fabs(mx1[k] - mx2[k])));
    }
    diag = sqrt(diag);
    /* Two Draco trips (one quantisation step each, 2^-14 of an extent)
       and two compressed tessellations (1e-6 of the diagonal each). */
    printf("RT5 %s: %u -> %u triangles, bounding boxes within %.3g of diagonal %.3g\n", name, rs1.triangles,
           rs2.triangles, worst, diag);
    PGR_CHECK(worst <= diag * (2.0 / 16383 + 2e-6) + 1e-12);
    pgr_scene_free(&s0);
    pgr_scene_free(&s1);
    pgr_scene_free(&s2);
    pgr_scene_free(&s3);
    pgr_scene_free(&s4);
}

int
main(void)
{
    ds_encode_options lossless;

    PGR_CHECK(ds_available());
    memset(&lossless, 0, sizeof(lossless));
    lossless.encode_speed = lossless.decode_speed = 5;

    reader_on_fixture();

    rt4("cube_flat", fx_cube_flat, ".glb", NULL, 2e-4);
    rt4("cube_smooth", fx_cube_smooth, ".glb", NULL, 2e-4);
    rt4("tri_vcolors", fx_tri_vcolors, ".glb", NULL, 2e-4);
    rt4("two_instances", fx_two_instances, ".glb", NULL, 2e-4);
    rt4("hierarchy3", fx_hierarchy3, ".glb", NULL, 2e-4);
    rt4("multi_material", fx_multi_material, ".glb", NULL, 2e-4);
    rt4("lines", fx_lines, ".glb", NULL, 2e-4);
    rt4("strip_fan", fx_strip_fan, ".glb", NULL, 2e-4);
    rt4("quad_textured", make_quad, ".glb", NULL, 2e-4);
    rt4("cube_flat_gltf", fx_cube_flat, ".gltf", NULL, 2e-4);
    rt4("cube_flat_lossless", fx_cube_flat, ".glb", &lossless, 1e-7);
    rt4("tri_vcolors_lossless", fx_tri_vcolors, ".glb", &lossless, 1e-7);
    rt4_soup();
    rt4_large();
    rt4_degenerate();
    texture_transform();

    rt5("cube_flat", fx_cube_flat);
    rt5("two_instances", fx_two_instances);
    rt5("hierarchy3", fx_hierarchy3);
    rt5("quad_textured", make_quad);
    return PGR_TEST_RESULT;
}
