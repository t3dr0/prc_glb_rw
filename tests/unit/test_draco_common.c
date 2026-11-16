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

/* KHR_draco_mesh_compression as far as it does not depend on Draco itself,
   run in builds with and without it:

     - cgltf's reading of the extension's id map as accessor pointers, and
       pgr_draco_attribute_id undoing it; an id past the last accessor makes
       the file unreadable;
     - without Draco: a primitive only in Draco form is refused as
       unsupported, naming the build option; one with an uncompressed copy
       is read from that copy, with a warning; asking the writer for Draco
       is refused;
     - with Draco: Draco data that does not decode is a parse error, even
       when an uncompressed copy is there. */

#include "cgltf.h"
#include "fixtures.h"
#include "pgr_glb.h"
#include "pgr_test.h"

#include <stdio.h>
#include <string.h>

typedef struct
{
    char last[512];
    unsigned warnings, errors;
} log_t;

static void
record(void *user, int is_error, const char *text)
{
    log_t *l = (log_t *)user;

    snprintf(l->last, sizeof(l->last), "%s", text);
    if (is_error)
        l->errors++;
    else
        l->warnings++;
}

static const unsigned char not_draco[16] = { 'n', 'o', 't', ' ', 'D', 'r', 'a', 'c',
                                             'o', ' ', 'd', 'a', 't', 'a', '!', 0 };

static int
read_file(const char *path, pgr_scene *scene, log_t *l, pgr_messages *m)
{
    pgr_glb_read_options o;

    memset(l, 0, sizeof(*l));
    memset(m, 0, sizeof(*m));
    m->fn = record;
    m->user = l;
    o.messages = m;
    pgr_scene_init(scene);
    return pgr_glb_read(path, &o, scene);
}

static void
test_id_map(void)
{
    cgltf_options o;
    cgltf_data *data = NULL;
    const cgltf_primitive *p;
    pgr_scene s;
    log_t l;
    pgr_messages m;

    /* Accessors: POSITION 0, NORMAL 1, indices 2. Ids 1 and 0, swapped
       against the accessors, so that a mix-up shows. */
    PGR_CHECK(fx_draco("draco_ids.gltf", not_draco, sizeof(not_draco), 3, 3, 1, 0, 0) == 0);
    memset(&o, 0, sizeof(o));
    PGR_CHECK(cgltf_parse_file(&o, "draco_ids.gltf", &data) == cgltf_result_success);
    if (data != NULL)
    {
        p = &data->meshes[0].primitives[0];
        PGR_CHECK(p->has_draco_mesh_compression);
        PGR_CHECK(p->draco_mesh_compression.attributes_count == 2);
        if (p->draco_mesh_compression.attributes_count == 2)
        {
            const cgltf_attribute *a = p->draco_mesh_compression.attributes;
            int pos = a[0].type == cgltf_attribute_type_position ? 0 : 1;

            PGR_CHECK(pgr_draco_attribute_id(data, &a[pos]) == 1);
            PGR_CHECK(pgr_draco_attribute_id(data, &a[1 - pos]) == 0);
        }
        PGR_CHECK(pgr_draco_attribute_id(data, NULL) == -1);
        cgltf_free(data);
    }

    /* Id 3 with three accessors: cgltf cannot make it a pointer. */
    PGR_CHECK(fx_draco("draco_bad_id.gltf", not_draco, sizeof(not_draco), 3, 3, 3, -1, 0) == 0);
    data = NULL;
    PGR_CHECK(cgltf_parse_file(&o, "draco_bad_id.gltf", &data) == cgltf_result_invalid_gltf);
    if (data != NULL)
        cgltf_free(data);
    PGR_CHECK(read_file("draco_bad_id.gltf", &s, &l, &m) == PGR_ERR_PARSE);
    pgr_scene_free(&s);
}

static void
test_without_draco(void)
{
    pgr_scene s;
    pgr_glb_write_options wo;
    log_t l;
    pgr_messages m;

    PGR_CHECK(fx_draco("draco_only.glb", not_draco, sizeof(not_draco), 3, 3, 0, -1, 0) == 0);
    PGR_CHECK(read_file("draco_only.glb", &s, &l, &m) == PGR_ERR_UNSUPPORTED);
    PGR_CHECK(strstr(l.last, "KHR_draco_mesh_compression") != NULL);
    PGR_CHECK(strstr(l.last, "PRC_GLB_WITH_DRACO=ON") != NULL);
    pgr_scene_free(&s);

    /* The same with an uncompressed copy: read from it, with a warning. */
    PGR_CHECK(fx_draco("draco_fallback.glb", not_draco, sizeof(not_draco), 3, 3, 0, -1, 1) == 0);
    PGR_CHECK(read_file("draco_fallback.glb", &s, &l, &m) == PGR_OK);
    PGR_CHECK(s.num_meshes == 1 && s.meshes[0].num_primitives == 1);
    if (s.num_meshes == 1 && s.meshes[0].num_primitives == 1)
        PGR_CHECK(s.meshes[0].primitives[0].num_indices == 3);
    PGR_CHECK(l.warnings == 1 && strstr(l.last, "uncompressed copy") != NULL);

    /* The writer cannot compress without Draco. */
    memset(&wo, 0, sizeof(wo));
    wo.messages = &m;
    wo.draco = 1;
    PGR_CHECK(pgr_glb_write(&s, "draco_refused.glb", &wo) == PGR_ERR_UNSUPPORTED);
    PGR_CHECK(strstr(l.last, "PRC_GLB_WITH_DRACO=ON") != NULL);
    pgr_scene_free(&s);
}

static void
test_with_draco(void)
{
    pgr_scene s;
    log_t l;
    pgr_messages m;

    PGR_CHECK(fx_draco("draco_garbage.glb", not_draco, sizeof(not_draco), 3, 3, 0, -1, 0) == 0);
    PGR_CHECK(read_file("draco_garbage.glb", &s, &l, &m) == PGR_ERR_PARSE);
    PGR_CHECK(strstr(l.last, "cannot be decoded") != NULL);
    pgr_scene_free(&s);

    /* The Draco data is what a reader with Draco uses, so its being
       corrupt is an error even with an uncompressed copy beside it. */
    PGR_CHECK(fx_draco("draco_garbage_fallback.glb", not_draco, sizeof(not_draco), 3, 3, 0, -1, 1) == 0);
    PGR_CHECK(read_file("draco_garbage_fallback.glb", &s, &l, &m) == PGR_ERR_PARSE);
    pgr_scene_free(&s);
}

int
main(void)
{
    test_id_map();
    if (ds_available())
        test_with_draco();
    else
    {
        /* The stub: nothing available, nothing allocated. */
        ds_mesh m;
        void *buf = &m;
        size_t size = 1;
        int id = 0;

        test_without_draco();
        PGR_CHECK(ds_version() == NULL);
        PGR_CHECK(ds_decode(not_draco, sizeof(not_draco), &id, 1, &m) == DS_ERR_UNAVAILABLE);
        PGR_CHECK(m.indices == NULL && m.attributes == NULL);
        memset(&m, 0, sizeof(m));
        PGR_CHECK(ds_encode(&m, NULL, &buf, &size, NULL) == DS_ERR_UNAVAILABLE);
        PGR_CHECK(buf == NULL && size == 0);
    }
    return PGR_TEST_RESULT;
}
