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

#include "pgr_scene.h"
#include "pgr_math.h"
#include "pgr_message.h"

#include <stdlib.h>
#include <string.h>

void
pgr_scene_init(pgr_scene *s)
{
    memset(s, 0, sizeof(*s));
    s->up_axis = PGR_Y_UP;
    s->unit_scale_to_m = 1.0;
}

static void
free_primitive(pgr_primitive *p)
{
    free(p->positions);
    free(p->normals);
    free(p->uvs);
    free(p->colors);
    free(p->indices);
}

void
pgr_scene_free(pgr_scene *s)
{
    uint32_t i, j;

    if (s == NULL)
        return;
    for (i = 0; i < s->num_nodes; i++)
    {
        free(s->nodes[i].name);
        free(s->nodes[i].children);
    }
    for (i = 0; i < s->num_meshes; i++)
    {
        for (j = 0; j < s->meshes[i].num_primitives; j++)
            free_primitive(&s->meshes[i].primitives[j]);
        free(s->meshes[i].primitives);
        free(s->meshes[i].name);
    }
    for (i = 0; i < s->num_materials; i++)
        free(s->materials[i].name);
    for (i = 0; i < s->num_textures; i++)
    {
        free(s->textures[i].name);
        free(s->textures[i].bytes);
    }
    free(s->nodes);
    free(s->roots);
    free(s->meshes);
    free(s->materials);
    free(s->textures);
    free(s->model_name);
    pgr_scene_init(s);
}

char *
pgr_strdup(const char *str)
{
    size_t n;
    char *copy;

    if (str == NULL)
        return NULL;
    n = strlen(str) + 1;
    copy = (char *)malloc(n);
    if (copy != NULL)
        memcpy(copy, str, n);
    return copy;
}

/* Grow *array of *count elements of size elem by one zeroed element; the
   new element's index, or -1 when memory runs out. */
static int
grow(void **array, uint32_t *count, size_t elem)
{
    void *bigger;

    if (*count >= 0x7fffffffu)
        return -1;
    bigger = realloc(*array, (*count + 1) * elem);
    if (bigger == NULL)
        return -1;
    memset((char *)bigger + *count * elem, 0, elem);
    *array = bigger;
    return (int)(*count)++;
}

/* Name a new element; a NULL name stays NULL. */
static int
set_name(char **dst, const char *name)
{
    if (name == NULL)
        return PGR_OK;
    *dst = pgr_strdup(name);
    return *dst != NULL ? PGR_OK : PGR_ERR_NOMEM;
}

int
pgr_scene_add_node(pgr_scene *s, const char *name, pgr_node **out)
{
    void *arr = s->nodes;
    int i = grow(&arr, &s->num_nodes, sizeof(pgr_node));

    s->nodes = (pgr_node *)arr;
    if (i < 0)
        return PGR_ERR_NOMEM;
    pgr_mat4_identity(s->nodes[i].matrix);
    s->nodes[i].mesh = -1;
    if (set_name(&s->nodes[i].name, name) != PGR_OK)
        return PGR_ERR_NOMEM;
    if (out != NULL)
        *out = &s->nodes[i];
    return i;
}

int
pgr_scene_add_mesh(pgr_scene *s, const char *name, pgr_mesh **out)
{
    void *arr = s->meshes;
    int i = grow(&arr, &s->num_meshes, sizeof(pgr_mesh));

    s->meshes = (pgr_mesh *)arr;
    if (i < 0)
        return PGR_ERR_NOMEM;
    if (set_name(&s->meshes[i].name, name) != PGR_OK)
        return PGR_ERR_NOMEM;
    if (out != NULL)
        *out = &s->meshes[i];
    return i;
}

