#define _CRT_SECURE_NO_WARNINGS
#include "encoder.h"
#include "bitops.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ============================================================================
 * SBAS MESSAGE ENCODER IMPLEMENTATION
 * ============================================================================
 * 
 * This file implements encoding functions that convert SBAS message structures
 * back into hex packet format, mirroring the decoder functions but in reverse.
 */

/* ============================================================================
 * BIT WRITING HELPER FUNCTIONS
 * ============================================================================ */

/**
 * Write bits to a byte buffer (reverse of sbas_bits_get functions)
 */
static void write_bits_to_buffer(unsigned char *buffer, int bit_offset, uint64_t value, int num_bits) {
    for (int i = 0; i < num_bits; i++) {
        int byte_pos = (bit_offset + i) / 8;
        int bit_pos = 7 - ((bit_offset + i) % 8);  // MSB first
        if ((value >> (num_bits - 1 - i)) & 1) {
            buffer[byte_pos] |= (1 << bit_pos);
        } else {
            buffer[byte_pos] &= ~(1 << bit_pos);
        }
    }
}

/* ============================================================================
 * CRC CALCULATION
 * ============================================================================ */

/**
 * Calculate SBAS CRC-24 using polynomial 0x1864CFB
 * This mirrors the CRC validation used in the decoder
 */
uint32_t calculate_sbas_crc24(const unsigned char *data, size_t data_length_bits) {
    const uint32_t POLY = 0x1864CFBu; /* CRC-24Q polynomial */
    uint32_t crc = 0u; /* 24-bit register */

    for (size_t bit_idx = 0; bit_idx < data_length_bits; ++bit_idx) {
        int byte_idx = bit_idx / 8;
        int bit_in_byte = 7 - (bit_idx % 8); /* MSB-first */
        
        uint32_t input_bit = 0u;
        if (byte_idx < (data_length_bits + 7) / 8) {
            input_bit = (uint32_t)((data[byte_idx] >> bit_in_byte) & 1u);
        }

        /* Feedback is MSB ^ input bit */
        uint32_t feedback = ((crc >> 23) & 1u) ^ input_bit;
        
        /* Shift and conditionally XOR polynomial */
        crc = (crc << 1) & 0xFFFFFFu;
        if (feedback) {
            crc ^= (POLY & 0xFFFFFFu);
        }
    }
    return crc & 0xFFFFFFu;
}

/* ============================================================================
 * MT0 ENCODER IMPLEMENTATION (Test Message)
 * ============================================================================ */
int encode_mt0_from(const SBAS_MT0_Message *mt0_msg, char *hex_output) {
    if (!mt0_msg || !hex_output) return -1;
    unsigned char b[32]; memset(b, 0, sizeof(b)); int off = 0;
    // Header
    write_bits_to_buffer(b, off, mt0_msg->header.preamble, 8); off += 8;
    write_bits_to_buffer(b, off, mt0_msg->header.message_type, 6); off += 6;
    // Payload: 212 bits from test_data[] MSB-first starting at bit 14
    // The test_data in struct is 27 bytes (per sbas_message_structs.h comment), but decoder copies 212 bits.
    // We'll write exactly 212 bits from mt0_msg->test_data MSB-first.
    for (int i = 0; i < 212; ++i) {
        int byte = i / 8; int bit_in_byte = 7 - (i % 8);
        uint64_t bit = (mt0_msg->test_data[byte] >> bit_in_byte) & 1u;
        write_bits_to_buffer(b, off, bit, 1); off += 1;
    }
    // CRC-24 over bits [0, off)
    uint32_t crc = calculate_sbas_crc24(b, off);
    write_bits_to_buffer(b, off, crc, 24); off += 24;
    // Hex output
    int total_bytes = (off + 7) / 8; hex_output[0] = '\0';
    for (int i = 0; i < total_bytes; ++i) { char byte_hex[3]; sprintf(byte_hex, "%02X", b[i]); strcat(hex_output, byte_hex);}    
    return 0;
}

/* ============================================================================
 * MT1 ENCODER IMPLEMENTATION
 * ============================================================================ */

/**
 * Encode MT1 message structure back to hex packet format
 * Mirrors decode_mt1_into() but performs reverse operation
 */
int encode_mt1_from(const SBAS_MT1_Message *mt1_msg, char *hex_output) {
    if (!mt1_msg || !hex_output) return -1;
    // Create binary buffer (32 bytes max for SBAS message)
    unsigned char binary_buffer[32];
    memset(binary_buffer, 0, sizeof(binary_buffer));
    int bit_pos = 0;
    // Write preamble (8 bits)
    write_bits_to_buffer(binary_buffer, bit_pos, mt1_msg->header.preamble, 8);
    bit_pos += 8;
    // Write message type (6 bits) 
    write_bits_to_buffer(binary_buffer, bit_pos, mt1_msg->header.message_type, 6);
    bit_pos += 6;
    // Write PRN mask (27 bytes = 216 bits, but only 210 used for PRNs 1-210)
    for (int i = 0; i < 27; i++) {
        write_bits_to_buffer(binary_buffer, bit_pos, mt1_msg->prn_mask[i], 8);
        bit_pos += 8;
    }
    // bit_pos is now at 14 + 216 = 230, but we only use 210 PRN bits (bit 14-223)
    // Write IODP (2 bits) at bit position 224 (after 210 PRN bits + 4 spare)
    write_bits_to_buffer(binary_buffer, 224, mt1_msg->iodp, 2);
    bit_pos = 226; // After IODP
    // Calculate CRC for data bits 0-225 (excluding CRC itself)
    uint32_t calculated_crc = calculate_sbas_crc24(binary_buffer, 226);
    // Write CRC (24 bits) starting at bit 226
    write_bits_to_buffer(binary_buffer, 226, calculated_crc, 24);
    // Convert binary to hex string format (no spaces)
    int total_bytes = (226 + 24 + 7) / 8; // Round up to complete bytes
    hex_output[0] = '\0'; // Initialize empty string
    for (int i = 0; i < total_bytes; i++) {
        char byte_hex[3];
        sprintf(byte_hex, "%02X", binary_buffer[i]);
        strcat(hex_output, byte_hex);
    }
    return 0;
}

