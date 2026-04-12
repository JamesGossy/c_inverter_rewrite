#ifndef SENSORS_CURRENT_H
#define SENSORS_CURRENT_H

#include "hal/hal_boostxl.h"

//-----------------------------------------------------------------------------
// Current sensor calibration
//
// TODO: Set CURRENT_GAIN_A_PER_COUNT for your hardware.
//   gain = (full-scale amps * 2) / 4096
//   Example: ±50 A range → gain = 100 / 4096 = 0.02441 A/count
//-----------------------------------------------------------------------------
#define CURRENT_CAL_SAMPLES         1000U
#define CURRENT_GAIN_A_PER_COUNT    0.0008057f

typedef struct {
    float offsetB;   // zero-current ADC count, phase B
    float offsetC;   // zero-current ADC count, phase C
} CurrentCalibration_t;

// Raw ADC reads (12-bit counts, 0-4095)
uint16_t Current_getRawB(void);
uint16_t Current_getRawC(void);

// Average CURRENT_CAL_SAMPLES at zero current to find DC offsets.
// Call once at startup before enabling the inverter.
void Current_runCalibration(CurrentCalibration_t *cal);

// Scaled reads in amps — require a completed calibration
// Phase A computed from KVL: Ia = -(Ib + Ic)
float Current_getPhaseA(float ib, float ic);
float Current_getPhaseB(const CurrentCalibration_t *cal);
float Current_getPhaseC(const CurrentCalibration_t *cal);

#endif // SENSORS_CURRENT_H
