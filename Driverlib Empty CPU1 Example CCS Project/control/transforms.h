#ifndef CONTROL_TRANSFORMS_H
#define CONTROL_TRANSFORMS_H

//-----------------------------------------------------------------------------
// Clarke and Park transforms (and their inverses)
//
// Convention: amplitude-invariant (|Ialpha, Ibeta| == |Ia|)
// Angle theta: electrical radians, 0 = phase A axis
//-----------------------------------------------------------------------------

// Clarke: 3-phase (a, b, c) -> stationary 2-phase (alpha, beta)
// Assumes balanced load: Ia + Ib + Ic = 0  (Ic is not needed)
void Clarke(float Ia, float Ib, float *Ialpha, float *Ibeta);

// Inverse Clarke: stationary 2-phase (alpha, beta) -> 3-phase (a, b, c)
void ClarkeInv(float Valpha, float Vbeta, float *Va, float *Vb, float *Vc);

// Park: stationary (alpha, beta) -> rotating (d, q)
void Park(float Ialpha, float Ibeta, float theta, float *Id, float *Iq);

// Inverse Park: rotating (d, q) -> stationary (alpha, beta)
void ParkInv(float Vd, float Vq, float theta, float *Valpha, float *Vbeta);

#endif // CONTROL_TRANSFORMS_H