/* ============================================================================
 * MT2-5 ENCODER IMPLEMENTATION  
 * ============================================================================ */

/**
 * Unified encoder for MT2, MT3, MT4, and MT5 messages
 * All these message types share the same structure and encoding format
 * The only difference is the message type field in the header
 */
int encode_mt2_5_from(const SBAS_MT2_5_Message *mt2_msg, char *hex_output) {
    if (!mt2_msg || !hex_output) return -1;
    // Create binary buffer
    unsigned char binary_buffer[32];
    memset(binary_buffer, 0, sizeof(binary_buffer));
    int bit_pos = 0;
    // Write preamble (8 bits)
    write_bits_to_buffer(binary_buffer, bit_pos, mt2_msg->header.preamble, 8);
    bit_pos += 8;
    // Write message type (6 bits)
    write_bits_to_buffer(binary_buffer, bit_pos, mt2_msg->header.message_type, 6);
    bit_pos += 6;
    // Write IODF (2 bits) - Issue of Data Fast Corrections
    write_bits_to_buffer(binary_buffer, bit_pos, mt2_msg->iodf, 2);
    bit_pos += 2;
    // Write IODP (2 bits) - Issue of Data PRN (must match MT1)
    write_bits_to_buffer(binary_buffer, bit_pos, mt2_msg->iodp, 2);
    bit_pos += 2;
    // Write fast correction data (13 satellites × 12 bits each = 156 bits)
    for (int i = 0; i < 13; i++) {
        // Fast correction values are signed 12-bit values (-2047 to +2047)
        uint16_t fc_encoded = (uint16_t)(mt2_msg->sat_corrections[i].fast_correction & 0xFFF);
        write_bits_to_buffer(binary_buffer, bit_pos, fc_encoded, 12);
        bit_pos += 12;
    }
    // Write UDREI (User Differential Range Error Indicator) for 13 satellites
    // Each UDREI is 4 bits (0-15 range)
    for (int i = 0; i < 13; i++) {
        write_bits_to_buffer(binary_buffer, bit_pos, mt2_msg->sat_corrections[i].udrei, 4);
        bit_pos += 4;
    }
    // bit_pos should now be at 14 + 2 + 2 + 156 + 52 = 226
    // Calculate CRC for data bits 0-225
    uint32_t calculated_crc = calculate_sbas_crc24(binary_buffer, 226);
    // Write CRC (24 bits) starting at bit 226
    write_bits_to_buffer(binary_buffer, 226, calculated_crc, 24);
    // Convert binary to hex string format (no spaces)
    int total_bytes = (226 + 24 + 7) / 8;
    hex_output[0] = '\0';
    for (int i = 0; i < total_bytes; i++) {
        char byte_hex[3];
        sprintf(byte_hex, "%02X", binary_buffer[i]);
        strcat(hex_output, byte_hex);
    }
    return 0;
}

/* ============================================================================
 * MT2-5 WRAPPER FUNCTIONS
 * ============================================================================ */

/**
 * Wrapper function for MT2 encoding
 */
int encode_mt2_from(const SBAS_MT2_5_Message *mt2_msg, char *hex_output) {
    return encode_mt2_5_from(mt2_msg, hex_output);
}
/**
 * Wrapper function for MT3 encoding
 */
int encode_mt3_from(const SBAS_MT2_5_Message *mt3_msg, char *hex_output) {
    return encode_mt2_5_from(mt3_msg, hex_output);
}
/**
 * Wrapper function for MT4 encoding
 */
int encode_mt4_from(const SBAS_MT2_5_Message *mt4_msg, char *hex_output) {
    return encode_mt2_5_from(mt4_msg, hex_output);
}
/**
 * Wrapper function for MT5 encoding
 */
int encode_mt5_from(const SBAS_MT2_5_Message *mt5_msg, char *hex_output) {
    return encode_mt2_5_from(mt5_msg, hex_output);
}

/* ============================================================================
 * MT9 ENCODER IMPLEMENTATION
 * ============================================================================ */
