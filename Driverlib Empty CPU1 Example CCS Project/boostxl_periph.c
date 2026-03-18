//#############################################################################
//
// FILE:   boostxl_3phganinv_periph.c
//
// TITLE:  Peripheral setup for BOOSTXL-3PhGaNInv on LAUNCHXL-F28379D
//
//#############################################################################

#include "boostxl_periph.h"

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

    // SPIA pins for BiSS-C encoder (site 1)
    GPIO_setPinConfig(BISS_SIMO_PIN_CFG);   // GPIO58 -> SPISIMOA
    GPIO_setPinConfig(BISS_SOMI_PIN_CFG);   // GPIO59 -> SPISOMIA
    GPIO_setPinConfig(BISS_CLK_PIN_CFG);    // GPIO60 -> SPICLKA
    GPIO_setPinConfig(BISS_STE_PIN_CFG);    // GPIO61 -> SPISTEA
    GPIO_setQualificationMode(BISS_SOMI_GPIO, GPIO_QUAL_ASYNC);
    GPIO_setQualificationMode(BISS_CLK_GPIO, GPIO_QUAL_ASYNC);

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

    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A, EPWM_AQ_OUTPUT_HIGH, EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPA);
    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A, EPWM_AQ_OUTPUT_LOW, EPWM_AQ_OUTPUT_ON_TIMEBASE_DOWN_CMPA);

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

    initEPWMModule(PHASE_A_PWM_BASE, true);
    initEPWMModule(PHASE_B_PWM_BASE, false);
    initEPWMModule(PHASE_C_PWM_BASE, false);

    // EPWM4 SOCA triggers ADC at counter zero (20 kHz)
    EPWM_enableADCTrigger(PHASE_A_PWM_BASE, EPWM_SOC_A);
    EPWM_setADCTriggerSource(PHASE_A_PWM_BASE, EPWM_SOC_A, EPWM_SOC_TBCTR_ZERO);
    EPWM_setADCTriggerEventPrescale(PHASE_A_PWM_BASE, EPWM_SOC_A, 1U);

    // EPWM4 interrupt at counter zero (20 kHz) — triggers current controller ISR
    // This is INT3.4 (INT_EPWM4), matching the big inverter Simulink model
    EPWM_setInterruptSource(PHASE_A_PWM_BASE, EPWM_INT_TBCTR_ZERO);
    EPWM_setInterruptEventCount(PHASE_A_PWM_BASE, 1U);
    EPWM_enableInterrupt(PHASE_A_PWM_BASE);

    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
}

//=============================================================================
// ADC Initialization
//=============================================================================
void BOOSTXL_initADC(void)
{
    // ADCB
    ADC_setPrescaler(IA_ADC_BASE, ADC_CLK_DIV_4_0);
    ADC_setMode(IA_ADC_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
    ADC_setInterruptPulseMode(IA_ADC_BASE, ADC_PULSE_END_OF_CONV);
    ADC_enableConverter(IA_ADC_BASE);
    DEVICE_DELAY_US(1000);

    // ADCC
    ADC_setPrescaler(IB_ADC_BASE, ADC_CLK_DIV_4_0);
    ADC_setMode(IB_ADC_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
    ADC_setInterruptPulseMode(IB_ADC_BASE, ADC_PULSE_END_OF_CONV);
    ADC_enableConverter(IB_ADC_BASE);
    DEVICE_DELAY_US(1000);

    // ADCD
    ADC_setPrescaler(VDC_ADC_BASE, ADC_CLK_DIV_4_0);
    ADC_setMode(VDC_ADC_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
    ADC_setInterruptPulseMode(VDC_ADC_BASE, ADC_PULSE_END_OF_CONV);
    ADC_enableConverter(VDC_ADC_BASE);
    DEVICE_DELAY_US(1000);

    // SOCs triggered by EPWM4 SOCA (20 kHz)
    ADC_setupSOC(IA_ADC_BASE, IA_ADC_SOC, ADC_TRIGGER_SOURCE, IA_ADC_CHANNEL, ADC_SAMPLE_WINDOW);
    ADC_setupSOC(IB_ADC_BASE, IB_ADC_SOC, ADC_TRIGGER_SOURCE, IB_ADC_CHANNEL, ADC_SAMPLE_WINDOW);
    ADC_setupSOC(IC_ADC_BASE, IC_ADC_SOC, ADC_TRIGGER_SOURCE, IC_ADC_CHANNEL, ADC_SAMPLE_WINDOW);
    ADC_setupSOC(VDC_ADC_BASE, VDC_ADC_SOC, ADC_TRIGGER_SOURCE, VDC_ADC_CHANNEL, ADC_SAMPLE_WINDOW);
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
    SPI_disableModule(BISS_SPI_BASE);

    SPI_setConfig(BISS_SPI_BASE,
                  SysCtl_getLowSpeedClock(DEVICE_OSCSRC_FREQ),
                  SPI_PROT_POL0PHA1,
                  SPI_MODE_CONTROLLER,
                  BISS_SPI_BITRATE,
                  BISS_SPI_DATAWIDTH);

    SPI_enableFIFO(BISS_SPI_BASE);
    SPI_resetTxFIFO(BISS_SPI_BASE);
    SPI_resetRxFIFO(BISS_SPI_BASE);
    SPI_setFIFOInterruptLevel(BISS_SPI_BASE, SPI_FIFO_TX1, SPI_FIFO_RX1);
    SPI_setEmulationMode(BISS_SPI_BASE, SPI_EMULATION_FREE_RUN);

    // Enable SPIA RX interrupt (INT6.1) for BiSS completion
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
    SCI_setConfig(SERIAL_SCI_BASE, DEVICE_LSPCLK_FREQ, SERIAL_BAUD,
                  (SCI_CONFIG_WLEN_8 | SCI_CONFIG_STOP_ONE | SCI_CONFIG_PAR_NONE));
    SCI_enableModule(SERIAL_SCI_BASE);
    SCI_enableTxModule(SERIAL_SCI_BASE);
    SCI_enableRxModule(SERIAL_SCI_BASE);
}

void BOOSTXL_serialSendString(const char *str)
{
    while (*str)
    {
        SCI_writeCharBlockingNonFIFO(SERIAL_SCI_BASE, (uint16_t)*str);
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

uint16_t BOOSTXL_readPhaseACurrent(void)
{
    return ADC_readResult(IA_ADC_RESULT_BASE, IA_ADC_SOC);
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

uint16_t BOOSTXL_bissSpiTransfer(uint16_t txData)
{
    SPI_writeDataNonBlocking(BISS_SPI_BASE, txData);

    while(SPI_getRxFIFOStatus(BISS_SPI_BASE) == SPI_FIFO_RX0)
    {
    }

    return SPI_readDataNonBlocking(BISS_SPI_BASE);
}

void BOOSTXL_canSend(uint16_t msgLen, const uint16_t* msgData)
{
    CAN_sendMessage(CAN_PERIPH_BASE, CAN_TX_MSG_OBJ_ID, msgLen, msgData);
}

bool BOOSTXL_canRead(uint16_t* msgData)
{
    return CAN_readMessage(CAN_PERIPH_BASE, CAN_RX_MSG_OBJ_ID, msgData);
}

