#ifndef SENSORS_VOLTAGE_H
#define SENSORS_VOLTAGE_H

#include "boostxl_periph.h"

//-----------------------------------------------------------------------------
// DC bus voltage scaling
//
// TODO: Update R_TOP and R_BOT to match your board schematic.
//   The resistor divider scales Vbus down to the ADC input range (0-3V).
//   Full-scale Vbus = VREF * (R_TOP + R_BOT) / R_BOT
//-----------------------------------------------------------------------------
#define VOLTAGE_ADC_VREF_V          3.0f
#define VOLTAGE_ADC_COUNTS_MAX      4096.0f

#define VOLTAGE_VBUS_R_TOP_OHM      100000.0f   // placeholder — update for your board
#define VOLTAGE_VBUS_R_BOT_OHM      3300.0f     // placeholder — update for your board

#define VOLTAGE_VBUS_FULL_SCALE_V   (VOLTAGE_ADC_VREF_V * \
                                     (VOLTAGE_VBUS_R_TOP_OHM + VOLTAGE_VBUS_R_BOT_OHM) \
                                     / VOLTAGE_VBUS_R_BOT_OHM)

// Raw ADC read (12-bit counts, 0-4095)
uint16_t Voltage_getRawDCBus(void);

// Scaled read in volts
float Voltage_getDCBus(void);

#endif // SENSORS_VOLTAGE_H
