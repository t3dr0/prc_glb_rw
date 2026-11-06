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

#include "fixtures.h"

#include "pgr_glb.h"
#include "pgr_info.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* glTF numbers for component types, targets and modes. */
#define GL_UNSIGNED_BYTE  5121
#define GL_UNSIGNED_SHORT 5123
#define GL_UNSIGNED_INT   5125
#define GL_FLOAT          5126
#define GL_ARRAY_BUFFER         34962
#define GL_ELEMENT_ARRAY_BUFFER 34963

static void
die(void)
{
    fprintf(stderr, "fixtures: out of memory\n");
    exit(2);
}

typedef struct
{
    char *s;
    size_t n, cap;
} text;

static void
text_printf(text *t, const char *fmt, ...)
{
    va_list ap;
    int need;

    va_start(ap, fmt);
    need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0)
        die();
    if (t->n + (size_t)need + 1 > t->cap)
    {
        size_t cap = t->cap ? t->cap : 256;
        char *s;

        while (cap < t->n + (size_t)need + 1)
            cap *= 2;
        s = (char *)realloc(t->s, cap);
        if (s == NULL)
            die();
        t->s = s;
        t->cap = cap;
    }
    va_start(ap, fmt);
    vsnprintf(t->s + t->n, (size_t)need + 1, fmt, ap);
    va_end(ap);
    t->n += (size_t)need;
}

typedef struct
{
    unsigned char *bin;
    size_t bin_size, bin_cap;
    text views, accessors;
    int n_views, n_accessors;
} fx;

static void
fx_init(fx *f)
{
    memset(f, 0, sizeof(*f));
}

static void
fx_free(fx *f)
{
    free(f->bin);
    free(f->views.s);
    free(f->accessors.s);
}

/* Data into the buffer at a 4-byte boundary, as a buffer view; its index. */
static int
fx_view(fx *f, const void *data, size_t n, int stride, int target)
{
    size_t start = (f->bin_size + 3) & ~(size_t)3;

    if (start + n > f->bin_cap)
    {
        size_t cap = f->bin_cap ? f->bin_cap : 1024;
        unsigned char *b;

        while (cap < start + n)
            cap *= 2;
        b = (unsigned char *)realloc(f->bin, cap);
        if (b == NULL)
            die();
        f->bin = b;
        f->bin_cap = cap;
    }
    memset(f->bin + f->bin_size, 0, start - f->bin_size);
    memcpy(f->bin + start, data, n);
    f->bin_size = start + n;

    text_printf(&f->views, "%s{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u",
                f->n_views ? "," : "", (unsigned)start, (unsigned)n);
    if (stride > 0)
        text_printf(&f->views, ",\"byteStride\":%d", stride);
    if (target > 0)
        text_printf(&f->views, ",\"target\":%d", target);
    text_printf(&f->views, "}");
    return f->n_views++;
}

/* An accessor on a view; extra is more JSON members, starting with a comma,
   or "". Its index. */
static int
fx_accessor(fx *f, int view, int component, int count, const char *type, const char *extra)
{
    text_printf(&f->accessors, "%s{", f->n_accessors ? "," : "");
    if (view >= 0)
        text_printf(&f->accessors, "\"bufferView\":%d,", view);
    text_printf(&f->accessors, "\"componentType\":%d,\"count\":%d,\"type\":\"%s\"%s}",
                component, count, type, extra);
    return f->n_accessors++;
}

/* Float VEC3 positions with the min and max glTF requires. */
static int
fx_positions(fx *f, const float *p, int count)
{
    float mn[3], mx[3];
    char extra[256];
    int i, k;

    for (k = 0; k < 3; k++)
        mn[k] = mx[k] = p[k];
    for (i = 1; i < count; i++)
        for (k = 0; k < 3; k++)
        {
            if (p[3 * i + k] < mn[k])
                mn[k] = p[3 * i + k];
            if (p[3 * i + k] > mx[k])
                mx[k] = p[3 * i + k];
        }
    snprintf(extra, sizeof(extra), ",\"min\":[%.9g,%.9g,%.9g],\"max\":[%.9g,%.9g,%.9g]",
             mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]);
    return fx_accessor(f, fx_view(f, p, (size_t)count * 12, 0, GL_ARRAY_BUFFER),
                       GL_FLOAT, count, "VEC3", extra);
}

static int
fx_floats(fx *f, const float *v, int count, int comps, const char *type)
{
    return fx_accessor(f, fx_view(f, v, (size_t)count * comps * 4, 0, GL_ARRAY_BUFFER),
                       GL_FLOAT, count, type, "");
}

