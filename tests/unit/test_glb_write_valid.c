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

/* pgr_glb_write: what it writes parses and validates in cgltf, has a
   well-formed GLB container, 4-byte aligned buffer views and correct
   position bounds, uses 16-bit indices exactly up to 65535 vertices, and
   reads back through pgr_glb_read as the scene that was written. */

#include "pgr_glb.h"
#include "pgr_math.h"
#include "pgr_test.h"

#include "cgltf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void
quiet(void *user, int is_error, const char *text)
{
    (void)user;
    if (is_error)
        fprintf(stderr, "error: %s\n", text);
}

/* A scene with the features the writer handles: a flat cube with normals,
   texture coordinates and colours and a material, drawn by two nodes; a
   one-triangle mesh; a line primitive in a third mesh; a three-level
   hierarchy with matrices. */
static void
build_scene(pgr_scene *s)
{
    static const int quad[6] = { 0, 1, 2, 0, 2, 3 };
    static const float cu[4] = { -1, 1, 1, -1 }, cw[4] = { -1, -1, 1, 1 };
    pgr_mesh *m;
    pgr_primitive *p;
    pgr_material *mat;
    pgr_node *n;
    const double t[3] = { 1, 2, 3 }, q[4] = { 0, 0, 0.38268343, 0.92387953 }, sc[3] = { 1, 2, 3 };
    int face, c, k, v = 0;

    pgr_scene_init(s);
    s->model_name = pgr_strdup("written");

    pgr_scene_add_material(s, "paint", &mat);
    mat->base_color[0] = 0.25f;
    mat->base_color[1] = 0.5f;
    mat->base_color[2] = 0.75f;
    mat->base_color[3] = 0.5f;
    mat->metallic = 0.0f;
    mat->roughness = 0.4f;
    mat->emissive[1] = 0.125f;
    mat->alpha_mode = PGR_ALPHA_BLEND;
    mat->double_sided = 1;

    pgr_scene_add_mesh(s, "cube", &m);
    pgr_mesh_add_primitive(m, PGR_TRIANGLES, &p);
    pgr_primitive_alloc(p, 24, 36, 1, 1, 1);
    p->material = 0;
    for (face = 0; face < 6; face++)
    {
        int a = face / 2;
        float sg = face % 2 == 0 ? 1.0f : -1.0f;
        float nn[3] = { 0, 0, 0 }, u[3] = { 0, 0, 0 }, w[3] = { 0, 0, 0 };

        nn[a] = sg;
        u[(a + 1) % 3] = 1.0f;
        w[(a + 2) % 3] = 1.0f;
        for (c = 0; c < 4; c++)
        {
            for (k = 0; k < 3; k++)
            {
                p->positions[3 * (v + c) + k] = 0.5f * (nn[k] + cu[c] * u[k] + cw[c] * w[k]) + 10.0f;
                p->normals[3 * (v + c) + k] = nn[k];
            }
            p->uvs[2 * (v + c)] = cu[c] > 0 ? 1.0f : 0.0f;
            p->uvs[2 * (v + c) + 1] = cw[c] > 0 ? 1.0f : 0.0f;
            for (k = 0; k < 4; k++)
                p->colors[4 * (v + c) + k] = (float)((face + k) % 5) / 4.0f;
        }
        for (c = 0; c < 6; c++)
            p->indices[6 * face + c] = (uint32_t)(v + quad[c]);
        v += 4;
    }

    /* One triangle: its 6 bytes of 16-bit indices leave the next buffer
       view to be padded to a 4-byte boundary. */
    pgr_scene_add_mesh(s, "single", &m);
    pgr_mesh_add_primitive(m, PGR_TRIANGLES, &p);
    pgr_primitive_alloc(p, 3, 3, 0, 0, 0);
    p->positions[3] = 1.0f;
    p->positions[7] = 1.0f;
    p->positions[8] = -2.0f;
    p->indices[0] = 0;
    p->indices[1] = 1;
    p->indices[2] = 2;

    pgr_scene_add_mesh(s, "edge", &m);
    pgr_mesh_add_primitive(m, PGR_LINES, &p);
    pgr_primitive_alloc(p, 3, 4, 0, 0, 0);
    p->positions[3] = 1.0f;
    p->positions[7] = 1.0f;
    p->indices[0] = 0;
    p->indices[1] = 1;
    p->indices[2] = 1;
    p->indices[3] = 2;

    /* A name a model file could carry: quotes, a backslash and a control
       character must come back as they went in, not break the JSON. */
    pgr_scene_add_node(s, "root \"quoted\" C:\\path\tand\nlines", &n);
    pgr_mat4_from_trs(t, q, sc, n->matrix);
    n->has_matrix = 1;
    pgr_scene_add_node(s, "first cube", &n);
    n->mesh = 0;
    pgr_scene_add_node(s, "second cube", &n);
    n->mesh = 0;
    n->matrix[12] = 5.0;
    n->has_matrix = 1;
    pgr_scene_add_node(s, NULL, &n);
    n->mesh = 2;
    pgr_scene_add_node(s, "single", &n);
    n->mesh = 1;
    pgr_node_add_child(s, 0, 1);
    pgr_node_add_child(s, 0, 2);
    pgr_node_add_child(s, 2, 3);
    pgr_node_add_child(s, 0, 4);
    pgr_scene_add_root(s, 0);
}

