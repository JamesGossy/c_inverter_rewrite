// //#############################################################################
// //
// // FILE:   closed_loop_test.c
// //
// // TITLE:  Field-Oriented Control (FOC) — speed + DQ current closed loop
// //
// // DESCRIPTION:
// //   Cascaded FOC: speed outer loop feeds Iq reference to the DQ current
// //   inner loops.  Id reference is held at zero (no field weakening).
// //
// //   Control hierarchy:
// //     Speed PI  (2 kHz,  Timer0)  → Iq reference
// //     D-axis PI (20 kHz, EPWM4)  → Vd command
// //     Q-axis PI (20 kHz, EPWM4)  → Vq command
// //
// //   Signal chain (epwm4ISR, 20 kHz):
// //     Sensors → Clarke → Park → PI_d, PI_q → ParkInv → ClarkeInv → SVPWM → PWM
// //
// //   NOTE: Electrical angle uses an open-loop ramp driven by the speed
// //         reference.  For true closed-loop angle, decode the BiSS encoder
// //         in spiaRxISR, write theta_rad from rotor position, and update
// //         speedMeas_radPerSec for the speed controller.
// //
// //   Tuning (adjust live in CCS Expressions):
// //     closedLoop_speedRefHz  — electrical speed reference (Hz)
// //     closedLoop_idRef       — d-axis current reference (A), normally 0
// //     closedLoop_enable      — 0 = coast, 1 = run
// //     piSpeed_Kp / Ki        — speed controller gains
// //     piD_Kp / Ki            — d-axis current controller gains
// //     piQ_Kp / Ki            — q-axis current controller gains
// //
// //   Serial RX (9 bytes): [0xBB][0xCC][speedHz:i16][idRef:i16][en:u8]
// //     speedHz x10 (Hz), idRef x100 (A)
// //
// //   Serial TX (12 bytes): [0xAA][0x55][Id:i16][Iq:i16][IqRef:i16][Vdc:u16][theta:u16]
// //     currents x100 (A), Vdc x100 (V), theta x10000 (rad)
// //
// //#############################################################################

// #include "driverlib.h"
// #include "device.h"
// #include "hal/hal_boostxl.h"
// #include "sensors/voltage.h"
// #include "sensors/current.h"
// #include "control/angle_ramp.h"
// #include "control/transforms.h"
// #include "control/svpwm.h"
// #include "control/pi.h"

// //-----------------------------------------------------------------------------
// // Control references — set from CCS Expressions or serial
// //-----------------------------------------------------------------------------
// volatile float    closedLoop_speedRefHz = 0.0f;   // Electrical speed reference (Hz)
// volatile float    closedLoop_idRef      = 0.0f;   // D-axis current reference (A)
// volatile uint16_t closedLoop_enable     = 0U;     // 1 = run, 0 = coast

// //-----------------------------------------------------------------------------
// // PI gains — tunable live from CCS Expressions
// //   Changes take effect on the next main-loop iteration.
// //   Start small and increase until response is acceptable:
// //     Kp — increase until snappy, reduce on overshoot
// //     Ki — start ~Ki_continuous * Ts; typically 10x below Kp in effect
// //-----------------------------------------------------------------------------
// volatile float piSpeed_Kp = 0.05f;   // Speed PI Kp  (A / rad/s)
// volatile float piSpeed_Ki = 0.001f;  // Speed PI Ki  (per sample at 2 kHz)
// volatile float piD_Kp     = 0.5f;    // D-axis PI Kp (V/A)
// volatile float piD_Ki     = 0.01f;   // D-axis PI Ki (per sample at 20 kHz)
// volatile float piQ_Kp     = 0.5f;    // Q-axis PI Kp (V/A)
// volatile float piQ_Ki     = 0.01f;   // Q-axis PI Ki (per sample at 20 kHz)

// //-----------------------------------------------------------------------------
// // Output limits
// //-----------------------------------------------------------------------------
// #define IQ_MAX_AMPS      15.0f    // Symmetric Iq clamp (A)
// #define VDQ_MAX_VOLTS    20.0f    // Symmetric DQ voltage clamp (V)

