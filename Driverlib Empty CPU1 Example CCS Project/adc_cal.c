//#############################################################################
//
// FILE:   adc_cal.c
//
// TITLE:  ADC offset calibration and scaled reads
//         BOOSTXL-3PhGaNInv on LAUNCHXL-F28379D
//
//#############################################################################

#include "adc_cal.h"

//-----------------------------------------------------------------------------
// Global calibration data — pre-loaded with ideal mid-scale offsets.
// Replaced by measured values after ADC_CAL_runOffsetCalibration().
//-----------------------------------------------------------------------------
AdcCalData_t gAdcCal = {2048.0f, 2048.0f, 2048.0f, false};

//-----------------------------------------------------------------------------
// forceSample — software-trigger one SOC and return the result.
//
// Uses a 1 µs fixed wait. Safe because:
//   ADCCLK = SYSCLK/4 = 50 MHz (20 ns/cycle)
//   Max conversion time = sample window (20 cycles = 400 ns) + convert (~75 ns)
//                       = ~475 ns < 1 µs
//
// Not used outside calibration — in normal operation SOCs are triggered
// by EPWM4 SOCA.
//-----------------------------------------------------------------------------
static uint16_t forceSample(uint32_t base, ADC_SOCNumber soc,
                             uint32_t resultBase, ADC_SOCNumber resultSOC)
{
    ADC_forceSOC(base, soc);
    DEVICE_DELAY_US(1);
    return ADC_readResult(resultBase, resultSOC);
}

//-----------------------------------------------------------------------------
// ADC_CAL_runOffsetCalibration
//
// Averages ADC_CAL_NUM_SAMPLES readings from each current channel to find
// the true zero-current ADC count (amplifier DC offset + bias).
//
// Requirements:
//   - Call after BOOSTXL_initADC() so SOCs are configured.
//   - Call before EINT / control loop start.
//   - Inverter must be disabled (nEN high) and no current flowing.
//-----------------------------------------------------------------------------
void ADC_CAL_runOffsetCalibration(void)
{
    uint32_t iaAcc = 0U;
    uint32_t ibAcc = 0U;
    uint32_t icAcc = 0U;
    uint16_t i;

    for(i = 0U; i < ADC_CAL_NUM_SAMPLES; i++)
    {
        iaAcc += forceSample(IA_ADC_BASE, IA_ADC_SOC,
                             IA_ADC_RESULT_BASE, IA_ADC_SOC);

        ibAcc += forceSample(IB_ADC_BASE, IB_ADC_SOC,
                             IB_ADC_RESULT_BASE, IB_ADC_SOC);

        // Ic is SOC1 on the same ADCC module as Ib (SOC0).
        // Forced separately so they do not race.
        icAcc += forceSample(IC_ADC_BASE, IC_ADC_SOC,
                             IC_ADC_RESULT_BASE, IC_ADC_SOC);
    }

    gAdcCal.iaOffset = (float32_t)iaAcc / (float32_t)ADC_CAL_NUM_SAMPLES;
    gAdcCal.ibOffset = (float32_t)ibAcc / (float32_t)ADC_CAL_NUM_SAMPLES;
    gAdcCal.icOffset = (float32_t)icAcc / (float32_t)ADC_CAL_NUM_SAMPLES;
    gAdcCal.isCalibrated = true;
}

//-----------------------------------------------------------------------------
// Calibrated current reads [Amps]
//
// Formula: I = (raw - offset) * gain
//   Positive = current flowing into phase (motoring convention)
//-----------------------------------------------------------------------------
float32_t ADC_CAL_getIa_A(void)
{
    return ((float32_t)BOOSTXL_readPhaseACurrent() - gAdcCal.iaOffset)
           * CURRENT_GAIN_A_PER_COUNT;
}

float32_t ADC_CAL_getIb_A(void)
{
    return ((float32_t)BOOSTXL_readPhaseBCurrent() - gAdcCal.ibOffset)
           * CURRENT_GAIN_A_PER_COUNT;
}

float32_t ADC_CAL_getIc_A(void)
{
    return ((float32_t)BOOSTXL_readPhaseCCurrent() - gAdcCal.icOffset)
           * CURRENT_GAIN_A_PER_COUNT;
}

//-----------------------------------------------------------------------------
// Calibrated DC bus voltage read [Volts]
//
// Formula: Vdc = raw * (Vref / counts_full_scale) * divider_ratio
//   No offset term — resistor divider reads 0 V at 0 V bus.
//-----------------------------------------------------------------------------
float32_t ADC_CAL_getVdc_V(void)
{
    return (float32_t)BOOSTXL_readDCBusVoltage()
           * (3.3f / 4096.0f) * VDC_DIVIDER_RATIO;
}
