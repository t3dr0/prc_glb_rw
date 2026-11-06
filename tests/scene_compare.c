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

#include "scene_compare.h"

#include "pgr_math.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    double *t;                 /* 9 doubles per triangle */
    uint32_t n, cap;
    uint32_t segments;
} world_tris;

static int
push_tri(world_tris *w, const double p[9])
{
    if (w->n == w->cap)
    {
        uint32_t cap = w->cap ? w->cap * 2 : 256;
        double *t = (double *)realloc(w->t, (size_t)cap * 9 * sizeof(double));

        if (t == NULL)
            return -1;
        w->t = t;
        w->cap = cap;
    }
    memcpy(w->t + (size_t)w->n * 9, p, 9 * sizeof(double));
    w->n++;
    return 0;
}

/* Every triangle the scene draws, in world space; nodes are visited from
   the roots with an explicit stack. */
static int
gather(const pgr_scene *s, world_tris *w)
{
    uint32_t *stack;
    double (*world)[16];
    uint32_t depth = 0, i, j, k, c;
    int rc = 0;

    stack = (uint32_t *)malloc((s->num_nodes + 1) * sizeof(uint32_t));
    world = (double (*)[16])malloc((s->num_nodes + 1) * sizeof(*world));
    if (stack == NULL || world == NULL)
        rc = -1;
    for (i = 0; rc == 0 && i < s->num_roots; i++)
    {
        uint32_t r = s->roots[i];

        memcpy(world[r], s->nodes[r].matrix, sizeof(world[r]));
        stack[depth++] = r;
    }
    while (rc == 0 && depth > 0)
    {
        uint32_t id = stack[--depth];
        const pgr_node *n = &s->nodes[id];

        if (n->mesh >= 0)
        {
            const pgr_mesh *m = &s->meshes[n->mesh];

            for (j = 0; j < m->num_primitives && rc == 0; j++)
            {
                const pgr_primitive *p = &m->primitives[j];

                if (p->mode == PGR_LINES)
                {
                    w->segments += p->num_indices / 2;
                    continue;
                }
                for (k = 0; k + 2 < p->num_indices && rc == 0; k += 3)
                {
                    double tri[9];

                    for (c = 0; c < 3; c++)
                    {
                        const float *v = &p->positions[3 * p->indices[k + c]];
                        double local[3];

                        local[0] = v[0];
                        local[1] = v[1];
                        local[2] = v[2];
                        pgr_mat4_transform_point(world[id], local, &tri[3 * c]);
                    }
                    rc = push_tri(w, tri);
                }
            }
        }
        for (c = 0; c < n->num_children && rc == 0; c++)
        {
            uint32_t ch = n->children[c];

            pgr_mat4_mul(world[id], s->nodes[ch].matrix, world[ch]);
            stack[depth++] = ch;
        }
    }
    free(stack);
    free(world);
    return rc;
}

static double
dist(const double *a, const double *b)
{
    double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];

    return sqrt(dx * dx + dy * dy + dz * dz);
}

int
scene_compare(const pgr_scene *a, const pgr_scene *b, double tol_fraction, scene_diff *d)
{
    world_tris wa = { NULL, 0, 0, 0 }, wb = { NULL, 0, 0, 0 };
    unsigned char *used = NULL;
    double mn[3], mx[3], tol;
    uint32_t i, j;
    int k, rc = 0;

    memset(d, 0, sizeof(*d));
    if (gather(a, &wa) != 0 || gather(b, &wb) != 0)
        rc = -1;
    d->triangles_a = wa.n;
    d->triangles_b = wb.n;
    d->segments_a = wa.segments;
    d->segments_b = wb.segments;
    if (rc == 0 && wa.n > 0)
    {
        for (k = 0; k < 3; k++)
            mn[k] = mx[k] = wa.t[k];
        for (i = 0; i < wa.n * 3; i++)
            for (k = 0; k < 3; k++)
            {
                double v = wa.t[3 * i + k];

                if (v < mn[k])
                    mn[k] = v;
                if (v > mx[k])
                    mx[k] = v;
            }
        d->diagonal = sqrt((mx[0] - mn[0]) * (mx[0] - mn[0]) + (mx[1] - mn[1]) * (mx[1] - mn[1])
                           + (mx[2] - mn[2]) * (mx[2] - mn[2]));
    }
    tol = tol_fraction * (d->diagonal > 0.0 ? d->diagonal : 1.0);
    used = (unsigned char *)calloc(wb.n ? wb.n : 1, 1);
    if (used == NULL)
        rc = -1;
    for (i = 0; rc == 0 && i < wa.n; i++)
    {
        const double *ta = wa.t + (size_t)i * 9;
        int found = 0;

        for (j = 0; j < wb.n && !found; j++)
        {
            const double *tb = wb.t + (size_t)j * 9;
            int rot;

            if (used[j])
                continue;
            for (rot = 0; rot < 3 && !found; rot++)
            {
                double e0 = dist(ta, tb + 3 * rot);
                double e1 = dist(ta + 3, tb + 3 * ((rot + 1) % 3));
                double e2 = dist(ta + 6, tb + 3 * ((rot + 2) % 3));

                if (e0 <= tol && e1 <= tol && e2 <= tol)
                {
                    double e = e0 > e1 ? (e0 > e2 ? e0 : e2) : (e1 > e2 ? e1 : e2);

                    used[j] = 1;
                    found = 1;
                    if (e > d->max_error)
                        d->max_error = e;
                }
            }
        }
        if (!found)
            d->unmatched++;
    }
    free(used);
    free(wa.t);
    free(wb.t);
    if (rc != 0)
        return -1;
    return (d->unmatched == 0 && wa.n == wb.n && d->segments_a == d->segments_b) ? 0 : -1;
}

int
scene_bounds(const pgr_scene *s, double min[3], double max[3])
{
    world_tris w = { NULL, 0, 0, 0 };
    uint32_t i;
    int k;

    if (gather(s, &w) != 0 || w.n == 0)
    {
        free(w.t);
        return 0;
    }
    for (k = 0; k < 3; k++)
        min[k] = max[k] = w.t[k];
    for (i = 0; i < w.n * 3; i++)
        for (k = 0; k < 3; k++)
        {
            double v = w.t[3 * i + k];

            if (v < min[k])
                min[k] = v;
            if (v > max[k])
                max[k] = v;
        }
    free(w.t);
    return 1;
}

static int
compare_names(const void *x, const void *y)
{
    return strcmp(*(const char *const *)x, *(const char *const *)y);
}

int
scene_same_names(const pgr_scene *a, const pgr_scene *b)
{
    const char **na, **nb;
    uint32_t i;
    int same = a->num_nodes == b->num_nodes;

    if (!same || a->num_nodes == 0)
        return same;
    na = (const char **)malloc(a->num_nodes * sizeof(char *));
    nb = (const char **)malloc(b->num_nodes * sizeof(char *));
    if (na == NULL || nb == NULL)
        same = 0;
    for (i = 0; same && i < a->num_nodes; i++)
    {
        na[i] = a->nodes[i].name ? a->nodes[i].name : "";
        nb[i] = b->nodes[i].name ? b->nodes[i].name : "";
    }
    if (same)
    {
        qsort((void *)na, a->num_nodes, sizeof(char *), compare_names);
        qsort((void *)nb, b->num_nodes, sizeof(char *), compare_names);
        for (i = 0; i < a->num_nodes && same; i++)
            same = strcmp(na[i], nb[i]) == 0;
    }
    free((void *)na);
    free((void *)nb);
    return same;
}
