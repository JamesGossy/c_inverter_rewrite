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
//   Signal chain (epwm4ISR, 20 kHz):
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
#include "boostxl_periph.h"
#include "sensors/voltage.h"
#include "sensors/current.h"
#include "control/angle_ramp.h"
#include "control/transforms.h"
#include "control/svpwm.h"

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
volatile uint32_t timer0IsrCount   = 0U;

//-----------------------------------------------------------------------------
// Module-private state
//-----------------------------------------------------------------------------
static AngleRamp_t          angleRamp   = {0};
static CurrentCalibration_t currentCal  = {0};

#define CONTROL_TS  (1.0f / (float)PWM_SWITCHING_FREQ_HZ)   // 50 µs

//-----------------------------------------------------------------------------
// Serial telemetry TX
// Format: "Ia,Ib,Ic,Vdc,theta\r\n"  (all floats, 2 decimal places)
//-----------------------------------------------------------------------------
static uint16_t appendFloat(char *buf, uint16_t pos, float val)
{
    char     tmp[6];
    uint16_t len = 0;
    uint16_t intPart;
    uint16_t fracPart;

    if (val < 0.0f) { buf[pos++] = '-'; val = -val; }

    intPart  = (uint16_t)val;
    fracPart = (uint16_t)((val - (float)intPart) * 100.0f + 0.5f);
    if (fracPart >= 100U) { intPart++; fracPart = 0U; }

    do { tmp[len++] = (char)('0' + (intPart % 10U)); intPart /= 10U; } while (intPart);
    uint16_t i = len;
    while (i--) buf[pos++] = tmp[i];
    
    buf[pos++] = '.';
    buf[pos++] = (char)('0' + (fracPart / 10U));
    buf[pos++] = (char)('0' + (fracPart % 10U));
    return pos;
}

static void sendTelemetry(void)
{
    char     buf[56];
    uint16_t pos = 0;

    pos = appendFloat(buf, pos, iAAmps);   buf[pos++] = ',';
    pos = appendFloat(buf, pos, iBAmps);   buf[pos++] = ',';
    pos = appendFloat(buf, pos, iCAmps);   buf[pos++] = ',';
    pos = appendFloat(buf, pos, vdcVolts); buf[pos++] = ',';
    pos = appendFloat(buf, pos, theta_rad);
    buf[pos++] = '\r';
    buf[pos++] = '\n';
    buf[pos]   = '\0';
    BOOSTXL_serialSendString(buf);
}

//-----------------------------------------------------------------------------
// Serial command RX
// Expected format: "vd,vq,freq,enable\r\n"  e.g. "0.00,5.00,10.00,1\r\n"
// Non-blocking — call from main loop only.
//-----------------------------------------------------------------------------
static char     rxBuf[48];
static uint16_t rxPos = 0U;

static float parseFloat(const char **p)
{
    const char *s = *p;
    float sign = 1.0f;
    float val  = 0.0f;
    float frac = 0.1f;

    if (*s == '-') { sign = -1.0f; s++; }
    while (*s >= '0' && *s <= '9') { val = val * 10.0f + (float)(*s - '0'); s++; }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') { val += (float)(*s - '0') * frac; frac *= 0.1f; s++; }
    }
    *p = s;
    return sign * val;
}

static void handleCommand(const char *line)
{
    const char *p = line;
    float vd   = parseFloat(&p); if (*p == ',') p++;
    float vq   = parseFloat(&p); if (*p == ',') p++;
    float freq = parseFloat(&p); if (*p == ',') p++;
    float en   = parseFloat(&p);

    // Clamp to safe ranges
    if (vd   >  20.0f) vd   =  20.0f;
    if (vd   < -20.0f) vd   = -20.0f;
    if (vq   >  20.0f) vq   =  20.0f;
    if (vq   < -20.0f) vq   = -20.0f;
    if (freq >  200.0f) freq = 200.0f;
    if (freq <    0.0f) freq =   0.0f;

    openLoop_Vd     = vd;
    openLoop_Vq     = vq;
    openLoop_freqHz = freq;
    openLoop_enable = (en > 0.5f) ? 1U : 0U;
}

static void pollSerial(void)
{
    while (SCI_getRxStatus(SERIAL_SCI_BASE) & SCI_RXSTATUS_READY)
    {
        char c = (char)(SCI_readCharNonBlocking(SERIAL_SCI_BASE) & 0xFFU);
        if (c == '\n')
        {
            rxBuf[rxPos] = '\0';
            handleCommand(rxBuf);
            rxPos = 0U;
        }
        else if (c != '\r' && rxPos < (uint16_t)(sizeof(rxBuf) - 1U))
        {
            rxBuf[rxPos++] = c;
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
//-----------------------------------------------------------------------------
__interrupt void spiaRxISR(void);
__interrupt void epwm4ISR(void);
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
    Interrupt_register(INT_SPIA_RX, &spiaRxISR);
    Interrupt_register(INT_EPWM4,   &epwm4ISR);
    Interrupt_register(INT_TIMER0,  &timer0ISR);

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

        // Send telemetry at ~20 Hz (every 100 ms = 200 timer ticks at 2 kHz)
        if ((timer0IsrCount - lastTeleTick) >= 20U)
        {
            lastTeleTick = timer0IsrCount;
            sendTelemetry();
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
// INT3.4 — EPWM4 @ 20 kHz: open-loop voltage injection
//-----------------------------------------------------------------------------
__interrupt void epwm4ISR(void)
{
    float Valpha, Vbeta;
    float Va, Vb, Vc;
    float dutyA, dutyB, dutyC;

    // Read sensors
    iAAmps   = Current_getPhaseA(&currentCal);
    iCAmps   = Current_getPhaseC(&currentCal);
    iBAmps   = Current_getPhaseB(iAAmps, iCAmps);
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

    // Kick BiSS encoder read
    SPI_writeDataNonBlocking(BISS_SPI_BASE, 0xFFFF);

    EPWM_clearEventTriggerInterruptFlag(PHASE_A_PWM_BASE);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP3);
}

//-----------------------------------------------------------------------------
// INT6.1 — SPIA RX @ 20 kHz (BiSS completion)
//-----------------------------------------------------------------------------
__interrupt void spiaRxISR(void)
{
    (void)SPI_readDataNonBlocking(BISS_SPI_BASE);
    SPI_clearInterruptStatus(BISS_SPI_BASE, SPI_INT_RXFF);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP6);
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
