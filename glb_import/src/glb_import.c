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
 * The glTF is read into the neutral scene (pgr_glb_read) and written from
 * it (pgr_prc_write):
 *
 *   - each glTF node becomes a PRC node with its matrix and name, and each
 *     node with a mesh a part; a mesh used by several nodes is written
 *     once;
 *   - each triangle primitive becomes a compressed tessellation, or with
 *     --triangles a plain one; strips and fans are triangulated first;
 *   - each line primitive becomes a wire tessellation;
 *   - the scene goes under one root node that turns glTF's +Y up into
 *     PRC's +Z up and its metres into millimetres (--units, and
 *     --no-axis-convert, change that). The coordinates are not changed.
 *
 * Not yet done: materials, vertex colours and textures (every part gets
 * nanoPRC's default material), and Draco-compressed meshes.
 *
 * Usage:
 *   nano_prc_glb_import <input.glb|input.gltf> [output.prc] [output.pdf] [options]
 *
 * Output paths default to the input's name with its extension replaced.
 * ==================================================================== */

#include "pgr_glb.h"
#include "pgr_info.h"
#include "pgr_prc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOOL "nano_prc_glb_import"

static void
usage(FILE *out)
{
    fprintf(out,
        "Usage: " TOOL " <input.glb|input.gltf> [output.prc] [output.pdf] [options]\n"
        "       " TOOL " --version\n"
        "\n"
        "Converts glTF 2.0 to PRC and a 3D PDF. Output paths default to the input's\n"
        "name with .prc and .pdf.\n"
        "\n"
        "  --triangles           write plain triangles (default: compressed)\n"
        "  --no-normals          leave the file's normals out; readers compute them\n"
        "  --quant-tolerance F   compressed tessellation's tolerance, as a fraction of\n"
        "                        the bounding-box diagonal (default %g)\n"
        "  --units mm|cm|m|in    units of the PRC (default mm)\n"
        "  --no-axis-convert     keep glTF's +Y up instead of turning it to +Z up\n"
        "  --version             print versions and exit\n",
        PGR_DEFAULT_QUANT_TOLERANCE);
}

/* path with its extension replaced by ext; NULL when memory runs out. */
static char *
replace_extension(const char *path, const char *ext)
{
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    size_t stem;
    char *out;

    if (bslash > slash)
        slash = bslash;
    stem = (dot != NULL && (slash == NULL || dot > slash)) ? (size_t)(dot - path) : strlen(path);
    out = (char *)malloc(stem + strlen(ext) + 1);
    if (out == NULL)
        return NULL;
    memcpy(out, path, stem);
    memcpy(out + stem, ext, strlen(ext) + 1);
    return out;
}

int
main(int argc, char **argv)
{
    const char *input = NULL, *prc_path = NULL, *pdf_path = NULL;
    char *prc_default = NULL, *pdf_default = NULL;
    pgr_prc_write_options wopt;
    pgr_prc_write_stats stats;
    pgr_messages msg;
    pgr_glb_read_options ropt;
    pgr_scene scene;
    int i, st, rc;

    memset(&wopt, 0, sizeof(wopt));
    memset(&msg, 0, sizeof(msg));
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
        if (strcmp(a, "--triangles") == 0)
            wopt.triangles = 1;
        else if (strcmp(a, "--no-normals") == 0)
            wopt.no_normals = 1;
        else if (strcmp(a, "--no-axis-convert") == 0)
            wopt.no_axis_convert = 1;
        else if (strcmp(a, "--quant-tolerance") == 0 && i + 1 < argc)
        {
            wopt.quant_tolerance = atof(argv[++i]);
            if (!(wopt.quant_tolerance > 0.0))
            {
                fprintf(stderr, TOOL ": --quant-tolerance needs a positive number\n");
                return PGR_EXIT_USAGE;
            }
        }
        else if (strcmp(a, "--units") == 0 && i + 1 < argc)
        {
            const char *u = argv[++i];

            if (strcmp(u, "mm") == 0)
                wopt.units_per_metre = PGR_UNITS_PER_METRE_MM;
            else if (strcmp(u, "cm") == 0)
                wopt.units_per_metre = PGR_UNITS_PER_METRE_CM;
            else if (strcmp(u, "m") == 0)
                wopt.units_per_metre = PGR_UNITS_PER_METRE_M;
            else if (strcmp(u, "in") == 0)
                wopt.units_per_metre = PGR_UNITS_PER_METRE_IN;
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
        else if (prc_path == NULL)
            prc_path = a;
        else if (pdf_path == NULL)
            pdf_path = a;
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
    if (prc_path != NULL && !pgr_has_extension(prc_path, ".prc"))
    {
        fprintf(stderr, TOOL ": the second argument must be a .prc file: %s\n", prc_path);
        return PGR_EXIT_USAGE;
    }
    if (pdf_path != NULL && !pgr_has_extension(pdf_path, ".pdf"))
    {
        fprintf(stderr, TOOL ": the third argument must be a .pdf file: %s\n", pdf_path);
        return PGR_EXIT_USAGE;
    }
    if (prc_path == NULL)
        prc_path = prc_default = replace_extension(input, ".prc");
    if (pdf_path == NULL)
        pdf_path = pdf_default = replace_extension(input, ".pdf");
    if (prc_path == NULL || pdf_path == NULL)
    {
        fprintf(stderr, TOOL ": out of memory\n");
        free(prc_default);
        free(pdf_default);
        return PGR_EXIT_READ;
    }

    pgr_scene_init(&scene);
    ropt.messages = &msg;
    wopt.messages = &msg;
    st = pgr_glb_read(input, &ropt, &scene);
    if (st == PGR_OK)
        st = pgr_prc_write(&scene, prc_path, pdf_path, &wopt, &stats);
    if (st == PGR_OK)
        printf("Wrote %s (%lu bytes) and %s: %u nodes, %u tessellations (%u compressed, "
               "%u wire), %u triangles, %u line segments\n",
               prc_path, (unsigned long)stats.prc_bytes, pdf_path, stats.nodes,
               stats.tessellations, stats.compressed, stats.wires, stats.triangles,
               stats.segments);
    rc = pgr_exit_code(st);
    pgr_scene_free(&scene);
    free(prc_default);
    free(pdf_default);
    return rc;
}
