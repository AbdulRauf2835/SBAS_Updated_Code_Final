#define _CRT_SECURE_NO_WARNINGS
#include "File_Parsing.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "rinex_reader.h"        /* provides: rinex_parse_file, rinex_free_packets, SBAS_Packet */
#include "message_types.h"      /* prototypes for decode_*_into() */
#include "bitops.h"             /* sbas_bits_get_u64 / sbas_bits_get_s64 helpers */

/* ============================================================================
 * TIMESTAMP CONVERSION UTILITIES
 * ============================================================================ */

/* Convert parsed timestamp string to Unix timestamp 
 * Input format: "2023 10 15 12 30 45" (year month day hour minute second)
 * Output: Unix timestamp (seconds since epoch)
 */
time_t sbas_convert_to_unix_timestamp(const char *timestamp_str) {
    if (!timestamp_str || timestamp_str[0] == '\0') {
        return 0;  // Return epoch time for invalid input
    }
    
    struct tm time_struct = {0};
    int year, month, day, hour, minute;
    double second_with_fraction;
    
    // Parse the RINEX timestamp format "YY MM DD HH MM SS.F"
    int parsed = sscanf(timestamp_str, "%d %d %d %d %d %lf", 
                       &year, &month, &day, &hour, &minute, &second_with_fraction);
    
    if (parsed != 6) {
        return 0;  // Return epoch time for parsing errors
    }
    
    // Convert 2-digit year to 4-digit year (assuming 21st century for YY >= 00)
    if (year >= 0 && year <= 99) {
        year += 2000;
    }
    
    // Fill struct tm (note: tm_year is years since 1900, tm_mon is 0-11)
    time_struct.tm_year = year - 1900;
    time_struct.tm_mon = month - 1;
    time_struct.tm_mday = day;
    time_struct.tm_hour = hour;
    time_struct.tm_min = minute;
    time_struct.tm_sec = (int)second_with_fraction;  // Truncate fractional seconds
    time_struct.tm_isdst = -1;  // Let mktime determine DST
    
    // Convert to Unix timestamp
    time_t unix_time = mktime(&time_struct);
    return (unix_time == -1) ? 0 : unix_time;
}

/* ============================================================================
 * HELPER FUNCTIONS FOR MESSAGE PROCESSING
 * ============================================================================ */

/* Check if satellite ID is valid (SBAS GEO satellites: 120-158 are valid PRNs) */
static int is_valid_geo_sat(int satid) {
    /* SBAS satellites use PRNs 120-158 (some common ones: 120-144)
     * Allow full range to support all SBAS systems (WAAS, EGNOS, MSAS, GAGAN, etc.) */
    return (satid >= 120 && satid <= 158);
}

/* Check message type plausible (0-63 range) */
static int is_valid_message_type(int mt) { return (mt >= 0 && mt <= 63); }

/* Convert cleaned hex string (pairs separated by spaces or not) into bytes array.
 * Returns number of bytes written, or -1 on error.
 * Note: output buffer must be large enough (I use max 256 in callers). */
static int hex_to_bytes(const char *hex, unsigned char *out, size_t max_out)
{
    if (!hex || !out) return -1;
    /* build compact hex (remove all non-hex) */
    size_t L = strlen(hex);
    char *compact = (char*)malloc(L + 1);
    if (!compact) return -1;
    size_t ci = 0;
    for (size_t i = 0; i < L; ++i) {
        unsigned char c = (unsigned char)hex[i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
            compact[ci++] = (char)c;
        }
    }
    compact[ci] = '\0';
    int bytes = (int)(ci / 2);
    if ((size_t)bytes > max_out) { free(compact); return -1; }
    for (int i = 0; i < bytes; ++i) {
        unsigned int v = 0;
        char tmp[3] = { compact[2*i], compact[2*i+1], '\0' };
        if (sscanf(tmp, "%2x", &v) != 1) { free(compact); return -1; }
        out[i] = (unsigned char)v;
    }
    free(compact);
    return bytes;
}






/* Extract the CRC24 field from the SBAS frame stored in hex string.
 * The code uses the SBAS payload layout in your existing code: payload start bit = 14,
 * CRC starts at bit (14 + 212) = 226. We convert the hex to bytes and extract the 24-bit
 * value using sbas_bits_get_u64 (already available in bitops).
 *
 * Returns CRC value (lower 24 bits) or UINT32_MAX on failure.
 */
static uint32_t extract_crc24_from_hex(const char *hex)
{
    unsigned char buf[256] = {0};
    int nbytes = hex_to_bytes(hex, buf, sizeof(buf));
    if (nbytes <= 0) return UINT32_MAX;
    /* offset in bits from start of bytes */
    const int crc_bit_offset = 14 + 212; /* 226 */
    uint32_t crc = (uint32_t)sbas_bits_get_u64(buf, crc_bit_offset, 24);
    return crc & 0xFFFFFFu;
}

