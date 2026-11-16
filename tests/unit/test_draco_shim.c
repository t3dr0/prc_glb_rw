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

/* The Draco shim on its own, in builds with Draco: encode, then decode.

   Draco may renumber the points, so each decoded point is matched to an
   original one: the one, among those within the position tolerance, whose
   texture coordinates are nearest (a seam has several points in one
   place). Then every decoded triangle must be an original triangle, in the
   same cyclic order, and every attribute within its quantisation step:
   one step of the largest extent for positions, a degree for normals, one
   step of 0..1 for texture coordinates and colours. With no quantisation,
   positions come back exactly. */

#include "draco_shim.h"
#include "pgr_test.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    uint32_t nv, nf;
    float *pos, *nrm, *uv, *col;
    uint32_t *idx;
} mesh;

static void
mesh_free(mesh *m)
{
    free(m->pos);
    free(m->nrm);
    free(m->uv);
    free(m->col);
    free(m->idx);
    memset(m, 0, sizeof(*m));
}

/* A UV sphere of radius 2 about (1, 2, 3): slices * (stacks + 1) points
   plus a seam column, two triangles per quad except at the poles. */
static void
make_sphere(mesh *m, uint32_t slices, uint32_t stacks)
{
    uint32_t i, j, f = 0;

    m->nv = (stacks + 1) * (slices + 1);
    m->nf = 2 * slices * (stacks - 1);
    m->pos = (float *)malloc(m->nv * 3 * sizeof(float));
    m->nrm = (float *)malloc(m->nv * 3 * sizeof(float));
    m->uv = (float *)malloc(m->nv * 2 * sizeof(float));
    m->col = (float *)malloc(m->nv * 4 * sizeof(float));
    m->idx = (uint32_t *)malloc(m->nf * 3 * sizeof(uint32_t));
    for (i = 0; i <= stacks; i++)
        for (j = 0; j <= slices; j++)
        {
            uint32_t v = i * (slices + 1) + j;
            double th = 3.14159265358979 * i / stacks, ph = 2 * 3.14159265358979 * j / slices;
            double n[3];

            n[0] = sin(th) * cos(ph);
            n[1] = sin(th) * sin(ph);
            n[2] = cos(th);
            m->pos[3 * v] = (float)(1 + 2 * n[0]);
            m->pos[3 * v + 1] = (float)(2 + 2 * n[1]);
            m->pos[3 * v + 2] = (float)(3 + 2 * n[2]);
            m->nrm[3 * v] = (float)n[0];
            m->nrm[3 * v + 1] = (float)n[1];
            m->nrm[3 * v + 2] = (float)n[2];
            m->uv[2 * v] = (float)j / slices;
            m->uv[2 * v + 1] = (float)i / stacks;
            m->col[4 * v] = (float)(0.5 + 0.5 * n[0]);
            m->col[4 * v + 1] = (float)(0.5 + 0.5 * n[1]);
            m->col[4 * v + 2] = (float)(0.5 + 0.5 * n[2]);
            m->col[4 * v + 3] = 1.0f;
        }
    for (i = 0; i < stacks; i++)
        for (j = 0; j < slices; j++)
        {
            uint32_t a = i * (slices + 1) + j, b = a + 1, c = a + slices + 1, d = c + 1;

            if (i != 0)
            {
                m->idx[3 * f] = a, m->idx[3 * f + 1] = c, m->idx[3 * f + 2] = b;
                f++;
            }
            if (i != stacks - 1)
            {
                m->idx[3 * f] = b, m->idx[3 * f + 1] = c, m->idx[3 * f + 2] = d;
                f++;
            }
        }
}

/* The flat-shaded unit cube: 24 points, 12 triangles, positions and
   normals only. */
static void
make_cube(mesh *m)
{
    int face, c, k;
    uint32_t v = 0, f = 0;

    memset(m, 0, sizeof(*m));
    m->nv = 24;
    m->nf = 12;
    m->pos = (float *)malloc(72 * sizeof(float));
    m->nrm = (float *)malloc(72 * sizeof(float));
    m->idx = (uint32_t *)malloc(36 * sizeof(uint32_t));
    for (face = 0; face < 6; face++)
    {
        int a = face / 2;
        float s = face % 2 == 0 ? 1.0f : -1.0f;
        static const float cu[4] = { -1, 1, 1, -1 };
        static const float cw[4] = { -1, -1, 1, 1 };

        for (c = 0; c < 4; c++, v++)
            for (k = 0; k < 3; k++)
            {
                m->pos[3 * v + k] = 0.5f * (k == a ? s : k == (a + 1) % 3 ? s * cu[c] : cw[c]);
                m->nrm[3 * v + k] = k == a ? s : 0.0f;
            }
        m->idx[3 * f] = v - 4, m->idx[3 * f + 1] = v - 3, m->idx[3 * f + 2] = v - 2;
        f++;
        m->idx[3 * f] = v - 4, m->idx[3 * f + 1] = v - 2, m->idx[3 * f + 2] = v - 1;
        f++;
    }
}

