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

/* ====================================================================
 * glb_export: read a .prc file, or a PDF with PRC 3D content, through
 * nanoPRC's public read API only (include/prc_api.h), and write it as
 * glTF 2.0, binary (.glb) or JSON with a separate .bin (.gltf).
 *
 * This is the skeleton: it checks its command line and reports versions,
 * and does not convert yet. The plan is for the model tree to become the
 * glTF node hierarchy with each node's transform, each tessellation a
 * mesh shared by every node that uses it, and the scene's millimetres and
 * +Z up turned back into glTF's metres and +Y up.
 *
 * Usage:
 *   nano_prc_glb_export <input.pdf|input.prc> <output.glb|output.gltf>
 *   nano_prc_glb_export --version
 * ==================================================================== */

#include "pgr_info.h"

#include <stdio.h>
#include <string.h>

#define TOOL "nano_prc_glb_export"

static void
usage(FILE *out)
{
    fprintf(out,
        "Usage: " TOOL " <input.pdf|input.prc> <output.glb|output.gltf>\n"
        "       " TOOL " --version\n"
        "\n"
        "Converts PRC, or a PDF with PRC 3D content, to glTF 2.0. This build is\n"
        "a skeleton: it checks its arguments but does not convert yet.\n");
}

int
main(int argc, char **argv)
{
    const char *input = NULL;
    const char *output = NULL;
    FILE *f;
    int i;

    for (i = 1; i < argc; i++)
    {
        const char *a = argv[i];

        if (strcmp(a, "--version") == 0)
        {
            pgr_print_version(stdout, TOOL);
            return PGR_EXIT_OK;
        }
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0)
        {
            usage(stdout);
            return PGR_EXIT_OK;
        }
        if (a[0] == '-' && a[1] != '\0')
        {
            fprintf(stderr, TOOL ": unknown option %s\n", a);
            usage(stderr);
            return PGR_EXIT_USAGE;
        }
        if (input == NULL)
            input = a;
        else if (output == NULL)
            output = a;
        else
        {
            fprintf(stderr, TOOL ": too many arguments\n");
            usage(stderr);
            return PGR_EXIT_USAGE;
        }
    }

    if (input == NULL || output == NULL)
    {
        usage(stderr);
        return PGR_EXIT_USAGE;
    }
    if (!pgr_has_extension(input, ".prc") && !pgr_has_extension(input, ".pdf"))
    {
        fprintf(stderr, TOOL ": the input must be a .prc or .pdf file: %s\n", input);
        return PGR_EXIT_USAGE;
    }
    if (!pgr_has_extension(output, ".glb") && !pgr_has_extension(output, ".gltf"))
    {
        fprintf(stderr, TOOL ": the output must be a .glb or .gltf file: %s\n", output);
        return PGR_EXIT_USAGE;
    }

    f = fopen(input, "rb");
    if (f == NULL)
    {
        fprintf(stderr, TOOL ": cannot open %s\n", input);
        return PGR_EXIT_READ;
    }
    fclose(f);

    fprintf(stderr, TOOL ": conversion is not implemented yet in this skeleton\n");
    return PGR_EXIT_UNSUPPORTED;
}