static unsigned
u32_at(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

/* The GLB header and chunk headers. */
static void
check_container(const char *path, int expect_bin)
{
    FILE *f = fopen(path, "rb");
    unsigned char *d;
    long size;
    unsigned json_len;

    PGR_CHECK(f != NULL);
    if (f == NULL)
        return;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = (unsigned char *)malloc((size_t)size);
    PGR_CHECK(d != NULL && fread(d, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    if (d == NULL)
        return;
    PGR_CHECK(size >= 20 && size % 4 == 0);
    PGR_CHECK(u32_at(d) == 0x46546C67u && u32_at(d + 4) == 2 && u32_at(d + 8) == (unsigned)size);
    json_len = u32_at(d + 12);
    PGR_CHECK(u32_at(d + 16) == 0x4E4F534Au && json_len % 4 == 0);
    if (expect_bin)
    {
        const unsigned char *b = d + 20 + json_len;

        PGR_CHECK((long)(20 + json_len + 8) <= size);
        PGR_CHECK(u32_at(b + 4) == 0x004E4942u && u32_at(b) % 4 == 0);
        PGR_CHECK((long)(20 + json_len + 8 + u32_at(b)) == size);
    }
    else
        PGR_CHECK((long)(20 + json_len) == size);
    free(d);
}

/* Parse with cgltf alone and check what glTF requires of the layout. */
static void
check_with_cgltf(const char *path, const pgr_scene *s)
{
    cgltf_options opt;
    cgltf_data *data = NULL;
    cgltf_size i;

    memset(&opt, 0, sizeof(opt));
    PGR_CHECK(cgltf_parse_file(&opt, path, &data) == cgltf_result_success);
    if (data == NULL)
        return;
    PGR_CHECK(cgltf_load_buffers(&opt, data, path) == cgltf_result_success);
    PGR_CHECK(cgltf_validate(data) == cgltf_result_success);
    PGR_CHECK(data->scene != NULL && data->scenes_count == 1);
    PGR_CHECK_STR(data->asset.version, "2.0");
    PGR_CHECK(data->asset.generator != NULL && strncmp(data->asset.generator, "prc_glb_rw ", 11) == 0);
    for (i = 0; i < data->buffer_views_count; i++)
        PGR_CHECK(data->buffer_views[i].offset % 4 == 0);
    PGR_CHECK(data->meshes_count == s->num_meshes);
    for (i = 0; i < data->meshes_count; i++)
    {
        const cgltf_primitive *cp = &data->meshes[i].primitives[0];
        cgltf_size a;

        for (a = 0; a < cp->attributes_count; a++)
        {
            const cgltf_accessor *acc = cp->attributes[a].data;

            if (cp->attributes[a].type == cgltf_attribute_type_position)
            {
                float mn[3], mx[3];

                pgr_primitive_bounds(&s->meshes[i].primitives[0], mn, mx);
                PGR_CHECK(acc->has_min && acc->has_max);
                PGR_CHECK(acc->min[0] == mn[0] && acc->min[1] == mn[1] && acc->min[2] == mn[2]);
                PGR_CHECK(acc->max[0] == mx[0] && acc->max[1] == mx[1] && acc->max[2] == mx[2]);
            }
        }
        PGR_CHECK(cp->indices != NULL && cp->indices->component_type == cgltf_component_type_r_16u);
    }
    cgltf_free(data);
}

/* The scene read back is the scene written. */
static void
compare(const pgr_scene *a, const pgr_scene *b)
{
    uint32_t i, j;
    int k;

    PGR_CHECK(a->num_nodes == b->num_nodes && a->num_meshes == b->num_meshes);
    PGR_CHECK(a->num_materials == b->num_materials && a->num_roots == b->num_roots);
    if (a->num_nodes != b->num_nodes || a->num_meshes != b->num_meshes
        || a->num_materials != b->num_materials || a->num_roots != b->num_roots)
        return;
    PGR_CHECK_STR(a->model_name, b->model_name);
    for (i = 0; i < a->num_roots; i++)
        PGR_CHECK(a->roots[i] == b->roots[i]);
    for (i = 0; i < a->num_nodes; i++)
    {
        const pgr_node *x = &a->nodes[i], *y = &b->nodes[i];

        if (x->name != NULL || y->name != NULL)
            PGR_CHECK_STR(x->name, y->name);
        PGR_CHECK(x->mesh == y->mesh && x->num_children == y->num_children);
        for (j = 0; j < x->num_children && j < y->num_children; j++)
            PGR_CHECK(x->children[j] == y->children[j]);
        /* Matrices go through float. */
        PGR_CHECK(pgr_mat4_equal(x->matrix, y->matrix, 1e-6));
    }
    for (i = 0; i < a->num_materials; i++)
    {
        const pgr_material *x = &a->materials[i], *y = &b->materials[i];

        PGR_CHECK_STR(x->name, y->name);
        for (k = 0; k < 4; k++)
            PGR_CHECK(x->base_color[k] == y->base_color[k]);
        for (k = 0; k < 3; k++)
            PGR_CHECK(x->emissive[k] == y->emissive[k]);
        PGR_CHECK(x->metallic == y->metallic && x->roughness == y->roughness);
        PGR_CHECK(x->alpha_mode == y->alpha_mode && x->double_sided == y->double_sided);
    }
    for (i = 0; i < a->num_meshes; i++)
    {
        const pgr_mesh *mx = &a->meshes[i], *my = &b->meshes[i];

        PGR_CHECK_STR(mx->name, my->name);
        PGR_CHECK(mx->num_primitives == my->num_primitives);
        for (j = 0; j < mx->num_primitives && j < my->num_primitives; j++)
        {
            const pgr_primitive *x = &mx->primitives[j], *y = &my->primitives[j];
            uint32_t nv = x->num_vertices;

            PGR_CHECK(x->mode == y->mode && x->material == y->material);
            PGR_CHECK(nv == y->num_vertices && x->num_indices == y->num_indices);
            if (nv != y->num_vertices || x->num_indices != y->num_indices)
                continue;
            PGR_CHECK(memcmp(x->positions, y->positions, nv * 3 * sizeof(float)) == 0);
            PGR_CHECK(memcmp(x->indices, y->indices, x->num_indices * sizeof(uint32_t)) == 0);
            PGR_CHECK((x->normals == NULL) == (y->normals == NULL));
            PGR_CHECK((x->uvs == NULL) == (y->uvs == NULL));
            PGR_CHECK((x->colors == NULL) == (y->colors == NULL));
            if (x->normals != NULL && y->normals != NULL)
                PGR_CHECK(memcmp(x->normals, y->normals, nv * 3 * sizeof(float)) == 0);
            if (x->uvs != NULL && y->uvs != NULL)
                PGR_CHECK(memcmp(x->uvs, y->uvs, nv * 2 * sizeof(float)) == 0);
            if (x->colors != NULL && y->colors != NULL)
                PGR_CHECK(memcmp(x->colors, y->colors, nv * 4 * sizeof(float)) == 0);
        }
    }
}

static void
test_round_trip(const char *path, int glb)
{
    pgr_scene s, back;
    pgr_messages msg;
    pgr_glb_write_options wopt;
    pgr_glb_read_options ropt;

    memset(&msg, 0, sizeof(msg));
    msg.fn = quiet;
    wopt.messages = &msg;
    wopt.generator = NULL;
    ropt.messages = &msg;

    build_scene(&s);
    PGR_CHECK(pgr_glb_write(&s, path, &wopt) == PGR_OK);
    if (glb)
        check_container(path, 1);
    check_with_cgltf(path, &s);
    pgr_scene_init(&back);
    PGR_CHECK(pgr_glb_read(path, &ropt, &back) == PGR_OK);
    compare(&s, &back);
    PGR_CHECK(msg.errors == 0 && msg.warnings == 0);
    pgr_scene_free(&back);
    pgr_scene_free(&s);
}

/* 65535 vertices take 16-bit indices, 65536 take 32-bit, and the highest
   index survives either way. */
static void
test_index_width(uint32_t vertices, cgltf_component_type expect)
{
    pgr_scene s, back;
    pgr_mesh *m;
    pgr_primitive *p;
    pgr_node *n;
    cgltf_options opt;
    cgltf_data *data = NULL;
    uint32_t i;

    pgr_scene_init(&s);
    pgr_scene_add_mesh(&s, NULL, &m);
    pgr_mesh_add_primitive(m, PGR_TRIANGLES, &p);
    pgr_primitive_alloc(p, vertices, 6, 0, 0, 0);
    for (i = 0; i < vertices; i++)
    {
        p->positions[3 * i] = (float)(i % 256);
        p->positions[3 * i + 1] = (float)(i / 256);
    }
    p->indices[0] = 0;
    p->indices[1] = 1;
    p->indices[2] = 256;
    p->indices[3] = vertices - 1;
    p->indices[4] = vertices - 2;
    p->indices[5] = vertices - 257;
    pgr_scene_add_node(&s, NULL, &n);
    n->mesh = 0;
    pgr_scene_add_root(&s, 0);

    PGR_CHECK(pgr_glb_write(&s, "index_width.glb", NULL) == PGR_OK);
    memset(&opt, 0, sizeof(opt));
    PGR_CHECK(cgltf_parse_file(&opt, "index_width.glb", &data) == cgltf_result_success);
    if (data != NULL)
    {
        PGR_CHECK(cgltf_load_buffers(&opt, data, "index_width.glb") == cgltf_result_success);
        PGR_CHECK(cgltf_validate(data) == cgltf_result_success);
        PGR_CHECK(data->meshes[0].primitives[0].indices->component_type == expect);
        cgltf_free(data);
    }
    pgr_scene_init(&back);
    PGR_CHECK(pgr_glb_read("index_width.glb", NULL, &back) == PGR_OK);
    if (back.num_meshes == 1 && back.meshes[0].num_primitives == 1)
        PGR_CHECK(memcmp(back.meshes[0].primitives[0].indices, p->indices, 6 * sizeof(uint32_t)) == 0);
    else
        PGR_CHECK(0);
    pgr_scene_free(&back);
    pgr_scene_free(&s);
}

static void
test_edge_cases(void)
{
    pgr_scene s, back;
    pgr_node *n;
    pgr_messages msg;
    pgr_glb_write_options wopt;

    memset(&msg, 0, sizeof(msg));
    msg.fn = quiet;
    wopt.messages = &msg;
    wopt.generator = "custom generator";

    /* A scene with one empty node: no buffer at all. */
    pgr_scene_init(&s);
    pgr_scene_add_node(&s, "lonely", &n);
    pgr_scene_add_root(&s, 0);
    PGR_CHECK(pgr_glb_write(&s, "empty.glb", &wopt) == PGR_OK);
    check_container("empty.glb", 0);
    pgr_scene_init(&back);
    PGR_CHECK(pgr_glb_read("empty.glb", NULL, &back) == PGR_OK);
    PGR_CHECK(back.num_nodes == 1 && back.num_meshes == 0 && back.num_roots == 1);
    pgr_scene_free(&back);

    /* An inconsistent scene is refused, not written. */
    pgr_node_add_child(&s, 0, 0);
    PGR_CHECK(pgr_glb_write(&s, "inconsistent.glb", &wopt) == PGR_ERR_ARG);
    pgr_scene_free(&s);

    /* A file that cannot be created. */
    pgr_scene_init(&s);
    pgr_scene_add_node(&s, NULL, NULL);
    pgr_scene_add_root(&s, 0);
    PGR_CHECK(pgr_glb_write(&s, "no_such_directory/out.glb", &wopt) == PGR_ERR_WRITE);
    pgr_scene_free(&s);
}

/* glTF's JSON must be UTF-8. A name already in UTF-8 is written as it is;
   one in Latin-1, as PRC files hold them, is converted; so is each byte of
   a sequence that is not well formed (here an encoded surrogate, and a
   sequence cut short by the end of the name). cgltf would accept the bytes
   either way, so the JSON is checked byte by byte through the names read
   back. */
static void
test_names_utf8(void)
{
    static const char *in[4] = { "St\xc3\xa4" "be", "St\xe4" "be", "\xed\xa0\x80", "end\xc3" };
    static const char *out[4] = { "St\xc3\xa4" "be", "St\xc3\xa4" "be", "\xc3\xad\xc2\xa0\xc2\x80", "end\xc3\x83" };
    pgr_scene s, back;
    pgr_node *n;
    uint32_t i;

    pgr_scene_init(&s);
    for (i = 0; i < 4; i++)
    {
        pgr_scene_add_node(&s, in[i], &n);
        pgr_scene_add_root(&s, i);
    }
    PGR_CHECK(pgr_glb_write(&s, "names.glb", NULL) == PGR_OK);
    pgr_scene_init(&back);
    PGR_CHECK(pgr_glb_read("names.glb", NULL, &back) == PGR_OK);
    PGR_CHECK(back.num_nodes == 4);
    for (i = 0; i < back.num_nodes && i < 4; i++)
        PGR_CHECK_STR(back.nodes[i].name, out[i]);
    pgr_scene_free(&back);
    pgr_scene_free(&s);
}

int
main(void)
{
    test_round_trip("written.glb", 1);
    test_round_trip("written.gltf", 0);
    test_index_width(65535, cgltf_component_type_r_16u);
    test_index_width(65536, cgltf_component_type_r_32u);
    test_edge_cases();
    test_names_utf8();
    return PGR_TEST_RESULT;
}