/* Compute CRC24 (bitwise) over bits 0..225 (inclusive) from the hex string using
 * CRC-24Q polynomial 0x1864CFB and initial register 0x000000.
 *
 * The algorithm is bitwise: read bits MSB-first from byte 0.
 * Returns computed 24-bit CRC (lower 24 bits), or UINT32_MAX on error.
 *
 * NOTE: This implementation is a canonical bitwise CRC. It matches common CRC-24Q
 * implementations (poly 0x1864CFB, init 0x000000, no reflection).
 */
static uint32_t compute_crc24_from_hex(const char *hex)
{
    unsigned char buf[256] = {0};
    int nbytes = hex_to_bytes(hex, buf, sizeof(buf));
    if (nbytes <= 0) return UINT32_MAX;

    const int total_bits_to_process = 226; /* bits 0..225 (exclude CRC tail) */
    const uint32_t POLY = 0x1864CFBu; /* CRC-24Q polynomial */

    uint32_t reg = 0u; /* 24-bit register */

    for (int bit_idx = 0; bit_idx < total_bits_to_process; ++bit_idx) {
        int byte_idx = bit_idx / 8;
        int bit_in_byte = 7 - (bit_idx % 8); /* MSB-first */
        uint32_t bit = 0u;
        if (byte_idx >= 0 && byte_idx < nbytes) bit = (uint32_t)((buf[byte_idx] >> bit_in_byte) & 1u);

        /* Feedback is MSB ^ input bit */
        uint32_t feedback = ((reg >> 23) & 1u) ^ bit;
        /* Shift and conditionally XOR polynomial */
        reg = (reg << 1) & 0xFFFFFFu;
        if (feedback) {
            reg ^= (POLY & 0xFFFFFFu);
        }
    }
    return reg & 0xFFFFFFu;
}

/* Check packet CRC: compute CRC24 and compare to CRC in packet hex. Return 1 if match, 0 if mismatch, -1 on error. */
static int packet_crc_check(const char *hex)
{
    if (!hex) return -1;
    uint32_t computed = compute_crc24_from_hex(hex);
    if (computed == UINT32_MAX) return -1;
    uint32_t extracted = extract_crc24_from_hex(hex);
    if (extracted == UINT32_MAX) return -1;
    return (computed == extracted) ? 1 : 0;
}







/* Build SBAS_MessageHeader from packet->hexData and packet->messageType and timestamp:
 * - preamble: first byte of the frame (hex)
 * - message_type: pkt->messageType (lower 6 bits used)
 * - timestamp: converted from SBAS_Packet.timestamp string to Unix timestamp
 */
static void fill_common_header_from_packet(const SBAS_Packet *pkt, SBAS_MessageHeader *hdr)
{
    memset(hdr, 0, sizeof(*hdr));
    hdr->message_type = (uint8_t)(pkt->messageType & 0x3F);
    hdr->satellite_id = pkt->satelliteId;  // Include satellite ID in header
    
    // // Convert timestamp string to Unix timestamp
    // if (pkt->timestamp[0] != '\0') {
    //     hdr->timestamp = sbas_convert_to_unix_timestamp(pkt->timestamp);
    // } else {
    //     hdr->timestamp = 0;  // Default to epoch for empty timestamps
    // }
    
    /* extract preamble as first hex byte (if present) */
    if (pkt->hexData && pkt->hexData[0]) {
        // Extract just the first hex pair for preamble (e.g., "53" from "53 0A C0...")
        char first_hex_pair[8] = {0};  // Buffer for first hex byte + space + next char
        
        // Copy first few characters to extract first hex pair
        int src_idx = 0, dst_idx = 0;
        while (dst_idx < 6 && pkt->hexData[src_idx]) {
            char c = pkt->hexData[src_idx];
            if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
                first_hex_pair[dst_idx++] = c;
                if (dst_idx == 2) break;  // We have the first hex byte
            }
            src_idx++;
        }
        first_hex_pair[dst_idx] = '\0';
        
        if (dst_idx == 2) {  // We have a complete hex pair
            unsigned char bytes[1] = {0};
            int nb = hex_to_bytes(first_hex_pair, bytes, sizeof(bytes));
            if (nb >= 1) {
                hdr->preamble = (uint8_t)bytes[0];
            }
        }
    }
}

/* ============================================================================
 * 2D ARRAY MANAGEMENT AND CORE PROCESSING
 * ============================================================================ */

