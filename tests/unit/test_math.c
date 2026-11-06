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

/* 4x4 column-major matrices: multiply, invert, TRS against cgltf's own,
   the normal matrix, and the axis and unit matrix and its inverse. */

#include "pgr_math.h"
#include "pgr_test.h"

#include "cgltf.h"

#include <math.h>

static int
near3(const double a[3], double x, double y, double z, double tol)
{
    return fabs(a[0] - x) <= tol && fabs(a[1] - y) <= tol && fabs(a[2] - z) <= tol;
}

static void
test_axis_matrix(void)
{
    double m[16], inv[16], prod[16], p[3];
    const double y[3] = { 0, 1, 0 }, z[3] = { 0, 0, 1 }, x[3] = { 1, 0, 0 };

    pgr_mat4_y_up_to_z_up(1000.0, m);
    pgr_mat4_transform_vector(m, y, p);
    PGR_CHECK(near3(p, 0, 0, 1000, 0));       /* glTF up is PRC up */
    pgr_mat4_transform_vector(m, z, p);
    PGR_CHECK(near3(p, 0, -1000, 0, 0));      /* glTF forward (+Z) is PRC -Y */
    pgr_mat4_transform_vector(m, x, p);
    PGR_CHECK(near3(p, 1000, 0, 0, 0));

    PGR_CHECK(pgr_mat4_invert(m, inv));
    pgr_mat4_mul(m, inv, prod);
    PGR_CHECK(pgr_mat4_is_identity(prod, 1e-12));
    pgr_mat4_mul(inv, m, prod);
    PGR_CHECK(pgr_mat4_is_identity(prod, 1e-12));
    /* A rotation, not a mirror: the 3x3 determinant is +scale^3. */
    PGR_CHECK(fabs(m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2])
                   + m[8] * (m[1] * m[6] - m[5] * m[2]) - 1e9) < 1e-3);
}

static void
test_multiply_and_invert(void)
{
    const double t[3] = { 3, -2, 7 }, q[4] = { 0.18257419, 0.36514837, 0.54772256, 0.73029674 };
    const double s[3] = { 2, 0.5, 4 };
    double m[16], inv[16], prod[16], id[16], a[16], b[16], ab[16], p[3], back[3];
    const double pt[3] = { 1.5, -4, 9 };
    double singular[16];
    int i;

    pgr_mat4_from_trs(t, q, s, m);
    PGR_CHECK(pgr_mat4_invert(m, inv));
    pgr_mat4_mul(m, inv, prod);
    PGR_CHECK(pgr_mat4_is_identity(prod, 1e-12));

    pgr_mat4_transform_point(m, pt, p);
    pgr_mat4_transform_point(inv, p, back);
    PGR_CHECK(near3(back, pt[0], pt[1], pt[2], 1e-12));

    /* Multiplying by the identity changes nothing, either side, in place too. */
    pgr_mat4_identity(id);
    pgr_mat4_mul(id, m, prod);
    PGR_CHECK(pgr_mat4_equal(prod, m, 0.0));
    for (i = 0; i < 16; i++)
        prod[i] = m[i];
    pgr_mat4_mul(prod, id, prod);
    PGR_CHECK(pgr_mat4_equal(prod, m, 0.0));

    /* (a * b) applies b first. */
    {
        const double ta[3] = { 10, 0, 0 }, tb[3] = { 0, 0, 0 }, q0[4] = { 0, 0, 0, 1 };
        const double s1[3] = { 1, 1, 1 }, s2[3] = { 2, 2, 2 }, one[3] = { 1, 0, 0 };

        pgr_mat4_from_trs(ta, q0, s1, a);   /* translate 10 in x */
        pgr_mat4_from_trs(tb, q0, s2, b);   /* scale 2 */
        pgr_mat4_mul(a, b, ab);
        pgr_mat4_transform_point(ab, one, p);
        PGR_CHECK(near3(p, 12, 0, 0, 0));
    }

    for (i = 0; i < 16; i++)
        singular[i] = 0.0;
    singular[15] = 1.0;
    PGR_CHECK(!pgr_mat4_invert(singular, inv));
}

static void
test_trs_matches_cgltf(void)
{
    cgltf_node node;
    cgltf_float local[16];
    double ours[16];
    const double t[3] = { 1, 2, 3 }, q[4] = { 0.5, -0.5, 0.5, 0.5 }, s[3] = { 2, 3, 0.25 };
    int i, close = 1;

    memset(&node, 0, sizeof(node));
    node.has_translation = node.has_rotation = node.has_scale = 1;
    for (i = 0; i < 3; i++)
    {
        node.translation[i] = (cgltf_float)t[i];
        node.scale[i] = (cgltf_float)s[i];
    }
    for (i = 0; i < 4; i++)
        node.rotation[i] = (cgltf_float)q[i];
    cgltf_node_transform_local(&node, local);
    pgr_mat4_from_trs(t, q, s, ours);
    for (i = 0; i < 16; i++)
        if (fabs(ours[i] - local[i]) > 1e-6)
            close = 0;
    PGR_CHECK(close);
}

static void
test_normal_matrix(void)
{
    const double t[3] = { 5, 5, 5 }, q0[4] = { 0, 0, 0, 1 }, s[3] = { 2, 3, 4 };
    const double qz[4] = { 0, 0, 0.70710678118654752, 0.70710678118654752 }, s1[3] = { 1, 1, 1 };
    double m[16], n[9];

    /* A non-uniform scale: normals scale by the reciprocals. */
    pgr_mat4_from_trs(t, q0, s, m);
    PGR_CHECK(pgr_mat4_normal_matrix(m, n));
    PGR_CHECK(fabs(n[0] - 0.5) < 1e-12 && fabs(n[4] - 1.0 / 3.0) < 1e-12 && fabs(n[8] - 0.25) < 1e-12);
    PGR_CHECK(fabs(n[1]) < 1e-12 && fabs(n[3]) < 1e-12 && fabs(n[6]) < 1e-12);

    /* A rotation is its own normal matrix. */
    pgr_mat4_from_trs(t, qz, s1, m);
    PGR_CHECK(pgr_mat4_normal_matrix(m, n));
    PGR_CHECK(fabs(n[0] - m[0]) < 1e-12 && fabs(n[1] - m[1]) < 1e-12 && fabs(n[3] - m[4]) < 1e-12
              && fabs(n[4] - m[5]) < 1e-12 && fabs(n[8] - m[10]) < 1e-12);
}

int
main(void)
{
    test_axis_matrix();
    test_multiply_and_invert();
    test_trs_matches_cgltf();
    test_normal_matrix();
    return PGR_TEST_RESULT;
}
