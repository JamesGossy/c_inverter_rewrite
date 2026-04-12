//#############################################################################
//
// FILE:   open_loop_test.c
//
// TITLE:  Open-loop V/Hz motor spin test
//
// DESCRIPTION:
//   Injects a rotating voltage vector at a fixed frequency and amplitude
//   (no current feedback — pure open-loop).  Use this to verify PWM,
//   gate drivers, and basic motor rotation before closing the current loop.
//
//   Signal chain (currentControlISR / INT_ADCB1, 20 kHz):
//     AngleRamp -> ParkInv(0, Vq) -> ClarkeInv -> SVPWM -> setDuty x3
//
//   Tuning knobs (adjust live in CCS Expressions):
//     openLoop_freqHz  — electrical frequency in Hz  (start: 5.0)
//     openLoop_Vq      — injected voltage in V       (start: ~3-5 V)
//     openLoop_enable  — 0 = inverter off, 1 = on
//
//   Safety:
//     - Start with openLoop_Vq small (e.g. 3 V on a 24 V bus = ~12% mod)
//     - Increase Vq slowly until the motor pulls into rotation
//     - Keep openLoop_enable = 0 until you are ready to spin
//
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "hal/hal_boostxl.h"
#include "sensors/voltage.h"
#include "sensors/current.h"
#include "control/angle_ramp.h"
#include "control/transforms.h"
#include "control/svpwm.h"
#include "comms/biss_api.h"

//-----------------------------------------------------------------------------
// Control knobs — set from serial (vd,vq,freq,enable\r\n) or CCS Expressions
//-----------------------------------------------------------------------------
volatile float    openLoop_freqHz  = 0.0f;   // Electrical frequency (Hz)
volatile float    openLoop_Vd      = 0.0f;   // Injected Vd (V) — normally 0
volatile float    openLoop_Vq      = 0.0f;   // Injected Vq (V)
volatile uint16_t openLoop_enable  = 0U;     // 1 = spin, 0 = coast

//-----------------------------------------------------------------------------
// Telemetry — watch in CCS Expressions
//-----------------------------------------------------------------------------
volatile float    iAAmps           = 0.0f;
volatile float    iBAmps           = 0.0f;
volatile float    iCAmps           = 0.0f;
volatile float    vdcVolts         = 0.0f;
volatile float    theta_rad        = 0.0f;
volatile uint16_t biss_angle_raw    = 0U;   // 13-bit raw count from BiSS encoder (0-8191)
volatile uint16_t biss_error        = 0U;   // last BiSS error code (0 = OK, see biss_api.h)
volatile uint16_t biss_spi_status   = 0U;   // raw SPISTS register at time of RX
volatile uint32_t timer0IsrCount   = 0U;

// BiSS encoder is 13-bit (8192 counts/rev): angle = raw * 2π/8192
#define BISS_COUNTS_PER_REV  8192U
#define BISS_RAD_PER_COUNT   (6.28318530f / (float)BISS_COUNTS_PER_REV)

//-----------------------------------------------------------------------------
// Module-private state
//-----------------------------------------------------------------------------
static AngleRamp_t          angleRamp   = {0};
static CurrentCalibration_t currentCal  = {0};

#define CONTROL_TS  (1.0f / (float)PWM_SWITCHING_FREQ_HZ)   // 50 µs