static int
fx_indices16(fx *f, const uint16_t *v, int count)
{
    return fx_accessor(f, fx_view(f, v, (size_t)count * 2, 0, GL_ELEMENT_ARRAY_BUFFER),
                       GL_UNSIGNED_SHORT, count, "SCALAR", "");
}

static void
base64(text *t, const unsigned char *d, size_t n)
{
    static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i;

    for (i = 0; i + 2 < n; i += 3)
        text_printf(t, "%c%c%c%c", tab[d[i] >> 2], tab[((d[i] & 3) << 4) | (d[i + 1] >> 4)],
                    tab[((d[i + 1] & 15) << 2) | (d[i + 2] >> 6)], tab[d[i + 2] & 63]);
    if (n - i == 1)
        text_printf(t, "%c%c==", tab[d[i] >> 2], tab[(d[i] & 3) << 4]);
    else if (n - i == 2)
        text_printf(t, "%c%c%c=", tab[d[i] >> 2], tab[((d[i] & 3) << 4) | (d[i + 1] >> 4)],
                    tab[(d[i + 1] & 15) << 2]);
}

/* Write the file: body is the top-level members other than asset,
   buffers, bufferViews and accessors, without enclosing braces. */
static int
fx_save(fx *f, const char *path, const char *body)
{
    int glb = pgr_has_extension(path, ".glb");
    text j = { NULL, 0, 0 };
    int st = 0;

    text_printf(&j, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"prc_glb_rw test fixtures\"}");
    if (body[0] != '\0')
        text_printf(&j, ",%s", body);
    if (f->bin_size > 0)
    {
        text_printf(&j, ",\"buffers\":[{\"byteLength\":%u", (unsigned)f->bin_size);
        if (!glb)
        {
            text_printf(&j, ",\"uri\":\"data:application/octet-stream;base64,");
            base64(&j, f->bin, f->bin_size);
            text_printf(&j, "\"");
        }
        text_printf(&j, "}]");
    }
    if (f->n_views > 0)
        text_printf(&j, ",\"bufferViews\":[%s]", f->views.s);
    if (f->n_accessors > 0)
        text_printf(&j, ",\"accessors\":[%s]", f->accessors.s);
    text_printf(&j, "}");

    if (glb)
        st = pgr_glb_write_container(path, j.s, j.n, f->bin, f->bin_size) == PGR_OK ? 0 : 1;
    else
    {
        FILE *out = fopen(path, "wb");

        if (out == NULL || fwrite(j.s, 1, j.n, out) != j.n)
            st = 1;
        if (out != NULL && fclose(out) != 0)
            st = 1;
    }
    free(j.s);
    fx_free(f);
    return st;
}

static const float tri_pos[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
static const uint16_t tri_idx[3] = { 0, 1, 2 };

/* The 24 vertices and normals of a flat-shaded unit cube, counter-clockwise
   seen from outside. */
static void
cube_flat_data(float pos[72], float nrm[72], uint16_t idx[36])
{
    int face, c, k, v = 0, i = 0;

    for (face = 0; face < 6; face++)
    {
        int a = face / 2;
        float s = face % 2 == 0 ? 1.0f : -1.0f;
        float n[3] = { 0, 0, 0 }, u[3] = { 0, 0, 0 }, w[3] = { 0, 0, 0 };
        static const float cu[4] = { -1, 1, 1, -1 };
        static const float cw[4] = { -1, -1, 1, 1 };

        n[a] = s;
        u[(a + 1) % 3] = 1.0f;
        w[(a + 2) % 3] = 1.0f;
        for (c = 0; c < 4; c++)
        {
            /* u x w is +n, so go round the other way on a negative face. */
            int cc = s > 0 ? c : 3 - c;

            for (k = 0; k < 3; k++)
            {
                pos[3 * (v + c) + k] = 0.5f * (n[k] + cu[cc] * u[k] + cw[cc] * w[k]);
                nrm[3 * (v + c) + k] = n[k];
            }
        }
        idx[i++] = (uint16_t)v;
        idx[i++] = (uint16_t)(v + 1);
        idx[i++] = (uint16_t)(v + 2);
        idx[i++] = (uint16_t)v;
        idx[i++] = (uint16_t)(v + 2);
        idx[i++] = (uint16_t)(v + 3);
        v += 4;
    }
}

int
fx_cube_flat(const char *path)
{
    float pos[72], nrm[72];
    uint16_t idx[36];
    fx f;
    char body[512];
    int p, n, ix;

    cube_flat_data(pos, nrm, idx);
    fx_init(&f);
    p = fx_positions(&f, pos, 24);
    n = fx_floats(&f, nrm, 24, 3, "VEC3");
    ix = fx_indices16(&f, idx, 36);
    snprintf(body, sizeof(body),
             "\"scene\":0,\"scenes\":[{\"name\":\"cube_flat\",\"nodes\":[0]}],"
             "\"nodes\":[{\"name\":\"cube\",\"mesh\":0}],"
             "\"meshes\":[{\"name\":\"cube_mesh\",\"primitives\":[{\"attributes\":{\"POSITION\":%d,\"NORMAL\":%d},\"indices\":%d}]}]",
             p, n, ix);
    return fx_save(&f, path, body);
}

int
fx_cube_smooth(const char *path)
{
    float flat[72], nrm[72], pos[24];
    uint16_t fidx[36];
    unsigned char idx[36];
    fx f;
    char body[512];
    int i, k, p, ix;

    cube_flat_data(flat, nrm, fidx);
    /* Corner c has x, y, z positive where bits 0, 1, 2 of c are set. */
    for (i = 0; i < 8; i++)
        for (k = 0; k < 3; k++)
            pos[3 * i + k] = (i >> k) & 1 ? 0.5f : -0.5f;
    for (i = 0; i < 36; i++)
    {
        const float *v = &flat[3 * fidx[i]];

        idx[i] = (unsigned char)((v[0] > 0) | ((v[1] > 0) << 1) | ((v[2] > 0) << 2));
    }
    fx_init(&f);
    p = fx_positions(&f, pos, 8);
    ix = fx_accessor(&f, fx_view(&f, idx, 36, 0, GL_ELEMENT_ARRAY_BUFFER), GL_UNSIGNED_BYTE, 36, "SCALAR", "");
    snprintf(body, sizeof(body),
             "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"name\":\"cube\",\"mesh\":0}],"
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%d},\"indices\":%d}]}]",
             p, ix);
    return fx_save(&f, path, body);
}