int
pgr_scene_add_material(pgr_scene *s, const char *name, pgr_material **out)
{
    void *arr = s->materials;
    int i = grow(&arr, &s->num_materials, sizeof(pgr_material));
    pgr_material *m;

    s->materials = (pgr_material *)arr;
    if (i < 0)
        return PGR_ERR_NOMEM;
    m = &s->materials[i];
    /* glTF's defaults for a material that says nothing. */
    m->base_color[0] = m->base_color[1] = m->base_color[2] = m->base_color[3] = 1.0f;
    m->metallic = 1.0f;
    m->roughness = 1.0f;
    m->texture = -1;
    m->alpha_mode = PGR_ALPHA_OPAQUE;
    m->alpha_cutoff = 0.5f;
    if (set_name(&m->name, name) != PGR_OK)
        return PGR_ERR_NOMEM;
    if (out != NULL)
        *out = m;
    return i;
}

int
pgr_scene_add_texture(pgr_scene *s, const char *name, pgr_texture **out)
{
    void *arr = s->textures;
    int i = grow(&arr, &s->num_textures, sizeof(pgr_texture));
    pgr_texture *t;

    s->textures = (pgr_texture *)arr;
    if (i < 0)
        return PGR_ERR_NOMEM;
    t = &s->textures[i];
    t->wrap_s = t->wrap_t = PGR_WRAP_REPEAT;
    t->uv_scale[0] = t->uv_scale[1] = 1.0f;
    if (set_name(&t->name, name) != PGR_OK)
        return PGR_ERR_NOMEM;
    if (out != NULL)
        *out = t;
    return i;
}

int
pgr_mesh_add_primitive(pgr_mesh *m, int mode, pgr_primitive **out)
{
    void *arr = m->primitives;
    int i = grow(&arr, &m->num_primitives, sizeof(pgr_primitive));

    m->primitives = (pgr_primitive *)arr;
    if (i < 0)
        return PGR_ERR_NOMEM;
    m->primitives[i].mode = mode;
    m->primitives[i].material = -1;
    if (out != NULL)
        *out = &m->primitives[i];
    return i;
}

static int
append_index(uint32_t **array, uint32_t *count, uint32_t value)
{
    void *arr = *array;
    int i = grow(&arr, count, sizeof(uint32_t));

    *array = (uint32_t *)arr;
    if (i < 0)
        return PGR_ERR_NOMEM;
    (*array)[i] = value;
    return i;
}

int
pgr_node_add_child(pgr_scene *s, uint32_t parent, uint32_t child)
{
    if (parent >= s->num_nodes || child >= s->num_nodes)
        return PGR_ERR_ARG;
    return append_index(&s->nodes[parent].children, &s->nodes[parent].num_children, child);
}

int
pgr_scene_add_root(pgr_scene *s, uint32_t node)
{
    if (node >= s->num_nodes)
        return PGR_ERR_ARG;
    return append_index(&s->roots, &s->num_roots, node);
}

int
pgr_primitive_alloc(pgr_primitive *p, uint32_t num_vertices, uint32_t num_indices,
                    int with_normals, int with_uvs, int with_colors)
{
    size_t nv = num_vertices ? num_vertices : 1;
    size_t ni = num_indices ? num_indices : 1;

    p->num_vertices = num_vertices;
    p->num_indices = num_indices;
    p->positions = (float *)calloc(nv * 3, sizeof(float));
    p->normals = with_normals ? (float *)calloc(nv * 3, sizeof(float)) : NULL;
    p->uvs = with_uvs ? (float *)calloc(nv * 2, sizeof(float)) : NULL;
    p->colors = with_colors ? (float *)calloc(nv * 4, sizeof(float)) : NULL;
    p->indices = (uint32_t *)calloc(ni, sizeof(uint32_t));
    if (p->positions == NULL || p->indices == NULL
        || (with_normals && p->normals == NULL)
        || (with_uvs && p->uvs == NULL)
        || (with_colors && p->colors == NULL))
        return PGR_ERR_NOMEM;
    return PGR_OK;
}

