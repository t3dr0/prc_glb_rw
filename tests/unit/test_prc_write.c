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

/* pgr_prc_write, checked by reading what it writes back through nanoPRC's
   own reader: the number of tessellations stored (one per primitive, shared
   by instances) and drawn (one per placement: nanoPRC's read API numbers a
   stored tessellation once for each part that draws it), their kinds, the triangles they draw and the box of their
   vertices; the nodes' names and matrices, and the root's turn from +Y up
   to +Z up and from metres to millimetres; for compressed and plain
   tessellations, as .prc and inside a .pdf. */

#include "fixtures.h"
#include "pgr_glb.h"
#include "pgr_math.h"
#include "pgr_prc.h"
#include "pgr_test.h"

#include "prc_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_NAMES 32

/* What the reader gives back. */
#define MAX_STORED 64

typedef struct
{
    uint32_t tessellations, surfaces, compressed, wires;   /* as drawn */
    uint32_t stored;               /* distinct biased_tess_index in the tree: the file's numbering */
    uint32_t stored_index[MAX_STORED];
    uint32_t triangles;
    double vmin[3], vmax[3];       /* every surface vertex, in its own coordinates */
    int has_box;
    uint32_t num_names;
    char names[MAX_NAMES][64];     /* named products, depth first */
    double matrices[MAX_NAMES][16];
    int located[MAX_NAMES];
} readback;

/* Record each tessellation a part and its nested items store, by the
   file's own numbering. */
static void
collect_stored(const prc_api_part *part, readback *rb)
{
    size_t i;

    if (part->biased_tess_index > 0)
    {
        uint32_t q;

        for (q = 0; q < rb->stored && rb->stored_index[q] != part->biased_tess_index; q++)
            ;
        if (q == rb->stored && rb->stored < MAX_STORED)
            rb->stored_index[rb->stored++] = part->biased_tess_index;
    }
    for (i = 0; i < part->num_rep_items; i++)
        collect_stored(&part->rep_items[i], rb);
}

static void
collect_products(const prc_api_product *p, readback *rb)
{
    size_t c;

    if (p->part != NULL)
        collect_stored(p->part, rb);

    if (p->name != NULL && rb->num_names < MAX_NAMES)
    {
        snprintf(rb->names[rb->num_names], sizeof(rb->names[0]), "%s", p->name);
        rb->located[rb->num_names] = p->location_set;
        if (p->location_set)
            memcpy(rb->matrices[rb->num_names], p->location.matrix, sizeof(rb->matrices[0]));
        else
            pgr_mat4_identity(rb->matrices[rb->num_names]);
        rb->num_names++;
    }
    for (c = 0; c < p->num_children; c++)
        collect_products(&p->children[c], rb);
}

/* The index of the product called name, or -1. */
static int
find(const readback *rb, const char *name)
{
    uint32_t i;

    for (i = 0; i < rb->num_names; i++)
        if (strcmp(rb->names[i], name) == 0)
            return (int)i;
    return -1;
}

static void
add_vertices(const prc_api_tess_vertex_buffer *b, readback *rb)
{
    size_t i;
    int k;

    for (i = 0; i < b->num_vertices; i++)
        for (k = 0; k < 3; k++)
        {
            double v = b->vertices[i].position[k];

            if (!rb->has_box || v < rb->vmin[k])
                rb->vmin[k] = v;
            if (!rb->has_box || v > rb->vmax[k])
                rb->vmax[k] = v;
            if (k == 2)
                rb->has_box = 1;
        }
}

/* Triangles a face's primitives draw. */
static uint32_t
face_triangles(prc_context *ctx, prc_api_data data, prc_api_tess *t, size_t face)
{
    uint32_t n = 0;
    size_t j;

    for (j = 0; j < t->tess_faces[face].num_graphic_primitives; j++)
    {
        prc_api_graphic_primitive gp;

        if (prc_api_get_graphics_primitive(ctx, data, t, face, j, &gp) < 0)
            continue;
        if (gp.type == PRC_API_TRIANGLES)
            n += (uint32_t)(gp.num_indices / 3);
        else if ((gp.type == PRC_API_FAN || gp.type == PRC_API_STRIP) && gp.num_indices >= 3)
            n += (uint32_t)(gp.num_indices - 2);
    }
    return n;
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
    collect_products(tree, rb);
    tess = (prc_api_tess *)calloc(nt ? nt : 1, sizeof(prc_api_tess));
    for (k = 0; tess != NULL && k < nt; k++)
    {
        prc_api_tess *t = &tess[k];

        t->num_faces = prc_api_get_number_faces(ctx, data, k);
        t->tess_faces = (prc_api_face *)calloc(t->num_faces ? t->num_faces : 1, sizeof(prc_api_face));
        if (prc_api_initialize_tessellation(ctx, data, tree, k, t, NULL, &has_lines) < 0)
            continue;
        rb->tessellations++;
        if (t->type == PRC_API_TESS_3D_Wire)
            rb->wires++;
        else if (t->type == PRC_API_TESS_3D_Compressed)
        {
            rb->surfaces++;
            rb->compressed++;
            /* Every face of a compressed tessellation gives the whole mesh:
               face 0 only. */
            if (t->num_faces > 0 && prc_api_get_tessellation_vertices(ctx, data, k, 0, &t->tess_faces[0], t) >= 0)
            {
                add_vertices(&t->tess_vertices, rb);
                rb->triangles += face_triangles(ctx, data, t, 0);
            }
        }
        else if (t->type == PRC_API_TESS_3D)
        {
            rb->surfaces++;
            for (j = 0; j < t->num_faces; j++)
                if (prc_api_get_tessellation_vertices(ctx, data, k, j, &t->tess_faces[j], t) >= 0)
                {
                    add_vertices(&t->tess_faces[j].face_vertices, rb);
                    rb->triangles += face_triangles(ctx, data, t, j);
                }
        }
    }
    prc_api_release_data(ctx, data, tess, nt, NULL, 0, NULL, 0, tree);
    return prc_api_release_context(ctx) == 0 ? 0 : -1;
}

