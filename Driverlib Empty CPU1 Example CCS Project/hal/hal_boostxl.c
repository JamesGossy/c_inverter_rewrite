//#############################################################################
//
// FILE:   hal_boostxl.c
//
// TITLE:  Peripheral setup for BOOSTXL-3PhGaNInv on LAUNCHXL-F28379D
//
//#############################################################################

#include "hal_boostxl.h"

//=============================================================================
// GPIO Initialization
//=============================================================================
void BOOSTXL_initGPIO(void)
{
    // PWM pins -> EPWM4/5/6 (site 2)
    GPIO_setPinConfig(PHASE_A_PWM_H_PIN_CFG);
    GPIO_setPinConfig(PHASE_A_PWM_L_PIN_CFG);
    GPIO_setPinConfig(PHASE_B_PWM_H_PIN_CFG);
    GPIO_setPinConfig(PHASE_B_PWM_L_PIN_CFG);
    GPIO_setPinConfig(PHASE_C_PWM_H_PIN_CFG);
    GPIO_setPinConfig(PHASE_C_PWM_L_PIN_CFG);

    // Inverter enable (nEN) — start DISABLED (HIGH = safe)
    GPIO_setPinConfig(INV_EN_PIN_CFG);
    GPIO_setDirectionMode(INV_EN_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(INV_EN_GPIO, GPIO_PIN_TYPE_STD);
    GPIO_writePin(INV_EN_GPIO, 1);

    // SPIA pins for BiSS-C encoder interface (site 1)
    // FPGA is SPI master: drives SPICLK and SPISTEA; MCU is peripheral (slave).
    GPIO_setPinConfig(BISS_SIMO_PIN_CFG);   // GPIO58 -> SPISIMOA (unused in peripheral mode)
    GPIO_setPinConfig(BISS_SOMI_PIN_CFG);   // GPIO59 -> SPISOMIA (position data in from FPGA)
    GPIO_setPinConfig(BISS_CLK_PIN_CFG);    // GPIO60 -> SPICLKA  (clock driven by FPGA)
    GPIO_setPinConfig(BISS_STE_PIN_CFG);    // GPIO61 -> SPISTEA  (asserted by FPGA, active low)
    GPIO_setQualificationMode(BISS_SOMI_GPIO, GPIO_QUAL_ASYNC);
    GPIO_setQualificationMode(BISS_CLK_GPIO, GPIO_QUAL_ASYNC);

    // GPIO123 — "Sample Now" output to FPGA, initially LOW.
    // samplePositionNowISR writes this HIGH every cycle and never clears it,
    // matching the Simulink reference (MiniGaN_SamplePositionNow).
    GPIO_setPinConfig(BISS_FPGA_SAMPLE_PIN_CFG);
    GPIO_setDirectionMode(BISS_FPGA_SAMPLE_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(BISS_FPGA_SAMPLE_GPIO, GPIO_PIN_TYPE_STD);
    GPIO_writePin(BISS_FPGA_SAMPLE_GPIO, 0U);

    // CANA pins (on-board transceiver at J12)
    GPIO_setPinConfig(CAN_TX_PIN_CFG);
    GPIO_setPinConfig(CAN_RX_PIN_CFG);
}

//=============================================================================
// EPWM Initialization — 20 kHz switching
//=============================================================================
static void initEPWMModule(uint32_t base, bool isMaster)
{
    EPWM_setTimeBasePeriod(base, PWM_TBPRD);
    EPWM_setTimeBaseCounter(base, 0U);
    EPWM_setTimeBaseCounterMode(base, EPWM_COUNTER_MODE_UP_DOWN);
    EPWM_setClockPrescaler(base, EPWM_CLOCK_DIVIDER_1, EPWM_HSCLOCK_DIVIDER_1);

    if(isMaster)
    {
        EPWM_disablePhaseShiftLoad(base);
        EPWM_setPhaseShift(base, 0U);
        EPWM_setSyncOutPulseMode(base, EPWM_SYNC_OUT_PULSE_ON_COUNTER_ZERO);
    }
    else
    {
        EPWM_enablePhaseShiftLoad(base);
        EPWM_setPhaseShift(base, 0U);
        EPWM_setSyncOutPulseMode(base, EPWM_SYNC_OUT_PULSE_ON_EPWMxSYNCIN);
    }

    EPWM_setCounterCompareValue(base, EPWM_COUNTER_COMPARE_A, PWM_TBPRD / 2);
    EPWM_setCounterCompareShadowLoadMode(base, EPWM_COUNTER_COMPARE_A, EPWM_COMP_LOAD_ON_CNTR_ZERO);

    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A, EPWM_AQ_OUTPUT_LOW,  EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPA);
    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A, EPWM_AQ_OUTPUT_HIGH, EPWM_AQ_OUTPUT_ON_TIMEBASE_DOWN_CMPA);


    // Dead band: AHC mode
    EPWM_setRisingEdgeDeadBandDelayInput(base, EPWM_DB_INPUT_EPWMA);
    EPWM_setFallingEdgeDeadBandDelayInput(base, EPWM_DB_INPUT_EPWMA);
    EPWM_setDeadBandDelayMode(base, EPWM_DB_RED, true);
    EPWM_setDeadBandDelayMode(base, EPWM_DB_FED, true);
    EPWM_setDeadBandDelayPolarity(base, EPWM_DB_RED, EPWM_DB_POLARITY_ACTIVE_HIGH);
    EPWM_setDeadBandDelayPolarity(base, EPWM_DB_FED, EPWM_DB_POLARITY_ACTIVE_LOW);
    EPWM_setRisingEdgeDelayCount(base, PWM_DEADBAND_RED_COUNT);
    EPWM_setFallingEdgeDelayCount(base, PWM_DEADBAND_FED_COUNT);

    EPWM_setEmulationMode(base, EPWM_EMULATION_FREE_RUN);
}