// //-----------------------------------------------------------------------------
// // Telemetry — watch in CCS Expressions or via serial
// //-----------------------------------------------------------------------------
// volatile float    idAmps              = 0.0f;
// volatile float    iqAmps              = 0.0f;
// volatile float    iqRefAmps           = 0.0f;
// volatile float    iAAmps              = 0.0f;
// volatile float    iBAmps              = 0.0f;
// volatile float    iCAmps              = 0.0f;
// volatile float    vdcVolts            = 0.0f;
// volatile float    theta_rad           = 0.0f;
// volatile float    speedMeas_radPerSec = 0.0f;  // Write from BiSS decode for closed-loop speed
// volatile uint32_t timer0IsrCount      = 0U;

// //-----------------------------------------------------------------------------
// // Module-private state
// //-----------------------------------------------------------------------------
// static AngleRamp_t          angleRamp  = {0};
// static CurrentCalibration_t currentCal = {0};
// static PI_t                 piSpeed    = {0};
// static PI_t                 piD        = {0};
// static PI_t                 piQ        = {0};

// #define CONTROL_TS_FAST  (1.0f / (float)PWM_SWITCHING_FREQ_HZ)  // 50 us  @ 20 kHz
// #define CONTROL_TS_SLOW  (1.0f / (float)TIMER0_FREQ_HZ)         // 500 us @  2 kHz

// // Iq reference shared between speed ISR (writer) and current ISR (reader)
// static volatile float s_iqRef = 0.0f;

// //-----------------------------------------------------------------------------
// // Serial telemetry TX — binary, non-blocking
// // Frame (12 bytes): [0xAA][0x55][Id:i16][Iq:i16][IqRef:i16][Vdc:u16][theta:u16]
// // Scales: currents x100 (A), Vdc x100 (V), theta x10000 (rad)
// // Big-endian.  Fits in the 16-deep SCI TX FIFO; takes ~40 us at 3 Mbaud.
// //-----------------------------------------------------------------------------
// static void sendTelemetryBinary(void)
// {
//     if (SCI_getTxFIFOStatus(SERIAL_SCI_BASE) != SCI_FIFO_TX0)
//         return;

//     int16_t  id16  = (int16_t)(idAmps    * 100.0f);
//     int16_t  iq16  = (int16_t)(iqAmps    * 100.0f);
//     int16_t  ir16  = (int16_t)(iqRefAmps * 100.0f);
//     uint16_t vdc16 = (uint16_t)(vdcVolts  * 100.0f);
//     uint16_t th16  = (uint16_t)(theta_rad * 10000.0f);

//     SCI_writeCharNonBlocking(SERIAL_SCI_BASE, 0xAAU);
//     SCI_writeCharNonBlocking(SERIAL_SCI_BASE, 0x55U);
//     SCI_writeCharNonBlocking(SERIAL_SCI_BASE, ((uint16_t)id16  >> 8U) & 0xFFU);
//     SCI_writeCharNonBlocking(SERIAL_SCI_BASE, (uint16_t) id16         & 0xFFU);
//     SCI_writeCharNonBlocking(SERIAL_SCI_BASE, ((uint16_t)iq16  >> 8U) & 0xFFU);
//     SCI_writeCharNonBlocking(SERIAL_SCI_BASE, (uint16_t) iq16         & 0xFFU);
//     SCI_writeCharNonBlocking(SERIAL_SCI_BASE, ((uint16_t)ir16  >> 8U) & 0xFFU);
//     SCI_writeCharNonBlocking(SERIAL_SCI_BASE, (uint16_t) ir16         & 0xFFU);
//     SCI_writeCharNonBlocking(SERIAL_SCI_BASE, (vdc16 >> 8U) & 0xFFU);
//     SCI_writeCharNonBlocking(SERIAL_SCI_BASE, vdc16         & 0xFFU);
//     SCI_writeCharNonBlocking(SERIAL_SCI_BASE, (th16  >> 8U) & 0xFFU);
//     SCI_writeCharNonBlocking(SERIAL_SCI_BASE, th16          & 0xFFU);
// }

