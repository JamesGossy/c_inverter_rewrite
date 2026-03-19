#ifndef CONTROL_SVPWM_H
#define CONTROL_SVPWM_H

//-----------------------------------------------------------------------------
// Space Vector PWM
//
// Inputs:
//   Va, Vb, Vc  — three-phase voltage references (Volts)
//   Vdc         — DC bus voltage (Volts)
//
// Outputs:
//   dutyA, dutyB, dutyC — phase duty cycles in [0.0, 1.0]
//
// Method: midpoint-clamp (equivalent to standard 7-segment SVPWM)
//   1. Add a zero-sequence offset so the min phase sits at -Vdc/2
//      and the max phase sits at +Vdc/2 — this is SVPWM modulation
//   2. Normalise to duty cycles
//-----------------------------------------------------------------------------
void SVPWM_calc(float Va, float Vb, float Vc, float Vdc, float *dutyA, float *dutyB, float *dutyC);

#endif // CONTROL_SVPWM_H
