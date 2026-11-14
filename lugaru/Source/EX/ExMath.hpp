/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef _EX_MATH_HPP_
#define _EX_MATH_HPP_

#include <cmath>
#include <cstring>

namespace EX
{
/**
 * CPU replacement for the "use the OpenGL matrix stack as a calculator"
 * pattern (glLoadIdentity/glRotatef/glTranslatef + glGetFloatv). Reading the
 * matrix back from the driver stalls it, and the original code did that per
 * vertex / per particle every frame. Same conventions as fixed-function GL:
 * column-major, each operation post-multiplies.
 */
struct Mat4
{
    float m[16];

    Mat4() { identity(); }

    void identity()
    {
        memset(m, 0, sizeof(m));
        m[0] = m[5] = m[10] = m[15] = 1;
    }

    void set(const float* src) { memcpy(m, src, sizeof(m)); }

    // this = this * b
    void mul(const float* b)
    {
        float r[16];
        for (int c = 0; c < 4; c++) {
            for (int rr = 0; rr < 4; rr++) {
                r[c * 4 + rr] = m[0 * 4 + rr] * b[c * 4 + 0] + m[1 * 4 + rr] * b[c * 4 + 1] + m[2 * 4 + rr] * b[c * 4 + 2] + m[3 * 4 + rr] * b[c * 4 + 3];
            }
        }
        memcpy(m, r, sizeof(m));
    }

    void translate(float x, float y, float z)
    {
        m[12] += m[0] * x + m[4] * y + m[8] * z;
        m[13] += m[1] * x + m[5] * y + m[9] * z;
        m[14] += m[2] * x + m[6] * y + m[10] * z;
        m[15] += m[3] * x + m[7] * y + m[11] * z;
    }

    void rotate(float angleDeg, float x, float y, float z)
    {
        // about a main axis (nearly every call): the same numbers as the general case below
        // (its zero terms add nothing; t + c is kept, it isn't always exactly 1 in floats),
        // with 3/4 of the multiplications left out
        if ((x == 1 || y == 1 || z == 1) && x + y + z == 1 && (x == 0 || y == 0) && (y == 0 || z == 0) && (x == 0 || z == 0)) {
            const float a = angleDeg * 3.14159265358979f / 180.f;
            const float c = cosf(a), s = sinf(a), t = 1 - c, one = t + c;
            // columns i, j of this matrix mix as (cos, sin) pairs; the axis column scales by one
            auto mix = [&](int i, int j, float cii, float cij, float cji, float cjj) {
                for (int rr = 0; rr < 4; rr++) {
                    const float mi = m[i * 4 + rr], mj = m[j * 4 + rr];
                    m[i * 4 + rr] = mi * cii + mj * cij;
                    m[j * 4 + rr] = mi * cji + mj * cjj;
                }
            };
            auto scale = [&](int k) {
                for (int rr = 0; rr < 4; rr++) {
                    m[k * 4 + rr] = m[k * 4 + rr] * one;
                }
            };
            if (x == 1) { // r = [one 0 0; 0 c s; 0 -s c] (columns)
                scale(0);
                mix(1, 2, c, s, -s, c);
            } else if (y == 1) { // columns (c,0,-s), (0,one,0), (s,0,c)
                scale(1);
                mix(0, 2, c, -s, s, c);
            } else { // columns (c,s,0), (-s,c,0), (0,0,one)
                scale(2);
                mix(0, 1, c, s, -s, c);
            }
            return;
        }
        rotateGeneral(angleDeg, x, y, z);
    }

    // any axis (the original glRotatef formula); also the reference for the main-axis case
    void rotateGeneral(float angleDeg, float x, float y, float z)
    {
        float len = sqrtf(x * x + y * y + z * z);
        if (len <= 0) {
            return;
        }
        x /= len;
        y /= len;
        z /= len;
        const float a = angleDeg * 3.14159265358979f / 180.f;
        const float c = cosf(a), s = sinf(a), t = 1 - c;
        float r[16] = {
            x * x * t + c, y * x * t + z * s, x * z * t - y * s, 0,
            x * y * t - z * s, y * y * t + c, y * z * t + x * s, 0,
            x * z * t + y * s, y * z * t - x * s, z * z * t + c, 0,
            0, 0, 0, 1
        };
        mul(r);
    }

    // translation column of (this * T(x,y,z)), i.e. what glTranslatef + glGetFloatv(M)[12..14] gave
    void point(float x, float y, float z, float& ox, float& oy, float& oz) const
    {
        ox = m[0] * x + m[4] * y + m[8] * z + m[12];
        oy = m[1] * x + m[5] * y + m[9] * z + m[13];
        oz = m[2] * x + m[6] * y + m[10] * z + m[14];
    }
};
}

#endif
