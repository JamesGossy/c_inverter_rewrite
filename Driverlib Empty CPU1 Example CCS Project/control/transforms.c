#include "transforms.h"
#include <math.h>

#define SQRT3       1.7320508f
#define INV_SQRT3   0.5773503f   // 1 / sqrt(3)

//-----------------------------------------------------------------------------
// Clarke transform
//
//   Ialpha = Ia
//   Ibeta  = (Ia + 2*Ib) / sqrt(3)
//
// Derivation: standard amplitude-invariant form with balanced load.
//-----------------------------------------------------------------------------
void Clarke(float Ia, float Ib, float *Ialpha, float *Ibeta)
{
    *Ialpha = Ia;
    *Ibeta  = (Ia + 2.0f * Ib) * INV_SQRT3;
}

//-----------------------------------------------------------------------------
// Inverse Clarke transform
//
//   Va =  Valpha
//   Vb = -Valpha/2 + sqrt(3)/2 * Vbeta
//   Vc = -Valpha/2 - sqrt(3)/2 * Vbeta
//-----------------------------------------------------------------------------
void ClarkeInv(float Valpha, float Vbeta, float *Va, float *Vb, float *Vc)
{
    *Va =  Valpha;
    *Vb = -0.5f * Valpha + 0.5f * SQRT3 * Vbeta;
    *Vc = -0.5f * Valpha - 0.5f * SQRT3 * Vbeta;
}

//-----------------------------------------------------------------------------
// Park transform
//
//   Id =  Ialpha * cos(theta) + Ibeta * sin(theta)
//   Iq = -Ialpha * sin(theta) + Ibeta * cos(theta)
//-----------------------------------------------------------------------------
void Park(float Ialpha, float Ibeta, float theta, float *Id, float *Iq)
{
    float cosTheta = cosf(theta);
    float sinTheta = sinf(theta);

    *Id =  Ialpha * cosTheta + Ibeta * sinTheta;
    *Iq = -Ialpha * sinTheta + Ibeta * cosTheta;
}

//-----------------------------------------------------------------------------
// Inverse Park transform
//
//   Valpha = Vd * cos(theta) - Vq * sin(theta)
//   Vbeta  = Vd * sin(theta) + Vq * cos(theta)
//-----------------------------------------------------------------------------
void ParkInv(float Vd, float Vq, float theta, float *Valpha, float *Vbeta)
{
    float cosTheta = cosf(theta);
    float sinTheta = sinf(theta);

    *Valpha = Vd * cosTheta - Vq * sinTheta;
    *Vbeta  = Vd * sinTheta + Vq * cosTheta;
}