void BOOSTXL_initPWM(void)
{
    SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);

    initEPWMModule(PHASE_C_PWM_BASE, true);   // EPWM4 is master (Phase C)
    initEPWMModule(PHASE_B_PWM_BASE, false);
    initEPWMModule(PHASE_A_PWM_BASE, false);

    // EPWM4 SOCA triggers ADC at CTR=PRD (top of up-count, 25 µs into half-period)
    // Matches Simulink: EPwm4Regs.ETSEL.bit.SOCASEL = 2 (CTRU=PRD)
    EPWM_enableADCTrigger(PHASE_C_PWM_BASE, EPWM_SOC_A);
    EPWM_setADCTriggerSource(PHASE_C_PWM_BASE, EPWM_SOC_A, EPWM_SOC_TBCTR_PERIOD);
    EPWM_setADCTriggerEventPrescale(PHASE_C_PWM_BASE, EPWM_SOC_A, 1U);

    // EPWM4 interrupt at CTRU=CMPC (count 1300, 13 µs) → samplePositionNowISR
    // Matches Simulink: EPwm4Regs.CMPC=1300, ETSEL.INTSEL=4 (CTRU=CMPC)
    EPWM_setCounterCompareValue(PHASE_C_PWM_BASE, EPWM_COUNTER_COMPARE_C, 1300U);
    EPWM_setCounterCompareShadowLoadMode(PHASE_C_PWM_BASE, EPWM_COUNTER_COMPARE_C,
                                         EPWM_COMP_LOAD_ON_CNTR_ZERO);
    EPWM_setInterruptSource(PHASE_C_PWM_BASE, EPWM_INT_TBCTR_U_CMPC);
    EPWM_setInterruptEventCount(PHASE_C_PWM_BASE, 1U);
    EPWM_enableInterrupt(PHASE_C_PWM_BASE);

    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
}

