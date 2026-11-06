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

/* pgr_glb_read on every fixture, each as .glb and as .gltf with a data
   URI: counts, values, the order of strip and fan triangles, the
   hierarchy's matrices, and what is skipped, refused or rejected. */

#include "fixtures.h"
#include "pgr_glb.h"
#include "pgr_math.h"
#include "pgr_test.h"

#include <math.h>
#include <stdio.h>

static const char *ext;      /* ".glb" or ".gltf" for this pass */

/* Messages are counted, not printed; the last one is kept. */
static void
collect(void *user, int is_error, const char *text)
{
    char *last = (char *)user;

    (void)is_error;
    snprintf(last, 512, "%s", text);
}

typedef struct
{
    pgr_scene scene;
    pgr_messages msg;
    char last[512];
    int status;
} result;

/* Make the fixture under name with this pass's extension, and read it. */
static void
load(result *r, const char *name, int (*make)(const char *))
{
    char path[256];
    pgr_glb_read_options opt;

    snprintf(path, sizeof(path), "read_%s%s", name, ext);
    memset(r, 0, sizeof(*r));
    pgr_scene_init(&r->scene);
    r->msg.fn = collect;
    r->msg.user = r->last;
    opt.messages = &r->msg;
    if (make(path) != 0)
    {
        fprintf(stderr, "could not write fixture %s\n", path);
        r->status = 99;
        PGR_CHECK(0);
        return;
    }
    r->status = pgr_glb_read(path, &opt, &r->scene);
}

static int
tri_is(const uint32_t *t, uint32_t a, uint32_t b, uint32_t c)
{
    return t[0] == a && t[1] == b && t[2] == c;
}

static int
make_meshopt(const char *path)
{
    return fx_requires(path, "EXT_meshopt_compression");
}

static int
make_draco(const char *path)
{
    return fx_requires(path, "KHR_draco_mesh_compression");
}

static int
make_unlit(const char *path)
{
    return fx_requires(path, "KHR_materials_unlit");
}

static void
test_cubes(void)
{
    result r;
    pgr_scene_stats st;
    const pgr_primitive *p;
    uint32_t i;

    load(&r, "cube_flat", fx_cube_flat);
    PGR_CHECK(r.status == PGR_OK);
    pgr_scene_get_stats(&r.scene, &st);
    PGR_CHECK(st.nodes == 1 && st.meshes == 1 && st.primitives == 1);
    PGR_CHECK(st.vertices == 24 && st.triangles == 12);
    PGR_CHECK(r.scene.num_roots == 1 && r.scene.roots[0] == 0);
    PGR_CHECK_STR(r.scene.nodes[0].name, "cube");
    PGR_CHECK_STR(r.scene.meshes[0].name, "cube_mesh");
    PGR_CHECK_STR(r.scene.model_name, "cube_flat");     /* the scene's name */
    PGR_CHECK(r.scene.nodes[0].mesh == 0 && !r.scene.nodes[0].has_matrix);
    p = &r.scene.meshes[0].primitives[0];
    PGR_CHECK(p->mode == PGR_TRIANGLES && p->normals != NULL && p->uvs == NULL && p->colors == NULL);
    PGR_CHECK(p->material == -1);
    for (i = 0; i < 24 && p->normals != NULL; i++)
    {
        const float *n = &p->normals[3 * i];

        PGR_CHECK(fabs(n[0] * n[0] + n[1] * n[1] + n[2] * n[2] - 1.0) < 1e-6);
    }
    PGR_CHECK(r.msg.warnings == 0 && r.msg.errors == 0);
    pgr_scene_free(&r.scene);

    load(&r, "cube_smooth", fx_cube_smooth);
    PGR_CHECK(r.status == PGR_OK);
    pgr_scene_get_stats(&r.scene, &st);
    PGR_CHECK(st.vertices == 8 && st.triangles == 12);
    p = &r.scene.meshes[0].primitives[0];
    PGR_CHECK(p->normals == NULL);
    /* Without a scene name, the model is named after the file. */
    PGR_CHECK_STR(r.scene.model_name, "read_cube_smooth");
    pgr_scene_free(&r.scene);
}

