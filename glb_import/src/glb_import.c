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
 * glb_import: read a glTF 2.0 file, binary (.glb) or JSON (.gltf), and
 * write it as a .prc file and a minimal 3D-annotated .pdf, through
 * nanoPRC's public write API only (include/prc_api.h).
 *
 * This is the skeleton: it checks its command line and reports versions,
 * and does not convert yet. The plan is for parsing by cgltf, each glTF
 * node becoming a PRC product node with its transform, each primitive a
 * tessellation with its own material, and the scene placed under one
 * root node that turns glTF's metres and +Y up into millimetres and +Z up.
 *
 * Usage:
 *   nano_prc_glb_import <input.glb|input.gltf> [output.prc] [output.pdf]
 *   nano_prc_glb_import --version
 * ==================================================================== */

#include "pgr_info.h"

#include <stdio.h>
#include <string.h>

#define TOOL "nano_prc_glb_import"

static void
usage(FILE *out)
{
    fprintf(out,
        "Usage: " TOOL " <input.glb|input.gltf> [output.prc] [output.pdf]\n"
        "       " TOOL " --version\n"
        "\n"
        "Converts glTF 2.0 to PRC and a 3D PDF. This build is a skeleton: it\n"
        "checks its arguments but does not convert yet.\n");
}

int
main(int argc, char **argv)
{
    const char *input = NULL;
    const char *outputs[2] = { NULL, NULL };
    int n_outputs = 0;
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
        else if (n_outputs < 2)
            outputs[n_outputs++] = a;
        else
        {
            fprintf(stderr, TOOL ": too many arguments\n");
            usage(stderr);
            return PGR_EXIT_USAGE;
        }
    }

    if (input == NULL)
    {
        usage(stderr);
        return PGR_EXIT_USAGE;
    }
    if (!pgr_has_extension(input, ".glb") && !pgr_has_extension(input, ".gltf"))
    {
        fprintf(stderr, TOOL ": the input must be a .glb or .gltf file: %s\n", input);
        return PGR_EXIT_USAGE;
    }
    for (i = 0; i < n_outputs; i++)
    {
        if (!pgr_has_extension(outputs[i], ".prc") && !pgr_has_extension(outputs[i], ".pdf"))
        {
            fprintf(stderr, TOOL ": an output must be a .prc or .pdf file: %s\n", outputs[i]);
            return PGR_EXIT_USAGE;
        }
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
