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

/* draco_shim.h without Draco: compiled when PRC_GLB_WITH_DRACO is OFF.
   Nothing is available, and the callers report that the build lacks Draco. */

#include "draco_shim.h"

#include <string.h>

int
ds_available(void)
{
    return 0;
}

const char *
ds_version(void)
{
    return NULL;
}

int
ds_decode(const void *buf, size_t size, const int *wanted_ids, uint32_t n_wanted, ds_mesh *out)
{
    (void)buf;
    (void)size;
    (void)wanted_ids;
    (void)n_wanted;
    if (out != NULL)
        memset(out, 0, sizeof(*out));
    return DS_ERR_UNAVAILABLE;
}

void
ds_mesh_free(ds_mesh *m)
{
    if (m != NULL)
        memset(m, 0, sizeof(*m));
}

void
ds_encode_options_default(ds_encode_options *o)
{
    if (o == NULL)
        return;
    o->qbits_position = 14;
    o->qbits_normal = 10;
    o->qbits_texcoord = 12;
    o->qbits_color = 8;
    o->qbits_generic = 12;
    o->encode_speed = 5;
    o->decode_speed = 5;
}

int
ds_encode(const ds_mesh *in, const ds_encode_options *o, void **out_buf, size_t *out_size, int *out_ids)
{
    (void)in;
    (void)o;
    (void)out_ids;
    if (out_buf != NULL)
        *out_buf = NULL;
    if (out_size != NULL)
        *out_size = 0;
    return DS_ERR_UNAVAILABLE;
}

void
ds_buffer_free(void *buf)
{
    (void)buf;
}
