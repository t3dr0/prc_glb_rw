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

/* pgr_image_info on PNG and JPEG headers, good, cut short, empty and too
   large, and the linear/sRGB conversions on values the sRGB definition
   fixes. */

#include "pgr_image.h"
#include "pgr_math.h"
#include "pgr_message.h"
#include "pgr_scene.h"
#include "pgr_test.h"

#include <math.h>
#include <string.h>

/* A PNG signature and IHDR chunk header for a w x h image. */
static size_t
png_header(unsigned char *b, uint32_t w, uint32_t h)
{
    static const unsigned char head[16] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n',
                                            0, 0, 0, 13, 'I', 'H', 'D', 'R' };
    int k;

    memcpy(b, head, 16);
    for (k = 0; k < 4; k++)
    {
        b[16 + k] = (unsigned char)(w >> (24 - 8 * k));
        b[20 + k] = (unsigned char)(h >> (24 - 8 * k));
    }
    memset(b + 24, 0, 9);
    return 33;
}

/* A JPEG start, an APP0 segment, then a baseline frame for w x h. */
static size_t
jpeg_header(unsigned char *b, uint32_t w, uint32_t h, unsigned char sof)
{
    static const unsigned char head[20] = { 0xFF, 0xD8, 0xFF, 0xE0, 0, 16, 'J', 'F', 'I', 'F', 0,
                                            1, 1, 0, 0, 1, 0, 1, 0, 0 };
    size_t n = 20;

    memcpy(b, head, n);
    b[n++] = 0xFF;
    b[n++] = sof;
    b[n++] = 0;
    b[n++] = 11;          /* length: itself, precision, size, one component */
    b[n++] = 8;
    b[n++] = (unsigned char)(h >> 8);
    b[n++] = (unsigned char)h;
    b[n++] = (unsigned char)(w >> 8);
    b[n++] = (unsigned char)w;
    b[n++] = 1;
    b[n++] = 1;
    b[n++] = 0x11;
    b[n++] = 0;
    return n;
}

static void
test_png(void)
{
    unsigned char b[64];
    size_t n;
    uint32_t w = 0, h = 0;
    int mime = 0;

    n = png_header(b, 640, 480);
    PGR_CHECK(pgr_image_info(b, n, &mime, &w, &h) == PGR_OK);
    PGR_CHECK(mime == PGR_IMAGE_PNG && w == 640 && h == 480);

    /* Cut inside the size, a zero side, and more pixels than allowed. */
    PGR_CHECK(pgr_image_info(b, 20, &mime, &w, &h) == PGR_ERR_PARSE);
    n = png_header(b, 0, 480);
    PGR_CHECK(pgr_image_info(b, n, &mime, &w, &h) == PGR_ERR_PARSE);
    n = png_header(b, 1u << 16, (1u << 12) + 1);
    PGR_CHECK(pgr_image_info(b, n, &mime, &w, &h) == PGR_ERR_PARSE);
    n = png_header(b, 1u << 16, 1u << 12);      /* exactly the limit */
    PGR_CHECK(pgr_image_info(b, n, &mime, &w, &h) == PGR_OK);
    n = png_header(b, 0xFFFFFFFFu, 0xFFFFFFFFu);
    PGR_CHECK(pgr_image_info(b, n, &mime, &w, &h) == PGR_ERR_PARSE);

    /* A signature, then not IHDR. */
    n = png_header(b, 2, 2);
    memcpy(b + 12, "IDAT", 4);
    PGR_CHECK(pgr_image_info(b, n, &mime, &w, &h) == PGR_ERR_PARSE);
}

