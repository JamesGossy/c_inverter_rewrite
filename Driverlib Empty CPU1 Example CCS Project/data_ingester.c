#include "data_ingester.h"

uint16_t getCount(void)
{
    static uint16_t counter = 0;
    counter++;
    return counter;
}

uint16_t getInvertedCount(void)
{
    static uint16_t counter = 0;
    counter++;
    return ~counter;
}

typedef union {
    struct {
        unsigned : 4;
        unsigned dataType : 2;
        unsigned loggingControl : 2;
        unsigned transactionCounter : 8;
    };
    uint16_t data;
} ControlData16_t;

void calculateChecksum(uint16_t* rawData)
{
#ifndef USE_CRC16_CHECKSUM
    uint16_t loopCount = 0;
    rawData[14] = 0;
    for (; loopCount < 16; loopCount++)
    {
        if (loopCount == 14) continue;
        rawData[14] += rawData[loopCount];
    }
#else
    rawData[14] = crc_16(rawData, 16);
#endif
}

void ingest14Ch16bit(uint16_t* dataSource)
{
    static unsigned char counter = 0;
    ControlData16_t controlData;

    controlData.data = 0;
    controlData.dataType = 0;
    controlData.loggingControl = 1;
    counter++;
    controlData.transactionCounter = counter;

    dataSource[15] = controlData.data;
    calculateChecksum(dataSource);
}

void float2uint16tArray(uint16_t* dataOutput, float dataSource)
{
    uint32_t x = *((uint32_t*)&dataSource);
    dataOutput[0] = (x >> 16) & 0xFFFF;
    dataOutput[1] = (x & 0xFFFF);
}

void uint32t2uint16tArray(uint16_t* dataOutput, uint32_t dataSource)
{
    dataOutput[0] = (dataSource >> 16) & 0xFFFF;
    dataOutput[1] = (dataSource & 0xFFFF);
}

