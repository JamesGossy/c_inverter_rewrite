//#############################################################################
//
// FILE:   sensors/voltage.c
//
// TITLE:  DC bus voltage ADC read and resistor-divider scaling
//
//#############################################################################

#include "sensors/voltage.h"

uint16_t Voltage_getRawDCBus(void)
{
    return ADC_readResult(VDC_ADC_RESULT_BASE, VDC_ADC_SOC);
}

float Voltage_getDCBus(void)
{
    return ((float)Voltage_getRawDCBus() / VOLTAGE_ADC_COUNTS_MAX) * VOLTAGE_VBUS_FULL_SCALE_V;
}