//-----------------------------------------------------------------------------
// Serial telemetry TX — binary, non-blocking
// Frame (16 bytes): [0xAA][0x55][Ia:i16][Ib:i16][Ic:i16][Vdc:u16][θ:u16][θ_enc:u16][biss_err:u16]
// Scales: currents ×100 (A), Vdc ×100 (V), θ ×10000 (rad), θ_enc ×10000 (rad), biss_err raw
// Big-endian. Fits in the 16-deep SCI TX FIFO; takes ~37 µs at 3 Mbaud.
//-----------------------------------------------------------------------------
static void sendTelemetryBinary(void)
{
    // Drop packet if the previous one is still draining (shouldn't happen at 1 kHz)
    if (SCI_getTxFIFOStatus(SERIAL_SCI_BASE) != SCI_FIFO_TX0)
        return;

    int16_t  ia16   = (int16_t) (iAAmps           * 100.0f);
    int16_t  ib16   = (int16_t) (iBAmps           * 100.0f);
    int16_t  ic16   = (int16_t) (iCAmps           * 100.0f);
    uint16_t vdc16  = (uint16_t)(vdcVolts          * 100.0f);
    uint16_t th16   = (uint16_t)(theta_rad         * 10000.0f);
    uint16_t enc16  = (uint16_t)((float)biss_angle_raw * BISS_RAD_PER_COUNT * 10000.0f);

    SCI_writeCharNonBlocking(SERIAL_SCI_BASE, 0xAAU);
    SCI_writeCharNonBlocking(SERIAL_SCI_BASE, 0x55U);
    SCI_writeCharNonBlocking(SERIAL_SCI_BASE, ((uint16_t)ia16  >> 8U) & 0xFFU);
    SCI_writeCharNonBlocking(SERIAL_SCI_BASE, (uint16_t) ia16         & 0xFFU);
    SCI_writeCharNonBlocking(SERIAL_SCI_BASE, ((uint16_t)ib16  >> 8U) & 0xFFU);
    SCI_writeCharNonBlocking(SERIAL_SCI_BASE, (uint16_t) ib16         & 0xFFU);
    SCI_writeCharNonBlocking(SERIAL_SCI_BASE, ((uint16_t)ic16  >> 8U) & 0xFFU);
    SCI_writeCharNonBlocking(SERIAL_SCI_BASE, (uint16_t) ic16         & 0xFFU);
    SCI_writeCharNonBlocking(SERIAL_SCI_BASE, (vdc16  >> 8U) & 0xFFU);
    SCI_writeCharNonBlocking(SERIAL_SCI_BASE, vdc16           & 0xFFU);
    SCI_writeCharNonBlocking(SERIAL_SCI_BASE, (th16   >> 8U) & 0xFFU);
    SCI_writeCharNonBlocking(SERIAL_SCI_BASE, th16            & 0xFFU);
    SCI_writeCharNonBlocking(SERIAL_SCI_BASE, (enc16      >> 8U) & 0xFFU);
    SCI_writeCharNonBlocking(SERIAL_SCI_BASE, enc16              & 0xFFU);
    SCI_writeCharNonBlocking(SERIAL_SCI_BASE, (biss_error >> 8U) & 0xFFU);
    SCI_writeCharNonBlocking(SERIAL_SCI_BASE, biss_error          & 0xFFU);
}

//-----------------------------------------------------------------------------
// Serial command RX — binary, non-blocking
// Frame (10 bytes): [0xBB][0xCC][vd:i16][vq:i16][freq:u16][en:u8][cal:u8]
// Scales: vd/vq ×100 (V), freq ×10 (Hz), en 0/1, cal 1=run calibration (only if en=0)
//-----------------------------------------------------------------------------
#define CMD_SYNC0      0xBBU
#define CMD_SYNC1      0xCCU
#define CMD_FRAME_LEN  10U

static uint16_t rxBuf[CMD_FRAME_LEN];
static uint16_t rxPos   = 0U;
static uint16_t rxSyncd = 0U;

static void pollSerial(void)
{
    while (SCI_getRxFIFOStatus(SERIAL_SCI_BASE) != SCI_FIFO_RX0)
    {
        uint16_t b = SCI_readCharNonBlocking(SERIAL_SCI_BASE) & 0xFFU;

        if (!rxSyncd)
        {
            if      (rxPos == 0U && b == CMD_SYNC0) { rxBuf[rxPos++] = b; }
            else if (rxPos == 1U && b == CMD_SYNC1) { rxBuf[rxPos++] = b; rxSyncd = 1U; }
            else                                    { rxPos = 0U; }
        }
        else
        {
            rxBuf[rxPos++] = b;
            if (rxPos == CMD_FRAME_LEN)
            {
                rxPos   = 0U;
                rxSyncd = 0U;

                int16_t  vd16   = (int16_t) ((rxBuf[2] << 8U) | rxBuf[3]);
                int16_t  vq16   = (int16_t) ((rxBuf[4] << 8U) | rxBuf[5]);
                uint16_t freq16 = (uint16_t)((rxBuf[6] << 8U) | rxBuf[7]);

                float vd   = (float)vd16   * 0.01f;
                float vq   = (float)vq16   * 0.01f;
                float freq = (float)freq16 * 0.1f;

                if (vd   >  20.0f) vd   =  20.0f;
                if (vd   < -20.0f) vd   = -20.0f;
                if (vq   >  20.0f) vq   =  20.0f;
                if (vq   < -20.0f) vq   = -20.0f;
                if (freq > 200.0f) freq = 200.0f;

                openLoop_Vd     = vd;
                openLoop_Vq     = vq;
                openLoop_freqHz = freq;
                openLoop_enable = (rxBuf[8] != 0U) ? 1U : 0U;

                if (rxBuf[9] != 0U && openLoop_enable == 0U)
                    Current_runCalibration(&currentCal);
            }
        }
    }
}

//-----------------------------------------------------------------------------
// Scope toggle pin
//-----------------------------------------------------------------------------
#define TMR_ISR_TOGGLE_GPIO    15U
#define TMR_ISR_TOGGLE_PIN_CFG GPIO_15_GPIO15