int
fx_tri_vcolors(const char *path)
{
    static const float col[12] = { 1, 0, 0, 1, 0, 1, 0, 0.5f, 0, 0, 1, 1 };
    fx f;
    char body[512];
    int p, c;

    fx_init(&f);
    p = fx_positions(&f, tri_pos, 3);
    c = fx_floats(&f, col, 3, 4, "VEC4");
    snprintf(body, sizeof(body),
             "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%d,\"COLOR_0\":%d}}]}]",
             p, c);
    return fx_save(&f, path, body);
}

int
fx_u8_colors(const char *path)
{
    /* RGB bytes, each vertex padded to 4 bytes as glTF requires. */
    static const unsigned char col[12] = { 255, 0, 0, 0, 0, 128, 0, 0, 0, 0, 255, 0 };
    fx f;
    char body[512];
    int p, c, ix;

    fx_init(&f);
    p = fx_positions(&f, tri_pos, 3);
    c = fx_accessor(&f, fx_view(&f, col, 12, 4, GL_ARRAY_BUFFER), GL_UNSIGNED_BYTE, 3, "VEC3",
                    ",\"normalized\":true");
    ix = fx_indices16(&f, tri_idx, 3);
    snprintf(body, sizeof(body),
             "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%d,\"COLOR_0\":%d},\"indices\":%d}]}]",
             p, c, ix);
    return fx_save(&f, path, body);
}

int
fx_two_instances(const char *path)
{
    fx f;
    char body[512];
    int p, ix;

    fx_init(&f);
    p = fx_positions(&f, tri_pos, 3);
    ix = fx_indices16(&f, tri_idx, 3);
    snprintf(body, sizeof(body),
             "\"scene\":0,\"scenes\":[{\"nodes\":[0,1]}],"
             "\"nodes\":[{\"name\":\"first\",\"mesh\":0},{\"name\":\"second\",\"mesh\":0,\"translation\":[2,0,0]}],"
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%d},\"indices\":%d}]}]",
             p, ix);
    return fx_save(&f, path, body);
}

int
fx_hierarchy3(const char *path)
{
    fx f;
    char body[1024];
    int p, ix;

    fx_init(&f);
    p = fx_positions(&f, tri_pos, 3);
    ix = fx_indices16(&f, tri_idx, 3);
    snprintf(body, sizeof(body),
             "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
             "\"nodes\":["
             "{\"name\":\"root\",\"translation\":[1,2,3],\"children\":[1,3]},"
             "{\"name\":\"middle\",\"rotation\":[0,0,0.70710678,0.70710678],\"children\":[2]},"
             "{\"name\":\"leaf\",\"scale\":[2,2,2],\"mesh\":0},"
             "{\"name\":\"matrix_node\",\"matrix\":[1,0,0,0, 0,1,0,0, 0,0,1,0, 4,5,6,1]}],"
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%d},\"indices\":%d}]}]",
             p, ix);
    return fx_save(&f, path, body);
}

