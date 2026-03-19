#include "pi.h"

void PI_init(PI_t *pi, float Kp, float Ki, float outMin, float outMax)
{
    pi->Kp          = Kp;
    pi->Ki          = Ki;
    pi->outMin      = outMin;
    pi->outMax      = outMax;
    pi->integrator  = 0.0f;
}

void PI_reset(PI_t *pi)
{
    pi->integrator = 0.0f;
}

//-----------------------------------------------------------------------------
// PI update
//
//   out = Kp * error + integrator
//
// Anti-windup: the integrator only accumulates when the output is not
// already clamped — this stops the integrator from growing unbounded
// when the controller is saturated.
//-----------------------------------------------------------------------------
float PI_update(PI_t *pi, float error)
{
    float out = pi->Kp * error + pi->integrator;

    // Clamp output
    if      (out > pi->outMax) out = pi->outMax;
    else if (out < pi->outMin) out = pi->outMin;
    else
    {
        // Only integrate when not saturated
        pi->integrator += pi->Ki * error;
    }

    return out;
}
