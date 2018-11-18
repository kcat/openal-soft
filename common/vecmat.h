#ifndef COMMON_VECMAT_H
#define COMMON_VECMAT_H

#include "AL/al.h"


struct aluVector {
    alignas(16) ALfloat v[4];
};

inline void aluVectorSet(aluVector *vector, ALfloat x, ALfloat y, ALfloat z, ALfloat w)
{
    vector->v[0] = x;
    vector->v[1] = y;
    vector->v[2] = z;
    vector->v[3] = w;
}


struct aluMatrixf {
    alignas(16) ALfloat m[4][4];

    static const aluMatrixf Identity;
};

inline void aluMatrixfSetRow(aluMatrixf *matrix, ALuint row,
                             ALfloat m0, ALfloat m1, ALfloat m2, ALfloat m3)
{
    matrix->m[row][0] = m0;
    matrix->m[row][1] = m1;
    matrix->m[row][2] = m2;
    matrix->m[row][3] = m3;
}

inline void aluMatrixfSet(aluMatrixf *matrix, ALfloat m00, ALfloat m01, ALfloat m02, ALfloat m03,
                                              ALfloat m10, ALfloat m11, ALfloat m12, ALfloat m13,
                                              ALfloat m20, ALfloat m21, ALfloat m22, ALfloat m23,
                                              ALfloat m30, ALfloat m31, ALfloat m32, ALfloat m33)
{
    aluMatrixfSetRow(matrix, 0, m00, m01, m02, m03);
    aluMatrixfSetRow(matrix, 1, m10, m11, m12, m13);
    aluMatrixfSetRow(matrix, 2, m20, m21, m22, m23);
    aluMatrixfSetRow(matrix, 3, m30, m31, m32, m33);
}

#endif /* COMMON_VECMAT_H */