/* Append one timestamp+message pair to the 2D array with dynamic growth */
static int append_typed_message(SBAS_MessageArray *arr, const SBAS_TypedMessage *msg, time_t timestamp)
{
    if (!arr || !msg) return -1;
    
    // Use provided timestamp directly (from RINEX packet parsing)
    // time_t timestamp = extract_timestamp_from_message(msg);  // Old approach - commented out
    
    // Check if we need to grow the array using proper per-array capacity tracking
    if (arr->count >= arr->capacity) {
        // Growth strategy: 1.5x exponential growth for memory efficiency
        size_t new_capacity = arr->capacity ? (arr->capacity * 3) / 2 : 512;  // Start with 512, grow by 1.5x
        
        SBAS_TimestampMessagePair *tmp = (SBAS_TimestampMessagePair*)realloc(arr->pairs, new_capacity * sizeof(SBAS_TimestampMessagePair));
        if (!tmp) return -1;
        
        arr->pairs = tmp;
        arr->capacity = new_capacity;  // Update the array's own capacity!
    }
    
    // Add the timestamp+message pair to 2D array
    arr->pairs[arr->count].timestamp = timestamp;    // First dimension: Unix timestamp
    arr->pairs[arr->count].message = *msg;          // Second dimension: Message data
    arr->count++;
    return 0;
}

/* Dispatch helper: call the right decode_into for the messageType and place in SBAS_TypedMessage.data
 * Returns 0 on success (including decoding success), negative on error or unsupported.
 */
static int decode_packet_into_typed(const SBAS_Packet *pkt, SBAS_TypedMessage *out_msg)
{
    if (!pkt || !out_msg) return -1;
    memset(out_msg, 0, sizeof(*out_msg));
    out_msg->message_type = pkt->messageType;
    out_msg->source_line = pkt->lineNumber;
    out_msg->satellite_id = pkt->satelliteId;

    /* fill the common header for caller; decode_*_into() in the project usually only populates fields
     * inside the message struct and expects caller to set header fields after - we populate header below.
     */
    SBAS_MessageHeader header;
    fill_common_header_from_packet(pkt, &header);

    switch (pkt->messageType) {
        case 0: {
            SBAS_MT0_Message tmp; memset(&tmp, 0, sizeof(tmp));
            int rc = decode_mt0_into(pkt->hexData, &tmp);
            if (rc != 0) return -3;
            tmp.header = header;
            out_msg->data.mt0 = tmp;
            return 0;
        }

        case 1: {
            SBAS_MT1_Message tmp; memset(&tmp, 0, sizeof(tmp));
            int rc = decode_mt1_into(pkt->hexData, &tmp);
            if (rc != 0) return -3;
            tmp.header = header;
            out_msg->data.mt1 = tmp;
            return 0;
        }
        case 2: case 3: case 4: case 5: {
            SBAS_MT2_5_Message tmp; memset(&tmp, 0, sizeof(tmp));
            int rc = decode_mt2_into(pkt->hexData, &tmp);
            if (rc != 0) return -3;
            tmp.header = header;
            out_msg->data.mt2_5 = tmp;
            return 0;
        }
        case 6: {
            SBAS_MT6_Message tmp; memset(&tmp, 0, sizeof(tmp));
            int rc = decode_mt6_into(pkt->hexData, &tmp);
            if (rc != 0) return -3;
            tmp.header = header;
            out_msg->data.mt6 = tmp;
            return 0;
        }
        case 7: {
            SBAS_MT7_Message tmp; memset(&tmp, 0, sizeof(tmp));
            int rc = decode_mt7_into(pkt->hexData, &tmp);
            if (rc != 0) return -3;
            tmp.header = header;
            out_msg->data.mt7 = tmp;
            return 0;
        }
        case 9: {
            SBAS_MT9_Message tmp; memset(&tmp, 0, sizeof(tmp));
            int rc = decode_mt9_into(pkt->hexData, &tmp);
            if (rc != 0) return -3;
            tmp.header = header;
            out_msg->data.mt9 = tmp;
            return 0;
        }
        case 10: {
            SBAS_MT10_Message tmp; memset(&tmp, 0, sizeof(tmp));
            int rc = decode_mt10_into(pkt->hexData, &tmp);
            if (rc != 0) return -3;
            tmp.header = header;
            out_msg->data.mt10 = tmp;
            return 0;
        }
        case 12: {
            SBAS_MT12_Message tmp; memset(&tmp, 0, sizeof(tmp));
            int rc = decode_mt12_into(pkt->hexData, &tmp);
            if (rc != 0) return -3;
            tmp.header = header;
            out_msg->data.mt12 = tmp;
            return 0;
        }
        case 17: {
            SBAS_MT17_Message tmp; memset(&tmp, 0, sizeof(tmp));
            int rc = decode_mt17_into(pkt->hexData, &tmp);
            if (rc != 0) return -3;
            tmp.header = header;
            out_msg->data.mt17 = tmp;
            return 0;
        }
        case 18: {
            SBAS_MT18_Message tmp; memset(&tmp, 0, sizeof(tmp));
            int rc = decode_mt18_into(pkt->hexData, &tmp);
            if (rc != 0) return -3;
            tmp.header = header;
            out_msg->data.mt18 = tmp;
            return 0;
        }
        case 24: {
            SBAS_MT24_Message tmp; memset(&tmp, 0, sizeof(tmp));
            int rc = decode_mt24_into(pkt->hexData, &tmp);
            if (rc != 0) return -3;
            tmp.header = header;
            out_msg->data.mt24 = tmp;
            return 0;
        }
        case 25: {
            SBAS_MT25_Message tmp; memset(&tmp, 0, sizeof(tmp));
            int rc = decode_mt25_into(pkt->hexData, &tmp);
            if (rc != 0) return -3;
            tmp.header = header;
            out_msg->data.mt25 = tmp;
            return 0;
        }
        case 26: {
            SBAS_MT26_Message tmp; memset(&tmp, 0, sizeof(tmp));
            int rc = decode_mt26_into(pkt->hexData, &tmp);
            if (rc != 0) return -3;
            tmp.header = header;
            out_msg->data.mt26 = tmp;
            return 0;
        }
        case 28: {
            SBAS_MT28_Message tmp; memset(&tmp, 0, sizeof(tmp));
            int rc = decode_mt28_into(pkt->hexData, &tmp);
            if (rc != 0) return -3;
            tmp.header = header;
            out_msg->data.mt28 = tmp;
            return 0;
        }
        case 62: {
            SBAS_MT62_Message tmp; memset(&tmp, 0, sizeof(tmp));
            int rc = decode_mt62_into(pkt->hexData, &tmp);
            if (rc != 0) return -3;
            tmp.header = header;
            out_msg->data.mt62 = tmp;
            return 0;
        }
        case 63: {
            SBAS_MT63_Message tmp; memset(&tmp, 0, sizeof(tmp));
            int rc = decode_mt63_into(pkt->hexData, &tmp);
            if (rc != 0) return -3;
            tmp.header = header;
            out_msg->data.mt63 = tmp;
            return 0;
        }
        default:
            return -2;
    }
}