static void
test_colours(void)
{
    result r;
    const pgr_primitive *p;

    load(&r, "tri_vcolors", fx_tri_vcolors);
    PGR_CHECK(r.status == PGR_OK);
    p = &r.scene.meshes[0].primitives[0];
    PGR_CHECK(p->colors != NULL && p->num_indices == 3);
    if (p->colors != NULL)
    {
        PGR_CHECK(p->colors[0] == 1.0f && p->colors[3] == 1.0f);
        PGR_CHECK(p->colors[5] == 1.0f && p->colors[7] == 0.5f);
        PGR_CHECK(p->colors[10] == 1.0f);
    }
    /* No indices: the vertices in order. */
    PGR_CHECK(tri_is(p->indices, 0, 1, 2));
    pgr_scene_free(&r.scene);

    load(&r, "u8_colors", fx_u8_colors);
    PGR_CHECK(r.status == PGR_OK);
    p = &r.scene.meshes[0].primitives[0];
    PGR_CHECK(p->colors != NULL);
    if (p->colors != NULL)
    {
        /* Normalised bytes are value / 255; an RGB colour gains alpha 1. */
        PGR_CHECK(p->colors[0] == 1.0f && p->colors[1] == 0.0f && p->colors[3] == 1.0f);
        PGR_CHECK(fabs(p->colors[5] - 128.0 / 255.0) < 1e-7 && p->colors[7] == 1.0f);
        PGR_CHECK(p->colors[10] == 1.0f && p->colors[11] == 1.0f);
    }
    pgr_scene_free(&r.scene);
}

static void
test_nodes(void)
{
    result r;
    double expect[16];
    const double t[3] = { 1, 2, 3 }, q0[4] = { 0, 0, 0, 1 }, s1[3] = { 1, 1, 1 }, z3[3] = { 0, 0, 0 };
    const double qz[4] = { 0, 0, 0.70710678, 0.70710678 }, s2[3] = { 2, 2, 2 };

    load(&r, "two_instances", fx_two_instances);
    PGR_CHECK(r.status == PGR_OK);
    PGR_CHECK(r.scene.num_nodes == 2 && r.scene.num_meshes == 1 && r.scene.num_roots == 2);
    PGR_CHECK(r.scene.nodes[0].mesh == 0 && r.scene.nodes[1].mesh == 0);
    PGR_CHECK(!r.scene.nodes[0].has_matrix && r.scene.nodes[1].has_matrix);
    PGR_CHECK(r.scene.nodes[1].matrix[12] == 2.0 && r.scene.nodes[1].matrix[13] == 0.0);
    pgr_scene_free(&r.scene);

    load(&r, "hierarchy3", fx_hierarchy3);
    PGR_CHECK(r.status == PGR_OK);
    PGR_CHECK(r.scene.num_nodes == 4 && r.scene.num_roots == 1 && r.scene.roots[0] == 0);
    PGR_CHECK(r.scene.nodes[0].num_children == 2 && r.scene.nodes[0].children[0] == 1
              && r.scene.nodes[0].children[1] == 3);
    PGR_CHECK(r.scene.nodes[1].num_children == 1 && r.scene.nodes[1].children[0] == 2);
    PGR_CHECK(r.scene.nodes[2].mesh == 0 && r.scene.nodes[0].mesh == -1);
    pgr_mat4_from_trs(t, q0, s1, expect);
    PGR_CHECK(pgr_mat4_equal(r.scene.nodes[0].matrix, expect, 1e-6));
    pgr_mat4_from_trs(z3, qz, s1, expect);
    PGR_CHECK(pgr_mat4_equal(r.scene.nodes[1].matrix, expect, 1e-6));
    pgr_mat4_from_trs(z3, q0, s2, expect);
    PGR_CHECK(pgr_mat4_equal(r.scene.nodes[2].matrix, expect, 1e-6));
    PGR_CHECK(r.scene.nodes[3].matrix[12] == 4.0 && r.scene.nodes[3].matrix[13] == 5.0
              && r.scene.nodes[3].matrix[14] == 6.0);
    PGR_CHECK_STR(r.scene.nodes[3].name, "matrix_node");
    pgr_scene_free(&r.scene);

    load(&r, "no_scene", fx_no_scene);
    PGR_CHECK(r.status == PGR_OK);
    /* No scene: the node without a parent is the root. */
    PGR_CHECK(r.scene.num_roots == 1 && r.scene.roots[0] == 0);
    pgr_scene_free(&r.scene);
}

