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

/* Round trips through PRC, compared in world space (scene_compare):

     RT1  glTF -> PRC (plain triangles) -> glTF: every triangle back, same
          winding, within 1e-5 of the diagonal; the same nodes by name, and
          a mesh drawn twice still one mesh;
     RT2  the same with compressed tessellation, within its tolerance;
     RT3  PRC -> glTF -> PRC, on nanoPRC's example PDFs and test files;
     RT6  glTF -> PRC -> glTF -> PRC -> glTF: with plain triangles, which
          keep positions exactly, the second and third glTF are byte for
          byte the same, so nothing accumulates (no extra root per trip).
          Compressed tessellation quantises the positions again on every
          trip, moving them by up to its tolerance, so there the scenes are
          compared instead: the same nodes and every triangle within the
          tolerance. */

#include "fixtures.h"
#include "pgr_glb.h"
#include "pgr_prc.h"
#include "pgr_test.h"
#include "scene_compare.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef PGR_NANOPRC_DIR
#define PGR_NANOPRC_DIR "."
#endif

static void
quiet(void *user, int is_error, const char *text)
{
    (void)user;
    if (is_error)
        fprintf(stderr, "error: %s\n", text);
}

static pgr_messages msg = { quiet, NULL, 0, 0, 0 };

/* glTF fixture -> scene a; a -> PRC -> scene b. */
static int
glb_prc_glb(const char *name, int (*make)(const char *), int triangles, pgr_scene *a, pgr_scene *b,
            pgr_prc_read_stats *rs)
{
    char glb[256], prc[256];
    pgr_glb_read_options ro;
    pgr_prc_write_options wo;
    pgr_prc_read_options pro;
    int st;

    memset(rs, 0, sizeof(*rs));
    snprintf(glb, sizeof(glb), "rt_%s.glb", name);
    snprintf(prc, sizeof(prc), "rt_%s%s.prc", name, triangles ? "_tri" : "");
    memset(&wo, 0, sizeof(wo));
    memset(&pro, 0, sizeof(pro));
    ro.messages = wo.messages = pro.messages = &msg;
    wo.triangles = triangles;
    pgr_scene_init(a);
    pgr_scene_init(b);
    if (make(glb) != 0)
        return -1;
    st = pgr_glb_read(glb, &ro, a);
    if (st == PGR_OK)
        st = pgr_prc_write(a, prc, NULL, &wo, NULL);
    if (st == PGR_OK)
        st = pgr_prc_read(prc, &pro, b, rs);
    return st;
}

static void
rt1_rt2(const char *name, int (*make)(const char *), int triangles)
{
    pgr_scene a, b;
    pgr_prc_read_stats rs;
    scene_diff d;
    int ok;

    PGR_CHECK(glb_prc_glb(name, make, triangles, &a, &b, &rs) == PGR_OK);
    ok = scene_compare(&a, &b, 1e-5, &d) == 0;
    if (!ok)
        fprintf(stderr, "%s (%s): triangles %u -> %u, segments %u -> %u, unmatched %u, max error %g\n",
                name, triangles ? "triangles" : "compressed", d.triangles_a, d.triangles_b,
                d.segments_a, d.segments_b, d.unmatched, d.max_error);
    PGR_CHECK(ok);
    PGR_CHECK(rs.collapsed_root == 1);
    PGR_CHECK(scene_same_names(&a, &b));
    PGR_CHECK(a.num_meshes == b.num_meshes || a.num_meshes == 0);
    if (a.model_name != NULL)
        PGR_CHECK_STR(b.model_name, a.model_name);
    pgr_scene_free(&a);
    pgr_scene_free(&b);
}