// //-----------------------------------------------------------------------------
// // Serial command RX — binary, non-blocking
// // Frame (9 bytes): [0xBB][0xCC][speedHz:i16][idRef:i16][en:u8]
// // Scales: speedHz x10 (Hz), idRef x100 (A), en 0/1
// //-----------------------------------------------------------------------------
// #define CMD_SYNC0      0xBBU
// #define CMD_SYNC1      0xCCU
// #define CMD_FRAME_LEN  9U

// static uint16_t rxBuf[CMD_FRAME_LEN];
// static uint16_t rxPos   = 0U;
// static uint16_t rxSyncd = 0U;

// static void pollSerial(void)
// {
//     while (SCI_getRxFIFOStatus(SERIAL_SCI_BASE) != SCI_FIFO_RX0)
//     {
//         uint16_t b = SCI_readCharNonBlocking(SERIAL_SCI_BASE) & 0xFFU;

//         if (!rxSyncd)
//         {
//             if      (rxPos == 0U && b == CMD_SYNC0) { rxBuf[rxPos++] = b; }
//             else if (rxPos == 1U && b == CMD_SYNC1) { rxBuf[rxPos++] = b; rxSyncd = 1U; }
//             else                                    { rxPos = 0U; }
//         }
//         else
//         {
//             rxBuf[rxPos++] = b;
//             if (rxPos == CMD_FRAME_LEN)
//             {
//                 rxPos   = 0U;
//                 rxSyncd = 0U;

//                 int16_t speed16 = (int16_t)((rxBuf[2] << 8U) | rxBuf[3]);
//                 int16_t idRef16 = (int16_t)((rxBuf[4] << 8U) | rxBuf[5]);

//                 float speed = (float)speed16 * 0.1f;
//                 float idRef = (float)idRef16 * 0.01f;

//                 if (speed >  200.0f) speed =  200.0f;
//                 if (speed < -200.0f) speed = -200.0f;
//                 if (idRef >   10.0f) idRef =   10.0f;
//                 if (idRef <  -10.0f) idRef =  -10.0f;

//                 closedLoop_speedRefHz = speed;
//                 closedLoop_idRef      = idRef;
//                 closedLoop_enable     = (rxBuf[8] != 0U) ? 1U : 0U;
//             }
//         }
//     }
// }

// //-----------------------------------------------------------------------------
// // Scope toggle pin
// //-----------------------------------------------------------------------------
// #define TMR_ISR_TOGGLE_GPIO    15U
// #define TMR_ISR_TOGGLE_PIN_CFG GPIO_15_GPIO15

// //-----------------------------------------------------------------------------
// // ISR prototypes
// //-----------------------------------------------------------------------------
// __interrupt void spiaRxISR(void);
// __interrupt void epwm4ISR(void);
// __interrupt void timer0ISR(void);

// //-----------------------------------------------------------------------------
// // Main
// //-----------------------------------------------------------------------------
// void main(void)
// {
//     Device_init();
//     Device_initGPIO();
//     Interrupt_initModule();
//     Interrupt_initVectorTable();

//     BOOSTXL_initAll();

//     // Scope / debug toggle GPIO
//     GPIO_setPinConfig(TMR_ISR_TOGGLE_PIN_CFG);
//     GPIO_setDirectionMode(TMR_ISR_TOGGLE_GPIO, GPIO_DIR_MODE_OUT);
//     GPIO_setPadConfig(TMR_ISR_TOGGLE_GPIO, GPIO_PIN_TYPE_STD);
//     GPIO_writePin(TMR_ISR_TOGGLE_GPIO, 0);

//     // LEDs
//     GPIO_setPinConfig(DEVICE_GPIO_CFG_LED1);
//     GPIO_setDirectionMode(DEVICE_GPIO_PIN_LED1, GPIO_DIR_MODE_OUT);
//     GPIO_setPadConfig(DEVICE_GPIO_PIN_LED1, GPIO_PIN_TYPE_STD);
//     GPIO_setPinConfig(DEVICE_GPIO_CFG_LED2);
//     GPIO_setDirectionMode(DEVICE_GPIO_PIN_LED2, GPIO_DIR_MODE_OUT);
//     GPIO_setPadConfig(DEVICE_GPIO_PIN_LED2, GPIO_PIN_TYPE_STD);

