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

/* PRC (or a PDF holding PRC) into the neutral scene, through nanoPRC's
   public read API.

   The call sequence is the one nanoPRC's demos use: open, build the model
   tree, count the tessellations, initialise each and fetch its vertices.
   Then the tree is walked, as stl_export walks it:

     - each product becomes a node, with its placement (parent-relative,
       column-major, as glTF's) and name;
     - a product's part, and every representation item nested in it, may
       name a tessellation. The API numbers tessellations once per place a
       part is drawn (part_detail_index), the file once per tessellation
       stored (biased_tess_index); the data is fetched by the first and
       meshes are shared by the second, so a part drawn twice becomes one
       mesh;
     - a surface tessellation becomes one triangle primitive. Every face of
       a compressed tessellation gives the whole mesh, so only face 0 is
       read; a plain one's faces each have their own vertices, which are
       joined. Fans and strips are triangulated as nanoPRC's own exporters
       do it;
     - a wire tessellation becomes one primitive of line segments.

   Markups and exact geometry are skipped with a warning. */

#include "pgr_prc.h"
#include "pgr_math.h"

#include "prc_api.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    uint32_t *v;
    uint32_t n, cap;
} u32_list;

static int
u32_push(u32_list *l, uint32_t x)
{
    if (l->n == l->cap)
    {
        uint32_t cap;
        uint32_t *v;

        if (l->cap >= 0x7fffffffu)
            return PGR_ERR_NOMEM;
        cap = l->cap ? l->cap * 2 : 64;
        v = (uint32_t *)realloc(l->v, (size_t)cap * sizeof(uint32_t));
        if (v == NULL)
            return PGR_ERR_NOMEM;
        l->v = v;
        l->cap = cap;
    }
    l->v[l->n++] = x;
    return PGR_OK;
}

/* The name nanoPRC's reader gives a product or part that has none stored:
   the API has no flag for "no name", so this exact text is taken as none,
   so that an unnamed glTF node comes back unnamed. */
#define NANOPRC_UNNAMED "Unnamed Product"

static const char *
stored_name(const char *name)
{
    return (name != NULL && strcmp(name, NANOPRC_UNNAMED) != 0) ? name : NULL;
}

/* A mesh made from a list of stored tessellations, kept so that another
   product drawing the same list shares it. */
typedef struct
{
    uint32_t *stored;           /* biased_tess_index of each, in order */
    uint32_t count;
    int mesh;
} mesh_key;

typedef struct
{
    prc_context *ctx;
    prc_api_data data;
    prc_api_product *tree;
    prc_api_tess *tess;
    uint32_t nt;
    pgr_scene *s;
    pgr_messages *msg;
    pgr_prc_read_stats *st;
    mesh_key *keys;
    size_t n_keys, cap_keys;
} reader;

/* Add the triangles one primitive draws over a buffer of nv vertices,
   offset by base, to out. */
static int
add_triangles(const prc_api_graphic_primitive *gp, uint32_t nv, uint32_t base, u32_list *out)
{
    size_t m;
    int st = PGR_OK;

    for (m = 0; m < gp->num_indices; m++)
        if (gp->indices[m] >= nv)
            return PGR_ERR_PARSE;
    if (gp->type == PRC_API_TRIANGLES)
    {
        for (m = 0; m + 2 < gp->num_indices && st == PGR_OK; m += 3)
        {
            st = u32_push(out, base + gp->indices[m]);
            if (st == PGR_OK)
                st = u32_push(out, base + gp->indices[m + 1]);
            if (st == PGR_OK)
                st = u32_push(out, base + gp->indices[m + 2]);
        }
    }
    else if (gp->type == PRC_API_STRIP || gp->type == PRC_API_FAN)
    {
        for (m = 2; m < gp->num_indices && st == PGR_OK; m++)
        {
            uint32_t a, b, c = gp->indices[m];

            if (gp->type == PRC_API_FAN)
            {
                a = gp->indices[0];
                b = gp->indices[m - 1];
            }
            else if (m % 2 == 0)
            {
                a = gp->indices[m - 2];
                b = gp->indices[m - 1];
            }
            else
            {
                a = gp->indices[m - 1];
                b = gp->indices[m - 2];
            }
            st = u32_push(out, base + a);
            if (st == PGR_OK)
                st = u32_push(out, base + b);
            if (st == PGR_OK)
                st = u32_push(out, base + c);
        }
    }
    return st;
}

