#ifndef CONTROL_PI_H
#define CONTROL_PI_H

//-----------------------------------------------------------------------------
// Basic PI controller with integrator anti-windup (output clamping)
//-----------------------------------------------------------------------------

typedef struct {
    float Kp;           // proportional gain
    float Ki;           // integral gain (per sample, i.e. Ki_continuous * Ts)
    float outMin;       // output lower clamp
    float outMax;       // output upper clamp
    float integrator;   // integrator state
} PI_t;

// Set gains and limits.  Resets integrator state to 0.
void PI_init(PI_t *pi, float Kp, float Ki, float outMin, float outMax);

// Zero the integrator without changing gains.
void PI_reset(PI_t *pi);

// Run one control step.  Returns clamped output.
// Call at a fixed rate; Ki should already include the sample period Ts.
float PI_update(PI_t *pi, float error);

#endif // CONTROL_PI_H