//     // Safe 50% duty before enabling
//     BOOSTXL_setDuty(PHASE_A_PWM_BASE, 0.5f);
//     BOOSTXL_setDuty(PHASE_B_PWM_BASE, 0.5f);
//     BOOSTXL_setDuty(PHASE_C_PWM_BASE, 0.5f);

//     // Calibrate current offsets (inverter still disabled)
//     Current_runCalibration(&currentCal);

//     // Angle ramp starts at 0 Hz — Timer0 ISR drives the frequency
//     AngleRamp_init(&angleRamp, 0.0f, CONTROL_TS_FAST);

//     // Initialise PI controllers
//     PI_init(&piSpeed, piSpeed_Kp, piSpeed_Ki, -IQ_MAX_AMPS,   IQ_MAX_AMPS);
//     PI_init(&piD,     piD_Kp,     piD_Ki,     -VDQ_MAX_VOLTS, VDQ_MAX_VOLTS);
//     PI_init(&piQ,     piQ_Kp,     piQ_Ki,     -VDQ_MAX_VOLTS, VDQ_MAX_VOLTS);

//     // Register ISRs
//     Interrupt_register(INT_SPIA_RX, &spiaRxISR);
//     Interrupt_register(INT_EPWM4,   &epwm4ISR);
//     Interrupt_register(INT_TIMER0,  &timer0ISR);

//     Interrupt_enable(INT_SPIA_RX);
//     Interrupt_enable(INT_EPWM4);
//     Interrupt_enable(INT_TIMER0);

//     EINT;
//     ERTM;

//     BOOSTXL_disableInverter();

//     uint32_t lastTick     = 0U;
//     uint32_t lastTeleTick = 0U;

//     while (1)
//     {
//         pollSerial();

//         // Sync live-tuning variables into PI structs each loop iteration
//         piSpeed.Kp = piSpeed_Kp;  piSpeed.Ki = piSpeed_Ki;
//         piD.Kp     = piD_Kp;      piD.Ki     = piD_Ki;
//         piQ.Kp     = piQ_Kp;      piQ.Ki     = piQ_Ki;

//         // Enable/disable inverter; reset integrators on disable
//         if (closedLoop_enable)
//         {
//             BOOSTXL_enableInverter();
//         }
//         else
//         {
//             BOOSTXL_disableInverter();
//             PI_reset(&piSpeed);
//             PI_reset(&piD);
//             PI_reset(&piQ);
//             s_iqRef = 0.0f;
//             AngleRamp_reset(&angleRamp);
//         }

//         // Send telemetry at ~1 kHz (every 2 timer ticks at 2 kHz)
//         if ((timer0IsrCount - lastTeleTick) >= 2U)
//         {
//             lastTeleTick = timer0IsrCount;
//             sendTelemetryBinary();
//         }

//         // LED heartbeat every 500 ms
//         if ((timer0IsrCount - lastTick) >= 1000U)
//         {
//             lastTick = timer0IsrCount;
//             GPIO_togglePin(DEVICE_GPIO_PIN_LED1);
//         }
//     }
// }

// //-----------------------------------------------------------------------------
// // INT3.4 — EPWM4 @ 20 kHz: DQ current control loop
// //
// //   1. Read phase currents and DC bus voltage
// //   2. Advance electrical angle ramp
// //   3. Clarke (abc → alpha/beta) + Park (alpha/beta → dq) → Id, Iq
// //   4. D and Q PI controllers → Vd, Vq
// //   5. Inverse Park (dq → alpha/beta) + Inverse Clarke (alpha/beta → abc) → Va,Vb,Vc
// //   6. SVPWM → duty cycles → PWM outputs
// //-----------------------------------------------------------------------------
// __interrupt void epwm4ISR(void)
// {
//     float Ialpha, Ibeta;
//     float Id, Iq;
//     float Vd, Vq;
//     float Valpha, Vbeta;
//     float Va, Vb, Vc;
//     float dutyA, dutyB, dutyC;