int encode_mt9_from(const SBAS_MT9_Message *mt9_msg, char *hex_output) {
    if (!mt9_msg || !hex_output) return -1;

    unsigned char b[32];
    memset(b, 0, sizeof(b));
    int bit_pos = 0;

    /* Header */
    write_bits_to_buffer(b, bit_pos, mt9_msg->header.preamble, 8); bit_pos += 8;
    write_bits_to_buffer(b, bit_pos, mt9_msg->header.message_type, 6); bit_pos += 6;

    /* Payload starts at bit 14 */
    bit_pos = 14;

    /* Reserved (8) */
    write_bits_to_buffer(b, bit_pos, mt9_msg->reserved, 8); bit_pos += 8;

    /* t0 (13), URA (4) */
    uint64_t t0_raw = (uint64_t)llround(mt9_msg->t0 / 16.0);
    write_bits_to_buffer(b, bit_pos, t0_raw & 0x1FFFu, 13); bit_pos += 13;
    write_bits_to_buffer(b, bit_pos, (uint64_t)(mt9_msg->ura & 0x0Fu), 4); bit_pos += 4;

    /* Positions */
    int64_t xg_raw = (int64_t)llround(mt9_msg->xg_ecef / 0.08);
    int64_t yg_raw = (int64_t)llround(mt9_msg->yg_ecef / 0.08);
    int64_t zg_raw = (int64_t)llround(mt9_msg->zg_ecef / 0.4);
    write_bits_to_buffer(b, bit_pos, (uint64_t)(xg_raw & 0x3FFFFFFFu), 30); bit_pos += 30;
    write_bits_to_buffer(b, bit_pos, (uint64_t)(yg_raw & 0x3FFFFFFFu), 30); bit_pos += 30;
    write_bits_to_buffer(b, bit_pos, (uint64_t)(zg_raw & 0x1FFFFFFu), 25); bit_pos += 25;

    /* Rates */
    int64_t xg_rate_raw = (int64_t)llround(mt9_msg->xg_rate / 0.000625);
    int64_t yg_rate_raw = (int64_t)llround(mt9_msg->yg_rate / 0.000625);
    int64_t zg_rate_raw = (int64_t)llround(mt9_msg->zg_rate / 0.004);
    write_bits_to_buffer(b, bit_pos, (uint64_t)(xg_rate_raw & 0x1FFFFu), 17); bit_pos += 17;
    write_bits_to_buffer(b, bit_pos, (uint64_t)(yg_rate_raw & 0x1FFFFu), 17); bit_pos += 17;
    write_bits_to_buffer(b, bit_pos, (uint64_t)(zg_rate_raw & 0x3FFFFu), 18); bit_pos += 18;

    /* Accelerations */
    int64_t xg_acc_raw = (int64_t)llround(mt9_msg->xg_accel / 0.0000125);
    int64_t yg_acc_raw = (int64_t)llround(mt9_msg->yg_accel / 0.0000125);
    int64_t zg_acc_raw = (int64_t)llround(mt9_msg->zg_accel / 0.00000625);
    write_bits_to_buffer(b, bit_pos, (uint64_t)(xg_acc_raw & 0x3FFu), 10); bit_pos += 10;
    write_bits_to_buffer(b, bit_pos, (uint64_t)(yg_acc_raw & 0x3FFu), 10); bit_pos += 10;
    write_bits_to_buffer(b, bit_pos, (uint64_t)(zg_acc_raw & 0x3FFu), 10); bit_pos += 10;

    /* Clock terms */
    int64_t agf0_raw = (int64_t)llround(mt9_msg->ag_f0 * pow(2.0, 31));
    int64_t agf1_raw = (int64_t)llround(mt9_msg->ag_f1 * pow(2.0, 40));
    write_bits_to_buffer(b, bit_pos, (uint64_t)(agf0_raw & 0xFFFu), 12); bit_pos += 12;
    write_bits_to_buffer(b, bit_pos, (uint64_t)(agf1_raw & 0xFFu), 8); bit_pos += 8;

    /* CRC over data bits 0..(bit_pos-1) excluding CRC itself; MT encoders elsewhere use 226 bits pre-CRC.
       Here we compute CRC for the current data region */
    uint32_t crc = calculate_sbas_crc24(b, bit_pos);
    write_bits_to_buffer(b, bit_pos, crc, 24); bit_pos += 24;

    /* Convert to hex */
    int total_bytes = (bit_pos + 7) / 8;
    hex_output[0] = '\0';
    for (int i = 0; i < total_bytes; i++) {
        char byte_hex[3];
        sprintf(byte_hex, "%02X", b[i]);
        strcat(hex_output, byte_hex);
    }
    return 0;
}

/* ============================================================================
 * MT7 ENCODER IMPLEMENTATION
 * ============================================================================ */

int encode_mt7_from(const SBAS_MT7_Message *mt7_msg, char *hex_output) {
    if (!mt7_msg || !hex_output) return -1;

    unsigned char b[32];
    memset(b, 0, sizeof(b));
    int bit_pos = 0;

    /* Header */
    write_bits_to_buffer(b, bit_pos, mt7_msg->header.preamble, 8); bit_pos += 8;
    write_bits_to_buffer(b, bit_pos, mt7_msg->header.message_type, 6); bit_pos += 6;

    /* Payload starts at bit 14: t_lat(4), iodp(2), spare(2) */
    bit_pos = 14;
    write_bits_to_buffer(b, bit_pos, (uint64_t)(mt7_msg->t_lat & 0x0Fu), 4); bit_pos += 4;
    write_bits_to_buffer(b, bit_pos, (uint64_t)(mt7_msg->iodp & 0x03u), 2); bit_pos += 2;
    /* spare 2 bits set to 0 */
    write_bits_to_buffer(b, bit_pos, 0u, 2); bit_pos += 2;

    /* 51 x 4-bit UDRE AI values */
    for (int i = 0; i < 51; ++i) {
        uint8_t v = mt7_msg->udre_ai[i] & 0x0F;
        write_bits_to_buffer(b, bit_pos, (uint64_t)v, 4); bit_pos += 4;
    }

    /* CRC over all data bits up to here */
    uint32_t crc = calculate_sbas_crc24(b, bit_pos);
    write_bits_to_buffer(b, bit_pos, crc, 24); bit_pos += 24;

    int total_bytes = (bit_pos + 7) / 8;
    hex_output[0] = '\0';
    for (int i = 0; i < total_bytes; i++) {
        char byte_hex[3];
        sprintf(byte_hex, "%02X", b[i]);
        strcat(hex_output, byte_hex);
    }
    return 0;
}

/* ============================================================================
 * MT17 ENCODER IMPLEMENTATION
 * ============================================================================ */

/**
 * Encode MT17 message structure back to hex packet format
 * Mirrors decode_mt17_into() but performs reverse operation
 */
