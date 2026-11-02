#pragma once
// CPU-side replacements for glMatrixLoadIdentityEXT / glMatrixScalefEXT / glMatrixMultfEXT.
// Piglet has none of those desktop-only extensions. Matrices are column-major, like OpenGL.
// The arrays themselves are defined once, in GLShim.cpp.

#include <string.h>

extern float glShimProjection[16];
extern float glShimModelView[16];

static inline void MatIdentity(float *m)
{
    memset(m, 0, sizeof(float) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static inline void MatScale(float *m, float x, float y, float z)
{
    for (int i = 0; i < 4; ++i) {
        m[i] *= x;
        m[4 + i] *= y;
        m[8 + i] *= z;
    }
}

// m = m * b
static inline void MatMul(float *m, const float *b)
{
    float r[16];
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            r[c * 4 + row] = m[row] * b[c * 4] + m[4 + row] * b[c * 4 + 1] + m[8 + row] * b[c * 4 + 2] + m[12 + row] * b[c * 4 + 3];
    memcpy(m, r, sizeof(r));
}
