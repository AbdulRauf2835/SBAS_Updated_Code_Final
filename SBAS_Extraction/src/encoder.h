#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>
#include <stddef.h>
#include "sbas_message_structs.h"

/* ============================================================================
 * SBAS MESSAGE ENCODER LIBRARY
 * ============================================================================
 * 
 * This library provides encoding functions that convert SBAS message structures
 * back into hex packet format. Each encoder function mirrors its corresponding
 * decoder function but performs the reverse operation.
 * 
 * Usage Pattern:
 * 1. Decode message using decode_*_into()
 * 2. Modify parameters in the structure
 * 3. Encode back using encode_*_from()
 * 4. Store updated message in array
 */

/* ============================================================================
 * ENCODER FUNCTION PROTOTYPES
 * ============================================================================ */

/**
 * Encode MT1 message structure back to hex packet format
 * @param mt1_msg: Pointer to MT1 message structure with parameters
 * @param hex_output: Buffer to store encoded hex string (must be at least 64 chars)
 * @return: 0 on success, negative on error
 */
int encode_mt1_from(const SBAS_MT1_Message *mt1_msg, char *hex_output);
int encode_mt0_from(const SBAS_MT0_Message *mt0_msg, char *hex_output);

/**
 * Unified encoder for MT2, MT3, MT4, and MT5 messages
 * @param mt2_msg: Pointer to MT2-5 message structure with parameters
 * @param hex_output: Buffer to store encoded hex string (must be at least 64 chars)
 * @return: 0 on success, negative on error
 */
int encode_mt2_5_from(const SBAS_MT2_5_Message *mt2_msg, char *hex_output);

/**
 * Wrapper functions for specific message types (all use encode_mt2_5_from internally)
 */
int encode_mt2_from(const SBAS_MT2_5_Message *mt2_msg, char *hex_output);
int encode_mt3_from(const SBAS_MT2_5_Message *mt3_msg, char *hex_output);
int encode_mt4_from(const SBAS_MT2_5_Message *mt4_msg, char *hex_output);
int encode_mt5_from(const SBAS_MT2_5_Message *mt5_msg, char *hex_output);

/**
 * Encode MT9 message structure back to hex packet format
 * @param mt9_msg: Pointer to MT9 message structure with parameters
 * @param hex_output: Buffer to store encoded hex string (must be at least 64 chars)
 * @return: 0 on success, negative on error
 */
int encode_mt9_from(const SBAS_MT9_Message *mt9_msg, char *hex_output);

/**
 * Encode MT7 message structure back to hex packet format
 * @param mt7_msg: Pointer to MT7 message structure with parameters
 * @param hex_output: Buffer to store encoded hex string (must be at least 64 chars)
 * @return: 0 on success, negative on error
 */
int encode_mt7_from(const SBAS_MT7_Message *mt7_msg, char *hex_output);

/**
 * Encode MT17 message structure back to hex packet format
 * @param mt17_msg: Pointer to MT17 message structure with parameters
 * @param hex_output: Buffer to store encoded hex string (must be at least 64 chars)
 * @return: 0 on success, negative on error
 */
int encode_mt17_from(const SBAS_MT17_Message *mt17_msg, char *hex_output);
int encode_mt18_from(const SBAS_MT18_Message *mt18_msg, char *hex_output);
int encode_mt26_from(const SBAS_MT26_Message *mt26_msg, char *hex_output);
int encode_mt10_from(const SBAS_MT10_Message *mt10_msg, char *hex_output);
int encode_mt6_from(const SBAS_MT6_Message *mt6_msg, char *hex_output);
int encode_mt24_from(const SBAS_MT24_Message *mt24_msg, char *hex_output);
int encode_mt25_from(const SBAS_MT25_Message *mt25_msg, char *hex_output);
int encode_mt28_from(const SBAS_MT28_Message *mt28_msg, char *hex_output);

/**
 * Encode MT63 (Null message) to hex string
 * @param mt63_msg: Pointer to MT63 message structure (can be NULL for default)
 * @param hex_output: Output buffer for hex string (must be at least 64 chars)
 * @return: Length of hex string on success, negative on error
 */
int encode_mt63_from(const SBAS_MT63_Message *mt63_msg, char *hex_output);

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================ */

/**
 * Calculate SBAS CRC-24 for encoded message
 * @param hex_data: Hex string of message (without CRC)
 * @param data_length: Length of data in bytes
 * @return: 24-bit CRC value
 */
uint32_t calculate_sbas_crc24(const unsigned char *data, size_t data_length);

/**
 * Convert binary data to hex string format
 * @param binary_data: Input binary data
 * @param data_length: Length of binary data
 * @param hex_output: Output hex string buffer
 * @return: 0 on success, negative on error
 */
int binary_to_hex_string(const unsigned char *binary_data, size_t data_length, char *hex_output);

#endif /* ENCODER_H */