int
fx_multi_material(const char *path)
{
    static const float pos[12] = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 };
    static const uint16_t a[3] = { 0, 1, 2 }, b[3] = { 0, 2, 3 };
    fx f;
    char body[1024];
    int p, ia, ib;

    fx_init(&f);
    p = fx_positions(&f, pos, 4);
    ia = fx_indices16(&f, a, 3);
    ib = fx_indices16(&f, b, 3);
    snprintf(body, sizeof(body),
             "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
             "\"materials\":["
             "{\"name\":\"red\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0,0,1],\"metallicFactor\":0,\"roughnessFactor\":0.25}},"
             "{\"name\":\"blue\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0,0,1,0.5]},\"alphaMode\":\"BLEND\","
             "\"doubleSided\":true,\"emissiveFactor\":[0.1,0.2,0.3]}],"
             "\"meshes\":[{\"primitives\":["
             "{\"attributes\":{\"POSITION\":%d},\"indices\":%d,\"material\":0},"
             "{\"attributes\":{\"POSITION\":%d},\"indices\":%d,\"material\":1}]}]",
             p, ia, p, ib);
    return fx_save(&f, path, body);
}

int
fx_lines(const char *path)
{
    static const float pos[12] = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 };
    static const uint16_t idx[4] = { 0, 1, 2, 3 };
    fx f;
    char body[1024];
    int p, ix;

    fx_init(&f);
    p = fx_positions(&f, pos, 4);
    ix = fx_indices16(&f, idx, 4);
    snprintf(body, sizeof(body),
             "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
             "\"meshes\":[{\"primitives\":["
             "{\"attributes\":{\"POSITION\":%d},\"indices\":%d,\"mode\":1},"
             "{\"attributes\":{\"POSITION\":%d},\"indices\":%d,\"mode\":3},"
             "{\"attributes\":{\"POSITION\":%d},\"indices\":%d,\"mode\":2}]}]",
             p, ix, p, ix, p, ix);
    return fx_save(&f, path, body);
}

int
fx_strip_fan(const char *path)
{
    static const float pos[15] = { 0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 0, 2, 0, 0 };
    static const uint16_t idx[5] = { 0, 1, 2, 3, 4 };
    fx f;
    char body[1024];
    int p, ix;

    fx_init(&f);
    p = fx_positions(&f, pos, 5);
    ix = fx_indices16(&f, idx, 5);
    snprintf(body, sizeof(body),
             "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
             "\"meshes\":[{\"primitives\":["
             "{\"attributes\":{\"POSITION\":%d},\"mode\":5},"
             "{\"attributes\":{\"POSITION\":%d},\"indices\":%d,\"mode\":6}]}]",
             p, p, ix);
    return fx_save(&f, path, body);
}

int
fx_sparse_positions(const char *path)
{
    static const unsigned char sparse_index[4] = { 1, 0, 0, 0 };
    static const float sparse_value[3] = { 5, 5, 5 };
    fx f;
    char body[512], extra[512];
    int base, si, sv, p;

    fx_init(&f);
    base = fx_view(&f, tri_pos, sizeof(tri_pos), 0, GL_ARRAY_BUFFER);
    si = fx_view(&f, sparse_index, 1, 0, 0);
    sv = fx_view(&f, sparse_value, sizeof(sparse_value), 0, 0);
    snprintf(extra, sizeof(extra),
             ",\"min\":[0,0,0],\"max\":[5,5,5],\"sparse\":{\"count\":1,"
             "\"indices\":{\"bufferView\":%d,\"componentType\":%d},\"values\":{\"bufferView\":%d}}",
             si, GL_UNSIGNED_BYTE, sv);
    p = fx_accessor(&f, base, GL_FLOAT, 3, "VEC3", extra);
    snprintf(body, sizeof(body),
             "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%d}}]}]",
             p);
    return fx_save(&f, path, body);
}

int
fx_points(const char *path)
{
    fx f;
    char body[512];
    int p;

    fx_init(&f);
    p = fx_positions(&f, tri_pos, 3);
    snprintf(body, sizeof(body),
             "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%d},\"mode\":0}]}]",
             p);
    return fx_save(&f, path, body);
}

int
fx_degenerate(const char *path)
{
    static const uint16_t idx[6] = { 0, 1, 2, 0, 0, 1 };
    fx f;
    char body[512];
    int p, ix;

    fx_init(&f);
    p = fx_positions(&f, tri_pos, 3);
    ix = fx_indices16(&f, idx, 6);
    snprintf(body, sizeof(body),
             "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%d},\"indices\":%d}]}]",
             p, ix);
    return fx_save(&f, path, body);
}

