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

/* Version information and exit codes shared by both tools. */

#ifndef PGR_INFO_H
#define PGR_INFO_H

#include <stdio.h>

/* Exit codes of both tools. */
#define PGR_EXIT_OK          0
#define PGR_EXIT_USAGE       1  /* bad command line */
#define PGR_EXIT_READ        2  /* the input could not be read or parsed */
#define PGR_EXIT_UNSUPPORTED 3  /* the input needs something this build does not do */
#define PGR_EXIT_WRITE       4  /* the output could not be written */

/* prc_glb_rw's own version, e.g. "0.1.0". */
const char *pgr_version(void);

/* The version cgltf states in the header it was built from, e.g. "1.15". */
const char *pgr_cgltf_version(void);

/* 1 when built with Draco (KHR_draco_mesh_compression), else 0. */
int pgr_draco_available(void);

/* Print the tool's version and those of the libraries it was built with. */
void pgr_print_version(FILE *out, const char *tool);

/* 1 when path ends in ext (".glb", say), ignoring case, else 0. */
int pgr_has_extension(const char *path, const char *ext);

#endif
