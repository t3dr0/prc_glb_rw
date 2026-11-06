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

/* glTF 2.0 to and from the neutral scene, through cgltf. */

#ifndef PGR_GLB_H
#define PGR_GLB_H

#include "pgr_message.h"
#include "pgr_scene.h"

#include <stddef.h>

typedef struct pgr_glb_read_options
{
    pgr_messages *messages;  /* NULL: messages go to stderr */
} pgr_glb_read_options;

/* Read a .glb, or a .gltf with its buffers in data URIs or in files beside
   it, into scene, which must be initialised and empty. Returns PGR_OK or a
   PGR_ERR_* status; on failure scene holds whatever was read, and is still
   to be freed. options may be NULL. */
int pgr_glb_read(const char *path, const pgr_glb_read_options *options, pgr_scene *scene);

typedef struct pgr_glb_write_options
{
    pgr_messages *messages;  /* NULL: messages go to stderr */
    const char *generator;   /* asset.generator; NULL: "prc_glb_rw <version>" */
} pgr_glb_write_options;

/* Write scene as binary glTF when path ends in .glb, otherwise as JSON
   glTF with its buffer in a .bin file of the same stem beside it. Returns
   PGR_OK or a PGR_ERR_* status. options may be NULL. */
int pgr_glb_write(const pgr_scene *scene, const char *path, const pgr_glb_write_options *options);

/* Write a binary glTF container: the 12-byte header, the JSON chunk padded
   with spaces and, when bin_size > 0, the BIN chunk padded with zeros, each
   to a multiple of 4 bytes. Returns PGR_OK or PGR_ERR_WRITE. */
int pgr_glb_write_container(const char *path, const void *json, size_t json_size,
                            const void *bin, size_t bin_size);

#endif
