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

/* 4x4 matrices, column-major as in glTF and nanoPRC's model tree: element
   (row r, column c) is m[4 * c + r], and a point p maps to m * p, so the
   translation is m[12], m[13], m[14]. */

#ifndef PGR_MATH_H
#define PGR_MATH_H

void pgr_mat4_identity(double m[16]);

/* out = a * b, so out applies b first; out may be a or b. */
void pgr_mat4_mul(const double a[16], const double b[16], double out[16]);

/* out = the inverse of m; returns 1, or 0 when m is singular (out untouched). */
int pgr_mat4_invert(const double m[16], double out[16]);

/* 1 when every element of a and b differs by at most tol. */
int pgr_mat4_equal(const double a[16], const double b[16], double tol);
int pgr_mat4_is_identity(const double m[16], double tol);

/* T * R * S, glTF's order: translation t, rotation the quaternion
   q = (x, y, z, w), scale s. */
void pgr_mat4_from_trs(const double t[3], const double q[4], const double s[3], double out[16]);

/* m * (p, 1) and m * (v, 0). out may be p or v. */
void pgr_mat4_transform_point(const double m[16], const double p[3], double out[3]);
void pgr_mat4_transform_vector(const double m[16], const double v[3], double out[3]);

/* The matrix that carries normals under m: the inverse transpose of m's
   upper 3x3, column-major in n. Returns 0 when that 3x3 is singular. */
int pgr_mat4_normal_matrix(const double m[16], double n[9]);

/* The matrix that takes glTF's +Y up to PRC's +Z up and scales by scale:
   (x, y, z) -> scale * (x, -z, y), a quarter-turn about +X. Its inverse
   takes PRC back to glTF. */
void pgr_mat4_y_up_to_z_up(double scale, double out[16]);

/* One colour component between glTF's linear values and the sRGB values
   PRC viewers show as stored, by the sRGB transfer function of
   IEC 61966-2-1. The value is clamped to 0..1 first; NaN gives 0. */
double pgr_linear_to_srgb(double c);
double pgr_srgb_to_linear(double c);

/* A value in 0..1 as a byte, rounded; clamped, and NaN gives 0. */
unsigned char pgr_unit_to_byte(double c);

#endif