//-----------------------------------------------------------------------------
// ISR prototypes
//
// Complete timing chain per PWM half-period (25 µs, count 0→2500):
//
//   Count 0     (0 µs)   — PWM counter zero, duty updates applied
//   Count 1300  (13 µs)  — CMPC match → INT_EPWM4 (INT3.4)
//                           → samplePositionNowISR:
//                               GPIO123 = HIGH (FPGA latches BiSS position)
//                               FPGA is SPI master — no MCU TX needed
//   Count ~1300–1600      — FPGA decodes BiSS, sends result over SPI
//                           (FPGA drives SCLK as SPI master)
//                           → SPI RX data arrives at MCU
//                           → INT_SPIA_RX (INT6.1)
//                           → spiaRxISR: read + parse → biss_angle_raw
//   Count 2500  (25 µs)  — CTR=PRD → SOCA → ADC samples phase currents
//                           → ADC completes → INT_ADCB1 (INT1.2)
//                           → currentControlISR:
//                               reads biss_angle_raw + currents
//                               Park/Clarke/SVPWM → duty update
//   Count 2500→0         — Counter reverses (down-count)
//
// Priority (Simulink task priorities): INT1.2=30 (current ctrl), INT6.1=35, INT3.4=29 (sample)
// Preemption: INT1.2 is preemptible; INT6.1 and INT3.4 are not.
//-----------------------------------------------------------------------------
__interrupt void samplePositionNowISR(void);    // INT3.4 — EPWM4 CMPC match, pulse GPIO123
__interrupt void spiaRxISR(void);               // INT6.1 — SPI RX complete, parse packet
__interrupt void currentControlISR(void);       // INT1.2 — ADCB1 complete, current control loop
__interrupt void timer0ISR(void);

