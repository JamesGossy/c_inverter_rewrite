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
//      adcResults[0..3]  — Ia, Ib, Ic, Vdc (updated at 20 kHz)
//      epwmIsrCount      — EPWM4 current loop counter (20 kHz)
//      bissSpiRxData     — last BiSS SPI read (updated at 20 kHz)
//      bissRxCount       — BiSS RX ISR counter (20 kHz)
//      timer0IsrCount    — Timer0 base rate counter (2 kHz)
//      canTxCount        — CAN messages sent (~1 Hz)
//      loopCount         — main loop iterations (~1 Hz)
//
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "boostxl_periph.h"

//-----------------------------------------------------------------------------
// Global variables (watch in CCS Expressions)
//-----------------------------------------------------------------------------
volatile uint16_t adcResults[4]   = {0};  // [0]=Ia [1]=Ib [2]=Ic [3]=Vdc
volatile uint32_t epwmIsrCount    = 0;    // INT3.4 current loop counter
volatile uint16_t bissSpiRxData   = 0;    // Latest BiSS SPI result
volatile uint32_t bissRxCount     = 0;    // INT6.1 BiSS RX counter
volatile uint32_t timer0IsrCount  = 0;    // 2 kHz base rate counter
volatile uint32_t canTxCount      = 0;
volatile bool     canRxFlag       = false;
volatile uint16_t canRxData[4]    = {0};
volatile uint32_t loopCount       = 0;

// Scope toggle pin
#define TMR_ISR_TOGGLE_GPIO     15U       // J8-73: Timer0 base rate toggle
#define TMR_ISR_TOGGLE_PIN_CFG  GPIO_15_GPIO15

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
    // Main loop (background) — CAN, nEN toggle, LEDs, faults
    //
    while(1)
    {
        GPIO_togglePin(DEVICE_GPIO_PIN_LED1);
        BOOSTXL_enableInverter();
        DEVICE_DELAY_US(500000);

        GPIO_togglePin(DEVICE_GPIO_PIN_LED2);
        BOOSTXL_disableInverter();

        // CAN TX: send ADC results
        {
            uint16_t canTxBuf[4];
            canTxBuf[0] = adcResults[0];
            canTxBuf[1] = adcResults[1];
            canTxBuf[2] = adcResults[2];
            canTxBuf[3] = adcResults[3];
            BOOSTXL_canSend(8U, canTxBuf);
            canTxCount++;
        }

        // CAN RX polling
        canRxFlag = BOOSTXL_canRead((uint16_t*)canRxData);

        // Serial — send Hello World once per loop iteration (~1 Hz)
        BOOSTXL_serialSendString("Hello World\r\n");

        loopCount++;
    }
}

//-----------------------------------------------------------------------------
// INT6.1 — SPIA RX @ 20 kHz (BiSS SPI completion)
// Priority 35, non-preemptable — reads encoder data when SPI transfer done
//-----------------------------------------------------------------------------
__interrupt void spiaRxISR(void)
{
    bissSpiRxData = SPI_readDataNonBlocking(BISS_SPI_BASE);

    bissRxCount++;

    SPI_clearInterruptStatus(BISS_SPI_BASE, SPI_INT_RXFF);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP6);
}

//-----------------------------------------------------------------------------
// INT3.4 — EPWM4 INT @ 20 kHz (current controller)
// Priority 29, non-preemptable — highest priority, reads ADC + kicks BiSS SPI
//-----------------------------------------------------------------------------
__interrupt void epwm4ISR(void)
{
    // Read ADC results (conversions already complete from EPWM4 SOCA)
    adcResults[0] = BOOSTXL_readPhaseACurrent();
    adcResults[1] = BOOSTXL_readPhaseBCurrent();
    adcResults[2] = BOOSTXL_readPhaseCCurrent();
    adcResults[3] = BOOSTXL_readDCBusVoltage();

    //
    // ---- Current controller would go here ----
    // Clarke(Ia, Ib) -> Park(Ialpha, Ibeta, theta) -> PI_d, PI_q
    // -> InvPark(Vd, Vq, theta) -> SVPWM -> setDuty()
    //

    // Kick off BiSS SPI transfer (non-blocking write, SPIA RX ISR reads result)
    SPI_writeDataNonBlocking(BISS_SPI_BASE, 0xFFFF);

    epwmIsrCount++;

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