int encode_mt17_from(const SBAS_MT17_Message *mt17_msg, char *hex_output) {
    if (!mt17_msg || !hex_output) return -1;
    
    // Create binary buffer (32 bytes max for SBAS message)
    unsigned char binary_buffer[32];
    memset(binary_buffer, 0, sizeof(binary_buffer));
    int bit_pos = 0;
    
    // Write preamble (8 bits)
    write_bits_to_buffer(binary_buffer, bit_pos, mt17_msg->header.preamble, 8);
    bit_pos += 8;
    
    // Write message type (6 bits) - should be 17
    write_bits_to_buffer(binary_buffer, bit_pos, mt17_msg->header.message_type, 6);
    bit_pos += 6;
    
    // Payload starts at bit 14
    bit_pos = 14;
    
    // Write 3 GEO blocks (each 67 bits)
    for (int i = 0; i < SBAS_MT17_GEOS; i++) {
        const SBAS_MT17_GeoBlock *geo = &mt17_msg->geo[i];
        
        // data_id (2 bits)
        write_bits_to_buffer(binary_buffer, bit_pos, geo->data_id, 2);
        bit_pos += 2;
        
        // prn_number (8 bits)
        write_bits_to_buffer(binary_buffer, bit_pos, geo->prn_number, 8);
        bit_pos += 8;
        
        // health_and_status (8 bits)
        write_bits_to_buffer(binary_buffer, bit_pos, geo->health_and_status, 8);
        bit_pos += 8;
        
        // Convert physical values back to raw values using ICD scales
        int64_t xg_raw = (int64_t)(geo->x_g / 2600.0);
        int64_t yg_raw = (int64_t)(geo->y_g / 2600.0);
        int64_t zg_raw = (int64_t)(geo->z_g / 26000.0);
        int64_t xg_dot_raw = (int64_t)(geo->xg_dot / 10.0);
        int64_t yg_dot_raw = (int64_t)(geo->yg_dot / 10.0);
        int64_t zg_dot_raw = (int64_t)(geo->zg_dot / 60.0);
        
        // x_g (15 bits signed)
        write_bits_to_buffer(binary_buffer, bit_pos, (uint64_t)xg_raw & 0x7FFF, 15);
        bit_pos += 15;
        
        // y_g (15 bits signed)
        write_bits_to_buffer(binary_buffer, bit_pos, (uint64_t)yg_raw & 0x7FFF, 15);
        bit_pos += 15;
        
        // z_g (9 bits signed)
        write_bits_to_buffer(binary_buffer, bit_pos, (uint64_t)zg_raw & 0x1FF, 9);
        bit_pos += 9;
        
        // xg_dot (3 bits signed)
        write_bits_to_buffer(binary_buffer, bit_pos, (uint64_t)xg_dot_raw & 0x7, 3);
        bit_pos += 3;
        
        // yg_dot (3 bits signed)
        write_bits_to_buffer(binary_buffer, bit_pos, (uint64_t)yg_dot_raw & 0x7, 3);
        bit_pos += 3;
        
        // zg_dot (4 bits signed)
        write_bits_to_buffer(binary_buffer, bit_pos, (uint64_t)zg_dot_raw & 0xF, 4);
        bit_pos += 4;
    }
    
    // t0 (11 bits)
    uint64_t t0_raw = (uint64_t)(mt17_msg->t0 / 64.0);
    write_bits_to_buffer(binary_buffer, bit_pos, t0_raw, 11);
    bit_pos += 11;
    
    // Calculate CRC-24 for data bits (exclude CRC itself)
    uint32_t calculated_crc = calculate_sbas_crc24(binary_buffer, bit_pos);
    
    // Write CRC-24 (24 bits)
    write_bits_to_buffer(binary_buffer, bit_pos, calculated_crc, 24);
    bit_pos += 24;
    
    // Convert binary buffer to hex string
    size_t data_length = (bit_pos + 7) / 8; // Round up to nearest byte
    return binary_to_hex_string(binary_buffer, data_length, hex_output);
}

/* ============================================================================
 * MT18 ENCODER IMPLEMENTATION (Ionospheric Grid Point Mask)
 * ============================================================================ */
int encode_mt18_from(const SBAS_MT18_Message *mt18_msg, char *hex_output) {
    if (!mt18_msg || !hex_output) return -1;

    unsigned char b[32]; /* 226+24 bits <= 32 bytes */
    memset(b, 0, sizeof(b));
    int bit_pos = 0;

    /* Header */
    write_bits_to_buffer(b, bit_pos, mt18_msg->header.preamble, 8); bit_pos += 8;
    write_bits_to_buffer(b, bit_pos, mt18_msg->header.message_type, 6); bit_pos += 6;

    /* Payload starts at bit 14 */
    bit_pos = 14;

    /* number_of_bands(4), band_number(4), iodi(2), spare(1) */
    write_bits_to_buffer(b, bit_pos, (uint64_t)(mt18_msg->number_of_bands & 0x0F), 4); bit_pos += 4;
    write_bits_to_buffer(b, bit_pos, (uint64_t)(mt18_msg->band_number & 0x0F), 4); bit_pos += 4;
    write_bits_to_buffer(b, bit_pos, (uint64_t)(mt18_msg->iodi & 0x03), 2); bit_pos += 2;
    write_bits_to_buffer(b, bit_pos, (uint64_t)(mt18_msg->spare & 0x01), 1); bit_pos += 1;

    /* IGP mask: 201 bits MSB-first stored across 26 bytes (208 bits) in struct.
       We emit only the first 201 bits exactly as decoder expects. */
    int igp_bits = 201;
    for (int i = 0; i < igp_bits; ++i) {
        int byte = i >> 3; /* i/8 */
        int bit_in_byte = 7 - (i & 7); /* MSB-first */
        uint8_t val = (mt18_msg->igp_mask[byte] >> bit_in_byte) & 0x01;
        write_bits_to_buffer(b, bit_pos + i, (uint64_t)val, 1);
    }
    bit_pos += igp_bits;

    /* CRC over data bits up to current bit_pos */
    uint32_t crc = calculate_sbas_crc24(b, bit_pos);
    write_bits_to_buffer(b, bit_pos, crc, 24); bit_pos += 24;

    /* Convert to hex */
    int total_bytes = (bit_pos + 7) / 8;
    hex_output[0] = '\0';
    for (int i = 0; i < total_bytes; ++i) {
        char byte_hex[3];
        sprintf(byte_hex, "%02X", b[i]);
        strcat(hex_output, byte_hex);
    }
    return 0;
}