static void
as_ds(const mesh *m, ds_mesh *d, ds_attribute *att)
{
    uint32_t n = 0;

    memset(att, 0, 4 * sizeof(ds_attribute));
    att[n].semantic = DS_POSITION, att[n].num_components = 3, att[n++].data = m->pos;
    if (m->nrm)
        att[n].semantic = DS_NORMAL, att[n].num_components = 3, att[n++].data = m->nrm;
    if (m->uv)
        att[n].semantic = DS_TEXCOORD, att[n].num_components = 2, att[n++].data = m->uv;
    if (m->col)
        att[n].semantic = DS_COLOR, att[n].num_components = 4, att[n++].data = m->col;
    d->num_points = m->nv;
    d->num_faces = m->nf;
    d->indices = m->idx;
    d->num_attributes = n;
    d->attributes = att;
}

static double
dist(const float *a, const float *b, int n)
{
    double s = 0;
    int k;

    for (k = 0; k < n; k++)
        s += ((double)a[k] - b[k]) * ((double)a[k] - b[k]);
    return sqrt(s);
}

static int
cmp_key(const void *a, const void *b)
{
    const uint32_t *x = (const uint32_t *)a, *y = (const uint32_t *)b;
    int k;

    for (k = 0; k < 3; k++)
        if (x[k] != y[k])
            return x[k] < y[k] ? -1 : 1;
    return 0;
}

/* The triangle rotated to start at its smallest index, keeping its order. */
static void
canonical(uint32_t a, uint32_t b, uint32_t c, uint32_t key[3])
{
    if (a <= b && a <= c)
        key[0] = a, key[1] = b, key[2] = c;
    else if (b <= a && b <= c)
        key[0] = b, key[1] = c, key[2] = a;
    else
        key[0] = c, key[1] = a, key[2] = b;
}

/* Encode m with o, decode, and check as described at the top. Returns the
   encoded size, or 0 on failure. */