/* Append a vertex buffer's positions, and normals while every vertex so
   far has one, to the primitive being built. */
static int
add_vertices(const prc_api_tess_vertex_buffer *b, float **pos, float **nrm, int *all_normals,
             uint32_t *count)
{
    size_t n = b->num_vertices, i;
    size_t total = (size_t)*count + n;
    float *p, *q;
    int k;

    if (total > 0xffffffffu)
        return PGR_ERR_PARSE;
    p = (float *)realloc(*pos, (total ? total : 1) * 3 * sizeof(float));
    if (p == NULL)
        return PGR_ERR_NOMEM;
    *pos = p;
    q = (float *)realloc(*nrm, (total ? total : 1) * 3 * sizeof(float));
    if (q == NULL)
        return PGR_ERR_NOMEM;
    *nrm = q;
    for (i = 0; i < n; i++)
    {
        const prc_api_vertex *v = &b->vertices[i];

        for (k = 0; k < 3; k++)
        {
            p[3 * (*count + i) + k] = (float)v->position[k];
            q[3 * (*count + i) + k] = (float)v->normal[k];
        }
        if (!v->normal_set)
            *all_normals = 0;
    }
    *count = (uint32_t)total;
    return PGR_OK;
}

/* One surface tessellation as a triangle primitive of mesh. */
static int
add_surface(reader *r, uint32_t k, pgr_mesh *mesh)
{
    prc_api_tess *t = &r->tess[k];
    int compressed = t->type == PRC_API_TESS_3D_Compressed;
    size_t faces = compressed ? (t->num_faces > 0 ? 1 : 0) : t->num_faces;
    float *pos = NULL, *nrm = NULL;
    int all_normals = 1;
    uint32_t count = 0;
    u32_list idx = { NULL, 0, 0 };
    size_t f, j;
    int st = PGR_OK;
    pgr_primitive *p;

    if (compressed)
        st = add_vertices(&t->tess_vertices, &pos, &nrm, &all_normals, &count);
    for (f = 0; f < faces && st == PGR_OK; f++)
    {
        const prc_api_face *face = &t->tess_faces[f];
        uint32_t base = count, nv;

        if (face->disable_face)
            continue;
        if (!compressed)
            st = add_vertices(&face->face_vertices, &pos, &nrm, &all_normals, &count);
        nv = compressed ? count : count - base;
        if (compressed)
            base = 0;
        for (j = 0; j < face->num_graphic_primitives && st == PGR_OK; j++)
        {
            prc_api_graphic_primitive gp;

            if (prc_api_get_graphics_primitive(r->ctx, r->data, t, (uint32_t)f, j, &gp) < 0)
                continue;
            st = add_triangles(&gp, nv, base, &idx);
        }
    }
    if (st == PGR_OK && idx.n > 0)
    {
        if (pgr_mesh_add_primitive(mesh, PGR_TRIANGLES, &p) < 0)
            st = PGR_ERR_NOMEM;
        else
        {
            p->num_vertices = count;
            p->positions = pos;
            p->normals = all_normals && count > 0 ? nrm : NULL;
            p->num_indices = idx.n;
            p->indices = idx.v;
            if (p->normals == NULL)
                free(nrm);
            pos = nrm = NULL;
            idx.v = NULL;
            r->st->primitives++;
            r->st->triangles += p->num_indices / 3;
        }
    }
    if (st == PGR_ERR_PARSE)
        pgr_error(r->msg, "tessellation %u has an index past its vertices", (unsigned)k);
    free(pos);
    free(nrm);
    free(idx.v);
    return st;
}

