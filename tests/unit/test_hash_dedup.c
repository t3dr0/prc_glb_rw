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

/* Merging equal vertices: a cube's 36 corners are 24 vertices with
   per-face normals and 8 without; -0.0 equals 0.0, NaN equals NaN; and the
   table keeps every vertex through many regrowths. */

#include "pgr_hash.h"
#include "pgr_test.h"

#include <stdlib.h>

/* The 36 corners of a unit cube's 12 triangles: position and face normal. */
static void
cube_corners(float out[36][6])
{
    static const int quad[6] = { 0, 1, 2, 0, 2, 3 };
    static const float cu[4] = { -1, 1, 1, -1 };
    static const float cw[4] = { -1, -1, 1, 1 };
    int face, c, k, i = 0;

    for (face = 0; face < 6; face++)
    {
        int a = face / 2;
        float s = face % 2 == 0 ? 1.0f : -1.0f;

        for (c = 0; c < 6; c++, i++)
        {
            float n[3] = { 0, 0, 0 }, u[3] = { 0, 0, 0 }, w[3] = { 0, 0, 0 };

            n[a] = s;
            u[(a + 1) % 3] = 1.0f;
            w[(a + 2) % 3] = 1.0f;
            for (k = 0; k < 3; k++)
            {
                out[i][k] = 0.5f * (n[k] + cu[quad[c]] * u[k] + cw[quad[c]] * w[k]);
                out[i][3 + k] = n[k];
            }
        }
    }
}

static void
test_cube(void)
{
    float corners[36][6];
    pgr_vertex_map *with_normals = pgr_vertex_map_new(6, 0);
    pgr_vertex_map *positions = pgr_vertex_map_new(3, 0);
    uint32_t idx[36], pidx[36];
    int i, added = 0;

    PGR_CHECK(with_normals != NULL && positions != NULL);
    if (with_normals == NULL || positions == NULL)
        return;
    cube_corners(corners);
    for (i = 0; i < 36; i++)
    {
        int r = pgr_vertex_map_insert(with_normals, corners[i], &idx[i]);

        PGR_CHECK(r == 0 || r == 1);
        added += r == 1;
        PGR_CHECK(pgr_vertex_map_insert(positions, corners[i], &pidx[i]) >= 0);
    }
    PGR_CHECK(pgr_vertex_map_count(with_normals) == 24);
    PGR_CHECK(added == 24);
    PGR_CHECK(pgr_vertex_map_count(positions) == 8);

    /* Each index leads back to its corner. */
    for (i = 0; i < 36; i++)
    {
        const float *v = pgr_vertex_map_data(with_normals) + 6 * idx[i];
        const float *p = pgr_vertex_map_data(positions) + 3 * pidx[i];

        PGR_CHECK(memcmp(v, corners[i], sizeof(corners[i])) == 0);
        PGR_CHECK(p[0] == corners[i][0] && p[1] == corners[i][1] && p[2] == corners[i][2]);
    }
    pgr_vertex_map_free(with_normals);
    pgr_vertex_map_free(positions);
}

static void
test_signed_zero_and_nan(void)
{
    pgr_vertex_map *m = pgr_vertex_map_new(2, 4);
    volatile float zero = 0.0f;
    float a[2], b[2];
    uint32_t ia, ib;

    PGR_CHECK(m != NULL);
    if (m == NULL)
        return;
    a[0] = 0.0f;
    a[1] = 1.0f;
    b[0] = -zero;
    b[1] = 1.0f;
    PGR_CHECK(pgr_vertex_map_insert(m, a, &ia) == 1);
    PGR_CHECK(pgr_vertex_map_insert(m, b, &ib) == 0);
    PGR_CHECK(ia == ib);

    a[0] = zero / zero;     /* a NaN */
    b[0] = -(zero / zero);  /* a NaN with the other sign */
    PGR_CHECK(a[0] != a[0] && b[0] != b[0]);
    PGR_CHECK(pgr_vertex_map_insert(m, a, &ia) == 1);
    PGR_CHECK(pgr_vertex_map_insert(m, b, &ib) == 0);
    PGR_CHECK(ia == ib);

    a[0] = 1.0f;
    a[1] = 2.0f;
    b[0] = 1.0f;
    b[1] = 2.0000002f;      /* the next float up: distinct */
    PGR_CHECK(pgr_vertex_map_insert(m, a, &ia) == 1);
    PGR_CHECK(pgr_vertex_map_insert(m, b, &ib) == 1);
    PGR_CHECK(ia != ib);
    PGR_CHECK(pgr_vertex_map_count(m) == 4);
    pgr_vertex_map_free(m);
}

static void
test_growth(void)
{
    const uint32_t n = 100000;
    pgr_vertex_map *m = pgr_vertex_map_new(3, 0);
    uint32_t i, index, wrong = 0;
    float v[3];

    PGR_CHECK(m != NULL);
    if (m == NULL)
        return;
    for (i = 0; i < n; i++)
    {
        v[0] = (float)i;
        v[1] = (float)(i % 7);
        v[2] = -(float)i;
        if (pgr_vertex_map_insert(m, v, &index) != 1 || index != i)
            wrong++;
    }
    for (i = 0; i < n; i++)
    {
        v[0] = (float)i;
        v[1] = (float)(i % 7);
        v[2] = -(float)i;
        if (pgr_vertex_map_insert(m, v, &index) != 0 || index != i)
            wrong++;
    }
    PGR_CHECK(wrong == 0);
    PGR_CHECK(pgr_vertex_map_count(m) == n);
    pgr_vertex_map_free(m);

    PGR_CHECK(pgr_vertex_map_new(0, 10) == NULL);
}

int
main(void)
{
    test_cube();
    test_signed_zero_and_nan();
    test_growth();
    return PGR_TEST_RESULT;
}
