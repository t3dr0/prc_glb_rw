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

#include "pgr_image.h"
#include "pgr_message.h"
#include "pgr_scene.h"

#include <string.h>

static uint32_t
be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t
be16(const uint8_t *p)
{
    return ((uint32_t)p[0] << 8) | p[1];
}

/* A PNG starts with its 8-byte signature and then the IHDR chunk: length,
   "IHDR", width and height as big-endian 32-bit numbers. */
static int
png_size(const uint8_t *b, size_t n, uint32_t *w, uint32_t *h)
{
    if (n < 24 || memcmp(b + 12, "IHDR", 4) != 0)
        return PGR_ERR_PARSE;
    *w = be32(b + 16);
    *h = be32(b + 20);
    return PGR_OK;
}

/* A JPEG is a run of marker segments after its start-of-image marker
   (FF D8). The size is in the first start-of-frame segment, markers C0 to
   CF other than C4 (Huffman tables), C8 (reserved) and CC (arithmetic
   coding): precision, then height and width as big-endian 16-bit numbers.
   Markers without a length (D0-D7, D8, 01) are stepped over; start of scan
   (DA) comes after the frame, so meeting it first means there is none. */
static int
jpeg_size(const uint8_t *b, size_t n, uint32_t *w, uint32_t *h)
{
    size_t pos = 2;

    while (pos + 1 < n)
    {
        uint8_t marker;
        uint32_t len;

        if (b[pos] != 0xFF)
            return PGR_ERR_PARSE;
        while (pos < n && b[pos] == 0xFF)
            pos++;
        if (pos >= n)
            break;
        marker = b[pos++];
        if (marker == 0x01 || marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7))
            continue;
        if (marker == 0xD9 || marker == 0xDA || pos + 2 > n)
            break;
        len = be16(b + pos);
        if (len < 2 || len > n - pos)
            break;
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC)
        {
            if (len < 7)
                break;
            *h = be16(b + pos + 3);
            *w = be16(b + pos + 5);
            return PGR_OK;
        }
        pos += len;
    }
    return PGR_ERR_PARSE;
}

int
pgr_image_info(const uint8_t *bytes, size_t size, int *mime, uint32_t *width, uint32_t *height)
{
    static const uint8_t png_sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    uint32_t w = 0, h = 0;
    int st;

    if (bytes == NULL)
        return PGR_ERR_UNSUPPORTED;
    if (size >= 8 && memcmp(bytes, png_sig, 8) == 0)
    {
        *mime = PGR_IMAGE_PNG;
        st = png_size(bytes, size, &w, &h);
    }
    else if (size >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF)
    {
        *mime = PGR_IMAGE_JPEG;
        st = jpeg_size(bytes, size, &w, &h);
    }
    else
        return PGR_ERR_UNSUPPORTED;
    if (st != PGR_OK || w == 0 || h == 0 || size > PGR_MAX_IMAGE_BYTES
        || (uint64_t)w * h > PGR_MAX_IMAGE_PIXELS)
        return PGR_ERR_PARSE;
    *width = w;
    *height = h;
    return PGR_OK;
}
