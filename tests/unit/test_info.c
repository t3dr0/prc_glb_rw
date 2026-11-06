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

/* Version strings and the extension check shared by both tools. */

#include "pgr_info.h"
#include "pgr_test.h"

#include "prc_api.h"

#ifndef PGR_EXPECTED_CGLTF_VERSION
#define PGR_EXPECTED_CGLTF_VERSION "unknown"
#endif

int
main(void)
{
    /* cgltf's version comes from the header the library was built from;
       CMake reads the same header for the expected value, so this checks
       that the definition reaches the code, not which tag is checked out. */
    PGR_CHECK_STR(pgr_cgltf_version(), PGR_EXPECTED_CGLTF_VERSION);
    PGR_CHECK(strcmp(pgr_cgltf_version(), "unknown") != 0);
    PGR_CHECK(strcmp(pgr_version(), "unknown") != 0);
    PGR_CHECK(prc_api_get_version() != NULL);
    PGR_CHECK(pgr_draco_available() == 0);

    PGR_CHECK(pgr_has_extension("model.glb", ".glb"));
    PGR_CHECK(pgr_has_extension("MODEL.GLB", ".glb"));
    PGR_CHECK(pgr_has_extension("dir.v2/model.gltf", ".gltf"));
    PGR_CHECK(!pgr_has_extension("model.gltf", ".glb"));
    PGR_CHECK(!pgr_has_extension("model.glb.txt", ".glb"));
    PGR_CHECK(!pgr_has_extension("glb", ".glb"));
    PGR_CHECK(!pgr_has_extension("", ".glb"));
    PGR_CHECK(!pgr_has_extension(NULL, ".glb"));
    PGR_CHECK(!pgr_has_extension("model.glb", ""));

    return PGR_TEST_RESULT;
}
