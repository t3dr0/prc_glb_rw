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

/* Minimal checks for the unit tests. A failed check prints where it failed
   and counts; a test's main returns PGR_TEST_RESULT, so CTest sees the
   failure as a non-zero exit. */

#ifndef PGR_TEST_H
#define PGR_TEST_H

#include <stdio.h>
#include <string.h>

static int pgr_test_failures = 0;

#define PGR_CHECK(cond)                                                     \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #cond);                             \
            pgr_test_failures++;                                            \
        }                                                                   \
    } while (0)

#define PGR_CHECK_STR(a, b)                                                 \
    do {                                                                    \
        const char *pgr_a_ = (a), *pgr_b_ = (b);                            \
        if (pgr_a_ == NULL || pgr_b_ == NULL || strcmp(pgr_a_, pgr_b_) != 0) { \
            fprintf(stderr, "%s:%d: check failed: %s == %s (\"%s\" vs \"%s\")\n", \
                    __FILE__, __LINE__, #a, #b,                             \
                    pgr_a_ ? pgr_a_ : "(null)", pgr_b_ ? pgr_b_ : "(null)"); \
            pgr_test_failures++;                                            \
        }                                                                   \
    } while (0)

#define PGR_TEST_RESULT (pgr_test_failures == 0 ? 0 : 1)

#endif