static size_t
round_trip(const char *name, const mesh *m, const ds_encode_options *o)
{
    ds_attribute att[4];
    ds_mesh in, out;
    int ids[4], wanted[5], k;
    void *blob = NULL;
    size_t size = 0;
    uint32_t i, j, n;
    double extent = 0, pos_tol, uv_tol, col_tol, worst_pos = 0, worst_nrm = 0, worst_uv = 0, worst_col = 0;
    uint32_t *match = NULL, *keys = NULL;
    uint32_t bad_match = 0, bad_face = 0;
    float mn[3], mx[3];

    as_ds(m, &in, att);
    n = in.num_attributes;
    if (ds_encode(&in, o, &blob, &size, ids) != DS_OK)
    {
        fprintf(stderr, "%s: encode failed\n", name);
        PGR_CHECK(0);
        return 0;
    }
    for (k = 0; k < (int)n; k++)
    {
        int l;

        wanted[k] = ids[k];
        for (l = 0; l < k; l++)
            PGR_CHECK(ids[l] != ids[k]);
    }
    wanted[n] = 99;   /* an id the mesh does not have */
    PGR_CHECK(ds_decode(blob, size, wanted, n + 1, &out) == DS_OK);
    ds_buffer_free(blob);
    PGR_CHECK(out.num_faces == m->nf);
    PGR_CHECK(out.num_attributes == n + 1);
    if (out.num_attributes != n + 1)
    {
        ds_mesh_free(&out);
        return 0;
    }
    PGR_CHECK(out.attributes[n].data == NULL && out.attributes[n].num_components == 0);
    for (k = 0; k < (int)n; k++)
    {
        PGR_CHECK(out.attributes[k].draco_id == ids[k]);
        PGR_CHECK(out.attributes[k].semantic == att[k].semantic);
        PGR_CHECK(out.attributes[k].num_components == att[k].num_components);
        PGR_CHECK(out.attributes[k].data != NULL);
    }

    for (k = 0; k < 3; k++)
        mn[k] = mx[k] = m->pos[k];
    for (i = 1; i < m->nv; i++)
        for (k = 0; k < 3; k++)
        {
            mn[k] = m->pos[3 * i + k] < mn[k] ? m->pos[3 * i + k] : mn[k];
            mx[k] = m->pos[3 * i + k] > mx[k] ? m->pos[3 * i + k] : mx[k];
        }
    for (k = 0; k < 3; k++)
        extent = mx[k] - mn[k] > extent ? mx[k] - mn[k] : extent;
    pos_tol = o->qbits_position > 0 ? extent / ((1 << o->qbits_position) - 1) : 0.0;
    uv_tol = o->qbits_texcoord > 0 ? 1.5 / ((1 << o->qbits_texcoord) - 1) : 0.0;
    col_tol = o->qbits_color > 0 ? 1.5 / ((1 << o->qbits_color) - 1) : 0.0;

    match = (uint32_t *)malloc((out.num_points + 1) * sizeof(uint32_t));
    keys = (uint32_t *)malloc((m->nf + 1) * 3 * sizeof(uint32_t));
    for (i = 0; i < out.num_points; i++)
    {
        const float *p = out.attributes[0].data + 3 * i;
        double best = 1e30;
        uint32_t found = 0xffffffffu;

        for (j = 0; j < m->nv; j++)
        {
            double d = dist(p, m->pos + 3 * j, 3), score;

            if (d > pos_tol + 1e-6 * extent)
                continue;
            score = m->uv ? dist(out.attributes[2].data + 2 * i, m->uv + 2 * j, 2) : d;
            if (m->uv == NULL && m->nrm != NULL)
                score = dist(out.attributes[1].data + 3 * i, m->nrm + 3 * j, 3);
            if (score < best)
                best = score, found = j;
        }
        match[i] = found;
        if (found == 0xffffffffu)
        {
            bad_match++;
            continue;
        }
        if (dist(p, m->pos + 3 * found, 3) > worst_pos)
            worst_pos = dist(p, m->pos + 3 * found, 3);
        if (m->nrm)
        {
            const float *a = out.attributes[1].data + 3 * i, *b = m->nrm + 3 * found;
            /* atan2 of the cross and dot products: acos of the dot is
               ill-conditioned near 1, and reads float rounding between
               equal normals as hundredths of a degree. */
            double cx = (double)a[1] * b[2] - (double)a[2] * b[1];
            double cy = (double)a[2] * b[0] - (double)a[0] * b[2];
            double cz = (double)a[0] * b[1] - (double)a[1] * b[0];
            double dot = (double)a[0] * b[0] + (double)a[1] * b[1] + (double)a[2] * b[2];
            double ang = atan2(sqrt(cx * cx + cy * cy + cz * cz), dot) * 180 / 3.14159265358979;

            worst_nrm = ang > worst_nrm ? ang : worst_nrm;
        }
        if (m->uv)
        {
            double d = dist(out.attributes[2].data + 2 * i, m->uv + 2 * found, 2);
            worst_uv = d > worst_uv ? d : worst_uv;
        }
        if (m->col)
        {
            int c = m->uv ? 3 : 2;
            for (k = 0; k < 4; k++)
            {
                double d = fabs((double)out.attributes[c].data[4 * i + k] - m->col[4 * found + k]);
                worst_col = d > worst_col ? d : worst_col;
            }
        }
    }

    for (i = 0; i < m->nf; i++)
        canonical(m->idx[3 * i], m->idx[3 * i + 1], m->idx[3 * i + 2], keys + 3 * i);
    qsort(keys, m->nf, 3 * sizeof(uint32_t), cmp_key);
    for (i = 0; i < out.num_faces && bad_match == 0; i++)
    {
        uint32_t key[3];

        canonical(match[out.indices[3 * i]], match[out.indices[3 * i + 1]], match[out.indices[3 * i + 2]], key);
        if (bsearch(key, keys, m->nf, 3 * sizeof(uint32_t), cmp_key) == NULL)
            bad_face++;
    }

    printf("%s: %u points -> %u, %u bytes; worst position %.3g (tol %.3g), normal %.3g deg, uv %.3g, colour %.3g\n",
           name, m->nv, out.num_points, (unsigned)size, worst_pos, pos_tol, worst_nrm, worst_uv, worst_col);
    PGR_CHECK(bad_match == 0);
    PGR_CHECK(bad_face == 0);
    PGR_CHECK(worst_pos <= pos_tol + 1e-6 * extent);
    PGR_CHECK(worst_nrm <= (o->qbits_normal > 0 ? 1.0 : 1e-4));
    PGR_CHECK(worst_uv <= uv_tol + 1e-6);
    PGR_CHECK(worst_col <= col_tol + 1e-6);
    free(match);
    free(keys);
    ds_mesh_free(&out);
    return size;
}

