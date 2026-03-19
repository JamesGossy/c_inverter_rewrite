//#############################################################################
//
// FILE:   sensors/current.c
//
// TITLE:  Phase current ADC reads and zero-current offset calibration
//
//#############################################################################

#include "sensors/current.h"

uint16_t Current_getRawA(void)
{
    return ADC_readResult(IA_ADC_RESULT_BASE, IA_ADC_SOC);
}

uint16_t Current_getRawB(void)
{
    return ADC_readResult(IB_ADC_RESULT_BASE, IB_ADC_SOC);
}

uint16_t Current_getRawC(void)
{
    return ADC_readResult(IC_ADC_RESULT_BASE, IC_ADC_SOC);
}

void Current_runCalibration(CurrentCalibration_t *cal)
{
    uint32_t sumA = 0, sumB = 0, sumC = 0;
    uint16_t i;

    for(i = 0; i < CURRENT_CAL_SAMPLES; i++)
    {
        sumA += ADC_readResult(IA_ADC_RESULT_BASE, IA_ADC_SOC);
        sumB += ADC_readResult(IB_ADC_RESULT_BASE, IB_ADC_SOC);
        sumC += ADC_readResult(IC_ADC_RESULT_BASE, IC_ADC_SOC);
        DEVICE_DELAY_US(100);   // wait for next 20 kHz conversion (50 us period)
    }

    cal->offsetA = (float)sumA / (float)CURRENT_CAL_SAMPLES;
    cal->offsetB = (float)sumB / (float)CURRENT_CAL_SAMPLES;
    cal->offsetC = (float)sumC / (float)CURRENT_CAL_SAMPLES;
}

float Current_getPhaseA(const CurrentCalibration_t *cal)
{
    return ((float)Current_getRawA() - cal->offsetA) * CURRENT_GAIN_A_PER_COUNT;
}

float Current_getPhaseB(const CurrentCalibration_t *cal)
{
    return ((float)Current_getRawB() - cal->offsetB) * CURRENT_GAIN_A_PER_COUNT;
}

float Current_getPhaseC(const CurrentCalibration_t *cal)
{
    return ((float)Current_getRawC() - cal->offsetC) * CURRENT_GAIN_A_PER_COUNT;
}