/**
 * Encode MT63 (Null message) to hex string
 * Mirrors decode_mt63_into() but performs reverse operation
 */
int encode_mt63_from(const SBAS_MT63_Message *mt63_msg, char *hex_output) {
    if (!hex_output) return -1;
    
    unsigned char binary_buffer[32] = {0}; // 250 bits = ~32 bytes
    size_t bit_pos = 0;
    
    // Preamble (8 bits) - Standard SBAS preamble
    write_bits_to_buffer(binary_buffer, bit_pos, 0x53, 8);
    bit_pos += 8;
    
    // Message Type (6 bits) - MT63
    write_bits_to_buffer(binary_buffer, bit_pos, 63, 6);
    bit_pos += 6;
    
    // Null data (212 bits) - All zeros for filler message
    if (mt63_msg && mt63_msg->null_data) {
        // If specific data provided, use it
        for (int i = 0; i < 212; i += 8) {
            int byte_idx = i / 8;
            if (byte_idx < (int)sizeof(mt63_msg->null_data)) {
                write_bits_to_buffer(binary_buffer, bit_pos, mt63_msg->null_data[byte_idx], 8);
            } else {
                write_bits_to_buffer(binary_buffer, bit_pos, 0, 8);
            }
            bit_pos += 8;
        }
    } else {
        // Default: all zeros for null data (212 bits)
        bit_pos += 212; // Skip 212 bits (already zero-initialized)
    }
    
    // Calculate CRC-24 for data bits (exclude CRC itself)
    uint32_t calculated_crc = calculate_sbas_crc24(binary_buffer, bit_pos);
    
    // Write CRC-24 (24 bits)
    write_bits_to_buffer(binary_buffer, bit_pos, calculated_crc, 24);
    bit_pos += 24;
    
    // Convert binary buffer to hex string
    size_t data_length = (bit_pos + 7) / 8; // Round up to nearest byte
    return binary_to_hex_string(binary_buffer, data_length, hex_output);
}

/* ============================================================================
 * PARAMETER VALIDATION
 * ============================================================================ */

/* ============================================================================
 * MT10 ENCODER IMPLEMENTATION (Degradation Parameters)
 * Mirrors decode_mt10_into() bit layout and scales
 * ============================================================================ */