static void
test_jpeg(void)
{
    unsigned char b[64];
    size_t n;
    uint32_t w = 0, h = 0;
    int mime = 0;

    n = jpeg_header(b, 300, 200, 0xC0);
    PGR_CHECK(pgr_image_info(b, n, &mime, &w, &h) == PGR_OK);
    PGR_CHECK(mime == PGR_IMAGE_JPEG && w == 300 && h == 200);
    /* Progressive frames count too; a Huffman table segment (C4) is not a frame. */
    n = jpeg_header(b, 30, 20, 0xC2);
    PGR_CHECK(pgr_image_info(b, n, &mime, &w, &h) == PGR_OK && w == 30 && h == 20);
    n = jpeg_header(b, 30, 20, 0xC4);
    PGR_CHECK(pgr_image_info(b, n, &mime, &w, &h) == PGR_ERR_PARSE);
    /* No frame before the end, cut short, and a zero side. */
    PGR_CHECK(pgr_image_info(b, 20, &mime, &w, &h) == PGR_ERR_PARSE);
    n = jpeg_header(b, 300, 200, 0xC0);
    PGR_CHECK(pgr_image_info(b, n - 3, &mime, &w, &h) == PGR_ERR_PARSE);
    n = jpeg_header(b, 300, 0, 0xC0);
    PGR_CHECK(pgr_image_info(b, n, &mime, &w, &h) == PGR_ERR_PARSE);
    /* A segment length running past the end. */
    n = jpeg_header(b, 300, 200, 0xC0);
    b[5] = 200;
    PGR_CHECK(pgr_image_info(b, n, &mime, &w, &h) == PGR_ERR_PARSE);
}

static void
test_other_formats(void)
{
    static const unsigned char gif[8] = { 'G', 'I', 'F', '8', '9', 'a', 1, 0 };
    static const unsigned char webp[12] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P' };
    uint32_t w, h;
    int mime;

    PGR_CHECK(pgr_image_info(gif, sizeof(gif), &mime, &w, &h) == PGR_ERR_UNSUPPORTED);
    PGR_CHECK(pgr_image_info(webp, sizeof(webp), &mime, &w, &h) == PGR_ERR_UNSUPPORTED);
    PGR_CHECK(pgr_image_info(gif, 0, &mime, &w, &h) == PGR_ERR_UNSUPPORTED);
    PGR_CHECK(pgr_image_info(NULL, 10, &mime, &w, &h) == PGR_ERR_UNSUPPORTED);
}

static void
test_srgb(void)
{
    volatile double minus_one = -1.0;     /* NaN at run time, not at compile time */
    int i;

    /* The ends, the linear segment, and mid-grey: 0.5 linear is 0.7354
       sRGB, the value every converter gives. */
    PGR_CHECK(pgr_linear_to_srgb(0.0) == 0.0 && fabs(pgr_linear_to_srgb(1.0) - 1.0) < 1e-12);
    PGR_CHECK(fabs(pgr_linear_to_srgb(0.002) - 0.02584) < 1e-6);
    PGR_CHECK(fabs(pgr_linear_to_srgb(0.5) - 0.735357) < 1e-6);
    PGR_CHECK(fabs(pgr_srgb_to_linear(0.735357) - 0.5) < 1e-6);
    PGR_CHECK(fabs(pgr_srgb_to_linear(0.02584) - 0.002) < 1e-6);
    /* The two pieces meet. */
    PGR_CHECK(fabs(pgr_linear_to_srgb(0.0031308) - pgr_linear_to_srgb(0.00313081)) < 1e-6);
    /* Clamped, and NaN is 0. */
    PGR_CHECK(pgr_linear_to_srgb(-1.0) == 0.0 && fabs(pgr_linear_to_srgb(2.0) - 1.0) < 1e-12);
    PGR_CHECK(pgr_linear_to_srgb(sqrt(minus_one)) == 0.0);
    /* Every byte survives sRGB -> linear -> sRGB. */
    for (i = 0; i < 256; i++)
        PGR_CHECK(pgr_unit_to_byte(pgr_linear_to_srgb(pgr_srgb_to_linear(i / 255.0))) == i);
    PGR_CHECK(pgr_unit_to_byte(0.5) == 128 && pgr_unit_to_byte(-3.0) == 0 && pgr_unit_to_byte(9.0) == 255);
}

int
main(void)
{
    test_png();
    test_jpeg();
    test_other_formats();
    test_srgb();
    return PGR_TEST_RESULT;
}