/* One wire tessellation as a primitive of line segments. */
static int
add_wire(reader *r, uint32_t k, pgr_mesh *mesh)
{
    prc_api_tess *t = &r->tess[k];
    const prc_api_tess_vertex_buffer *b = &t->tess_vertices;
    float *pos = NULL, *nrm = NULL;
    int all_normals = 1;
    uint32_t count = 0, nv;
    u32_list idx = { NULL, 0, 0 };
    size_t j, m;
    int st;
    pgr_primitive *p;

    st = add_vertices(b, &pos, &nrm, &all_normals, &count);
    free(nrm);
    nrm = NULL;
    nv = count;
    for (j = 0; j < t->num_line_primitives && st == PGR_OK; j++)
    {
        prc_api_graphic_primitive gp;

        if (prc_api_get_graphics_primitive(r->ctx, r->data, t, 0, j, &gp) < 0)
            continue;
        for (m = 0; m < gp.num_indices; m++)
            if (gp.indices[m] >= nv)
                st = PGR_ERR_PARSE;
        if (st != PGR_OK)
            break;
        if (gp.type == PRC_API_LINE)
        {
            for (m = 0; m + 1 < gp.num_indices && st == PGR_OK; m += 2)
                if ((st = u32_push(&idx, gp.indices[m])) == PGR_OK)
                    st = u32_push(&idx, gp.indices[m + 1]);
        }
        else if (gp.type == PRC_API_LINE_STRIP || gp.type == PRC_API_LINE_LOOP)
        {
            for (m = 0; m + 1 < gp.num_indices && st == PGR_OK; m++)
                if ((st = u32_push(&idx, gp.indices[m])) == PGR_OK)
                    st = u32_push(&idx, gp.indices[m + 1]);
            if (gp.type == PRC_API_LINE_LOOP && gp.num_indices >= 2 && st == PGR_OK)
                if ((st = u32_push(&idx, gp.indices[gp.num_indices - 1])) == PGR_OK)
                    st = u32_push(&idx, gp.indices[0]);
        }
    }
    if (st == PGR_OK && idx.n > 0)
    {
        if (pgr_mesh_add_primitive(mesh, PGR_LINES, &p) < 0)
            st = PGR_ERR_NOMEM;
        else
        {
            p->num_vertices = count;
            p->positions = pos;
            p->num_indices = idx.n;
            p->indices = idx.v;
            pos = NULL;
            idx.v = NULL;
            r->st->primitives++;
            r->st->segments += p->num_indices / 2;
        }
    }
    if (st == PGR_ERR_PARSE)
        pgr_error(r->msg, "wire tessellation %u has an index past its vertices", (unsigned)k);
    free(pos);
    free(idx.v);
    return st;
}

/* The tessellations a part and its nested items draw: API index and stored
   index, depth first, as stl_export visits them. */
static int
part_tessellations(const reader *r, const prc_api_part *part, u32_list *api, u32_list *stored)
{
    const prc_api_part **stack;
    size_t depth = 0, cap = 16, i;
    int st = PGR_OK;

    stack = (const prc_api_part **)malloc(cap * sizeof(*stack));
    if (stack == NULL)
        return PGR_ERR_NOMEM;
    stack[depth++] = part;
    while (depth > 0 && st == PGR_OK)
    {
        const prc_api_part *item = stack[--depth];

        if (item->biased_tess_index > 0 && item->part_detail_index < r->nt)
        {
            st = u32_push(api, item->part_detail_index);
            if (st == PGR_OK)
                st = u32_push(stored, item->biased_tess_index);
        }
        for (i = item->num_rep_items; i > 0 && st == PGR_OK; i--)
        {
            if (depth == cap)
            {
                const prc_api_part **bigger =
                    (const prc_api_part **)realloc((void *)stack, cap * 2 * sizeof(*stack));

                if (bigger == NULL)
                {
                    st = PGR_ERR_NOMEM;
                    break;
                }
                stack = bigger;
                cap *= 2;
            }
            stack[depth++] = &item->rep_items[i - 1];
        }
    }
    free((void *)stack);
    return st;
}

/* The mesh for a part: the one already made for the same stored
   tessellations, or a new one. -1 when the part draws nothing. */