static void
test_materials(void)
{
    result r;
    const pgr_material *red, *blue;

    load(&r, "multi_material", fx_multi_material);
    PGR_CHECK(r.status == PGR_OK);
    PGR_CHECK(r.scene.num_materials == 2 && r.scene.meshes[0].num_primitives == 2);
    if (r.scene.num_materials == 2 && r.scene.meshes[0].num_primitives == 2)
    {
        red = &r.scene.materials[0];
        blue = &r.scene.materials[1];
        PGR_CHECK_STR(red->name, "red");
        PGR_CHECK(red->base_color[0] == 1.0f && red->base_color[2] == 0.0f && red->base_color[3] == 1.0f);
        PGR_CHECK(red->metallic == 0.0f && red->roughness == 0.25f);
        PGR_CHECK(red->alpha_mode == PGR_ALPHA_OPAQUE && !red->double_sided);
        PGR_CHECK(blue->base_color[2] == 1.0f && blue->base_color[3] == 0.5f);
        PGR_CHECK(blue->metallic == 1.0f && blue->roughness == 1.0f);   /* glTF's defaults */
        PGR_CHECK(blue->alpha_mode == PGR_ALPHA_BLEND && blue->double_sided);
        PGR_CHECK(blue->emissive[0] == 0.1f && blue->emissive[2] == 0.3f);
        PGR_CHECK(r.scene.meshes[0].primitives[0].material == 0);
        PGR_CHECK(r.scene.meshes[0].primitives[1].material == 1);
    }
    pgr_scene_free(&r.scene);
}

static void
test_lines_strips_fans(void)
{
    result r;
    const pgr_mesh *m;

    load(&r, "lines", fx_lines);
    PGR_CHECK(r.status == PGR_OK);
    m = &r.scene.meshes[0];
    PGR_CHECK(m->num_primitives == 3);
    if (m->num_primitives == 3)
    {
        const uint32_t *loop = m->primitives[2].indices;

        PGR_CHECK(m->primitives[0].mode == PGR_LINES && m->primitives[0].num_indices == 4);
        PGR_CHECK(m->primitives[1].mode == PGR_LINES && m->primitives[1].num_indices == 6);
        PGR_CHECK(m->primitives[2].mode == PGR_LINES && m->primitives[2].num_indices == 8);
        /* The loop closes from the last vertex back to the first. */
        PGR_CHECK(loop[6] == 3 && loop[7] == 0);
    }
    pgr_scene_free(&r.scene);

    load(&r, "strip_fan", fx_strip_fan);
    PGR_CHECK(r.status == PGR_OK);
    m = &r.scene.meshes[0];
    PGR_CHECK(m->num_primitives == 2);
    if (m->num_primitives == 2)
    {
        const uint32_t *s = m->primitives[0].indices, *f = m->primitives[1].indices;

        /* glTF's order: strip triangle i is (i, i+1+i%2, i+2-i%2), fan
           triangle i is (i+1, i+2, 0). */
        PGR_CHECK(m->primitives[0].num_indices == 9 && m->primitives[1].num_indices == 9);
        PGR_CHECK(tri_is(s, 0, 1, 2) && tri_is(s + 3, 1, 3, 2) && tri_is(s + 6, 2, 3, 4));
        PGR_CHECK(tri_is(f, 1, 2, 0) && tri_is(f + 3, 2, 3, 0) && tri_is(f + 6, 3, 4, 0));
    }
    pgr_scene_free(&r.scene);
}

