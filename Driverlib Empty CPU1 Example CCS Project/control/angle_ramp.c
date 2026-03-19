#include "angle_ramp.h"

void AngleRamp_init(AngleRamp_t *r, float freqHz, float Ts)
{
    r->theta = 0.0f;
    r->Ts    = Ts;
    r->omega = ANGLE_RAMP_TWO_PI * freqHz;
}

float AngleRamp_update(AngleRamp_t *r)
{
    r->theta += r->omega * r->Ts;
    if(r->theta >= ANGLE_RAMP_TWO_PI)
        r->theta -= ANGLE_RAMP_TWO_PI;
    return r->theta;
}

void AngleRamp_setFreq(AngleRamp_t *r, float freqHz)
{
    r->omega = ANGLE_RAMP_TWO_PI * freqHz;
}

void AngleRamp_reset(AngleRamp_t *r)
{
    r->theta = 0.0f;
}
