//#############################################################################
//
// FILE:   peripheral_test.c
//
// TITLE:  Peripheral Test for BOOSTXL-3PhGaNInv on LAUNCHXL-F28379D
//
// DESCRIPTION:
//   ISR architecture matches the big inverter Simulink model:
//
//   INT6.1  SPIA RX     @ 20 kHz — BiSS SPI RX completion
//   INT3.4  EPWM4 INT   @ 20 kHz — current controller (reads ADC, updates PWM)
//   Timer0              @  2 kHz — speed controller / field weakening
//   Main loop           — CAN TX/RX, nEN toggle, LEDs, faults
//
//   WHAT TO PROBE:
//      Pin         | Signal          | Expected
//      ------------+-----------------+-----------------------------------------
//      J8-80       | EPWM4A (Ph A H) | 20 kHz, 25% duty
//      J8-79       | EPWM4B (Ph A L) | Complementary + 100 ns dead band
//      J8-78       | EPWM5A (Ph B H) | 20 kHz, 50% duty
//      J8-77       | EPWM5B (Ph B L) | Complementary
//      J8-76       | EPWM6A (Ph C H) | 20 kHz, 75% duty
//      J8-75       | EPWM6B (Ph C L) | Complementary
//      J6-53       | GPIO26 (nEN)    | ~1 Hz square wave
//      J8-73       | GPIO15 (Timer0) | 2 kHz toggle -> 1 kHz sq wave
//      J1-7        | SPICLKA         | 5 MHz bursts at 20 kHz (BiSS)
//      J2-15       | SPISIMOA        | 0xFFFF during SPI burst
//      J2-14       | SPISOMIA        | Encoder data (noise if none)
//      J12         | CANH/CANL       | ~1 Hz CAN frames (ID 0x100)
//
//   CCS EXPRESSIONS:
//      adcResults[0..2]  — Ia, Ib, Ic raw counts (updated at 20 kHz)
//      vdcVolts          — Calibrated DC bus voltage in V (updated at 20 kHz)
//      timer0IsrCount    — Timer0 base rate counter (2 kHz)
//
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "boostxl_periph.h"
#include "sensors/voltage.h"
#include "sensors/current.h"

//-----------------------------------------------------------------------------
// Global variables (watch in CCS Expressions)
//-----------------------------------------------------------------------------
volatile float    iAAmps          = 0.0f; // Calibrated phase A current (A)
volatile float    iBAmps          = 0.0f; // Calibrated phase B current (A)
volatile float    iCAmps          = 0.0f; // Calibrated phase C current (A)
volatile float    vdcVolts        = 0.0f; // Calibrated DC bus voltage (V)
volatile uint32_t timer0IsrCount  = 0;    // 2 kHz base rate counter

static CurrentCalibration_t currentCal = {0};

// Scope toggle pin
#define TMR_ISR_TOGGLE_GPIO     15U       // J8-73: Timer0 base rate toggle
#define TMR_ISR_TOGGLE_PIN_CFG  GPIO_15_GPIO15

//-----------------------------------------------------------------------------
// Append a signed float (2 decimal places) to buf at pos; returns new pos.
//-----------------------------------------------------------------------------
static uint16_t appendFloat(char *buf, uint16_t pos, float val)
{
    char     tmp[5];
    uint16_t len = 0;
    uint16_t intPart;
    uint16_t fracPart;

    if (val < 0.0f) { buf[pos++] = '-'; val = -val; }

    intPart  = (uint16_t)val;
    fracPart = (uint16_t)((val - (float)intPart) * 100.0f + 0.5f);
    if (fracPart >= 100U) { intPart++; fracPart = 0U; }

    do { tmp[len++] = (char)('0' + (intPart % 10U)); intPart /= 10U; } while (intPart);
    while (len--) buf[pos++] = tmp[len];

    buf[pos++] = '.';
    buf[pos++] = (char)('0' + (fracPart / 10U));
    buf[pos++] = (char)('0' + (fracPart % 10U));
    return pos;
}

//-----------------------------------------------------------------------------
// Format Ia, Ib, Ic (calibrated amps) + Vdc (volts) as "A.AA,B.BB,C.CC,D.DD\r\n"
// without using printf/sprintf
//-----------------------------------------------------------------------------
static void sendSensorData(void)
{
    char     buf[48];
    uint16_t pos = 0;

    pos = appendFloat(buf, pos, iAAmps);  buf[pos++] = ',';
    pos = appendFloat(buf, pos, iBAmps);  buf[pos++] = ',';
    pos = appendFloat(buf, pos, iCAmps);  buf[pos++] = ',';
    pos = appendFloat(buf, pos, vdcVolts);

    buf[pos++] = '\r';
    buf[pos++] = '\n';
    buf[pos]   = '\0';
    BOOSTXL_serialSendString(buf);
}

//-----------------------------------------------------------------------------
// ISR prototypes
//-----------------------------------------------------------------------------
__interrupt void spiaRxISR(void);       // INT6.1 — SPIA RX (BiSS completion)
__interrupt void epwm4ISR(void);        // INT3.4 — EPWM4 (current controller)
__interrupt void timer0ISR(void);       // Timer0 — 2 kHz base rate

