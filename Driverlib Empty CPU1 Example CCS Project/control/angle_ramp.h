#ifndef CONTROL_ANGLE_RAMP_H
#define CONTROL_ANGLE_RAMP_H

//-----------------------------------------------------------------------------
// Electrical angle accumulator for open-loop V/Hz spinning
//
// Call AngleRamp_update() once per control period (e.g. 20 kHz ISR).
// Returns theta in [0, 2*pi).
//-----------------------------------------------------------------------------

#define ANGLE_RAMP_TWO_PI   6.28318530f

typedef struct {
    float theta;    // current angle (rad), [0, 2*pi)
    float omega;    // electrical angular velocity (rad/s)
    float Ts;       // sample period (s)
} AngleRamp_t;

// Initialise with a target electrical frequency (Hz) and sample period (s).
void AngleRamp_init(AngleRamp_t *r, float freqHz, float Ts);

// Advance angle by one sample.  Returns updated theta.
float AngleRamp_update(AngleRamp_t *r);

// Change target frequency without resetting angle.
void AngleRamp_setFreq(AngleRamp_t *r, float freqHz);

// Reset angle to zero.
void AngleRamp_reset(AngleRamp_t *r);

#endif // CONTROL_ANGLE_RAMP_H
