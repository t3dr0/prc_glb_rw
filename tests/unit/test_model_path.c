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

/* model_path_resolve on a small tree made here: paths used as written when
   they only go down; absolute paths, drive letters and ".." reduced to a
   name and searched for inside the model's folder only; nearest first,
   alphabetical, exact before letter case, at most four levels down; and,
   where links can be made, never through a link. */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "model_path.h"
#include "pgr_test.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define make_dir(p) mkdir((p), 0755)
#endif

static void
touch(const char *path)
{
    FILE *f = fopen(path, "wb");

    if (f != NULL)
    {
        fputs("x", f);
        fclose(f);
    }
}

static void
make_tree(void)
{
    static const char *const dirs[] = {
        "mp", "mp/model", "mp/model/textures", "mp/model/z", "mp/model/a", "mp/model/a/b",
        "mp/model/b", "mp/model/c", "mp/model/Case", "mp/model/s", "mp/model/x", "mp/model/x/y", "mp/model/x/y/z",
        "mp/model/x/y/z/w", "mp/model/1", "mp/model/1/2", "mp/model/1/2/3", "mp/model/1/2/3/4",
        "mp/model/1/2/3/4/5", "mp/outside"
    };
    static const char *const files[] = {
        "mp/model/scene.gltf", "mp/model/textures/wood.png", "mp/model/z/stone.png",
        "mp/model/a/b/stone.png", "mp/model/b/brick.png", "mp/model/c/brick.png",
        "mp/model/Case/Leaf.PNG", "mp/model/x/y/z/w/Bark.png", "mp/model/s/BARK.png",
        "mp/model/1/2/3/4/four.png", "mp/model/1/2/3/4/5/deep.png", "mp/secret.png",
        "mp/outside/only.png"
    };
    size_t i;

    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
        make_dir(dirs[i]);
    for (i = 0; i < sizeof(files) / sizeof(files[0]); i++)
        touch(files[i]);
}

/* Resolve ref for the model and check the result and the path found
   (NULL for none). */
static void
expect(const char *ref, int result, const char *path)
{
    char note[512];
    char *out = NULL;
    int r = model_path_resolve("mp/model/scene.gltf", ref, &out, note, sizeof(note));

    if (r != result)
        fprintf(stderr, "ref '%s': result %d, expected %d (%s)\n", ref, r, result, note);
    PGR_CHECK(r == result);
    if (path == NULL)
        PGR_CHECK(out == NULL);
    else
        PGR_CHECK_STR(out, path);
    /* A search, found or not, explains itself. */
    if (result != MODEL_PATH_AS_WRITTEN)
        PGR_CHECK(note[0] != '\0');
    free(out);
}

int
main(void)
{
    char note[256];
    char *out = NULL;

    make_tree();

    /* 1. As written. */
    expect("textures/wood.png", MODEL_PATH_AS_WRITTEN, "mp/model/textures/wood.png");
    expect("textures\\wood.png", MODEL_PATH_AS_WRITTEN, "mp/model/textures/wood.png");
    expect("./textures/./wood.png", MODEL_PATH_AS_WRITTEN, "mp/model/textures/wood.png");

    /* 2. Paths that leave the folder, or come from another machine,
          become a search by name. */
    expect("C:\\Users\\me\\textures\\wood.png", MODEL_PATH_SUBSTITUTED, "mp/model/textures/wood.png");
    expect("/home/me/wood.png", MODEL_PATH_SUBSTITUTED, "mp/model/textures/wood.png");
    expect("textures/../textures/wood.png", MODEL_PATH_SUBSTITUTED, "mp/model/textures/wood.png");
    expect("D:wood.png", MODEL_PATH_SUBSTITUTED, "mp/model/textures/wood.png");
    expect("../secret.png", MODEL_PATH_NOT_FOUND, NULL);       /* exists, but outside */
    expect("../../mp/secret.png", MODEL_PATH_NOT_FOUND, NULL);
    expect("missing/wood.png", MODEL_PATH_SUBSTITUTED, "mp/model/textures/wood.png");

    /* Nearest first, then alphabetical. */
    expect("stone.png", MODEL_PATH_SUBSTITUTED, "mp/model/z/stone.png");
    expect("brick.png", MODEL_PATH_SUBSTITUTED, "mp/model/b/brick.png");
    PGR_CHECK(model_path_resolve("mp/model/scene.gltf", "brick.png", &out, note, sizeof(note))
              == MODEL_PATH_SUBSTITUTED);
    PGR_CHECK(strstr(note, "1 other file") != NULL);
    free(out);

    /* 3. Letter case only when nothing matches exactly, however deep. */
    expect("leaf.png", MODEL_PATH_SUBSTITUTED, "mp/model/Case/Leaf.PNG");
    expect("Bark.png", MODEL_PATH_SUBSTITUTED, "mp/model/x/y/z/w/Bark.png");

    /* Four levels down is searched; five is not. */
    expect("four.png", MODEL_PATH_SUBSTITUTED, "mp/model/1/2/3/4/four.png");
    expect("deep.png", MODEL_PATH_NOT_FOUND, NULL);

    /* Not files. */
    expect("http://example.com/wood.png", MODEL_PATH_NOT_FOUND, NULL);
    expect("data:image/png;base64,AAAA", MODEL_PATH_NOT_FOUND, NULL);
    expect("", MODEL_PATH_NOT_FOUND, NULL);
    expect("textures/", MODEL_PATH_NOT_FOUND, NULL);
    expect("..", MODEL_PATH_NOT_FOUND, NULL);

#ifndef _WIN32
    /* A link out of the folder is not followed, by name or as written. */
    if (symlink("../outside", "mp/model/link") == 0)
    {
        expect("only.png", MODEL_PATH_NOT_FOUND, NULL);
        expect("link/only.png", MODEL_PATH_NOT_FOUND, NULL);
    }
#endif
    return PGR_TEST_RESULT;
}