static void
test_sparse_and_cleanup(void)
{
    result r;
    const pgr_primitive *p;

    load(&r, "sparse_positions", fx_sparse_positions);
    PGR_CHECK(r.status == PGR_OK);
    p = &r.scene.meshes[0].primitives[0];
    PGR_CHECK(p->positions[3] == 5.0f && p->positions[4] == 5.0f && p->positions[5] == 5.0f);
    PGR_CHECK(p->positions[6] == 0.0f && p->positions[7] == 1.0f);
    pgr_scene_free(&r.scene);

    load(&r, "points", fx_points);
    PGR_CHECK(r.status == PGR_OK);
    PGR_CHECK(r.scene.num_meshes == 1 && r.scene.meshes[0].num_primitives == 0);
    PGR_CHECK((r.msg.warned & PGR_WARN_POINTS) != 0);
    pgr_scene_free(&r.scene);

    load(&r, "degenerate", fx_degenerate);
    PGR_CHECK(r.status == PGR_OK);
    p = &r.scene.meshes[0].primitives[0];
    PGR_CHECK(p->num_indices == 3 && tri_is(p->indices, 0, 1, 2));
    PGR_CHECK((r.msg.warned & PGR_WARN_DEGENERATE) != 0);
    pgr_scene_free(&r.scene);

    load(&r, "zero_normal", fx_zero_normal);
    PGR_CHECK(r.status == PGR_OK);
    PGR_CHECK(r.scene.meshes[0].primitives[0].normals == NULL);
    PGR_CHECK((r.msg.warned & PGR_WARN_ZERO_NORMAL) != 0);
    pgr_scene_free(&r.scene);
}

static void
test_refused_and_rejected(void)
{
    result r;
    pgr_glb_read_options opt;
    FILE *f;

    load(&r, "requires_meshopt", make_meshopt);
    PGR_CHECK(r.status == PGR_ERR_UNSUPPORTED);
    PGR_CHECK(strstr(r.last, "EXT_meshopt_compression") != NULL);
    pgr_scene_free(&r.scene);

    load(&r, "requires_draco", make_draco);
    PGR_CHECK(r.status == PGR_ERR_UNSUPPORTED);
    PGR_CHECK(strstr(r.last, "PRC_GLB_WITH_DRACO") != NULL);
    pgr_scene_free(&r.scene);

    /* An extension that only changes appearance is accepted. */
    load(&r, "requires_unlit", make_unlit);
    PGR_CHECK(r.status == PGR_OK);
    pgr_scene_free(&r.scene);

    load(&r, "bad_index", fx_bad_index);
    PGR_CHECK(r.status == PGR_ERR_PARSE);
    pgr_scene_free(&r.scene);

    /* A missing file, and a file that is not glTF. */
    memset(&r, 0, sizeof(r));
    r.msg.fn = collect;
    r.msg.user = r.last;
    opt.messages = &r.msg;
    pgr_scene_init(&r.scene);
    PGR_CHECK(pgr_glb_read("no_such_file.glb", &opt, &r.scene) == PGR_ERR_PARSE);
    pgr_scene_free(&r.scene);
    f = fopen("not_gltf.glb", "wb");
    if (f != NULL)
    {
        fputs("this is not a glTF file", f);
        fclose(f);
    }
    PGR_CHECK(pgr_glb_read("not_gltf.glb", &opt, &r.scene) == PGR_ERR_PARSE);
    pgr_scene_free(&r.scene);
}

int
main(void)
{
    static const char *const passes[2] = { ".glb", ".gltf" };
    int i;

    for (i = 0; i < 2; i++)
    {
        int before = pgr_test_failures;

        ext = passes[i];
        test_cubes();
        test_colours();
        test_nodes();
        test_materials();
        test_lines_strips_fans();
        test_sparse_and_cleanup();
        test_refused_and_rejected();
        if (pgr_test_failures != before)
            fprintf(stderr, "(failures above are in the %s pass)\n", ext);
    }
    return PGR_TEST_RESULT;
}
