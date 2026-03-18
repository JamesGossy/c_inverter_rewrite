//#############################################################################
//
// FILE:   boostxl_3phganinv_periph.h
//
// TITLE:  Peripheral setup for BOOSTXL-3PhGaNInv on LAUNCHXL-F28379D
//
// DESCRIPTION:
//   BOOSTXL-3PhGaNInv on BoosterPack site 2 (J5-J8)
//   BiSS-C decoder board on BoosterPack site 1 (J1-J4)
//
//   Peripherals:
//     - EPWM4/5/6: 3-phase complementary PWM with dead band (20 kHz)
//     - ADC B/C/D: Phase current + DC bus voltage (EPWM4 SOCA triggered)
//     - GPIO26:    Inverter enable (nEN, active low)
//     - CPU Timer0: 2 kHz base rate interrupt (speed loop)
//     - SPIA:      BiSS-C encoder interface on site 1 (RX interrupt)
//     - CANA:      CAN bus via on-board transceiver (J12)
//
//   ISR architecture:
//     INT1.3  ADCB INT3   @ 20 kHz — debug GPIO toggle (pri 30, preemptable)
//     INT6.1  SPIA RX     @ 20 kHz — BiSS SPI completion (pri 35)
//     INT3.4  EPWM4 INT   @ 20 kHz — current controller + ADC + transforms (pri 29)
//     Timer0              @  2 kHz — speed controller + field weakening
//     Main loop           — CAN TX/RX, faults, LED, non-critical
//
//#############################################################################

#ifndef BOOSTXL_3PHGANINV_PERIPH_H
#define BOOSTXL_3PHGANINV_PERIPH_H

#include "driverlib.h"
#include "device.h"

//=============================================================================
// PINOUT TABLE
//
// --- Site 2 (J5-J8): BOOSTXL-3PhGaNInv ---
// Function         | BP Pin | LP Header-Pin | GPIO/ADC        | Peripheral
// -----------------+--------+---------------+-----------------+-------------
// Phase A PWM_H    |   40   | J8-80         | GPIO6           | EPWM4A
// Phase A PWM_L    |   39   | J8-79         | GPIO7           | EPWM4B
// Phase B PWM_H    |   38   | J8-78         | GPIO8           | EPWM5A
// Phase B PWM_L    |   37   | J8-77         | GPIO9           | EPWM5B
// Phase C PWM_H    |   36   | J8-76         | GPIO10          | EPWM6A
// Phase C PWM_L    |   35   | J8-75         | GPIO11          | EPWM6B
// Phase A current  |   25   | J7-65         | ADCINB5         | ADCB ch5
// Phase B current  |   27   | J7-67         | ADCINC4         | ADCC ch4
// Phase C current  |   24   | J7-64         | ADCINC5         | ADCC ch5
// DC bus voltage   |   23   | J7-63         | ADCIN15         | ADCD ch15
// Inverter nEN     |   13   | J6-53         | GPIO26          | GPIO out
//
// --- Site 1 (J1-J4): BiSS-C Decoder Board ---
// SPI SIMO (BiSS)  |   15   | J2-15         | GPIO58          | SPISIMOA
// SPI SOMI (BiSS)  |   14   | J2-14         | GPIO59          | SPISOMIA
// SPI CLK  (BiSS)  |    7   | J1-7          | GPIO60          | SPICLKA
// SPI STE  (BiSS)  |   19   | J2-19         | GPIO61          | SPISTEA
//
// --- On-board (not on BP headers) ---
// CAN TX           |   --   | (on-board)    | GPIO37          | CANTXA
// CAN RX           |   --   | (on-board)    | GPIO36          | CANRXA
//=============================================================================

//-----------------------------------------------------------------------------
// PWM Configuration — 20 kHz switching
//-----------------------------------------------------------------------------
#define PHASE_A_PWM_BASE        EPWM4_BASE
#define PHASE_B_PWM_BASE        EPWM5_BASE
#define PHASE_C_PWM_BASE        EPWM6_BASE

#define PHASE_A_PWM_H_GPIO      6U
#define PHASE_A_PWM_L_GPIO      7U
#define PHASE_B_PWM_H_GPIO      8U
#define PHASE_B_PWM_L_GPIO      9U
#define PHASE_C_PWM_H_GPIO      10U
#define PHASE_C_PWM_L_GPIO      11U

#define PHASE_A_PWM_H_PIN_CFG   GPIO_6_EPWM4A
#define PHASE_A_PWM_L_PIN_CFG   GPIO_7_EPWM4B
#define PHASE_B_PWM_H_PIN_CFG   GPIO_8_EPWM5A
#define PHASE_B_PWM_L_PIN_CFG   GPIO_9_EPWM5B
#define PHASE_C_PWM_H_PIN_CFG   GPIO_10_EPWM6A
#define PHASE_C_PWM_L_PIN_CFG   GPIO_11_EPWM6B

// 20 kHz, up-down count, EPWMCLK = 100 MHz
// TBPRD = 100e6 / (2 * 20e3) = 2500
#define PWM_SWITCHING_FREQ_HZ   20000U
#define PWM_TBPRD               2500U

// Dead band: 10 counts * 10 ns = 100 ns
#define PWM_DEADBAND_RED_COUNT  10U
#define PWM_DEADBAND_FED_COUNT  10U