//-----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------
void main(void)
{
    Device_init();
    Device_initGPIO();
    Interrupt_initModule();
    Interrupt_initVectorTable();

    //
    // Initialize ALL peripherals
    //
    BOOSTXL_initAll();

    //
    // Test duty cycles
    //
    BOOSTXL_setDuty(PHASE_A_PWM_BASE, 0.25f);
    BOOSTXL_setDuty(PHASE_B_PWM_BASE, 0.50f);
    BOOSTXL_setDuty(PHASE_C_PWM_BASE, 0.75f);

    //
    // Scope toggle pin
    //
    GPIO_setPinConfig(TMR_ISR_TOGGLE_PIN_CFG);
    GPIO_setDirectionMode(TMR_ISR_TOGGLE_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(TMR_ISR_TOGGLE_GPIO, GPIO_PIN_TYPE_STD);
    GPIO_writePin(TMR_ISR_TOGGLE_GPIO, 0);

    //
    // LEDs
    //
    GPIO_setPinConfig(DEVICE_GPIO_CFG_LED1);
    GPIO_setDirectionMode(DEVICE_GPIO_PIN_LED1, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(DEVICE_GPIO_PIN_LED1, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(DEVICE_GPIO_CFG_LED2);
    GPIO_setDirectionMode(DEVICE_GPIO_PIN_LED2, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(DEVICE_GPIO_PIN_LED2, GPIO_PIN_TYPE_STD);

    //
    // Register ISRs — matching Simulink interrupt block configuration
    //
    //
    // Calibrate current sensors at zero current (inverter still disabled)
    //
    Current_runCalibration(&currentCal);

    //
    // Register ISRs — matching Simulink interrupt block configuration
    //
    Interrupt_register(INT_SPIA_RX, &spiaRxISR);    // INT6.1 (CPU6, PIE1)
    Interrupt_register(INT_EPWM4, &epwm4ISR);       // INT3.4 (CPU3, PIE4)
    Interrupt_register(INT_TIMER0, &timer0ISR);

    Interrupt_enable(INT_SPIA_RX);
    Interrupt_enable(INT_EPWM4);
    Interrupt_enable(INT_TIMER0);

    EINT;
    ERTM;

    BOOSTXL_disableInverter();

    //
    // Main loop (background) — serial TX as fast as possible, LED/CAN at 1 Hz
    //
    // LED and inverter toggle every 500 ms — gated by timer0IsrCount
    // (Timer0 @ 2 kHz, so 1000 ticks = 500 ms)
    //
    uint32_t lastToggleTick = 0;
    bool     invEnabled     = false;

    while(1)
    {
        // Serial — send calibrated sensor data as CSV at full loop rate
        sendSensorData();

        // LED flash + inverter toggle + CAN TX every 500 ms
        if ((timer0IsrCount - lastToggleTick) >= 1000U)
        {
            lastToggleTick = timer0IsrCount;

            if (invEnabled)
            {
                GPIO_togglePin(DEVICE_GPIO_PIN_LED2);
                BOOSTXL_disableInverter();
                invEnabled = false;
            }
            else
            {
                GPIO_togglePin(DEVICE_GPIO_PIN_LED1);
                BOOSTXL_enableInverter();
                invEnabled = true;
            }

            // CAN TX: send raw phase current counts
            {
                uint16_t canTxBuf[4];
                canTxBuf[0] = Current_getRawA();
                canTxBuf[1] = 0U;
                canTxBuf[2] = Current_getRawC();
                canTxBuf[3] = 0U;
                BOOSTXL_canSend(8U, canTxBuf);
            }
        }
    }
}

//-----------------------------------------------------------------------------
// INT6.1 — SPIA RX @ 20 kHz (BiSS SPI completion)
// Priority 35, non-preemptable — reads encoder data when SPI transfer done
//-----------------------------------------------------------------------------
__interrupt void spiaRxISR(void)
{
    (void)SPI_readDataNonBlocking(BISS_SPI_BASE);   // drain RX FIFO

    SPI_clearInterruptStatus(BISS_SPI_BASE, SPI_INT_RXFF);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP6);
}

//-----------------------------------------------------------------------------
// INT3.4 — EPWM4 INT @ 20 kHz (current controller)
// Priority 29, non-preemptable — highest priority, reads ADC + kicks BiSS SPI
//-----------------------------------------------------------------------------
__interrupt void epwm4ISR(void)
{
    // Read calibrated sensor values (conversions complete from EPWM4 SOCA)
    iAAmps   = Current_getPhaseA(&currentCal);
    iCAmps   = Current_getPhaseC(&currentCal);
    iBAmps   = Current_getPhaseB(iAAmps, iCAmps);
    vdcVolts = Voltage_getDCBus();

    //
    // ---- Current controller would go here ----
    // Clarke(Ia, Ib) -> Park(Ialpha, Ibeta, theta) -> PI_d, PI_q
    // -> InvPark(Vd, Vq, theta) -> SVPWM -> setDuty()
    //

    // Kick off BiSS SPI transfer (non-blocking write, SPIA RX ISR reads result)
    SPI_writeDataNonBlocking(BISS_SPI_BASE, 0xFFFF);

    EPWM_clearEventTriggerInterruptFlag(PHASE_A_PWM_BASE);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP3);
}

//-----------------------------------------------------------------------------
// Timer 0 ISR — 2 kHz (base rate)
// Speed controller + field weakening
//-----------------------------------------------------------------------------
__interrupt void timer0ISR(void)
{
    GPIO_togglePin(TMR_ISR_TOGGLE_GPIO);

    //
    // ---- Speed controller + field weakening would go here ----
    // speed_error = speed_ref - speed_fb
    // Iq_ref = PI_speed(speed_error)
    //

    timer0IsrCount++;

    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}
