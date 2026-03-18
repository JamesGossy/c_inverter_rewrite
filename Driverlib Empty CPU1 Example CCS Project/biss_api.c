//#############################################################################
//
// FILE:   biss_api.c
//
// TITLE:  BiSS-C encoder SPI packet parser
//
//#############################################################################

#include "biss_api.h"

void processBissSpiPacket(uint16_t spiPacket,
                          uint16_t* errorCode,
                          uint16_t* angleOutput)
{
    static uint16_t prevAngleOutput = 0;

    //
    // Calculate odd parity over full 16-bit word
    //
    uint16_t spiPacketParity = spiPacket;
    spiPacketParity ^= (spiPacketParity >> 8);
    spiPacketParity ^= (spiPacketParity >> 4);
    spiPacketParity ^= (spiPacketParity >> 2);
    spiPacketParity ^= (spiPacketParity >> 1);
    spiPacketParity = (spiPacketParity) & 1;  // 1 = odd parity valid

    if(!spiPacketParity)
    {
        *errorCode = 16;
        *angleOutput = prevAngleOutput;
        return;
    }

    if(spiPacket & 0x4000)  // Bit 14: error flag
    {
        *errorCode = spiPacket & 0xF;
        *angleOutput = prevAngleOutput;
        return;
    }

    if(spiPacket & 0x8000)  // Bit 15: CRC invalid
    {
        *errorCode = 32;
    }
    else
    {
        *errorCode = 0;
    }

    prevAngleOutput = spiPacket & 0x1FFF;  // 13-bit angle
    *angleOutput = prevAngleOutput;
}

//
// End of File
//

