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

/* PNG encoding, with stb_image_write, the one translation unit that
   compiles it.

   stb_image_write v1.16 computes image sizes in int without checking them
   (CVE-2026-102805), and a texture's width, height and channels come from
   the PRC file. An image is encoded only when they are sensible and its
   pixels take at most PGR_MAX_IMAGE_BYTES (2^28), far inside int. */

#include "pgr_image.h"
#include "pgr_message.h"

#include <stdlib.h>
#include <string.h>

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

typedef struct
{
    uint8_t *data;
    size_t size, cap;
    int failed;
} png_out;

static void
append(void *context, void *bytes, int n)
{
    png_out *o = (png_out *)context;

    if (o->failed || n <= 0)
        return;
    if (o->size + (size_t)n > o->cap)
    {
        size_t cap = o->cap ? o->cap : 4096;
        uint8_t *bigger;

        while (cap < o->size + (size_t)n)
            cap *= 2;
        bigger = (uint8_t *)realloc(o->data, cap);
        if (bigger == NULL)
        {
            o->failed = 1;
            return;
        }
        o->data = bigger;
        o->cap = cap;
    }
    memcpy(o->data + o->size, bytes, (size_t)n);
    o->size += (size_t)n;
}

int
pgr_image_encode_png(const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t channels,
                     uint8_t **png, size_t *size)
{
    png_out o;

    *png = NULL;
    *size = 0;
    if (pixels == NULL || width == 0 || height == 0 || channels < 1 || channels > 4
        || (uint64_t)width * height > PGR_MAX_IMAGE_PIXELS
        || (uint64_t)width * height * channels > PGR_MAX_IMAGE_BYTES)
        return PGR_ERR_ARG;
    memset(&o, 0, sizeof(o));
    if (!stbi_write_png_to_func(append, &o, (int)width, (int)height, (int)channels, pixels,
                                (int)(width * channels)) || o.failed)
    {
        free(o.data);
        return PGR_ERR_NOMEM;
    }
    *png = o.data;
    *size = o.size;
    return PGR_OK;
}
