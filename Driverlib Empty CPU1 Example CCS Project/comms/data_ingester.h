#pragma once
#ifndef COMMS_DATA_INGESTER_H
#define COMMS_DATA_INGESTER_H

#include <stdint.h>

// #define USE_CRC16_CHECKSUM

uint16_t getCount(void);
uint16_t getInvertedCount(void);
void float2uint16tArray(uint16_t *dataOutput, float dataSource);
void uint32t2uint16tArray(uint16_t *dataOutput, uint32_t dataSource);
void ingest14Ch16bit(uint16_t *dataSource);

#endif // COMMS_DATA_INGESTER_H