//=============================================================================
// ADC Initialization
//=============================================================================
void BOOSTXL_initADC(void)
{
    // ADCB
    ADC_setPrescaler(IB_ADC_BASE, ADC_CLK_DIV_4_0);
    ADC_setMode(IB_ADC_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
    ADC_setInterruptPulseMode(IB_ADC_BASE, ADC_PULSE_END_OF_CONV);
    ADC_enableConverter(IB_ADC_BASE);
    DEVICE_DELAY_US(1000);

    // ADCC
    ADC_setPrescaler(IC_ADC_BASE, ADC_CLK_DIV_4_0);
    ADC_setMode(IC_ADC_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
    ADC_setInterruptPulseMode(IC_ADC_BASE, ADC_PULSE_END_OF_CONV);
    ADC_enableConverter(IC_ADC_BASE);
    DEVICE_DELAY_US(1000);

    // ADCD
    ADC_setPrescaler(VDC_ADC_BASE, ADC_CLK_DIV_4_0);
    ADC_setMode(VDC_ADC_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
    ADC_setInterruptPulseMode(VDC_ADC_BASE, ADC_PULSE_END_OF_CONV);
    ADC_enableConverter(VDC_ADC_BASE);
    DEVICE_DELAY_US(1000);

    // SOCs triggered by EPWM4 SOCA (20 kHz)
    ADC_setupSOC(IB_ADC_BASE, IB_ADC_SOC, ADC_TRIGGER_SOURCE, IB_ADC_CHANNEL, ADC_SAMPLE_WINDOW);
    ADC_setupSOC(IC_ADC_BASE, IC_ADC_SOC, ADC_TRIGGER_SOURCE, IC_ADC_CHANNEL, ADC_SAMPLE_WINDOW);
    ADC_setupSOC(VDC_ADC_BASE, VDC_ADC_SOC, ADC_TRIGGER_SOURCE, VDC_ADC_CHANNEL, ADC_SAMPLE_WINDOW);

    // INT1 on each converter — used by Current_runCalibration to poll for completion
    ADC_setInterruptSource(IB_ADC_BASE, ADC_INT_NUMBER1, IB_ADC_SOC);
    ADC_enableInterrupt(IB_ADC_BASE, ADC_INT_NUMBER1);
    ADC_setInterruptSource(IC_ADC_BASE, ADC_INT_NUMBER1, IC_ADC_SOC);
    ADC_enableInterrupt(IC_ADC_BASE, ADC_INT_NUMBER1);
}

//=============================================================================
// CPU Timer 0 — 2 kHz base rate
//=============================================================================
void BOOSTXL_initTimer0(void)
{
    CPUTimer_stopTimer(CPUTIMER0_BASE);
    CPUTimer_setPeriod(CPUTIMER0_BASE, TIMER0_PERIOD_COUNTS - 1);
    CPUTimer_setPreScaler(CPUTIMER0_BASE, 0U);
    CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
    CPUTimer_setEmulationMode(CPUTIMER0_BASE, CPUTIMER_EMULATIONMODE_RUNFREE);
    CPUTimer_enableInterrupt(CPUTIMER0_BASE);
    CPUTimer_startTimer(CPUTIMER0_BASE);
}

//=============================================================================
// SPIA — BiSS-C Encoder on Site 1
//=============================================================================
void BOOSTXL_initBissSPI(void)
{
    // Full timing chain:
    //   Count 1300 (CMPC match) → INT_EPWM4 → samplePositionNowISR:
    //       GPIO123 = HIGH  (FPGA latches BiSS encoder position)
    //   FPGA decodes BiSS, drives SCLK and sends 16-bit position word to MCU
    //   → SPI RX FIFO threshold hit → SPI RX interrupt fires (CPU INT6, PIE 1)
    //   → spiaRxISR:
    //       1. Read SPI RX buffer (16-bit BiSS packet from FPGA)
    //       2. Capture SPI status
    //       3. processBissSpiPacket() → angleOutput + errorCode
    //       4. Store in volatile globals
    //   Count 2500 (CTR=PRD) → SOCA → ADC → INT_ADCB1 → currentControlISR:
    //       reads biss_angle_raw, runs control

    SPI_disableModule(BISS_SPI_BASE);

    // MCU is SPI peripheral (slave): FPGA drives SCLK and SPISTEA.
    // Mode 0: CPOL=0 (clock idles low), CPHA=0 (sample on rising edge).
    // Character length: 16 bits (Simulink Advanced tab: Data bits = 16).
    // Bitrate parameter is don't-care in peripheral mode.
    SPI_setConfig(BISS_SPI_BASE,
                  SysCtl_getLowSpeedClock(DEVICE_OSCSRC_FREQ),
                  SPI_PROT_POL0PHA0,
                  SPI_MODE_PERIPHERAL,
                  BISS_SPI_BITRATE,
                  BISS_SPI_DATAWIDTH);

    SPI_setcharLength(BISS_SPI_BASE, 16U);

    SPI_setPTESignalPolarity(BISS_SPI_BASE, SPI_PTE_ACTIVE_LOW);

    SPI_enableFIFO(BISS_SPI_BASE);
    SPI_resetTxFIFO(BISS_SPI_BASE);
    SPI_resetRxFIFO(BISS_SPI_BASE);
    // Fire RX interrupt after 1 word (16 bits) received from FPGA
    SPI_setFIFOInterruptLevel(BISS_SPI_BASE, SPI_FIFO_TX1, SPI_FIFO_RX1);
    SPI_setEmulationMode(BISS_SPI_BASE, SPI_EMULATION_FREE_RUN);

    // Enable SPIA RX FIFO interrupt (INT6.1) — spiaRxISR
    SPI_enableInterrupt(BISS_SPI_BASE, SPI_INT_RXFF);

    SPI_enableModule(BISS_SPI_BASE);
}

//=============================================================================
// CANA — CAN Bus (on-board transceiver, J12)
//=============================================================================
void BOOSTXL_initCAN(void)
{
    CAN_initModule(CAN_PERIPH_BASE);
    CAN_setBitRate(CAN_PERIPH_BASE, DEVICE_SYSCLK_FREQ, CAN_BITRATE, CAN_BIT_TIME);

    CAN_setupMessageObject(CAN_PERIPH_BASE, CAN_TX_MSG_OBJ_ID,
                           CAN_TX_MSG_ID, CAN_MSG_FRAME_STD,
                           CAN_MSG_OBJ_TYPE_TX, 0U,
                           CAN_MSG_OBJ_NO_FLAGS, 8U);

    CAN_setupMessageObject(CAN_PERIPH_BASE, CAN_RX_MSG_OBJ_ID,
                           CAN_RX_MSG_ID, CAN_MSG_FRAME_STD,
                           CAN_MSG_OBJ_TYPE_RX, CAN_RX_MSG_ID,
                           CAN_MSG_OBJ_USE_ID_FILTER, 8U);

    CAN_startModule(CAN_PERIPH_BASE);
}

//=============================================================================
// SCIA — On-board USB-to-serial (virtual COM port)
//=============================================================================
void BOOSTXL_initSerial(void)
{
    GPIO_setPinConfig(SERIAL_TX_PIN_CFG);
    GPIO_setPinConfig(SERIAL_RX_PIN_CFG);
    GPIO_setQualificationMode(SERIAL_RX_GPIO, GPIO_QUAL_ASYNC);

    SCI_performSoftwareReset(SERIAL_SCI_BASE);
    SCI_setConfig(SERIAL_SCI_BASE, DEVICE_LSPCLK_FREQ, SERIAL_BAUD, (SCI_CONFIG_WLEN_8 | SCI_CONFIG_STOP_ONE | SCI_CONFIG_PAR_NONE));
    // Enable 16-deep FIFOs so incoming bytes are buffered while the main loop
    // is busy transmitting telemetry (blocking TX).  Without this, RX overruns
    // drop bytes mid-frame and commands are parsed incorrectly.
    SCI_enableFIFO(SERIAL_SCI_BASE);
    SCI_resetRxFIFO(SERIAL_SCI_BASE);
    SCI_resetTxFIFO(SERIAL_SCI_BASE);
    SCI_enableModule(SERIAL_SCI_BASE);
    SCI_enableTxModule(SERIAL_SCI_BASE);
    SCI_enableRxModule(SERIAL_SCI_BASE);
}

void BOOSTXL_serialSendString(const char *str)
{
    while (*str)
    {
        SCI_writeCharBlockingFIFO(SERIAL_SCI_BASE, (uint16_t)*str);
        str++;
    }
}

//=============================================================================
// Top-Level Initialization
//=============================================================================
void BOOSTXL_initAll(void)
{
    BOOSTXL_initGPIO();
    BOOSTXL_initPWM();
    BOOSTXL_initADC();
    BOOSTXL_initTimer0();
    BOOSTXL_initBissSPI();
    BOOSTXL_initCAN();
    BOOSTXL_initSerial();
}

//=============================================================================
// Runtime Functions
//=============================================================================

void BOOSTXL_enableInverter(void)
{
    GPIO_writePin(INV_EN_GPIO, 0);
}

void BOOSTXL_disableInverter(void)
{
    GPIO_writePin(INV_EN_GPIO, 1);
}

void BOOSTXL_setDuty(uint32_t pwmBase, float32_t duty)
{
    if(duty < 0.0f) duty = 0.0f;
    if(duty > 1.0f) duty = 1.0f;

    uint16_t cmpVal = (uint16_t)((float32_t)PWM_TBPRD * (1.0f - duty));
    EPWM_setCounterCompareValue(pwmBase, EPWM_COUNTER_COMPARE_A, cmpVal);
}

uint16_t BOOSTXL_readPhaseBCurrent(void)
{
    return ADC_readResult(IB_ADC_RESULT_BASE, IB_ADC_SOC);
}

uint16_t BOOSTXL_readPhaseCCurrent(void)
{
    return ADC_readResult(IC_ADC_RESULT_BASE, IC_ADC_SOC);
}

uint16_t BOOSTXL_readDCBusVoltage(void)
{
    return ADC_readResult(VDC_ADC_RESULT_BASE, VDC_ADC_SOC);
}

// BOOSTXL_bissSpiTransfer() removed: the FPGA is SPI master and sends the
// decoded BiSS position autonomously after GPIO123 goes high. No MCU TX needed.

void BOOSTXL_canSend(uint16_t msgLen, const uint16_t* msgData)
{
    CAN_sendMessage(CAN_PERIPH_BASE, CAN_TX_MSG_OBJ_ID, msgLen, msgData);
}

bool BOOSTXL_canRead(uint16_t* msgData)
{
    return CAN_readMessage(CAN_PERIPH_BASE, CAN_RX_MSG_OBJ_ID, msgData);
}