//     // --- Sensor reads ---
//     iBAmps   = Current_getPhaseB(&currentCal);
//     iCAmps   = Current_getPhaseC(&currentCal);
//     iAAmps   = Current_getPhaseA(iBAmps, iCAmps);
//     vdcVolts = Voltage_getDCBus();

//     // --- Electrical angle ---
//     // AngleRamp frequency is set by Timer0 ISR to match the speed reference.
//     // Replace with decoded BiSS encoder angle for true position-closed-loop FOC.
//     theta_rad = AngleRamp_update(&angleRamp);

//     // --- Clarke: abc → alpha/beta ---
//     Clarke(iAAmps, iBAmps, &Ialpha, &Ibeta);

//     // --- Park: alpha/beta → dq ---
//     Park(Ialpha, Ibeta, theta_rad, &Id, &Iq);
//     idAmps = Id;
//     iqAmps = Iq;

//     // --- Current PI controllers ---
//     float iqRef = s_iqRef;                       // set by Timer0 speed controller
//     Vd = PI_update(&piD, closedLoop_idRef - Id);
//     Vq = PI_update(&piQ, iqRef            - Iq);

//     // --- Inverse Park: dq → alpha/beta ---
//     ParkInv(Vd, Vq, theta_rad, &Valpha, &Vbeta);

//     // --- Inverse Clarke: alpha/beta → abc ---
//     ClarkeInv(Valpha, Vbeta, &Va, &Vb, &Vc);

//     // --- SVPWM modulation ---
//     if (vdcVolts > 1.0f)
//     {
//         SVPWM_calc(Va, Vb, Vc, vdcVolts, &dutyA, &dutyB, &dutyC);
//         BOOSTXL_setDuty(PHASE_A_PWM_BASE, dutyA);
//         BOOSTXL_setDuty(PHASE_B_PWM_BASE, dutyB);
//         BOOSTXL_setDuty(PHASE_C_PWM_BASE, dutyC);
//     }

//     // Kick BiSS encoder read for next cycle
//     SPI_writeDataNonBlocking(BISS_SPI_BASE, 0xFFFF);

//     EPWM_clearEventTriggerInterruptFlag(PHASE_A_PWM_BASE);
//     Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP3);
// }

// //-----------------------------------------------------------------------------
// // INT6.1 — SPIA RX @ 20 kHz (BiSS completion)
// //
// // TODO: Decode the BiSS frame here to get rotor position and speed.
// //       Write the electrical angle to theta_rad and electrical angular
// //       velocity to speedMeas_radPerSec for closed-loop speed control.
// //-----------------------------------------------------------------------------
// __interrupt void spiaRxISR(void)
// {
//     (void)SPI_readDataNonBlocking(BISS_SPI_BASE);
//     SPI_clearInterruptStatus(BISS_SPI_BASE, SPI_INT_RXFF);
//     Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP6);
// }

// //-----------------------------------------------------------------------------
// // Timer0 — 2 kHz: speed control loop
// //
// //   Speed error (rad/s) → PI → Iq reference (A) → s_iqRef
// //   Angle ramp frequency is also updated here to track the speed reference.
// //
// //   NOTE: speedMeas_radPerSec is 0 until BiSS decode is implemented.
// //         Until then the speed loop acts as a feed-forward: Iq ramps up
// //         proportional to the speed reference.
// //-----------------------------------------------------------------------------
// __interrupt void timer0ISR(void)
// {
//     GPIO_togglePin(TMR_ISR_TOGGLE_GPIO);
//     timer0IsrCount++;

//     float speedRef_radPerSec = closedLoop_speedRefHz * 6.28318530f;
//     float speedError         = speedRef_radPerSec - speedMeas_radPerSec;

//     s_iqRef   = PI_update(&piSpeed, speedError);
//     iqRefAmps = s_iqRef;

//     // Keep angle ramp aligned with speed command
//     AngleRamp_setFreq(&angleRamp, closedLoop_speedRefHz);

//     Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
// }
