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

/* The reader on Draco data from another encoder, in builds with Draco:
   the Khronos glTF sample Box, glTF-Draco variant (tests/data/
   khronos_box_draco, CC-BY 4.0, Cesium). Every other Draco test reads data
   this project encoded; this one shows the reader agrees with a file it did
   not write.

   The file is a unit cube, side 1 about the origin, as 24 points (4 per
   side, each with that side's normal) and 12 triangles, under a root node
   that turns Z up into Y up. Its Draco attribute ids are in the opposite
   order to its accessors (NORMAL 0, POSITION 1). Expected after reading:
   no warning; every coordinate +-0.5 and every normal a unit axis, within
   the encoder's quantisation; each triangle's winding facing out along its
   corners' normal; two triangles of area 0.5 on each of the six sides; the
   material's red base colour; and the two nodes. */

#include "pgr_glb.h"
#include "pgr_test.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* tests/CMakeLists.txt gives the sample's full path. */
#ifndef PGR_KHRONOS_BOX
#define PGR_KHRONOS_BOX "data/khronos_box_draco/Box.gltf"
#endif

static void
quiet(void *user, int is_error, const char *text)
{
    (void)user;
    fprintf(stderr, "%s: %s\n", is_error ? "error" : "warning", text);
}

/* The axis a unit vector lies along, as 0..5 for +x -x +y -y +z -z, or -1
   when it is not within tol of one. */
static int
axis_of(const float *v, double tol)
{
    int k;

    for (k = 0; k < 3; k++)
    {
        if (fabs(fabs(v[k]) - 1.0) <= tol && fabs(v[(k + 1) % 3]) <= tol && fabs(v[(k + 2) % 3]) <= tol)
            return 2 * k + (v[k] < 0 ? 1 : 0);
    }
    return -1;
}

int
main(void)
{
    pgr_messages counted = { NULL, NULL, 0, 0, 0 };
    pgr_glb_read_options o;
    pgr_scene s;
    const pgr_primitive *p;
    int per_side[6] = { 0, 0, 0, 0, 0, 0 };
    double area[6] = { 0, 0, 0, 0, 0, 0 };
    uint32_t i, t;
    int k;

    counted.fn = quiet;
    o.messages = &counted;
    pgr_scene_init(&s);
    PGR_CHECK(pgr_glb_read(PGR_KHRONOS_BOX, &o, &s) == PGR_OK);
    PGR_CHECK(counted.warnings == 0 && counted.errors == 0);

    PGR_CHECK(s.num_nodes == 2 && s.num_roots == 1);
    PGR_CHECK(s.num_materials == 1);
    if (s.num_materials == 1)
    {
        PGR_CHECK(fabs(s.materials[0].base_color[0] - 0.8) < 1e-6);
        PGR_CHECK(s.materials[0].base_color[1] == 0.0f && s.materials[0].base_color[2] == 0.0f);
    }
    if (s.num_meshes != 1 || s.meshes[0].num_primitives != 1)
    {
        PGR_CHECK(0);
        pgr_scene_free(&s);
        return PGR_TEST_RESULT;
    }
    p = &s.meshes[0].primitives[0];
    PGR_CHECK(p->mode == PGR_TRIANGLES);
    PGR_CHECK(p->num_vertices == 24 && p->num_indices == 36);
    PGR_CHECK(p->normals != NULL && p->uvs == NULL && p->colors == NULL);
    PGR_CHECK(p->material == 0);
    if (p->normals == NULL || p->num_indices % 3 != 0)
    {
        pgr_scene_free(&s);
        return PGR_TEST_RESULT;
    }

    /* Positions within 1e-3 of +-0.5 (the file's own bounds are 0.5005),
       normals within 1e-2 of an axis. */
    for (i = 0; i < p->num_vertices; i++)
    {
        for (k = 0; k < 3; k++)
            PGR_CHECK(fabs(fabs(p->positions[3 * i + k]) - 0.5) < 1e-3);
        PGR_CHECK(axis_of(&p->normals[3 * i], 1e-2) >= 0);
    }

    for (t = 0; t < p->num_indices / 3; t++)
    {
        const uint32_t *c = &p->indices[3 * t];
        double e1[3], e2[3], n[3], len, centre_dot = 0;
        int side = -1, same = 1;

        if (c[0] >= p->num_vertices || c[1] >= p->num_vertices || c[2] >= p->num_vertices)
        {
            PGR_CHECK(0);
            continue;
        }
        for (k = 0; k < 3; k++)
        {
            e1[k] = p->positions[3 * c[1] + k] - p->positions[3 * c[0] + k];
            e2[k] = p->positions[3 * c[2] + k] - p->positions[3 * c[0] + k];
        }
        n[0] = e1[1] * e2[2] - e1[2] * e2[1];
        n[1] = e1[2] * e2[0] - e1[0] * e2[2];
        n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        len = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        /* The three corners carry one side's normal. */
        for (k = 0; k < 3; k++)
        {
            int a = axis_of(&p->normals[3 * c[k]], 1e-2);

            if (k == 0)
                side = a;
            else if (a != side)
                same = 0;
        }
        PGR_CHECK(same && side >= 0);
        if (!same || side < 0 || len == 0)
            continue;
        /* The winding faces the way the corners' normal does, and outward. */
        for (k = 0; k < 3; k++)
        {
            PGR_CHECK(fabs(n[k] / len - p->normals[3 * c[0] + k]) < 1e-2);
            centre_dot += (p->positions[3 * c[0] + k] + p->positions[3 * c[1] + k] + p->positions[3 * c[2] + k])
                          * n[k];
        }
        PGR_CHECK(centre_dot > 0);
        per_side[side]++;
        area[side] += len / 2;
    }
    for (k = 0; k < 6; k++)
    {
        PGR_CHECK(per_side[k] == 2);
        PGR_CHECK(fabs(area[k] - 1.0) < 1e-2);
    }

    pgr_scene_free(&s);
    return PGR_TEST_RESULT;
}
