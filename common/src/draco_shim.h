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

/* Draco mesh compression behind a C interface.

   Google's Draco is C++. All of the C++ is in draco_shim.cpp, which is
   compiled only when the build option PRC_GLB_WITH_DRACO is ON; otherwise
   draco_stub.c is compiled instead, ds_available() returns 0 and every
   other call returns DS_ERR_UNAVAILABLE. This header is C99 and is the
   only thing the rest of the code sees.

   Memory crosses this interface only as malloc'd blocks freed by the
   functions below; no C++ exception leaves the shim. */

#ifndef DRACO_SHIM_H
#define DRACO_SHIM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Status codes. */
#define DS_OK               0
#define DS_ERR_UNAVAILABLE -1  /* built without Draco */
#define DS_ERR_DECODE      -2  /* not a Draco mesh, or a corrupt one */
#define DS_ERR_ENCODE      -3
#define DS_ERR_ARG         -4
#define DS_ERR_NOMEM       -5

/* ds_attribute.semantic: what an attribute holds, as Draco types it. */
#define DS_POSITION 0
#define DS_NORMAL   1
#define DS_COLOR    2
#define DS_TEXCOORD 3
#define DS_GENERIC  4

typedef struct ds_attribute
{
    int draco_id;            /* Draco's unique id: from the glTF extension's map when
                                decoding, assigned by the encoder when encoding */
    int semantic;            /* DS_* */
    int num_components;      /* 1..4 */
    float *data;             /* num_points * num_components floats */
} ds_attribute;

typedef struct ds_mesh
{
    uint32_t num_points;
    uint32_t num_faces;      /* triangles */
    uint32_t *indices;       /* 3 * num_faces, each < num_points */
    uint32_t num_attributes;
    ds_attribute *attributes;
} ds_mesh;

/* 1 when built with Draco, else 0. */
int ds_available(void);

/* The Draco version built in, e.g. "1.5.9", or NULL. */
const char *ds_version(void);

/* Decode the Draco mesh in buf. out gets one attribute per entry of
   wanted_ids, in that order, with that id; an id the mesh does not have
   gives an attribute with data NULL and num_components 0. Points that
   Draco reordered are kept in its order: use out's indices and attributes
   together, never with the glTF's own. On failure out is left empty.
   Returns DS_OK or a DS_ERR_* status. */
int ds_decode(const void *buf, size_t size, const int *wanted_ids, uint32_t n_wanted,
              ds_mesh *out);

/* Free what ds_decode put into m, and empty it. */
void ds_mesh_free(ds_mesh *m);

typedef struct ds_encode_options
{
    /* Quantisation bits per kind of attribute, 1..30; 0 keeps it lossless
       (not quantised). */
    int qbits_position, qbits_normal, qbits_texcoord, qbits_color, qbits_generic;
    int encode_speed, decode_speed;   /* 0 (best compression) .. 10 (fastest) */
} ds_encode_options;

/* Defaults: position 14 bits, normal 10, texture coordinate 12, colour 8,
   generic 12; speeds 5 and 5. */
void ds_encode_options_default(ds_encode_options *o);

/* Encode mesh in (its attributes' draco_id is ignored) into a new block
   *out_buf of *out_size bytes, to be freed with ds_buffer_free. out_ids,
   when not NULL, gets each attribute's Draco unique id, in the order of
   in->attributes. o may be NULL for the defaults. Returns DS_OK or a
   DS_ERR_* status. */
int ds_encode(const ds_mesh *in, const ds_encode_options *o, void **out_buf, size_t *out_size,
              int *out_ids);

void ds_buffer_free(void *buf);

#ifdef __cplusplus
}
#endif

#endif
