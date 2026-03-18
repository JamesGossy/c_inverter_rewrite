//#############################################################################
//
// FILE:   biss_api.h
//
// TITLE:  BiSS-C encoder SPI packet parser
//
//#############################################################################

#ifndef BISS_API_H
#define BISS_API_H

#include <stdint.h>

//
// Parse a 16-bit SPI packet from the BiSS FPGA interface.
//
// Packet format:
//   <12:0>  Position data (13-bit angle)
//   <13>    Parity (odd parity over the whole word)
//   <14>    Error flag
//             When set, <3:0> contains:
//               <0>   Encoder E0
//               <1>   Encoder E1
//               <3:2> FPGA biss_sampler_error_code
//   <15>    CRC invalid — use previous valid data
//
// Outputs:
//   errorCode:   0 = no error
//                16 = parity fail
//                32 = CRC fail (angle still updated from FPGA)
//                1-15 = encoder/FPGA error bits
//   angleOutput: 13-bit angle (0-8191), holds last valid on error
//
void processBissSpiPacket(uint16_t spiPacket,
                          uint16_t* errorCode,
                          uint16_t* angleOutput);

#endif // BISS_API_H