static int
mesh_for_part(reader *r, const prc_api_part *part, int *mesh_out)
{
    u32_list api = { NULL, 0, 0 }, stored = { NULL, 0, 0 };
    size_t i;
    uint32_t j;
    int st, m = -1;

    *mesh_out = -1;
    st = part_tessellations(r, part, &api, &stored);
    if (st != PGR_OK || api.n == 0)
    {
        free(api.v);
        free(stored.v);
        return st;
    }
    for (i = 0; i < r->n_keys; i++)
        if (r->keys[i].count == stored.n
            && memcmp(r->keys[i].stored, stored.v, stored.n * sizeof(uint32_t)) == 0)
        {
            *mesh_out = r->keys[i].mesh;
            free(api.v);
            free(stored.v);
            return PGR_OK;
        }

    m = pgr_scene_add_mesh(r->s, stored_name(part->name), NULL);
    if (m < 0)
        st = PGR_ERR_NOMEM;
    for (j = 0; j < api.n && st == PGR_OK; j++)
    {
        prc_api_tess *t = &r->tess[api.v[j]];
        pgr_mesh *mesh = &r->s->meshes[m];

        if (t->type == PRC_API_TESS_3D || t->type == PRC_API_TESS_3D_Compressed)
            st = add_surface(r, api.v[j], mesh);
        else if (t->type == PRC_API_TESS_3D_Wire)
            st = add_wire(r, api.v[j], mesh);
        else
        {
            r->st->skipped++;
            pgr_warn_once(r->msg, PGR_WARN_EXTENSION,
                          "markups and exact geometry are not read; they are skipped");
        }
    }
    if (st == PGR_OK)
    {
        if (r->n_keys == r->cap_keys)
        {
            size_t cap = r->cap_keys ? r->cap_keys * 2 : 16;
            mesh_key *bigger = (mesh_key *)realloc(r->keys, cap * sizeof(mesh_key));

            if (bigger == NULL)
                st = PGR_ERR_NOMEM;
            else
            {
                r->keys = bigger;
                r->cap_keys = cap;
            }
        }
        if (st == PGR_OK)
        {
            r->keys[r->n_keys].stored = stored.v;
            r->keys[r->n_keys].count = stored.n;
            r->keys[r->n_keys].mesh = m;
            r->n_keys++;
            stored.v = NULL;
            r->st->meshes++;
            *mesh_out = m;
        }
    }
    free(api.v);
    free(stored.v);
    return st;
}

/* A product's own matrix: its location, or the identity. */
static void
location_of(const prc_api_product *p, double out[16])
{
    if (p->location_set && !p->location.is_identity)
        memcpy(out, p->location.matrix, 16 * sizeof(double));
    else
        pgr_mat4_identity(out);
}

static int
part_draws_nothing(const reader *r, const prc_api_part *part)
{
    u32_list api = { NULL, 0, 0 }, stored = { NULL, 0, 0 };
    int none;

    if (part == NULL)
        return 1;
    if (part_tessellations(r, part, &api, &stored) != PGR_OK)
        none = 0;
    else
        none = api.n == 0;
    free(api.v);
    free(stored.v);
    return none;
}

/* A product still to place: its parent node (-1 for a root), and whether
   it is placed under the conversion matrix (a root of the PRC) or as it
   stands (a child, or a root that was under pgr_prc_write's own root). */
typedef struct
{
    const prc_api_product *p;
    int parent;
    int convert;
} pending;

