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

/* Merge vertices that are equal in every attribute.

   PRC gives positions, normals, texture coordinates and colours each their
   own index per triangle corner; glTF has one index per vertex for all of
   them. Turning one into the other means giving each distinct combination
   one vertex. Writing one vertex per corner instead would break smooth
   shading in some viewers and triple the file.

   A vertex is a fixed number of floats, compared exactly, except that -0.0
   equals 0.0 and every NaN equals every other NaN. */

#ifndef PGR_HASH_H
#define PGR_HASH_H

#include <stdint.h>

typedef struct pgr_vertex_map pgr_vertex_map;

/* A map of vertices of stride floats each; expected is a hint. NULL when
   memory runs out or stride is 0. */
pgr_vertex_map *pgr_vertex_map_new(uint32_t stride, uint32_t expected);
void pgr_vertex_map_free(pgr_vertex_map *map);

/* Set *index to the index of the vertex equal to v, adding v when it is
   new. Returns 1 when added, 0 when found, or PGR_ERR_NOMEM. */
int pgr_vertex_map_insert(pgr_vertex_map *map, const float *v, uint32_t *index);

uint32_t pgr_vertex_map_count(const pgr_vertex_map *map);

/* The distinct vertices in the order first added, count * stride floats,
   with -0.0 stored as 0.0 and every NaN as one NaN. */
const float *pgr_vertex_map_data(const pgr_vertex_map *map);

#endif
