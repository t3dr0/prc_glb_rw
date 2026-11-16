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
 * The PRC is read into the neutral scene (pgr_prc_read) and written from
 * it (pgr_glb_write):
 *
 *   - the model tree becomes the glTF node hierarchy, each node with its
 *     placement and name;
 *   - each part becomes a mesh, shared by every node that draws the same
 *     tessellations; surfaces become triangle primitives, wires line ones;
 *   - the PRC's millimetres and +Z up become glTF's metres and +Y up
 *     (--units and --no-axis-convert change that). A file written by
 *     glb_import comes back with the nodes it started with: the root that
 *     glb_import added for the conversion is recognised and removed.
 *
 * With --draco, in a build with Draco, each triangle primitive is
 * compressed with KHR_draco_mesh_compression; lines are left as they are.
 *
 * Usage:
 *   nano_prc_glb_export <input.pdf|input.prc> <output.glb|output.gltf> [options]
 * ==================================================================== */

#include "pgr_glb.h"
#include "pgr_info.h"
#include "pgr_prc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOOL "nano_prc_glb_export"

static void
usage(FILE *out)
{
    fprintf(out,
        "Usage: " TOOL " <input.pdf|input.prc> <output.glb|output.gltf> [options]\n"
        "       " TOOL " --version\n"
        "\n"
        "Converts PRC, or a PDF with PRC 3D content, to glTF 2.0: binary for .glb,\n"
        "JSON with a .bin beside it for .gltf.\n"
        "\n"
        "  --units mm|cm|m|in    units of the PRC (default mm)\n"
        "  --no-axis-convert     keep the PRC's +Z up instead of turning it to +Y up\n"
        "  --draco               compress triangle meshes with Draco\n"
        "                        (KHR_draco_mesh_compression; needs a build with Draco)\n"
        "  --draco-qpos N        Draco quantisation bits for positions (default 14),\n"
        "  --draco-qnorm N         normals (10), texture coordinates (12) and\n"
        "  --draco-quv N           colours (8): 1 to 30, or 0 for none\n"
        "  --draco-qcolor N\n"
        "  --draco-speed N       Draco encoding and decoding speed, 0 (smallest)\n"
        "                        to 10 (fastest; default 5)\n"
        "  --version             print versions and exit\n");
}

/* An option's whole-number value s, from lo to hi, into *out; 0 when s is
   not one. */
static int
int_arg(const char *s, int lo, int hi, int *out)
{
    char *end;
    long v = strtol(s, &end, 10);

    if (end == s || *end != '\0' || v < lo || v > hi)
        return 0;
    *out = (int)v;
    return 1;
}

int
main(int argc, char **argv)
{
    const char *input = NULL, *output = NULL;
    pgr_prc_read_options ropt;
    pgr_prc_read_stats stats;
    pgr_glb_write_options wopt;
    pgr_messages msg;
    pgr_scene scene;
    ds_encode_options dopt;
    int i, st, rc, draco_tuned = 0;

    memset(&ropt, 0, sizeof(ropt));
    memset(&wopt, 0, sizeof(wopt));
    memset(&msg, 0, sizeof(msg));
    ds_encode_options_default(&dopt);
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
        if (strcmp(a, "--no-axis-convert") == 0)
            ropt.no_axis_convert = 1;
        else if (strcmp(a, "--draco") == 0)
            wopt.draco = 1;
        else if (strcmp(a, "--draco-qpos") == 0 || strcmp(a, "--draco-qnorm") == 0
                 || strcmp(a, "--draco-quv") == 0 || strcmp(a, "--draco-qcolor") == 0
                 || strcmp(a, "--draco-speed") == 0)
        {
            int speed = strcmp(a, "--draco-speed") == 0;
            int v;

            if (i + 1 >= argc || !int_arg(argv[++i], 0, speed ? 10 : 30, &v))
            {
                fprintf(stderr, TOOL ": %s takes a whole number from 0 to %d\n", a, speed ? 10 : 30);
                return PGR_EXIT_USAGE;
            }
            if (speed)
                dopt.encode_speed = dopt.decode_speed = v;
            else if (strcmp(a, "--draco-qpos") == 0)
                dopt.qbits_position = v;
            else if (strcmp(a, "--draco-qnorm") == 0)
                dopt.qbits_normal = v;
            else if (strcmp(a, "--draco-quv") == 0)
                dopt.qbits_texcoord = v;
            else
                dopt.qbits_color = v;
            draco_tuned = 1;
        }
        else if (strcmp(a, "--units") == 0 && i + 1 < argc)
        {
            const char *u = argv[++i];

            if (strcmp(u, "mm") == 0)
                ropt.units_per_metre = PGR_UNITS_PER_METRE_MM;
            else if (strcmp(u, "cm") == 0)
                ropt.units_per_metre = PGR_UNITS_PER_METRE_CM;
            else if (strcmp(u, "m") == 0)
                ropt.units_per_metre = PGR_UNITS_PER_METRE_M;
            else if (strcmp(u, "in") == 0)
                ropt.units_per_metre = PGR_UNITS_PER_METRE_IN;
            else
            {
                fprintf(stderr, TOOL ": --units takes mm, cm, m or in\n");
                return PGR_EXIT_USAGE;
            }
        }
        else if (a[0] == '-' && a[1] != '\0')
        {
            fprintf(stderr, TOOL ": unknown option %s\n", a);
            usage(stderr);
            return PGR_EXIT_USAGE;
        }
        else if (input == NULL)
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

    if (draco_tuned && !wopt.draco)
    {
        fprintf(stderr, TOOL ": the --draco-... options need --draco\n");
        return PGR_EXIT_USAGE;
    }
    if (wopt.draco && !pgr_draco_available())
    {
        fprintf(stderr, TOOL ": --draco needs a build with Draco: "
                "configure prc_glb_rw with -DPRC_GLB_WITH_DRACO=ON\n");
        return PGR_EXIT_UNSUPPORTED;
    }
    wopt.draco_options = &dopt;

    pgr_scene_init(&scene);
    ropt.messages = &msg;
    wopt.messages = &msg;
    st = pgr_prc_read(input, &ropt, &scene, &stats);
    if (st == PGR_OK)
        st = pgr_glb_write(&scene, output, &wopt);
    if (st == PGR_OK)
        printf("Wrote %s: %u nodes, %u meshes, %u primitives, %u triangles, %u line segments%s\n",
               output, stats.nodes, stats.meshes, stats.primitives, stats.triangles,
               stats.segments, stats.skipped ? " (markups or exact geometry skipped)" : "");
    rc = pgr_exit_code(st);
    pgr_scene_free(&scene);
    return rc;
}
