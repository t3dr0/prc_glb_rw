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

/* Encoded images, checked without decoding them.

   Textures pass from glTF to PRC as the PNG or JPEG bytes the file holds;
   nothing here decodes them. What is checked is what the PRC writer and
   later readers rely on: that the bytes are a PNG or a JPEG, and that the
   header gives a size, neither side of it zero, within the bounds below.
   A reader that decodes the image allocates width x height pixels, so the
   pixel bound keeps a few bytes of header from asking it for gigabytes. */

#ifndef PGR_IMAGE_H
#define PGR_IMAGE_H

#include <stddef.h>
#include <stdint.h>

/* The largest encoded image accepted, in bytes. */
#define PGR_MAX_IMAGE_BYTES  ((size_t)1 << 28)
/* The most pixels an accepted image may declare. */
#define PGR_MAX_IMAGE_PIXELS ((uint64_t)1 << 28)

/* What bytes holds: PGR_IMAGE_PNG or PGR_IMAGE_JPEG in *mime, with the
   size from its header. Returns PGR_OK; PGR_ERR_UNSUPPORTED when it is
   neither format; PGR_ERR_PARSE when it is one of them but its header is
   cut short, gives no size, or exceeds the bounds above. */
int pgr_image_info(const uint8_t *bytes, size_t size, int *mime, uint32_t *width,
                   uint32_t *height);

/* Raw pixels, as nanoPRC's reader gives a texture, encoded as a PNG: width x
   height pixels of channels bytes each (1 grey, 2 grey and alpha, 3 RGB,
   4 RGBA), row by row from the top, tightly packed. The PNG is allocated
   with malloc into *png, its size in *size. Returns PGR_OK; PGR_ERR_ARG
   when a size is zero, channels is not 1 to 4, or the image exceeds the
   bounds above; PGR_ERR_NOMEM when memory runs out. */
int pgr_image_encode_png(const uint8_t *pixels, uint32_t width, uint32_t height, uint32_t channels,
                         uint8_t **png, size_t *size);

#endif
