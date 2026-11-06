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

#include "pgr_message.h"
#include "pgr_info.h"

#include <stdarg.h>
#include <stdio.h>

static void
deliver(pgr_messages *m, int is_error, const char *fmt, va_list ap)
{
    char text[1024];

    vsnprintf(text, sizeof(text), fmt, ap);
    if (m != NULL)
    {
        if (is_error)
            m->errors++;
        else
            m->warnings++;
    }
    if (m != NULL && m->fn != NULL)
        m->fn(m->user, is_error, text);
    else
        fprintf(stderr, "%s: %s\n", is_error ? "error" : "warning", text);
}

void
pgr_warn_once(pgr_messages *m, unsigned kind, const char *fmt, ...)
{
    va_list ap;

    if (m != NULL)
    {
        if (m->warned & kind)
            return;
        m->warned |= kind;
    }
    va_start(ap, fmt);
    deliver(m, 0, fmt, ap);
    va_end(ap);
}

void
pgr_warn(pgr_messages *m, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    deliver(m, 0, fmt, ap);
    va_end(ap);
}

void
pgr_error(pgr_messages *m, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    deliver(m, 1, fmt, ap);
    va_end(ap);
}

int
pgr_exit_code(int status)
{
    switch (status)
    {
    case PGR_OK:
        return PGR_EXIT_OK;
    case PGR_ERR_PARSE:
        return PGR_EXIT_READ;
    case PGR_ERR_UNSUPPORTED:
        return PGR_EXIT_UNSUPPORTED;
    case PGR_ERR_WRITE:
        return PGR_EXIT_WRITE;
    case PGR_ERR_ARG:
        return PGR_EXIT_USAGE;
    default:
        /* Out of memory, or an unknown status, is reported as a read
           failure: the input could not be processed. */
        return PGR_EXIT_READ;
    }
}