/* Public API */
int sbas_pipeline_process_file(const char *file_buffer, const char *filename, SBAS_MessageArray *out_array)
{
    if (!file_buffer || !out_array) return -1;
    out_array->pairs = NULL;   // Initialize 2D array
    out_array->count = 0;
    out_array->capacity = 0;   // Initialize capacity tracking

    SBAS_Packet *packets = NULL;
    size_t packet_count = 0;
    int rc = rinex_parse_file(file_buffer, filename, &packets, &packet_count);
    if (rc != 0) return -2;

    for (size_t i = 0; i < packet_count; ++i) {
        SBAS_Packet *pkt = &packets[i];
        if (!pkt->hexData || pkt->hexData[0] == '\0') continue;

        /* Step 2: CRC check */
        int crc_ok = packet_crc_check(pkt->hexData);
        if (crc_ok != 1) {
            /* CRC failed (or error): skip packet */
            continue;
        }

        /* Step 3: validate satID and mt */
        if (!is_valid_message_type(pkt->messageType)) continue;
        if (!is_valid_geo_sat(pkt->satelliteId)) continue;

        /* Step 4-5: decode into typed message using decode_*_into() */
        SBAS_TypedMessage typed; memset(&typed, 0, sizeof(typed));
        int dec_rc = decode_packet_into_typed(pkt, &typed);
        if (dec_rc != 0) {
            /* unsupported/failed decoder; skip */
            continue;
        }

        /* Convert RINEX timestamp string to Unix timestamp for 2D array */
        time_t packet_timestamp = sbas_convert_to_unix_timestamp(pkt->timestamp);
        
        /* append to out array with timestamp from RINEX packet */
        if (append_typed_message(out_array, &typed, packet_timestamp) != 0) {
            rinex_free_packets(packets, packet_count);
            return -1;
        }
    }

    rinex_free_packets(packets, packet_count);
    return 0;
}

void sbas_pipeline_free_array(SBAS_MessageArray *arr)
{
    if (!arr) return;
    if (arr->pairs) free(arr->pairs);
    arr->pairs = NULL;
    arr->count = 0;
    arr->capacity = 0;  // Reset capacity tracking
}