/* RT3: a PRC file -> scene p1 -> glTF -> scene -> PRC -> scene p2. */
static void
rt3(const char *file)
{
    char path[1024], base[256], glb[300], prc[300];
    pgr_scene p1, g, p2;
    pgr_prc_read_stats rs;
    pgr_prc_write_options wo;
    pgr_prc_read_options pro;
    pgr_glb_read_options ro;
    pgr_glb_write_options gwo;
    scene_diff d;
    const char *slash;
    FILE *f;
    int st, ok;

    snprintf(path, sizeof(path), "%s/%s", PGR_NANOPRC_DIR, file);
    f = fopen(path, "rb");
    if (f == NULL)
    {
        fprintf(stderr, "RT3: %s not present, skipped\n", path);
        return;
    }
    fclose(f);
    slash = strrchr(file, '/');
    snprintf(base, sizeof(base), "%s", slash ? slash + 1 : file);
    snprintf(glb, sizeof(glb), "rt3_%s.glb", base);
    snprintf(prc, sizeof(prc), "rt3_%s.prc", base);
    memset(&wo, 0, sizeof(wo));
    memset(&pro, 0, sizeof(pro));
    memset(&gwo, 0, sizeof(gwo));
    ro.messages = wo.messages = pro.messages = gwo.messages = &msg;
    pgr_scene_init(&p1);
    pgr_scene_init(&g);
    pgr_scene_init(&p2);

    st = pgr_prc_read(path, &pro, &p1, &rs);
    if (st == PGR_OK)
        st = pgr_glb_write(&p1, glb, &gwo);
    if (st == PGR_OK)
        st = pgr_glb_read(glb, &ro, &g);
    if (st == PGR_OK)
        st = pgr_prc_write(&g, prc, NULL, &wo, NULL);
    if (st == PGR_OK)
        st = pgr_prc_read(prc, &pro, &p2, &rs);
    if (st != PGR_OK)
        fprintf(stderr, "RT3 %s: status %d\n", file, st);
    PGR_CHECK(st == PGR_OK);
    ok = scene_compare(&p1, &p2, 1e-5, &d) == 0;
    if (!ok || d.triangles_a == 0)
        fprintf(stderr, "RT3 %s: triangles %u -> %u, segments %u -> %u, unmatched %u, max error %g\n",
                file, d.triangles_a, d.triangles_b, d.segments_a, d.segments_b, d.unmatched, d.max_error);
    PGR_CHECK(ok && d.triangles_a > 0);
    PGR_CHECK(scene_same_names(&p1, &p2));
    pgr_scene_free(&p1);
    pgr_scene_free(&g);
    pgr_scene_free(&p2);
}

/* A PRC with no root of pgr_prc_write's is converted by the reader itself:
   read raw (metres per unit, axes kept), its box (x, y, z) must come out of
   the default read as (x, z, -y) / 1000. */
static void
conversion(const char *file)
{
    char path[1024];
    pgr_scene raw, conv;
    pgr_prc_read_options o;
    pgr_prc_read_stats rs;
    double rmn[3], rmx[3], cmn[3], cmx[3], tol;
    FILE *f;

    snprintf(path, sizeof(path), "%s/%s", PGR_NANOPRC_DIR, file);
    f = fopen(path, "rb");
    if (f == NULL)
    {
        fprintf(stderr, "conversion: %s not present, skipped\n", path);
        return;
    }
    fclose(f);
    pgr_scene_init(&raw);
    pgr_scene_init(&conv);
    memset(&o, 0, sizeof(o));
    o.messages = &msg;
    PGR_CHECK(pgr_prc_read(path, &o, &conv, &rs) == PGR_OK);
    PGR_CHECK(rs.collapsed_root == 0);
    o.no_axis_convert = 1;
    o.units_per_metre = PGR_UNITS_PER_METRE_M;
    PGR_CHECK(pgr_prc_read(path, &o, &raw, NULL) == PGR_OK);
    if (!scene_bounds(&raw, rmn, rmx) || !scene_bounds(&conv, cmn, cmx))
    {
        PGR_CHECK(0);
        pgr_scene_free(&raw);
        pgr_scene_free(&conv);
        return;
    }
    tol =1e-9 * (fabs(rmx[0]) + fabs(rmx[1]) + fabs(rmx[2]) + fabs(rmn[0]) + fabs(rmn[1]) + fabs(rmn[2]) + 1.0);
    PGR_CHECK(fabs(cmn[0] - rmn[0] / 1000.0) < tol && fabs(cmx[0] - rmx[0] / 1000.0) < tol);
    PGR_CHECK(fabs(cmn[1] - rmn[2] / 1000.0) < tol && fabs(cmx[1] - rmx[2] / 1000.0) < tol);
    PGR_CHECK(fabs(cmn[2] + rmx[1] / 1000.0) < tol && fabs(cmx[2] + rmn[1] / 1000.0) < tol);
    pgr_scene_free(&raw);
    pgr_scene_free(&conv);
}

static unsigned char *
slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *d;
    long len;

    *n = 0;
    if (f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0 || (d = (unsigned char *)malloc((size_t)len + 1)) == NULL)
    {
        fclose(f);
        return NULL;
    }
    if (fread(d, 1, (size_t)len, f) != (size_t)len)
    {
        free(d);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *n = (size_t)len;
    return d;
}