int
pgr_scene_check(const pgr_scene *s)
{
    uint32_t *parents;
    uint32_t i, j, k;
    int status = PGR_OK;

    for (i = 0; i < s->num_meshes; i++)
    {
        for (j = 0; j < s->meshes[i].num_primitives; j++)
        {
            const pgr_primitive *p = &s->meshes[i].primitives[j];
            uint32_t per = p->mode == PGR_LINES ? 2 : 3;

            if (p->mode != PGR_TRIANGLES && p->mode != PGR_LINES)
                return PGR_ERR_ARG;
            if (p->num_indices % per != 0)
                return PGR_ERR_ARG;
            if (p->num_vertices > 0 && p->positions == NULL)
                return PGR_ERR_ARG;
            if (p->num_indices > 0 && p->indices == NULL)
                return PGR_ERR_ARG;
            for (k = 0; k < p->num_indices; k++)
                if (p->indices[k] >= p->num_vertices)
                    return PGR_ERR_ARG;
            if (p->material < -1 || (p->material >= 0 && (uint32_t)p->material >= s->num_materials))
                return PGR_ERR_ARG;
        }
    }
    for (i = 0; i < s->num_materials; i++)
        if (s->materials[i].texture < -1
            || (s->materials[i].texture >= 0 && (uint32_t)s->materials[i].texture >= s->num_textures))
            return PGR_ERR_ARG;

    /* parents[n] is n's parent plus one, or 0 for none. */
    parents = (uint32_t *)calloc(s->num_nodes ? s->num_nodes : 1, sizeof(uint32_t));
    if (parents == NULL)
        return PGR_ERR_NOMEM;
    for (i = 0; i < s->num_nodes && status == PGR_OK; i++)
    {
        const pgr_node *n = &s->nodes[i];

        if (n->mesh < -1 || (n->mesh >= 0 && (uint32_t)n->mesh >= s->num_meshes))
            status = PGR_ERR_ARG;
        for (j = 0; j < n->num_children && status == PGR_OK; j++)
        {
            uint32_t c = n->children[j];

            if (c >= s->num_nodes || parents[c] != 0 || c == i)
                status = PGR_ERR_ARG;
            else
                parents[c] = i + 1;
        }
    }
    for (i = 0; i < s->num_roots && status == PGR_OK; i++)
        if (s->roots[i] >= s->num_nodes || parents[s->roots[i]] != 0)
            status = PGR_ERR_ARG;
    /* With one parent each, a cycle is a walk up the parents that does not
       end within num_nodes steps. */
    for (i = 0; i < s->num_nodes && status == PGR_OK; i++)
    {
        uint32_t n = i, steps = 0;

        while (parents[n] != 0 && status == PGR_OK)
        {
            n = parents[n] - 1;
            if (++steps > s->num_nodes)
                status = PGR_ERR_ARG;
        }
    }
    free(parents);
    return status;
}

int
pgr_primitive_bounds(const pgr_primitive *p, float min[3], float max[3])
{
    uint32_t i;
    int k;

    if (p->num_vertices == 0 || p->positions == NULL)
        return 0;
    for (k = 0; k < 3; k++)
        min[k] = max[k] = p->positions[k];
    for (i = 1; i < p->num_vertices; i++)
    {
        for (k = 0; k < 3; k++)
        {
            float v = p->positions[3 * i + k];

            if (v < min[k])
                min[k] = v;
            if (v > max[k])
                max[k] = v;
        }
    }
    return 1;
}

void
pgr_scene_get_stats(const pgr_scene *s, pgr_scene_stats *st)
{
    uint32_t i, j;

    memset(st, 0, sizeof(*st));
    st->nodes = s->num_nodes;
    st->meshes = s->num_meshes;
    for (i = 0; i < s->num_meshes; i++)
    {
        st->primitives += s->meshes[i].num_primitives;
        for (j = 0; j < s->meshes[i].num_primitives; j++)
        {
            const pgr_primitive *p = &s->meshes[i].primitives[j];

            st->vertices += p->num_vertices;
            if (p->mode == PGR_LINES)
                st->segments += p->num_indices / 2;
            else
                st->triangles += p->num_indices / 3;
        }
    }
}