/* Read a fixture, write it with the given options, read the PRC (and PDF)
   back. */
static int
convert(const char *name, int (*make)(const char *), const pgr_prc_write_options *o,
        readback *from_prc, readback *from_pdf, pgr_prc_write_stats *st)
{
    char glb[256], prc[256], pdf[256];
    pgr_scene s;
    int r;

    memset(from_prc, 0, sizeof(*from_prc));
    memset(from_pdf, 0, sizeof(*from_pdf));
    memset(st, 0, sizeof(*st));
    snprintf(glb, sizeof(glb), "pw_%s.glb", name);
    snprintf(prc, sizeof(prc), "pw_%s%s.prc", name, o->triangles ? "_tri" : "");
    snprintf(pdf, sizeof(pdf), "pw_%s%s.pdf", name, o->triangles ? "_tri" : "");
    if (make(glb) != 0)
        return -1;
    pgr_scene_init(&s);
    r = pgr_glb_read(glb, NULL, &s);
    if (r == PGR_OK)
        r = pgr_prc_write(&s, prc, pdf, o, st);
    pgr_scene_free(&s);
    if (r != PGR_OK)
        return r;
    if (read_back(prc, from_prc) != 0 || read_back(pdf, from_pdf) != 0)
        return -1;
    return 0;
}

static void
test_cube(int triangles)
{
    pgr_prc_write_options o;
    pgr_prc_write_stats st;
    readback a, b;
    double expect[16];
    int root, cube;

    memset(&o, 0, sizeof(o));
    o.triangles = triangles;
    PGR_CHECK(convert("cube_flat", fx_cube_flat, &o, &a, &b, &st) == 0);
    PGR_CHECK(st.tessellations == 1 && st.triangles == 12 && st.nodes == 2);
    PGR_CHECK(st.compressed == (triangles ? 0u : 1u));
    PGR_CHECK(a.tessellations == 1 && a.stored == 1 && a.surfaces == 1 && a.triangles == 12);
    PGR_CHECK(a.compressed == (triangles ? 0u : 1u));
    /* The vertices, in the part's own coordinates: the cube's +-0.5,
       within the quantisation tolerance when compressed. */
    PGR_CHECK(a.has_box);
    PGR_CHECK(fabs(a.vmin[0] + 0.5) < 1e-5 && fabs(a.vmax[2] - 0.5) < 1e-5);

    /* The root is named after the model and turns +Y up to +Z up, metres
       to millimetres; the cube's own node is placed by identity. */
    root = find(&a, "cube_flat");
    cube = find(&a, "cube");
    PGR_CHECK(root >= 0 && cube >= 0);
    if (root >= 0)
    {
        pgr_mat4_y_up_to_z_up(1000.0, expect);
        PGR_CHECK(a.located[root] && pgr_mat4_equal(a.matrices[root], expect, 1e-9));
    }
    if (cube >= 0)
        PGR_CHECK(!a.located[cube] || pgr_mat4_is_identity(a.matrices[cube], 1e-12));

    /* The PDF holds the same PRC. */
    PGR_CHECK(b.tessellations == a.tessellations && b.triangles == a.triangles);
}

