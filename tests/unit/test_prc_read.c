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

/* pgr_prc_read on structures pgr_prc_write never writes, made here with
   nanoPRC's write API directly: a plain tessellation of two faces, each
   with its own vertices, the second also carrying a triangle strip. The
   triangles read must be exactly those drawn, winding included: strip
   triangle m is (m-2, m-1, m) for even m and (m-1, m-2, m) for odd m, as
   nanoPRC's own exporters take it. */

#include "pgr_prc.h"
#include "pgr_test.h"
#include "scene_compare.h"

#include "prc_api.h"

#include <stdio.h>
#include <string.h>

/* Eight points, none repeated: a triangle on face 0 (points 0-2), and on
   face 1 a triangle (3-5) and a strip through points 3-7. */
static const double P[24] = {
    0, 0, 0,   1, 0, 0,   0, 1, 0,
    0, 0, 2,   1, 0, 2,   0, 1, 2,   1, 1, 2,   0, 2, 2
};

static int
write_fixture(const char *path)
{
    static const uint32_t tri[6] = { 0, 1, 2, 3, 4, 5 };
    static const uint32_t counts[2] = { 1, 1 };
    static const uint32_t strip_v[5] = { 3, 4, 5, 6, 7 };
    prc_api_write_tri_group strip;
    prc_api_write_face_groups groups[2];
    prc_api_write_tessellation t;
    prc_api_write_rep_item ri;
    prc_api_write_node node;
    prc_context *ctx = prc_api_new_context(NULL);
    int rc;

    if (ctx == NULL)
        return -1;
    memset(&strip, 0, sizeof(strip));
    strip.vertex_indices = strip_v;
    strip.num_vertices = 5;
    memset(groups, 0, sizeof(groups));
    groups[1].strips = &strip;
    groups[1].num_strips = 1;
    memset(&t, 0, sizeof(t));
    t.kind = PRC_API_WRITE_TESS_KIND_TRIANGLES;
    t.positions = P;
    t.num_positions = 8;
    t.tri_indices = tri;
    t.num_triangles = 2;
    t.face_tri_counts = counts;
    t.num_faces = 2;
    t.face_groups = groups;
    memset(&ri, 0, sizeof(ri));
    ri.kind = PRC_API_WRITE_RI_SURFACE;
    ri.biased_tessellation_index = 1;
    memset(&node, 0, sizeof(node));
    node.rep_items = &ri;
    node.num_rep_items = 1;
    node.name = "faces";
    rc = prc_api_write_prc_file(ctx, path, "faces", &node, &t, 1);
    if (rc != 0)
        prc_api_print_error_stack(ctx);
    prc_api_release_context(ctx);
    return rc;
}

/* The scene that file draws: one node, one primitive. */
static void
expected(pgr_scene *s)
{
    static const uint32_t idx[15] = {
        0, 1, 2,                 /* face 0 */
        3, 4, 5,                 /* face 1, triangle */
        3, 4, 5, 5, 4, 6, 5, 6, 7 /* face 1, strip */
    };
    pgr_mesh *m;
    pgr_primitive *p;
    pgr_node *n;
    int i;

    pgr_scene_init(s);
    pgr_scene_add_mesh(s, NULL, &m);
    pgr_mesh_add_primitive(m, PGR_TRIANGLES, &p);
    pgr_primitive_alloc(p, 8, 15, 0, 0, 0);
    for (i = 0; i < 24; i++)
        p->positions[i] = (float)P[i];
    memcpy(p->indices, idx, sizeof(idx));
    pgr_scene_add_node(s, "faces", &n);
    n->mesh = 0;
    pgr_scene_add_root(s, 0);
}

int
main(void)
{
    pgr_scene want, got;
    pgr_prc_read_options o;
    pgr_prc_read_stats rs;
    scene_diff d;
    int ok;

    PGR_CHECK(write_fixture("faces.prc") == 0);
    expected(&want);
    pgr_scene_init(&got);
    memset(&o, 0, sizeof(o));
    /* Read as stored: metres per unit and axes kept, so the points are
       the ones written. */
    o.no_axis_convert = 1;
    o.units_per_metre = PGR_UNITS_PER_METRE_M;
    PGR_CHECK(pgr_prc_read("faces.prc", &o, &got, &rs) == PGR_OK);
    PGR_CHECK(rs.triangles == 5);
    ok = scene_compare(&want, &got, 1e-9, &d) == 0;
    if (!ok)
        fprintf(stderr, "triangles %u -> %u, unmatched %u\n", d.triangles_a, d.triangles_b, d.unmatched);
    PGR_CHECK(ok);
    pgr_scene_free(&want);
    pgr_scene_free(&got);
    return PGR_TEST_RESULT;
}
