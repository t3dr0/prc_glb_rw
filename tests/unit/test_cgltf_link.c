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

/* cgltf's reader and writer are compiled once, into the shared library, and
   reachable from a program that links it: parse a small glTF from memory,
   check what it holds, write it back out as JSON and parse that again. */

#include "cgltf.h"
#include "cgltf_write.h"
#include "pgr_test.h"

#include <stdlib.h>

static const char GLTF[] =
    "{\"asset\":{\"version\":\"2.0\",\"generator\":\"test_cgltf_link\"},"
    "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
    "\"nodes\":[{\"name\":\"root\",\"translation\":[1,2,3]}]}";

int
main(void)
{
    cgltf_options options;
    cgltf_data *data = NULL;
    cgltf_data *again = NULL;
    cgltf_size size;
    char *json;

    memset(&options, 0, sizeof(options));

    PGR_CHECK(cgltf_parse(&options, GLTF, sizeof(GLTF) - 1, &data) == cgltf_result_success);
    if (data == NULL)
        return PGR_TEST_RESULT;
    PGR_CHECK(cgltf_validate(data) == cgltf_result_success);
    PGR_CHECK(data->nodes_count == 1);
    PGR_CHECK_STR(data->nodes[0].name, "root");
    PGR_CHECK(data->nodes[0].has_translation);
    PGR_CHECK(data->nodes[0].translation[2] == 3.0f);
    PGR_CHECK(data->scene == &data->scenes[0]);

    /* The first call measures, the second writes; the size includes the
       terminating zero. */
    size = cgltf_write(&options, NULL, 0, data);
    PGR_CHECK(size > 1);
    json = (char *)malloc(size);
    PGR_CHECK(json != NULL);
    if (json != NULL)
    {
        PGR_CHECK(cgltf_write(&options, json, size, data) == size);
        PGR_CHECK(cgltf_parse(&options, json, size - 1, &again) == cgltf_result_success);
        if (again != NULL)
        {
            PGR_CHECK(again->nodes_count == 1);
            PGR_CHECK_STR(again->nodes[0].name, "root");
            PGR_CHECK(again->nodes[0].translation[1] == 2.0f);
            cgltf_free(again);
        }
        free(json);
    }

    cgltf_free(data);
    return PGR_TEST_RESULT;
}
