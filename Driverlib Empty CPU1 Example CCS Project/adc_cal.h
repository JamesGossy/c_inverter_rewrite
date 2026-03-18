//#############################################################################
//
// FILE:   adc_cal.h
//
// TITLE:  ADC offset calibration and scaled reads
//         BOOSTXL-3PhGaNInv on LAUNCHXL-F28379D
//
//#############################################################################

#ifndef ADC_CAL_H
#define ADC_CAL_H

#include "driverlib.h"
#include "device.h"
#include "boostxl_periph.h"

//-----------------------------------------------------------------------------
// Hardware scaling constants — verify against board schematic (SLVUB77)
//
// Current sensing:
//   Shunt:     R_shunt  = 10 mΩ  (!! confirm from schematic !!)
//   Amplifier: INA240A1, gain = 20 V/V  (!! confirm from schematic !!)
//   ADC:       Vref = 3.3 V, 12-bit (4096 counts)
//   Zero current => 1.65 V => ~2048 counts (amplifier output biased at Vref/2)
//
//   Sensitivity = R_shunt * Amp_gain * (4096 / 3.3)
//               = 0.010 * 20 * 1241.2 = 248.2 counts/A
//   CURRENT_GAIN_A_PER_COUNT = 1 / 248.2
//
// DC bus voltage:
//   Resistor divider scales bus voltage into ADC range.
//   VDC_DIVIDER_RATIO = (R_top + R_bottom) / R_bottom
//   Placeholder assumes ~48 V max bus. (!! confirm R_top, R_bottom from schematic !!)
//
// !! Also verify VDC_ADC_CHANNEL in boostxl_periph.h — pinout table says  !!
// !! ADCIN15 (ch15) but code defines ADC_CH_ADCIN14. Check schematic.     !!
//-----------------------------------------------------------------------------
#define CURRENT_GAIN_A_PER_COUNT    0.004029f   // A per ADC count
#define VDC_DIVIDER_RATIO           14.55f      // Vdc divider (R_top+R_bot)/R_bot

#define ADC_CAL_NUM_SAMPLES         1000U

//-----------------------------------------------------------------------------
// Calibration data struct — populated by ADC_CAL_runOffsetCalibration()
//-----------------------------------------------------------------------------
typedef struct
{
    float32_t iaOffset;     // Zero-current ADC count, phase A
    float32_t ibOffset;     // Zero-current ADC count, phase B
    float32_t icOffset;     // Zero-current ADC count, phase C
    bool      isCalibrated;
} AdcCalData_t;

extern AdcCalData_t gAdcCal;

//-----------------------------------------------------------------------------
// Function prototypes
//-----------------------------------------------------------------------------

// Call once at startup with inverter disabled and no current flowing.
void      ADC_CAL_runOffsetCalibration(void);

// Calibrated reads — use these in your ISR and control loop.
float32_t ADC_CAL_getIa_A(void);   // Phase A current [A]
float32_t ADC_CAL_getIb_A(void);   // Phase B current [A]
float32_t ADC_CAL_getIc_A(void);   // Phase C current [A]
float32_t ADC_CAL_getVdc_V(void);  // DC bus voltage  [V]

#endif // ADC_CAL_H