static void
test_instances_and_hierarchy(void)
{
    pgr_prc_write_options o;
    pgr_prc_write_stats st;
    readback a, b;
    double expect[16];
    const double t[3] = { 1, 2, 3 }, q0[4] = { 0, 0, 0, 1 }, s1[3] = { 1, 1, 1 }, z3[3] = { 0, 0, 0 };
    const double qz[4] = { 0, 0, 0.70710678, 0.70710678 }, s2[3] = { 2, 2, 2 };
    int i;

    memset(&o, 0, sizeof(o));
    /* One mesh, two nodes: one tessellation, drawn twice. */
    PGR_CHECK(convert("two_instances", fx_two_instances, &o, &a, &b, &st) == 0);
    PGR_CHECK(st.tessellations == 1 && a.stored == 1);
    PGR_CHECK(a.tessellations == 2 && a.triangles == 2);
    PGR_CHECK(find(&a, "first") >= 0 && find(&a, "second") >= 0);
    i = find(&a, "second");
    if (i >= 0)
        PGR_CHECK(a.located[i] && a.matrices[i][12] == 2.0);

    PGR_CHECK(convert("hierarchy3", fx_hierarchy3, &o, &a, &b, &st) == 0);
    PGR_CHECK(st.nodes == 5 && a.tessellations == 1);
    i = find(&a, "root");
    PGR_CHECK(i >= 0);
    if (i >= 0)
    {
        pgr_mat4_from_trs(t, q0, s1, expect);
        PGR_CHECK(pgr_mat4_equal(a.matrices[i], expect, 1e-6));
    }
    i = find(&a, "middle");
    PGR_CHECK(i >= 0);
    if (i >= 0)
    {
        pgr_mat4_from_trs(z3, qz, s1, expect);
        PGR_CHECK(pgr_mat4_equal(a.matrices[i], expect, 1e-6));
    }
    i = find(&a, "leaf");
    PGR_CHECK(i >= 0);
    if (i >= 0)
    {
        pgr_mat4_from_trs(z3, q0, s2, expect);
        PGR_CHECK(pgr_mat4_equal(a.matrices[i], expect, 1e-6));
    }
    i = find(&a, "matrix_node");
    PGR_CHECK(i >= 0 && a.matrices[i][12] == 4.0 && a.matrices[i][14] == 6.0);
}

static void
test_primitives(void)
{
    pgr_prc_write_options o;
    pgr_prc_write_stats st;
    readback a, b;

    memset(&o, 0, sizeof(o));
    /* Two primitives, two tessellations. */
    PGR_CHECK(convert("multi_material", fx_multi_material, &o, &a, &b, &st) == 0);
    PGR_CHECK(st.tessellations == 2 && a.tessellations == 2 && a.triangles == 2);

    /* Strips and fans arrive as triangles. */
    PGR_CHECK(convert("strip_fan", fx_strip_fan, &o, &a, &b, &st) == 0);
    PGR_CHECK(st.triangles == 6 && a.triangles == 6);

    /* Lines become wire tessellations. */
    PGR_CHECK(convert("lines", fx_lines, &o, &a, &b, &st) == 0);
    PGR_CHECK(st.wires == 3 && st.segments == 9);
    PGR_CHECK(a.tessellations == 3 && a.wires == 3 && a.surfaces == 0);
}

static void
test_options(void)
{
    pgr_prc_write_options o;
    pgr_prc_write_stats st;
    readback a, b;
    double expect[16];
    int root;

    /* Inches, axes left alone: the root only scales. */
    memset(&o, 0, sizeof(o));
    o.units_per_metre = PGR_UNITS_PER_METRE_IN;
    o.no_axis_convert = 1;
    PGR_CHECK(convert("cube_flat_in", fx_cube_flat, &o, &a, &b, &st) == 0);
    root = find(&a, "cube_flat");
    PGR_CHECK(root >= 0);
    if (root >= 0)
    {
        pgr_mat4_identity(expect);
        expect[0] = expect[5] = expect[10] = 1.0 / 0.0254;
        PGR_CHECK(pgr_mat4_equal(a.matrices[root], expect, 1e-9));
    }

    /* Metres with axes left alone: no transform on the root at all. */
    o.units_per_metre = PGR_UNITS_PER_METRE_M;
    PGR_CHECK(convert("cube_flat_m", fx_cube_flat, &o, &a, &b, &st) == 0);
    root = find(&a, "cube_flat");
    PGR_CHECK(root >= 0 && (!a.located[root] || pgr_mat4_is_identity(a.matrices[root], 1e-12)));

    /* Without normals, the reader still draws every triangle. */
    memset(&o, 0, sizeof(o));
    o.no_normals = 1;
    PGR_CHECK(convert("cube_flat_nn", fx_cube_flat, &o, &a, &b, &st) == 0);
    PGR_CHECK(a.triangles == 12);
    o.triangles = 1;
    PGR_CHECK(convert("cube_flat_nn", fx_cube_flat, &o, &a, &b, &st) == 0);
    PGR_CHECK(a.triangles == 12 && a.compressed == 0);
}

int
main(void)
{
    test_cube(0);
    test_cube(1);
    test_instances_and_hierarchy();
    test_primitives();
    test_options();
    return PGR_TEST_RESULT;
}
