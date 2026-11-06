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

/* Compare two scenes by what they draw, in world space: every triangle of
   every mesh, placed by its node's world matrix, for every node that draws
   it. Order and the numbering of vertices do not matter; a triangle
   matches another whose corners lie within a tolerance of its own, in the
   same cyclic order (so the winding must agree). */

#ifndef SCENE_COMPARE_H
#define SCENE_COMPARE_H

#include "pgr_scene.h"

typedef struct
{
    uint32_t triangles_a, triangles_b;
    uint32_t segments_a, segments_b;
    uint32_t unmatched;        /* triangles of a with no match in b */
    double max_error;          /* largest corner distance among matches */
    double diagonal;           /* of a's world bounding box */
} scene_diff;

/* Compare a and b, matching within tol_fraction of a's bounding-box
   diagonal. Returns 0 when every triangle of a matches one of b and the
   triangle and segment counts are equal; -1 otherwise, or when memory
   runs out. */
int scene_compare(const pgr_scene *a, const pgr_scene *b, double tol_fraction, scene_diff *d);

/* 1 when a and b have the same number of nodes and the same names (as
   multisets; unnamed nodes count as ""). */
int scene_same_names(const pgr_scene *a, const pgr_scene *b);

/* The world bounding box of every triangle the scene draws; 0 when it
   draws none (or memory runs out), else 1. */
int scene_bounds(const pgr_scene *s, double min[3], double max[3]);

#endif
