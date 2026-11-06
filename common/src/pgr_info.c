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

#include "pgr_info.h"

#include "prc_api.h"

#include <ctype.h>
#include <string.h>

/* CMake defines both from the project and from cgltf.h; the fallbacks keep
   the file parseable on its own, as the C99 check parses it. */
#ifndef PGR_VERSION
#define PGR_VERSION "unknown"
#endif
#ifndef PGR_CGLTF_VERSION
#define PGR_CGLTF_VERSION "unknown"
#endif

const char *
pgr_version(void)
{
    return PGR_VERSION;
}

const char *
pgr_cgltf_version(void)
{
    return PGR_CGLTF_VERSION;
}

int
pgr_draco_available(void)
{
    return 0;
}

void
pgr_print_version(FILE *out, const char *tool)
{
    const char *prc = prc_api_get_version();

    fprintf(out, "%s %s\n", tool, pgr_version());
    fprintf(out, "  nanoPRC %s\n", prc ? prc : "unknown");
    fprintf(out, "  cgltf %s\n", pgr_cgltf_version());
    fprintf(out, "  Draco %s\n", pgr_draco_available() ? "built in" : "not built in");
}

int
pgr_has_extension(const char *path, const char *ext)
{
    size_t n, e, i;

    if (path == NULL || ext == NULL)
        return 0;
    n = strlen(path);
    e = strlen(ext);
    if (e == 0 || n < e)
        return 0;
    for (i = 0; i < e; i++)
        if (tolower((unsigned char)path[n - e + i]) != tolower((unsigned char)ext[i]))
            return 0;
    return 1;
}