int encode_mt10_from(const SBAS_MT10_Message *mt10_msg, char *hex_output) {
    if (!mt10_msg || !hex_output) return -1;

    unsigned char b[32];
    memset(b, 0, sizeof(b));
    int off = 0;

    /* Header */
    write_bits_to_buffer(b, off, mt10_msg->header.preamble, 8); off += 8;
    write_bits_to_buffer(b, off, mt10_msg->header.message_type, 6); off += 6;

    /* Payload base at bit 14 */
    off = 14;

    /* Convert physical values back to raw per scales used in decoder */
    uint64_t brrc_raw       = (uint64_t)llround(mt10_msg->B_rrc / 0.002);
    uint64_t cltc_lsb_raw   = (uint64_t)llround(mt10_msg->C_ltc_lsb / 0.002);
    uint64_t cltc_v1_raw    = (uint64_t)llround(mt10_msg->C_ltc_v1 / 0.00005);
    uint64_t iltc_v1_raw    = (uint64_t)llround(mt10_msg->I_ltc_v1 / 1.0);
    uint64_t cltc_v0_raw    = (uint64_t)llround(mt10_msg->C_ltc_v0 / 0.002);
    uint64_t iltc_v0_raw    = (uint64_t)llround(mt10_msg->I_ltc_v0 / 1.0);
    uint64_t cgeo_lsb_raw   = (uint64_t)llround(mt10_msg->C_geo_lsb / 0.0005);
    uint64_t cgeo_v_raw     = (uint64_t)llround(mt10_msg->C_geo_v / 0.00005);
    uint64_t igeo_raw       = (uint64_t)llround(mt10_msg->I_geo / 1.0);
    uint64_t cer_raw        = (uint64_t)llround(mt10_msg->C_er / 0.5);
    uint64_t ciono_step_raw = (uint64_t)llround(mt10_msg->C_iono_step / 0.001);
    uint64_t iono_raw       = (uint64_t)llround(mt10_msg->I_iono / 1.0);
    uint64_t ciono_ramp_raw = (uint64_t)llround(mt10_msg->C_iono_ramp / 0.000005);
    uint64_t rss_udre_raw   = (uint64_t)(mt10_msg->rss_udre & 0x1);
    uint64_t rss_iono_raw   = (uint64_t)(mt10_msg->rss_iono & 0x1);
    uint64_t ccov_raw       = (uint64_t)llround(mt10_msg->C_covariance / 0.1);

    /* Write fields using same bit widths as decode_mt10_into */
    write_bits_to_buffer(b, off, brrc_raw & 0x3FFu, 10); off += 10;
    write_bits_to_buffer(b, off, cltc_lsb_raw & 0x3FFu, 10); off += 10;
    write_bits_to_buffer(b, off, cltc_v1_raw & 0x3FFu, 10); off += 10;
    write_bits_to_buffer(b, off, iltc_v1_raw & 0x1FFu, 9); off += 9;
    write_bits_to_buffer(b, off, cltc_v0_raw & 0x3FFu, 10); off += 10;
    write_bits_to_buffer(b, off, iltc_v0_raw & 0x1FFu, 9); off += 9;
    write_bits_to_buffer(b, off, cgeo_lsb_raw & 0x3FFu, 10); off += 10;
    write_bits_to_buffer(b, off, cgeo_v_raw & 0x3FFu, 10); off += 10;
    write_bits_to_buffer(b, off, igeo_raw & 0x1FFu, 9); off += 9;
    write_bits_to_buffer(b, off, cer_raw & 0x3Fu, 6); off += 6;
    write_bits_to_buffer(b, off, ciono_step_raw & 0x3FFu, 10); off += 10;
    write_bits_to_buffer(b, off, iono_raw & 0x1FFu, 9); off += 9;
    write_bits_to_buffer(b, off, ciono_ramp_raw & 0x3FFu, 10); off += 10;
    write_bits_to_buffer(b, off, rss_udre_raw & 0x1u, 1); off += 1;
    write_bits_to_buffer(b, off, rss_iono_raw & 0x1u, 1); off += 1;
    write_bits_to_buffer(b, off, ccov_raw & 0x7Fu, 7); off += 7;

    /* Spare: 81 bits. We store the 11-byte array as is, MSB-first per decoder note. */
    for (int i = 0; i < 81; ++i) {
        int byte_idx = i / 8;
        int bit_idx  = 7 - (i % 8);
        uint8_t val = 0;
        if (byte_idx < (int)sizeof(mt10_msg->spare)) {
            val = (mt10_msg->spare[byte_idx] >> bit_idx) & 0x01;
        }
        write_bits_to_buffer(b, off + i, (uint64_t)val, 1);
    }
    off += 81;

    /* CRC over data up to now */
    uint32_t crc = calculate_sbas_crc24(b, off);
    write_bits_to_buffer(b, off, crc, 24); off += 24;

    int total_bytes = (off + 7) / 8;
    hex_output[0] = '\0';
    for (int i = 0; i < total_bytes; ++i) {
        char byte_hex[3]; sprintf(byte_hex, "%02X", b[i]); strcat(hex_output, byte_hex);
    }
    return 0;
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Convert binary data to hex string format (no spaces)
 */
int binary_to_hex_string(const unsigned char *binary_data, size_t data_length, char *hex_output) {
    if (!binary_data || !hex_output) return -1;
    
    hex_output[0] = '\0';
    
    for (size_t i = 0; i < data_length; i++) {
        char byte_hex[3];
        sprintf(byte_hex, "%02X", binary_data[i]);
        strcat(hex_output, byte_hex);
    }
    
    return 0;
}

/* ============================================================================
 * MT26 ENCODER IMPLEMENTATION (Ionospheric delay corrections)
 * Mirrors decode_mt26_into() bit layout
 * ============================================================================ */
int encode_mt26_from(const SBAS_MT26_Message *mt26_msg, char *hex_output) {
    if (!mt26_msg || !hex_output) return -1;

    unsigned char b[32];
    memset(b, 0, sizeof(b));
    int off = 0;

    /* Header */
    write_bits_to_buffer(b, off, mt26_msg->header.preamble, 8); off += 8;
    write_bits_to_buffer(b, off, mt26_msg->header.message_type, 6); off += 6;

    /* Payload start at bit 14 */
    off = 14;

    /* Headers: band_number(4), block_id(4) */
    write_bits_to_buffer(b, off, (uint64_t)(mt26_msg->band_number & 0x0F), 4); off += 4;
    write_bits_to_buffer(b, off, (uint64_t)(mt26_msg->block_id & 0x0F), 4); off += 4;

    /* 15 IGPs: each 9-bit vertical delay then 4-bit GIVEI */
    for (int i = 0; i < SBAS_MT26_IGP_COUNT; ++i) {
        write_bits_to_buffer(b, off, (uint64_t)(mt26_msg->igp_vertical_delay[i] & 0x1FF), 9); off += 9;
        write_bits_to_buffer(b, off, (uint64_t)(mt26_msg->givei[i] & 0x0F), 4); off += 4;
    }

    /* Control + spare: iodi(2), spare(7) */
    write_bits_to_buffer(b, off, (uint64_t)(mt26_msg->iodi & 0x03), 2); off += 2;
    write_bits_to_buffer(b, off, (uint64_t)(mt26_msg->spare & 0x7F), 7); off += 7;

    /* CRC over all bits up to now */
    uint32_t crc = calculate_sbas_crc24(b, off);
    write_bits_to_buffer(b, off, crc, 24); off += 24;

    /* Convert to hex */
    int total_bytes = (off + 7) / 8;
    hex_output[0] = '\0';
    for (int i = 0; i < total_bytes; ++i) {
        char byte_hex[3];
        sprintf(byte_hex, "%02X", b[i]);
        strcat(hex_output, byte_hex);
    }
    return 0;
}

/* ============================================================================
 * MT24 ENCODER IMPLEMENTATION (Mixed Fast & Long-Term Corrections)
 * Mirrors decode_mt24_into() bit layout
 * ============================================================================ */
int encode_mt24_from(const SBAS_MT24_Message *mt24_msg, char *hex_output) {
    if (!mt24_msg || !hex_output) return -1;

    unsigned char b[32];
    memset(b, 0, sizeof(b));
    int off = 0;

    /* Header */
    write_bits_to_buffer(b, off, mt24_msg->header.preamble, 8); off += 8;
    write_bits_to_buffer(b, off, mt24_msg->header.message_type, 6); off += 6;

    off = 14; /* payload */

    /* Fast Corrections: 6 x 12-bit signed */
    for (int i = 0; i < 6; ++i) {
        uint16_t raw = (uint16_t)(mt24_msg->fast_corrections[i] & 0x0FFF);
        write_bits_to_buffer(b, off, raw, 12); off += 12;
    }

    /* UDREIs: 6 x 4-bit */
    for (int i = 0; i < 6; ++i) {
        write_bits_to_buffer(b, off, (uint64_t)(mt24_msg->udreis[i] & 0x0F), 4); off += 4;
    }

    /* Control: iodp(2), block_id(2), iodf(2), spare(4) */
    write_bits_to_buffer(b, off, (uint64_t)(mt24_msg->iodp & 0x03), 2); off += 2;
    write_bits_to_buffer(b, off, (uint64_t)(mt24_msg->block_id & 0x03), 2); off += 2;
    write_bits_to_buffer(b, off, (uint64_t)(mt24_msg->iodf & 0x03), 2); off += 2;
    write_bits_to_buffer(b, off, (uint64_t)(mt24_msg->spare & 0x0F), 4); off += 4;

    /* Long-term corrections: 106 bits from 14-byte array MSB-first */
    for (int i = 0; i < 106; ++i) {
        int byte = i >> 3;
        int bit_in_byte = 7 - (i & 7);
        uint8_t val = (mt24_msg->long_term_corrections[byte] >> bit_in_byte) & 0x01;
        write_bits_to_buffer(b, off + i, (uint64_t)val, 1);
    }
    off += 106;

    /* CRC */
    uint32_t crc = calculate_sbas_crc24(b, off);
    write_bits_to_buffer(b, off, crc, 24); off += 24;

    int total_bytes = (off + 7) / 8;
    hex_output[0] = '\0';
    for (int i = 0; i < total_bytes; ++i) { char byte_hex[3]; sprintf(byte_hex, "%02X", b[i]); strcat(hex_output, byte_hex);}    
    return 0;
}

/* ============================================================================
 * MT6 ENCODER IMPLEMENTATION (Integrity Information / UDREIs)
 * Mirrors decode_mt6_into() bit layout
 * ============================================================================ */
int encode_mt6_from(const SBAS_MT6_Message *mt6_msg, char *hex_output) {
    if (!mt6_msg || !hex_output) return -1;

    unsigned char b[32]; memset(b, 0, sizeof(b)); int off = 0;
    write_bits_to_buffer(b, off, mt6_msg->header.preamble, 8); off += 8;
    write_bits_to_buffer(b, off, mt6_msg->header.message_type, 6); off += 6;
    off = 14;
    write_bits_to_buffer(b, off, (uint64_t)(mt6_msg->iodf2 & 0x03), 2); off += 2;
    write_bits_to_buffer(b, off, (uint64_t)(mt6_msg->iodf3 & 0x03), 2); off += 2;
    write_bits_to_buffer(b, off, (uint64_t)(mt6_msg->iodf4 & 0x03), 2); off += 2;
    write_bits_to_buffer(b, off, (uint64_t)(mt6_msg->iodf5 & 0x03), 2); off += 2;
    for (int i = 0; i < 51; ++i) { write_bits_to_buffer(b, off, (uint64_t)(mt6_msg->udrei[i] & 0x0F), 4); off += 4; }
    uint32_t crc = calculate_sbas_crc24(b, off); write_bits_to_buffer(b, off, crc, 24); off += 24;
    int total_bytes = (off + 7) / 8; hex_output[0] = '\0';
    for (int i = 0; i < total_bytes; ++i) { char byte_hex[3]; sprintf(byte_hex, "%02X", b[i]); strcat(hex_output, byte_hex);}    
    return 0;
}

/* ============================================================================
 * MT25 ENCODER IMPLEMENTATION (Clock-Ephemeris long-term deltas)
 * Supports both halves (velocity_code 0 and 1) as per structs.
 * ============================================================================ */
static void write_mt25_half_v0(unsigned char *b, int *off, const SBAS_MT25_Half_Vel0 *v0) {
    write_bits_to_buffer(b, *off, (uint64_t)(v0->velocity_code & 0x1), 1); *off += 1;
    write_bits_to_buffer(b, *off, (uint64_t)(v0->prn_mask_1 & 0x3F), 6); *off += 6;
    write_bits_to_buffer(b, *off, (uint64_t)(v0->iod_1 & 0xFF), 8); *off += 8;
    write_bits_to_buffer(b, *off, (uint64_t)((int64_t)llround(v0->dx_1 / 0.125)) & 0x1FF, 9); *off += 9;
    write_bits_to_buffer(b, *off, (uint64_t)((int64_t)llround(v0->dy_1 / 0.125)) & 0x1FF, 9); *off += 9;
    write_bits_to_buffer(b, *off, (uint64_t)((int64_t)llround(v0->dz_1 / 0.125)) & 0x1FF, 9); *off += 9;
    write_bits_to_buffer(b, *off, (uint64_t)((int64_t)llround(v0->da_f0_1 * pow(2.0, 31))) & 0x3FF, 10); *off += 10;
    write_bits_to_buffer(b, *off, (uint64_t)(v0->prn_mask_2 & 0x3F), 6); *off += 6;
    write_bits_to_buffer(b, *off, (uint64_t)(v0->iod_2 & 0xFF), 8); *off += 8;
    write_bits_to_buffer(b, *off, (uint64_t)((int64_t)llround(v0->dx_2 / 0.125)) & 0x1FF, 9); *off += 9;
    write_bits_to_buffer(b, *off, (uint64_t)((int64_t)llround(v0->dy_2 / 0.125)) & 0x1FF, 9); *off += 9;
    write_bits_to_buffer(b, *off, (uint64_t)((int64_t)llround(v0->dz_2 / 0.125)) & 0x1FF, 9); *off += 9;
    write_bits_to_buffer(b, *off, (uint64_t)((int64_t)llround(v0->da_f0_2 * pow(2.0, 31))) & 0x3FF, 10); *off += 10;
    write_bits_to_buffer(b, *off, (uint64_t)(v0->iodp & 0x3), 2); *off += 2;
    write_bits_to_buffer(b, *off, (uint64_t)(v0->spare & 0x1), 1); *off += 1;
}

static void write_mt25_half_v1(unsigned char *b, int *off, const SBAS_MT25_Half_Vel1 *v1) {
    write_bits_to_buffer(b, *off, (uint64_t)(v1->velocity_code & 0x1), 1); *off += 1;
    write_bits_to_buffer(b, *off, (uint64_t)(v1->prn_mask & 0x3F), 6); *off += 6;
    write_bits_to_buffer(b, *off, (uint64_t)(v1->iod & 0xFF), 8); *off += 8;
    write_bits_to_buffer(b, *off, (uint64_t)((int64_t)llround(v1->dx / 0.125)) & 0x7FF, 11); *off += 11;
    write_bits_to_buffer(b, *off, (uint64_t)((int64_t)llround(v1->dy / 0.125)) & 0x7FF, 11); *off += 11;
    write_bits_to_buffer(b, *off, (uint64_t)((int64_t)llround(v1->dz / 0.125)) & 0x7FF, 11); *off += 11;
    write_bits_to_buffer(b, *off, (uint64_t)((int64_t)llround(v1->da_f0 * pow(2.0, 31))) & 0x7FF, 11); *off += 11;
    write_bits_to_buffer(b, *off, (uint64_t)((int64_t)llround(v1->dx_rate * pow(2.0, -11))) & 0xFF, 8); *off += 8;
    write_bits_to_buffer(b, *off, (uint64_t)((int64_t)llround(v1->dy_rate * pow(2.0, -11))) & 0xFF, 8); *off += 8;
    write_bits_to_buffer(b, *off, (uint64_t)((int64_t)llround(v1->dz_rate * pow(2.0, -11))) & 0xFF, 8); *off += 8;
    write_bits_to_buffer(b, *off, (uint64_t)((int64_t)llround(v1->da_f1 * pow(2.0, 39))) & 0xFF, 8); *off += 8;
    write_bits_to_buffer(b, *off, (uint64_t)((uint64_t)llround(v1->t0 / 16.0)) & 0x1FFF, 13); *off += 13;
    write_bits_to_buffer(b, *off, (uint64_t)(v1->iodp & 0x3), 2); *off += 2;
}

int encode_mt25_from(const SBAS_MT25_Message *mt25_msg, char *hex_output) {
    if (!mt25_msg || !hex_output) return -1;
    unsigned char b[32]; memset(b, 0, sizeof(b)); int off = 0;
    write_bits_to_buffer(b, off, mt25_msg->header.preamble, 8); off += 8;
    write_bits_to_buffer(b, off, mt25_msg->header.message_type, 6); off += 6;
    off = 14;
    /* First half */
    if (mt25_msg->half1.raw_velocity_code == 0) write_mt25_half_v0(b, &off, &mt25_msg->half1.u.v0);
    else write_mt25_half_v1(b, &off, &mt25_msg->half1.u.v1);
    /* Second half */
    if (mt25_msg->half2.raw_velocity_code == 0) write_mt25_half_v0(b, &off, &mt25_msg->half2.u.v0);
    else write_mt25_half_v1(b, &off, &mt25_msg->half2.u.v1);
    uint32_t crc = calculate_sbas_crc24(b, off); write_bits_to_buffer(b, off, crc, 24); off += 24;
    int total_bytes = (off + 7) / 8; hex_output[0] = '\0';
    for (int i = 0; i < total_bytes; ++i) { char byte_hex[3]; sprintf(byte_hex, "%02X", b[i]); strcat(hex_output, byte_hex);}    
    return 0;
}

/* ============================================================================
 * MT28 ENCODER IMPLEMENTATION (Clock-ephemeris covariance matrix)
 * Mirrors decode_mt28_into() bit layout
 * ============================================================================ */
int encode_mt28_from(const SBAS_MT28_Message *mt28_msg, char *hex_output) {
    if (!mt28_msg || !hex_output) return -1;
    unsigned char b[32]; memset(b, 0, sizeof(b)); int off = 0;
    write_bits_to_buffer(b, off, mt28_msg->header.preamble, 8); off += 8;
    write_bits_to_buffer(b, off, mt28_msg->header.message_type, 6); off += 6;
    off = 14;
    write_bits_to_buffer(b, off, (uint64_t)(mt28_msg->iodp & 0x3), 2); off += 2;
    for (int i = 0; i < SBAS_MT28_SAT_BLOCKS; ++i) {
        const SBAS_MT28_SatBlock *s = &mt28_msg->sat[i];
        write_bits_to_buffer(b, off, (uint64_t)(s->prn_mask & 0x3F), 6); off += 6;
        write_bits_to_buffer(b, off, (uint64_t)(s->scale_exponent & 0x7), 3); off += 3;
        write_bits_to_buffer(b, off, (uint64_t)(s->E1_1 & 0x1FF), 9); off += 9;
        write_bits_to_buffer(b, off, (uint64_t)(s->E2_2 & 0x1FF), 9); off += 9;
        write_bits_to_buffer(b, off, (uint64_t)(s->E3_3 & 0x1FF), 9); off += 9;
        write_bits_to_buffer(b, off, (uint64_t)(s->E4_4 & 0x1FF), 9); off += 9;
        write_bits_to_buffer(b, off, (uint64_t)(s->E1_2 & 0x3FF), 10); off += 10;
        write_bits_to_buffer(b, off, (uint64_t)(s->E1_3 & 0x3FF), 10); off += 10;
        write_bits_to_buffer(b, off, (uint64_t)(s->E1_4 & 0x3FF), 10); off += 10;
        write_bits_to_buffer(b, off, (uint64_t)(s->E2_3 & 0x3FF), 10); off += 10;
        write_bits_to_buffer(b, off, (uint64_t)(s->E2_4 & 0x3FF), 10); off += 10;
        write_bits_to_buffer(b, off, (uint64_t)(s->E3_4 & 0x3FF), 10); off += 10;
    }
    uint32_t crc = calculate_sbas_crc24(b, off); write_bits_to_buffer(b, off, crc, 24); off += 24;
    int total_bytes = (off + 7) / 8; hex_output[0] = '\0';
    for (int i = 0; i < total_bytes; ++i) { char byte_hex[3]; sprintf(byte_hex, "%02X", b[i]); strcat(hex_output, byte_hex);}    
    return 0;
}