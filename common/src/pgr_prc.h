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

/* The neutral scene to and from PRC, through nanoPRC's public API only. */

#ifndef PGR_PRC_H
#define PGR_PRC_H

#include "pgr_message.h"
#include "pgr_scene.h"

#include <stddef.h>
#include <stdint.h>

/* PRC units per metre for each --units choice. PRC from CAD is usually in
   millimetres, which nanoPRC's tolerances also assume. */
#define PGR_UNITS_PER_METRE_MM 1000.0
#define PGR_UNITS_PER_METRE_CM 100.0
#define PGR_UNITS_PER_METRE_M  1.0
#define PGR_UNITS_PER_METRE_IN (1.0 / 0.0254)

/* The compressed tessellation's quantization tolerance, as a fraction of
   the bounding-box diagonal, when none is given: nanoPRC's own default and
   the sibling tools'. */
#define PGR_DEFAULT_QUANT_TOLERANCE 1e-6

typedef struct pgr_prc_write_options
{
    pgr_messages *messages;    /* NULL: messages go to stderr */
    int triangles;             /* 1: plain triangles instead of compressed */
    int no_normals;            /* 1: leave the scene's normals out; readers compute them */
    double quant_tolerance;    /* compressed only; <= 0 selects PGR_DEFAULT_QUANT_TOLERANCE */
    int no_axis_convert;       /* 1: keep a +Y up scene's axes as they are */
    double units_per_metre;    /* PRC units per metre; <= 0 selects millimetres */
} pgr_prc_write_options;

typedef struct pgr_prc_write_stats
{
    uint32_t nodes;            /* PRC nodes, the root wrapper included */
    uint32_t tessellations;
    uint32_t compressed;       /* of which compressed */
    uint32_t wires;            /* of which wire (lines) */
    uint32_t triangles;
    uint32_t segments;
    size_t prc_bytes;
} pgr_prc_write_stats;

/* Write scene as a .prc file at prc_path and, when pdf_path is not NULL, a
   one-page 3D PDF with a default view at pdf_path.

   Every primitive becomes one tessellation, written once however many
   nodes use its mesh; a node's mesh becomes its part, one representation
   item per primitive. The scene's roots are placed under one root node
   that carries the conversion to PRC: for a +Y up scene (glTF), a
   quarter-turn about +X so that +Y becomes +Z, unless no_axis_convert; and
   the scale from the scene's units to the PRC units. The coordinates
   themselves are written unchanged.

   options and stats may be NULL. Returns PGR_OK or a PGR_ERR_* status. */
int pgr_prc_write(const pgr_scene *scene, const char *prc_path, const char *pdf_path,
                  const pgr_prc_write_options *options, pgr_prc_write_stats *stats);

/* The root node's matrix pgr_prc_write uses for scene under options:
   the axis turn (if any) and the unit scale. */
void pgr_prc_root_matrix(const pgr_scene *scene, const pgr_prc_write_options *options,
                         double out[16]);

#endif