static void
test_bad_input(const mesh *cube)
{
    ds_attribute att[4];
    ds_mesh in, out;
    ds_encode_options o;
    void *blob = NULL;
    size_t size = 0;
    int ids[4], wanted = 0;
    unsigned char junk[64];
    uint32_t saved;
    size_t i;

    as_ds(cube, &in, att);
    PGR_CHECK(ds_encode(&in, NULL, &blob, &size, ids) == DS_OK);

    /* Truncated, or not Draco at all: a decode error, out left empty. */
    PGR_CHECK(ds_decode(blob, size / 2, &wanted, 1, &out) == DS_ERR_DECODE);
    PGR_CHECK(out.indices == NULL && out.attributes == NULL && out.num_points == 0);
    for (i = 0; i < sizeof(junk); i++)
        junk[i] = (unsigned char)(i * 37 + 11);
    PGR_CHECK(ds_decode(junk, sizeof(junk), &wanted, 1, &out) == DS_ERR_DECODE);
    PGR_CHECK(ds_decode(blob, 0, &wanted, 1, &out) == DS_ERR_ARG);
    PGR_CHECK(ds_decode(NULL, size, &wanted, 1, &out) == DS_ERR_ARG);
    /* Every cut of the data, so that no length crashes the decoder. */
    for (i = 1; i < size; i++)
        if (ds_decode(blob, i, &wanted, 1, &out) == DS_OK)
            ds_mesh_free(&out);
    ds_buffer_free(blob);

    /* Arguments the encoder refuses. */
    saved = cube->idx[5];
    cube->idx[5] = 24;
    PGR_CHECK(ds_encode(&in, NULL, &blob, &size, ids) == DS_ERR_ARG);
    PGR_CHECK(blob == NULL && size == 0);
    cube->idx[5] = saved;
    att[1].num_components = 5;
    PGR_CHECK(ds_encode(&in, NULL, &blob, &size, ids) == DS_ERR_ARG);
    att[1].num_components = 3;
    ds_encode_options_default(&o);
    o.qbits_position = 31;
    PGR_CHECK(ds_encode(&in, &o, &blob, &size, ids) == DS_ERR_ARG);
    ds_encode_options_default(&o);
    o.encode_speed = 11;
    PGR_CHECK(ds_encode(&in, &o, &blob, &size, ids) == DS_ERR_ARG);
    in.num_faces = 0;
    PGR_CHECK(ds_encode(&in, NULL, &blob, &size, ids) == DS_ERR_ARG);
}

int
main(void)
{
    mesh cube, sphere;
    ds_encode_options o, lossless, fast;
    size_t compressed, raw;

    PGR_CHECK(ds_available() == 1);
    PGR_CHECK(ds_version() != NULL && strcmp(ds_version(), "1.5.9") == 0);

    ds_encode_options_default(&o);
    PGR_CHECK(o.qbits_position == 14 && o.qbits_normal == 10 && o.qbits_texcoord == 12
              && o.qbits_color == 8 && o.encode_speed == 5 && o.decode_speed == 5);
    memset(&lossless, 0, sizeof(lossless));
    lossless.encode_speed = lossless.decode_speed = 5;
    fast = o;
    fast.encode_speed = fast.decode_speed = 10;

    make_cube(&cube);
    round_trip("cube", &cube, &o);
    round_trip("cube lossless", &cube, &lossless);

    make_sphere(&sphere, 72, 70);
    PGR_CHECK(sphere.nf == 9936);
    compressed = round_trip("sphere", &sphere, &o);
    raw = (size_t)sphere.nv * (3 + 3 + 2 + 4) * 4 + (size_t)sphere.nf * 3 * 4;
    PGR_CHECK(compressed > 0 && compressed < raw / 4);
    round_trip("sphere lossless", &sphere, &lossless);
    round_trip("sphere speed 10", &sphere, &fast);

    test_bad_input(&cube);
    mesh_free(&cube);
    mesh_free(&sphere);
    return PGR_TEST_RESULT;
}