//-----------------------------------------------------------------------------
// ADC Configuration
//-----------------------------------------------------------------------------
#define IA_ADC_BASE             ADCB_BASE
#define IA_ADC_RESULT_BASE      ADCBRESULT_BASE
#define IA_ADC_CHANNEL          ADC_CH_ADCIN5
#define IA_ADC_SOC              ADC_SOC_NUMBER0

#define IB_ADC_BASE             ADCC_BASE
#define IB_ADC_RESULT_BASE      ADCCRESULT_BASE
#define IB_ADC_CHANNEL          ADC_CH_ADCIN4
#define IB_ADC_SOC              ADC_SOC_NUMBER0

#define IC_ADC_BASE             ADCC_BASE
#define IC_ADC_RESULT_BASE      ADCCRESULT_BASE
#define IC_ADC_CHANNEL          ADC_CH_ADCIN5
#define IC_ADC_SOC              ADC_SOC_NUMBER1

#define VDC_ADC_BASE            ADCD_BASE
#define VDC_ADC_RESULT_BASE     ADCDRESULT_BASE
#define VDC_ADC_CHANNEL         ADC_CH_ADCIN14
#define VDC_ADC_SOC             ADC_SOC_NUMBER0

#define ADC_SAMPLE_WINDOW       20U
#define ADC_TRIGGER_SOURCE      ADC_TRIGGER_EPWM4_SOCA

//-----------------------------------------------------------------------------
// Inverter Enable (nEN) — active low
//-----------------------------------------------------------------------------
#define INV_EN_GPIO             26U
#define INV_EN_PIN_CFG          GPIO_26_GPIO26

//-----------------------------------------------------------------------------
// CPU Timer 0 — 2 kHz base rate
//-----------------------------------------------------------------------------
#define TIMER0_FREQ_HZ          2000U
#define TIMER0_PERIOD_COUNTS    (200000000UL / TIMER0_FREQ_HZ)

//-----------------------------------------------------------------------------
// SPIA — BiSS-C Encoder on Site 1 (J1-J4)
//-----------------------------------------------------------------------------
#define BISS_SPI_BASE           SPIA_BASE

#define BISS_SIMO_GPIO          58U
#define BISS_SOMI_GPIO          59U
#define BISS_CLK_GPIO           60U
#define BISS_STE_GPIO           61U

#define BISS_SIMO_PIN_CFG       GPIO_58_SPISIMOA
#define BISS_SOMI_PIN_CFG       GPIO_59_SPISOMIA
#define BISS_CLK_PIN_CFG        GPIO_60_SPICLKA
#define BISS_STE_PIN_CFG        GPIO_61_SPISTEA

#define BISS_SPI_BITRATE        5000000U
#define BISS_SPI_DATAWIDTH      16U

//-----------------------------------------------------------------------------
// SCIA — On-board USB-to-serial (virtual COM port, J1-3/J1-4)
//   115200 8N1 — connect laptop via micro-USB / XDS110 COM port
//-----------------------------------------------------------------------------
#define SERIAL_SCI_BASE         SCIA_BASE
#define SERIAL_TX_GPIO          42U
#define SERIAL_RX_GPIO          43U
#define SERIAL_TX_PIN_CFG       GPIO_42_SCITXDA
#define SERIAL_RX_PIN_CFG       GPIO_43_SCIRXDA
#define SERIAL_BAUD             115200U

//-----------------------------------------------------------------------------
// CANA — CAN Bus (on-board transceiver, J12)
//-----------------------------------------------------------------------------
#define CAN_PERIPH_BASE         CANA_BASE

#define CAN_TX_GPIO             37U
#define CAN_RX_GPIO             36U
#define CAN_TX_PIN_CFG          GPIO_37_CANTXA
#define CAN_RX_PIN_CFG          GPIO_36_CANRXA

#define CAN_BITRATE             500000U
#define CAN_BIT_TIME            20U

#define CAN_TX_MSG_OBJ_ID       1U
#define CAN_RX_MSG_OBJ_ID       2U
#define CAN_TX_MSG_ID           0x100U
#define CAN_RX_MSG_ID           0x200U

//-----------------------------------------------------------------------------
// Function Prototypes
//-----------------------------------------------------------------------------

void BOOSTXL_initPWM(void);
void BOOSTXL_initADC(void);
void BOOSTXL_initGPIO(void);
void BOOSTXL_initTimer0(void);
void BOOSTXL_initBissSPI(void);
void BOOSTXL_initCAN(void);
void BOOSTXL_initAll(void);

void BOOSTXL_enableInverter(void);
void BOOSTXL_disableInverter(void);
void BOOSTXL_setDuty(uint32_t pwmBase, float32_t duty);

uint16_t BOOSTXL_readPhaseACurrent(void);
uint16_t BOOSTXL_readPhaseBCurrent(void);
uint16_t BOOSTXL_readPhaseCCurrent(void);
uint16_t BOOSTXL_readDCBusVoltage(void);

uint16_t BOOSTXL_bissSpiTransfer(uint16_t txData);

void BOOSTXL_canSend(uint16_t msgLen, const uint16_t* msgData);
bool BOOSTXL_canRead(uint16_t* msgData);

void BOOSTXL_initSerial(void);
void BOOSTXL_serialSendString(const char *str);

#endif // BOOSTXL_3PHGANINV_PERIPH_H