int
fx_zero_normal(const char *path)
{
    static const float nrm[9] = { 0, 0, 1, 0, 0, 0, 0, 0, 1 };
    fx f;
    char body[512];
    int p, n, ix;

    fx_init(&f);
    p = fx_positions(&f, tri_pos, 3);
    n = fx_floats(&f, nrm, 3, 3, "VEC3");
    ix = fx_indices16(&f, tri_idx, 3);
    snprintf(body, sizeof(body),
             "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%d,\"NORMAL\":%d},\"indices\":%d}]}]",
             p, n, ix);
    return fx_save(&f, path, body);
}

int
fx_requires(const char *path, const char *extension)
{
    fx f;
    char body[768];
    int p, ix;

    fx_init(&f);
    p = fx_positions(&f, tri_pos, 3);
    ix = fx_indices16(&f, tri_idx, 3);
    snprintf(body, sizeof(body),
             "\"extensionsUsed\":[\"%s\"],\"extensionsRequired\":[\"%s\"],"
             "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%d},\"indices\":%d}]}]",
             extension, extension, p, ix);
    return fx_save(&f, path, body);
}

int
fx_bad_index(const char *path)
{
    static const uint16_t idx[3] = { 0, 1, 7 };
    fx f;
    char body[512];
    int p, ix;

    fx_init(&f);
    p = fx_positions(&f, tri_pos, 3);
    ix = fx_indices16(&f, idx, 3);
    snprintf(body, sizeof(body),
             "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%d},\"indices\":%d}]}]",
             p, ix);
    return fx_save(&f, path, body);
}

int
fx_edited(const char *path, int (*make)(const char *), const char *old, const char *replacement)
{
    FILE *f;
    char *text, *at;
    long n;
    int st = 1;

    if (make(path) != 0 || (f = fopen(path, "rb")) == NULL)
        return 1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0)
    {
        fclose(f);
        return 1;
    }
    text = (char *)malloc((size_t)n + 1);
    if (text == NULL)
        die();
    if (fread(text, 1, (size_t)n, f) != (size_t)n)
        n = -1;
    fclose(f);
    if (n >= 0)
    {
        text[n] = '\0';
        at = strstr(text, old);
        if (at != NULL && (f = fopen(path, "wb")) != NULL)
        {
            fwrite(text, 1, (size_t)(at - text), f);
            fputs(replacement, f);
            fputs(at + strlen(old), f);
            st = fclose(f) != 0;
        }
    }
    free(text);
    return st;
}

int
fx_external_buffer(const char *gltf_path, const char *uri, const char *bin_path)
{
    char body[1024];
    FILE *f;
    text j = { NULL, 0, 0 };
    int st = 0;
    uint16_t idx[4] = { 0, 1, 2, 0 };    /* padded to 4 bytes */

    f = fopen(bin_path, "wb");
    if (f == NULL)
        return 1;
    if (fwrite(tri_pos, 1, sizeof(tri_pos), f) != sizeof(tri_pos) || fwrite(idx, 1, 8, f) != 8)
        st = 1;
    if (fclose(f) != 0)
        st = 1;
    snprintf(body, sizeof(body),
             "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
             "\"buffers\":[{\"byteLength\":44,\"uri\":\"");
    text_printf(&j, "%s", body);
    /* The URI as a JSON string: backslashes doubled. */
    for (; *uri; uri++)
    {
        if (*uri == '\\')
            text_printf(&j, "\\\\");
        else
            text_printf(&j, "%c", *uri);
    }
    text_printf(&j, "\"}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
                "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
                "\"min\":[0,0,0],\"max\":[1,1,0]},{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}]}");
    f = fopen(gltf_path, "wb");
    if (f == NULL || fwrite(j.s, 1, j.n, f) != j.n)
        st = 1;
    if (f != NULL && fclose(f) != 0)
        st = 1;
    free(j.s);
    return st;
}

int
fx_no_scene(const char *path)
{
    fx f;
    char body[512];
    int p, ix;

    fx_init(&f);
    p = fx_positions(&f, tri_pos, 3);
    ix = fx_indices16(&f, tri_idx, 3);
    snprintf(body, sizeof(body),
             "\"nodes\":[{\"name\":\"parent\",\"children\":[1]},{\"name\":\"child\",\"mesh\":0}],"
             "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%d},\"indices\":%d}]}]",
             p, ix);
    return fx_save(&f, path, body);
}