static int
walk_tree(reader *r, const double root_m[16], const double conv[16], double tol)
{
    pending *stack;
    size_t depth = 0, cap, c;
    const prc_api_product *top = r->tree;
    int st = PGR_OK;

    /* Room for every root at once; the walk grows it as needed after. */
    cap = top->num_children + 64;
    stack = (pending *)malloc(cap * sizeof(*stack));
    if (stack == NULL)
        return PGR_ERR_NOMEM;
    /* The API's root is the model; its children are the file's roots. */
    if (top->is_model)
    {
        for (c = top->num_children; c > 0; c--)
        {
            stack[depth].p = &top->children[c - 1];
            stack[depth].parent = -1;
            stack[depth++].convert = 1;
        }
    }
    else
    {
        stack[depth].p = top;
        stack[depth].parent = -1;
        stack[depth++].convert = 1;
    }

    while (depth > 0 && st == PGR_OK)
    {
        pending e = stack[--depth];
        double loc[16];
        int id, mesh = -1, keep_as_is = 0;
        pgr_node *n;

        location_of(e.p, loc);
        /* pgr_prc_write's own root: placed exactly by the conversion, with
           nothing of its own to draw. Leave it out; its children are the
           scene's roots, already in glTF's terms. */
        if (e.parent < 0 && e.convert && pgr_mat4_equal(loc, root_m, tol)
            && part_draws_nothing(r, e.p->part))
        {
            if (!r->st->collapsed_root && stored_name(e.p->name) != NULL)
            {
                free(r->s->model_name);
                r->s->model_name = pgr_strdup(e.p->name);
            }
            r->st->collapsed_root = 1;
            keep_as_is = 1;
        }
        if (!keep_as_is)
        {
            if (e.p->part != NULL)
                st = mesh_for_part(r, e.p->part, &mesh);
            if (st != PGR_OK)
                break;
            id = pgr_scene_add_node(r->s, stored_name(e.p->name), &n);
            if (id < 0)
            {
                st = PGR_ERR_NOMEM;
                break;
            }
            n->mesh = mesh;
            if (e.convert)
                pgr_mat4_mul(conv, loc, n->matrix);
            else
                memcpy(n->matrix, loc, sizeof(loc));
            n->has_matrix = !pgr_mat4_is_identity(n->matrix, 0.0);
            st = e.parent < 0 ? pgr_scene_add_root(r->s, (uint32_t)id)
                              : pgr_node_add_child(r->s, (uint32_t)e.parent, (uint32_t)id);
            if (st < 0)
                break;
            st = PGR_OK;
        }
        else
            id = -1;

        /* Children, pushed in reverse so they are visited in order. */
        for (c = e.p->num_children; c > 0; c--)
        {
            if (depth == cap)
            {
                pending *bigger = (pending *)realloc(stack, cap * 2 * sizeof(*stack));

                if (bigger == NULL)
                {
                    st = PGR_ERR_NOMEM;
                    break;
                }
                stack = bigger;
                cap *= 2;
            }
            stack[depth].p = &e.p->children[c - 1];
            stack[depth].parent = id;
            stack[depth++].convert = 0;
        }
    }
    free(stack);
    return st;
}