//-----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------
void main(void)
{
    Device_init();
    Device_initGPIO();
    Interrupt_initModule();
    Interrupt_initVectorTable();

    BOOSTXL_initAll();

    // Scope / debug toggle GPIO
    GPIO_setPinConfig(TMR_ISR_TOGGLE_PIN_CFG);
    GPIO_setDirectionMode(TMR_ISR_TOGGLE_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(TMR_ISR_TOGGLE_GPIO, GPIO_PIN_TYPE_STD);
    GPIO_writePin(TMR_ISR_TOGGLE_GPIO, 0);

    // LEDs
    GPIO_setPinConfig(DEVICE_GPIO_CFG_LED1);
    GPIO_setDirectionMode(DEVICE_GPIO_PIN_LED1, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(DEVICE_GPIO_PIN_LED1, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(DEVICE_GPIO_CFG_LED2);
    GPIO_setDirectionMode(DEVICE_GPIO_PIN_LED2, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(DEVICE_GPIO_PIN_LED2, GPIO_PIN_TYPE_STD);

    // Neutral duty (50%) on all phases before enabling
    BOOSTXL_setDuty(PHASE_A_PWM_BASE, 0.5f);
    BOOSTXL_setDuty(PHASE_B_PWM_BASE, 0.5f);
    BOOSTXL_setDuty(PHASE_C_PWM_BASE, 0.5f);

    // Calibrate current offsets with inverter still disabled
    Current_runCalibration(&currentCal);

    // Initialise angle ramp
    AngleRamp_init(&angleRamp, openLoop_freqHz, CONTROL_TS);

    // Register ISRs
    // INT3.4: EPWM4 CMPC match (count 1300) → SamplePositionNow (pulses GPIO123 to FPGA)
    Interrupt_register(INT_EPWM4,   &samplePositionNowISR);
    // INT6.1: SPIA RX FIFO → ProcessReceivedPosition (parses FPGA SPI packet)
    Interrupt_register(INT_SPIA_RX, &spiaRxISR);
    // INT1.2: ADCB1 completion → current control loop (reads biss_angle_raw)
    Interrupt_register(INT_ADCB1,   &currentControlISR);
    Interrupt_register(INT_TIMER0,  &timer0ISR);

    Interrupt_enable(INT_ADCB1);
    Interrupt_enable(INT_SPIA_RX);
    Interrupt_enable(INT_EPWM4);
    Interrupt_enable(INT_TIMER0);

    EINT;
    ERTM;

    BOOSTXL_disableInverter();

    //
    // Main loop — update ramp frequency from live variable, manage enable
    //
    uint32_t lastTick     = 0U;
    uint32_t lastTeleTick = 0U;

    while(1)
    {
        // Receive and apply serial commands (vd,vq,freq,enable\r\n)
        pollSerial();

        // Keep ramp frequency in sync with live knob (safe to write from bg loop)
        AngleRamp_setFreq(&angleRamp, openLoop_freqHz);

        // Enable/disable inverter based on live flag
        if (openLoop_enable)
            BOOSTXL_enableInverter();
        else
            BOOSTXL_disableInverter();

        // Send telemetry at ~1 kHz (every 2 timer ticks at 2 kHz)
        if ((timer0IsrCount - lastTeleTick) >= 2U)
        {
            lastTeleTick = timer0IsrCount;
            sendTelemetryBinary();
        }

        // LED heartbeat every 500 ms
        if ((timer0IsrCount - lastTick) >= 1000U)
        {
            lastTick = timer0IsrCount;
            GPIO_togglePin(DEVICE_GPIO_PIN_LED1);
        }
    }
}

//-----------------------------------------------------------------------------
// INT3.4 — EPWM4 CMPC match (count 1300, 13 µs into half-period)
//
// Simulink task priority 29 (highest), non-preemptible.
// Sets GPIO123 HIGH each cycle to tell the FPGA to latch the current BiSS
// encoder position.  The pin is never cleared — it stays permanently HIGH
// after the first execution, matching MiniGaN_SamplePositionNow.
// The FPGA is the SPI master — it drives SCLK and sends the decoded position
// back to the MCU automatically.  No MCU SPI TX here.
//-----------------------------------------------------------------------------
__interrupt void samplePositionNowISR(void)
{
    // Set GPIO123 HIGH — tells FPGA to latch current BiSS encoder position.
    // Pin is never cleared; stays HIGH permanently after first cycle.
    GPIO_writePin(BISS_FPGA_SAMPLE_GPIO, 1U);

    // No SPI activity — FPGA is SPI master and will send data autonomously,
    // triggering spiaRxISR (INT6.1) when the 16-bit word arrives.

    EPWM_clearEventTriggerInterruptFlag(PHASE_C_PWM_BASE);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP3);
}

//-----------------------------------------------------------------------------
// INT6.1 — SPIA RX complete → ProcessReceivedPosition
//
// Simulink task priority 35, non-preemptible.
// Fires when the FPGA (SPI master) has clocked a 16-bit position word into
// the MCU RX FIFO.  Reads the word, captures SPI status, calls
// processBissSpiPacket() to extract angle and error, and stores results in
// volatile globals read by currentControlISR (INT1.2).
//-----------------------------------------------------------------------------
__interrupt void spiaRxISR(void)
{
    // Capture SPI status register before reading data (clears on read)
    biss_spi_status = SPI_getInterruptStatus(BISS_SPI_BASE);

    // Read 16-bit packet from FPGA and parse angle + error
    processBissSpiPacket(SPI_readDataNonBlocking(BISS_SPI_BASE),
                         &biss_error,
                         &biss_angle_raw);

    SPI_clearInterruptStatus(BISS_SPI_BASE, SPI_INT_RXFF);
    SPI_resetRxFIFO(BISS_SPI_BASE);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP6);
}

//-----------------------------------------------------------------------------
// INT1.2 — ADCB1 completion → current control loop
//
// Simulink task priority 30, preemptible.
// Fires after ADCB finishes sampling (triggered by EPWM4 SOCA at CTR=PRD,
// 25 µs into the half-period).  Reads biss_angle_raw written by spiaRxISR
// and runs the open-loop voltage injection.
//-----------------------------------------------------------------------------
__interrupt void currentControlISR(void)
{
    float Valpha, Vbeta;
    float Va, Vb, Vc;
    float dutyA, dutyB, dutyC;

    // Read sensors
    iBAmps   = Current_getPhaseB(&currentCal);
    iCAmps   = Current_getPhaseC(&currentCal);
    iAAmps   = Current_getPhaseA(iBAmps, iCAmps);
    vdcVolts = Voltage_getDCBus();

    // Advance angle
    theta_rad = AngleRamp_update(&angleRamp);

    // Voltage injection
    ParkInv(openLoop_Vd, openLoop_Vq, theta_rad, &Valpha, &Vbeta);

    // alpha/beta -> three-phase
    ClarkeInv(Valpha, Vbeta, &Va, &Vb, &Vc);

    // SVPWM modulation (guard against zero Vdc)
    if (vdcVolts > 1.0f)
    {
        SVPWM_calc(Va, Vb, Vc, vdcVolts, &dutyA, &dutyB, &dutyC);
        BOOSTXL_setDuty(PHASE_A_PWM_BASE, dutyA);
        BOOSTXL_setDuty(PHASE_B_PWM_BASE, dutyB);
        BOOSTXL_setDuty(PHASE_C_PWM_BASE, dutyC);
    }

    // Clear GPIO123 — Prepare for the next rising edge trigger in the next cycle
    GPIO_writePin(BISS_FPGA_SAMPLE_GPIO, 0U);

    ADC_clearInterruptStatus(IB_ADC_BASE, ADC_INT_NUMBER1);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

//-----------------------------------------------------------------------------
// Timer0 — 2 kHz base rate
//-----------------------------------------------------------------------------
__interrupt void timer0ISR(void)
{
    GPIO_togglePin(TMR_ISR_TOGGLE_GPIO);
    timer0IsrCount++;
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}
