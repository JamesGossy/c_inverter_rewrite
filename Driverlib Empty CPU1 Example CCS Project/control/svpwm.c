#include "svpwm.h"

// Simple min/max helpers
static float fmin2(float a, float b) { return (a < b) ? a : b; }
static float fmax2(float a, float b) { return (a > b) ? a : b; }
static float fclamp(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

void SVPWM_calc(float Va, float Vb, float Vc, float Vdc,
                float *dutyA, float *dutyB, float *dutyC)
{
    // Step 1: Add zero-sequence (midpoint clamp) offset
    //
    // This centres the three-phase voltages so the highest phase just
    // reaches +Vdc/2 and the lowest just reaches -Vdc/2.
    //
    float Vmax    = fmax2(Va, fmax2(Vb, Vc));
    float Vmin    = fmin2(Va, fmin2(Vb, Vc));
    float Voffset = -0.5f * (Vmax + Vmin);

    Va += Voffset;
    Vb += Voffset;
    Vc += Voffset;

    // Step 3: Normalise to duty cycle [0, 1]
    //
    //   duty = V_phase / Vdc + 0.5
    //   (maps -Vdc/2 -> 0.0, 0 -> 0.5, +Vdc/2 -> 1.0)
    //
    float invVdc = 1.0f / Vdc;

    *dutyA = fclamp(Va * invVdc + 0.5f, 0.0f, 1.0f);
    *dutyB = fclamp(Vb * invVdc + 0.5f, 0.0f, 1.0f);
    *dutyC = fclamp(Vc * invVdc + 0.5f, 0.0f, 1.0f);
}