/* The file name without its folder or extension. */
static char *
file_stem(const char *path)
{
    const char *base = path, *p, *dot;
    size_t n;
    char *out;

    for (p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    dot = strrchr(base, '.');
    n = dot != NULL && dot != base ? (size_t)(dot - base) : strlen(base);
    out = (char *)malloc(n + 1);
    if (out != NULL)
    {
        memcpy(out, base, n);
        out[n] = '\0';
    }
    return out;
}

int
pgr_prc_read(const char *path, const pgr_prc_read_options *options, pgr_scene *s,
             pgr_prc_read_stats *stats)
{
    pgr_prc_read_stats st_local;
    pgr_prc_write_options wo;
    pgr_scene unit;
    reader r;
    double root_m[16], conv[16], tol;
    uint32_t np = 0, nprod = 0, nm = 0, nlt = 0, nx = 0, k;
    uint8_t has_lines = 0;
    size_t i, f;
    int st = PGR_OK;

    if (stats == NULL)
        stats = &st_local;
    memset(stats, 0, sizeof(*stats));
    if (path == NULL || s == NULL)
        return PGR_ERR_ARG;
    memset(&r, 0, sizeof(r));
    r.s = s;
    r.st = stats;
    r.msg = options != NULL ? options->messages : NULL;

    /* The matrix pgr_prc_write gives its root under the same options, and
       its inverse, which takes the PRC's units and axes to glTF's. */
    memset(&wo, 0, sizeof(wo));
    if (options != NULL)
    {
        wo.no_axis_convert = options->no_axis_convert;
        wo.units_per_metre = options->units_per_metre;
    }
    pgr_scene_init(&unit);
    pgr_prc_root_matrix(&unit, &wo, root_m);
    if (!pgr_mat4_invert(root_m, conv))
        return PGR_ERR_ARG;
    tol = 1e-9 * (fabs(root_m[0]) + fabs(root_m[6]) + fabs(root_m[9]) + fabs(root_m[5]));

    r.ctx = prc_api_new_context(NULL);
    if (r.ctx == NULL)
        return PGR_ERR_NOMEM;
    r.data = prc_api_open_contents(r.ctx, path);
    if (r.data == NULL)
    {
        pgr_error(r.msg, "%s: no PRC could be read", path);
        prc_api_release_context(r.ctx);
        return PGR_ERR_PARSE;
    }
    if (prc_api_prep_model_tree(r.ctx, r.data, &np, &nprod, &nm) != 0
        || prc_api_create_model_tree(r.ctx, r.data, &r.tree, np, nprod, nm) != 0
        || r.tree == NULL
        || prc_api_get_number_tessellations(r.ctx, r.data, r.tree, &r.nt, &nlt, &nx, 0) != 0)
    {
        pgr_error(r.msg, "%s: the model tree could not be read", path);
        prc_api_print_error_stack(r.ctx);
        st = PGR_ERR_PARSE;
    }

    if (st == PGR_OK)
    {
        r.tess = (prc_api_tess *)calloc(r.nt ? r.nt : 1, sizeof(prc_api_tess));
        if (r.tess == NULL)
            st = PGR_ERR_NOMEM;
    }
    for (k = 0; st == PGR_OK && k < r.nt; k++)
    {
        prc_api_tess *t = &r.tess[k];

        t->num_faces = prc_api_get_number_faces(r.ctx, r.data, k);
        t->tess_faces = (prc_api_face *)calloc(t->num_faces ? t->num_faces : 1, sizeof(prc_api_face));
        if (t->tess_faces == NULL)
        {
            st = PGR_ERR_NOMEM;
            break;
        }
        if (prc_api_initialize_tessellation(r.ctx, r.data, r.tree, k, t, NULL, &has_lines) < 0)
        {
            t->type = PRC_API_TESS_UNKNOWN;
            continue;
        }
        if (t->type == PRC_API_TESS_3D_Wire)
        {
            if (prc_api_get_tessellation_vertices(r.ctx, r.data, k, 0, NULL, t) < 0)
                t->type = PRC_API_TESS_UNKNOWN;
        }
        else if (t->type == PRC_API_TESS_3D || t->type == PRC_API_TESS_3D_Compressed)
        {
            for (f = 0; f < t->num_faces; f++)
                if (prc_api_get_tessellation_vertices(r.ctx, r.data, k, (uint32_t)f, &t->tess_faces[f], t) < 0)
                {
                    pgr_error(r.msg, "%s: tessellation %u could not be read", path, (unsigned)k);
                    st = PGR_ERR_PARSE;
                    break;
                }
        }
    }

    if (st == PGR_OK)
        st = walk_tree(&r, root_m, conv, tol);
    if (st == PGR_OK && s->model_name == NULL)
    {
        s->model_name = file_stem(path);
        if (s->model_name == NULL)
            st = PGR_ERR_NOMEM;
    }
    s->up_axis = (options != NULL && options->no_axis_convert) ? PGR_Z_UP : PGR_Y_UP;
    s->unit_scale_to_m = 1.0;
    stats->nodes = s->num_nodes;

    if (r.data != NULL)
        prc_api_release_data(r.ctx, r.data, r.tess, r.tess != NULL ? r.nt : 0, NULL, 0, NULL, 0, r.tree);
    if (r.tess != NULL)
    {
        /* release_data frees what the reader allocated inside each
           tessellation, not the face arrays allocated here. */
        for (k = 0; k < r.nt; k++)
            free(r.tess[k].tess_faces);
        free(r.tess);
    }
    prc_api_release_context(r.ctx);
    for (i = 0; i < r.n_keys; i++)
        free(r.keys[i].stored);
    free(r.keys);
    if (st == PGR_OK && pgr_scene_check(s) != PGR_OK)
    {
        pgr_error(r.msg, "%s: the model tree is not a tree", path);
        st = PGR_ERR_PARSE;
    }
    return st;
}
