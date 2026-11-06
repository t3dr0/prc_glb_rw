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

#include "pgr_math.h"

#include <math.h>
#include <string.h>

void
pgr_mat4_identity(double m[16])
{
    memset(m, 0, 16 * sizeof(double));
    m[0] = m[5] = m[10] = m[15] = 1.0;
}

void
pgr_mat4_mul(const double a[16], const double b[16], double out[16])
{
    double r[16];
    int row, col, k;

    for (col = 0; col < 4; col++)
    {
        for (row = 0; row < 4; row++)
        {
            double sum = 0.0;

            for (k = 0; k < 4; k++)
                sum += a[4 * k + row] * b[4 * col + k];
            r[4 * col + row] = sum;
        }
    }
    memcpy(out, r, sizeof(r));
}

int
pgr_mat4_invert(const double m[16], double out[16])
{
    /* Cofactor expansion; the layout of m does not matter, since the
       inverse of the transpose is the transpose of the inverse. */
    double inv[16], det;
    int i;

    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15]
           + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15]
           - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15]
           + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14]
            - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15]
           - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15]
           + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15]
           - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14]
            + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15]
           + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15]
           - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15]
            + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14]
            - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11]
           - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11]
           + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11]
            - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10]
            + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0.0 || !isfinite(det))
        return 0;
    det = 1.0 / det;
    for (i = 0; i < 16; i++)
        out[i] = inv[i] * det;
    return 1;
}

int
pgr_mat4_equal(const double a[16], const double b[16], double tol)
{
    int i;

    for (i = 0; i < 16; i++)
        if (!(fabs(a[i] - b[i]) <= tol))
            return 0;
    return 1;
}

int
pgr_mat4_is_identity(const double m[16], double tol)
{
    double id[16];

    pgr_mat4_identity(id);
    return pgr_mat4_equal(m, id, tol);
}

void
pgr_mat4_from_trs(const double t[3], const double q[4], const double s[3], double out[16])
{
    double x = q[0], y = q[1], z = q[2], w = q[3];

    /* Rotation columns, each scaled by its axis's scale, as R * S. */
    out[0] = (1.0 - 2.0 * (y * y + z * z)) * s[0];
    out[1] = (2.0 * (x * y + z * w)) * s[0];
    out[2] = (2.0 * (x * z - y * w)) * s[0];
    out[3] = 0.0;
    out[4] = (2.0 * (x * y - z * w)) * s[1];
    out[5] = (1.0 - 2.0 * (x * x + z * z)) * s[1];
    out[6] = (2.0 * (y * z + x * w)) * s[1];
    out[7] = 0.0;
    out[8] = (2.0 * (x * z + y * w)) * s[2];
    out[9] = (2.0 * (y * z - x * w)) * s[2];
    out[10] = (1.0 - 2.0 * (x * x + y * y)) * s[2];
    out[11] = 0.0;
    out[12] = t[0];
    out[13] = t[1];
    out[14] = t[2];
    out[15] = 1.0;
}

void
pgr_mat4_transform_point(const double m[16], const double p[3], double out[3])
{
    double r[3];
    int i;

    for (i = 0; i < 3; i++)
        r[i] = m[i] * p[0] + m[4 + i] * p[1] + m[8 + i] * p[2] + m[12 + i];
    memcpy(out, r, sizeof(r));
}

void
pgr_mat4_transform_vector(const double m[16], const double v[3], double out[3])
{
    double r[3];
    int i;

    for (i = 0; i < 3; i++)
        r[i] = m[i] * v[0] + m[4 + i] * v[1] + m[8 + i] * v[2];
    memcpy(out, r, sizeof(r));
}

int
pgr_mat4_normal_matrix(const double m[16], double n[9])
{
    /* a is m's upper 3x3, a[3 * c + r] = m[4 * c + r]. The inverse
       transpose is the cofactor matrix divided by the determinant. */
    double a[9], det;
    int c, r;

    for (c = 0; c < 3; c++)
        for (r = 0; r < 3; r++)
            a[3 * c + r] = m[4 * c + r];

    /* cof(r, c) stored at n[3 * c + r]. */
    n[0] = a[4] * a[8] - a[7] * a[5];
    n[1] = -(a[3] * a[8] - a[6] * a[5]);
    n[2] = a[3] * a[7] - a[6] * a[4];
    n[3] = -(a[1] * a[8] - a[7] * a[2]);
    n[4] = a[0] * a[8] - a[6] * a[2];
    n[5] = -(a[0] * a[7] - a[6] * a[1]);
    n[6] = a[1] * a[5] - a[4] * a[2];
    n[7] = -(a[0] * a[5] - a[3] * a[2]);
    n[8] = a[0] * a[4] - a[3] * a[1];

    det = a[0] * n[0] + a[3] * n[3] + a[6] * n[6];
    if (det == 0.0 || !isfinite(det))
        return 0;
    for (c = 0; c < 9; c++)
        n[c] /= det;
    return 1;
}

void
pgr_mat4_y_up_to_z_up(double scale, double out[16])
{
    memset(out, 0, 16 * sizeof(double));
    out[0] = scale;    /* x -> x */
    out[6] = scale;    /* y -> z */
    out[9] = -scale;   /* z -> -y */
    out[15] = 1.0;
}
