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

/* Status codes, and the warnings and errors the readers and writers report.

   Content a tool skips (points, skins, animations and so on) is reported
   once per kind, not once per occurrence: each kind has a bit, and a
   warning whose bit is already set is not repeated. */

#ifndef PGR_MESSAGE_H
#define PGR_MESSAGE_H

/* Status codes returned by the shared code; PGR_OK is 0, failures are
   negative. pgr_exit_code() maps them to the tools' exit codes. */
#define PGR_OK               0
#define PGR_ERR_PARSE       -1  /* the input could not be read or parsed */
#define PGR_ERR_UNSUPPORTED -2  /* the input needs something not supported */
#define PGR_ERR_NOMEM       -3
#define PGR_ERR_WRITE       -4  /* the output could not be written */
#define PGR_ERR_ARG         -5  /* invalid arguments or an inconsistent scene */

/* Kinds of skipped or altered content, one bit each. */
#define PGR_WARN_POINTS          (1u << 0)
#define PGR_WARN_SKINS           (1u << 1)
#define PGR_WARN_ANIMATIONS      (1u << 2)
#define PGR_WARN_MORPH_TARGETS   (1u << 3)
#define PGR_WARN_CAMERAS         (1u << 4)
#define PGR_WARN_LIGHTS          (1u << 5)
#define PGR_WARN_TEXTURES        (1u << 6)
#define PGR_WARN_NO_POSITION     (1u << 7)
#define PGR_WARN_ATTRIBUTE       (1u << 8)
#define PGR_WARN_DEGENERATE      (1u << 9)
#define PGR_WARN_ZERO_NORMAL     (1u << 10)
#define PGR_WARN_EMPTY_MESH      (1u << 11)
#define PGR_WARN_EXTENSION       (1u << 12)
#define PGR_WARN_MULTIPLE_SCENES (1u << 13)
#define PGR_WARN_TEXCOORD        (1u << 14)
#define PGR_WARN_ALPHA_MASK      (1u << 15)
#define PGR_WARN_WRAP            (1u << 16)
#define PGR_WARN_NO_UVS          (1u << 17)
#define PGR_WARN_UNLIT           (1u << 18)
#define PGR_WARN_MASKED_OUT      (1u << 19)
#define PGR_WARN_TEXTURE_KIND    (1u << 20)
#define PGR_WARN_TEXTURE_LAYERS  (1u << 21)
#define PGR_WARN_TEXTURE_IMAGE   (1u << 22)
#define PGR_WARN_DRACO           (1u << 23)

typedef void (*pgr_message_fn)(void *user, int is_error, const char *text);

/* Where messages go. A NULL pgr_messages, or one whose fn is NULL, prints
   to stderr. */
typedef struct pgr_messages
{
    pgr_message_fn fn;
    void *user;
    unsigned warned;         /* PGR_WARN_* bits already reported */
    unsigned warnings;       /* number of warnings reported */
    unsigned errors;         /* number of errors reported */
} pgr_messages;

/* Report a warning of the given kind unless that kind was already reported. */
void pgr_warn_once(pgr_messages *m, unsigned kind, const char *fmt, ...);

/* Report a warning every time, for things the user needs to see each of,
   such as a file found somewhere other than where the model said. */
void pgr_warn(pgr_messages *m, const char *fmt, ...);

/* Report an error. */
void pgr_error(pgr_messages *m, const char *fmt, ...);

/* The tool exit code for a status: 0 ok, 1 usage, 2 read, 3 unsupported,
   4 write. */
int pgr_exit_code(int status);

#endif