/* One trip: glTF in -> PRC -> glTF out. */
static int
trip(const char *in, const char *prc, const char *out, int triangles)
{
    pgr_scene s;
    pgr_glb_read_options ro;
    pgr_prc_write_options wo;
    pgr_prc_read_options pro;
    pgr_glb_write_options gwo;
    int st;

    memset(&wo, 0, sizeof(wo));
    memset(&pro, 0, sizeof(pro));
    memset(&gwo, 0, sizeof(gwo));
    ro.messages = wo.messages = pro.messages = gwo.messages = &msg;
    wo.triangles = triangles;
    pgr_scene_init(&s);
    st = pgr_glb_read(in, &ro, &s);
    if (st == PGR_OK)
        st = pgr_prc_write(&s, prc, NULL, &wo, NULL);
    pgr_scene_free(&s);
    pgr_scene_init(&s);
    if (st == PGR_OK)
        st = pgr_prc_read(prc, &pro, &s, NULL);
    if (st == PGR_OK)
        st = pgr_glb_write(&s, out, &gwo);
    pgr_scene_free(&s);
    return st;
}

static void
rt6(const char *name, int (*make)(const char *), int triangles)
{
    char g1[256], g2[256], g3[256], p1[256], p2[256];
    const char *kind = triangles ? "tri" : "cmp";

    snprintf(g1, sizeof(g1), "rt6_%s_1.glb", name);
    snprintf(g2, sizeof(g2), "rt6_%s_%s_2.glb", name, kind);
    snprintf(g3, sizeof(g3), "rt6_%s_%s_3.glb", name, kind);
    snprintf(p1, sizeof(p1), "rt6_%s_%s_1.prc", name, kind);
    snprintf(p2, sizeof(p2), "rt6_%s_%s_2.prc", name, kind);
    PGR_CHECK(make(g1) == 0);
    PGR_CHECK(trip(g1, p1, g2, triangles) == PGR_OK);
    PGR_CHECK(trip(g2, p2, g3, triangles) == PGR_OK);
    if (triangles)
    {
        unsigned char *a, *b;
        size_t na, nb;

        a = slurp(g2, &na);
        b = slurp(g3, &nb);
        PGR_CHECK(a != NULL && b != NULL && na == nb && na > 0 && memcmp(a, b, na) == 0);
        if (a != NULL && b != NULL && (na != nb || memcmp(a, b, na < nb ? na : nb) != 0))
            fprintf(stderr, "RT6 %s: second and third glTF differ (%u and %u bytes)\n", name,
                    (unsigned)na, (unsigned)nb);
        free(a);
        free(b);
    }
    else
    {
        pgr_scene s2, s3;
        scene_diff d;

        pgr_scene_init(&s2);
        pgr_scene_init(&s3);
        PGR_CHECK(pgr_glb_read(g2, NULL, &s2) == PGR_OK && pgr_glb_read(g3, NULL, &s3) == PGR_OK);
        PGR_CHECK(s2.num_nodes == s3.num_nodes && s2.num_meshes == s3.num_meshes);
        PGR_CHECK(scene_same_names(&s2, &s3));
        PGR_CHECK(scene_compare(&s2, &s3, 1e-5, &d) == 0);
        pgr_scene_free(&s2);
        pgr_scene_free(&s3);
    }
}

int
main(void)
{
    static const struct
    {
        const char *name;
        int (*make)(const char *);
    } fixtures[] = {
        { "cube_flat", fx_cube_flat },
        { "cube_smooth", fx_cube_smooth },
        { "two_instances", fx_two_instances },
        { "hierarchy3", fx_hierarchy3 },
        { "multi_material", fx_multi_material },
        { "strip_fan", fx_strip_fan },
        { "lines", fx_lines },
        { "no_scene", fx_no_scene },
    };
    static const char *const prc_files[] = {
        "examples/cube.pdf", "examples/triangle.pdf", "examples/cylinder.pdf",
        "tests/fixtures/pcof_hasfacesT_6bit.prc", "tests/fixtures/pcof_hasfacesF_1bit.prc",
    };
    size_t i;

    for (i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); i++)
    {
        rt1_rt2(fixtures[i].name, fixtures[i].make, 1);
        rt1_rt2(fixtures[i].name, fixtures[i].make, 0);
        rt6(fixtures[i].name, fixtures[i].make, 1);
        rt6(fixtures[i].name, fixtures[i].make, 0);
    }
    for (i = 0; i < sizeof(prc_files) / sizeof(prc_files[0]); i++)
        rt3(prc_files[i]);
    conversion("examples/cylinder.pdf");
    return PGR_TEST_RESULT;
}
