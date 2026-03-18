#include "rinex_writer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

/* Global state */
static FILE *g_rinex_file = NULL;
static int g_packet_count = 0;
static time_t g_base_timestamp = 0;  /* First message timestamp from input file */
static int g_use_incremental_timestamps = 0;  /* Whether to use incremental timestamps */

/* Helper: Format timestamp from Unix time to RINEX format */
static void format_rinex_timestamp(time_t timestamp, char *output, size_t output_size)
{
    struct tm *tm_info = gmtime(&timestamp);
    if (tm_info) {
        snprintf(output, output_size, "%02d %02d %02d %02d %02d %02d.0",
                 tm_info->tm_year % 100,  /* YY */
                 tm_info->tm_mon + 1,     /* MM */
                 tm_info->tm_mday,        /* DD */
                 tm_info->tm_hour,        /* HH */
                 tm_info->tm_min,         /* MM */
                 tm_info->tm_sec);        /* SS */
    } else {
        snprintf(output, output_size, "00 00 00 00 00 00.0");
    }
}

/* Helper: Write RINEX header */
static int write_rinex_header(FILE *fp)
{
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    char date_str[32] = "";
    
    if (tm_info) {
        snprintf(date_str, sizeof(date_str), "%02d-%s-%02d %02d:%02d",
                 tm_info->tm_mday,
                 (const char *[]){
                     "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                     "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
                 }[tm_info->tm_mon],
                 tm_info->tm_year % 100,
                 tm_info->tm_hour,
                 tm_info->tm_min);
    } else {
        snprintf(date_str, sizeof(date_str), "01-JAN-25 00:00");
    }
    
    /* Write RINEX 2.10 header */
    fprintf(fp, "     2.10           B: SBAS BROADCAST                       RINEX VERSION / TYPE\n");
    fprintf(fp, "hercules_transmitter                    %-15s     PGM / RUN BY / DATE\n", date_str);
    fprintf(fp, "                                                            END OF HEADER\n");
    
    return 0;
}

/* Helper: Format hex data with RINEX line wrapping
 * Input file format (to match):
 *   First line: 2 spaces + MT (2-char right-aligned) + 4 spaces + 18 hex bytes
 *   Continuation: 7 spaces + 14 hex bytes
 * Total: 32 bytes (18 + 14)
 */
static void write_hex_data_lines(FILE *fp, const char *hex_data)
{
    if (!hex_data || !fp) return;
    
    /* Remove spaces and convert to uppercase */
    char clean[256];
    size_t w = 0;
    for (size_t i = 0; hex_data[i] && w + 2 < sizeof(clean); i++) {
        if (!isspace((unsigned char)hex_data[i])) {
            clean[w++] = (char)toupper((unsigned char)hex_data[i]);
        }
    }
    clean[w] = '\0';
    
    if (w < 4) return;

    /* Compute message type from the second hex byte (chars 2..3).
       Convert the two hex chars to a numeric value, then extract bits 2..7. */
    unsigned int second_val = 0;
    char second_byte_buf[3] = { clean[2], clean[3], '\0' };
    if (sscanf(second_byte_buf, "%x", &second_val) != 1) second_val = 0;

    int msg_type = (int)((second_val >> 2) & 0x3F);
    
    /* First line: "  MT    " (2 spaces + MT right-aligned in 2 chars + 4 spaces) + 18 hex bytes */
    fprintf(fp, "%3d   ", msg_type);  /* 2 leading spaces implied by %3d, then 3 spaces */
    
    size_t pos = 0;
    /* First line: 18 hex bytes (36 hex chars) */
    for (int i = 0; i < 18 && pos + 1 < w; i++) {
        fprintf(fp, " %c%c", clean[pos], clean[pos + 1]);
        pos += 2;
    }
    fprintf(fp, "\n");
    
    /* Continuation line: 7 spaces + 14 hex bytes (28 hex chars) */
    if (pos + 1 < w) {
        fprintf(fp, "      ");  /* 6 spaces, first byte adds 1 more via " %c%c" = 7 total indent */
        for (int i = 0; i < 14 && pos + 1 < w; i++) {
            fprintf(fp, " %c%c", clean[pos], clean[pos + 1]);
            pos += 2;
        }
        fprintf(fp, "\n");
    }
}

/* Public API Implementation */

int rinex_writer_init(const char *output_path)
{
    if (!output_path) {
        fprintf(stderr, "[RINEX Writer] Error: NULL output path\n");
        return -1;
    }
    
    if (g_rinex_file) {
        fprintf(stderr, "[RINEX Writer] Warning: Writer already initialized, closing previous file\n");
        rinex_writer_close();
    }
    
    g_rinex_file = fopen(output_path, "w");
    if (!g_rinex_file) {
        fprintf(stderr, "[RINEX Writer] Error: Failed to open output file: %s\n", output_path);
        return -1;
    }
    
    g_packet_count = 0;
    
    /* Write RINEX header */
    if (write_rinex_header(g_rinex_file) != 0) {
        fprintf(stderr, "[RINEX Writer] Error: Failed to write RINEX header\n");
        fclose(g_rinex_file);
        g_rinex_file = NULL;
        return -1;
    }
    
    printf("[RINEX Writer] Initialized output file: %s\n", output_path);
    return 0;
}

int rinex_writer_init_from_template(const char *template_path, const char *output_path)
{
    if (!template_path || !output_path) return -1;

    FILE *t = fopen(template_path, "r");
    if (!t) {
        fprintf(stderr, "[RINEX Writer] Error: Could not open template: %s\n", template_path);
        return -1;
    }

    FILE *out = fopen(output_path, "w");
    if (!out) {
        fclose(t);
        fprintf(stderr, "[RINEX Writer] Error: Failed to open output file: %s\n", output_path);
        return -1;
    }

    char line[1024];
    while (fgets(line, sizeof(line), t)) {
        /* Copy all header lines as-is, including PGM / RUN BY / DATE */
        fputs(line, out);

        if (strstr(line, "END OF HEADER") != NULL) break;
    }

    fclose(t);

    g_rinex_file = out;
    g_packet_count = 0;
    g_base_timestamp = 0;
    g_use_incremental_timestamps = 0;
    printf("[RINEX Writer] Initialized output (from template): %s\n", output_path);
    return 0;
}

int rinex_writer_add_packet(int sat_id, int msg_type, const char *hex_data, time_t timestamp)
{
    if (!g_rinex_file) {
        fprintf(stderr, "[RINEX Writer] Error: Writer not initialized\n");
        return -1;
    }
    
    if (!hex_data) {
        fprintf(stderr, "[RINEX Writer] Error: NULL hex data\n");
        return -1;
    }
    
    /* Validate satellite ID */
    if (sat_id < 128 || sat_id > 144) {
        fprintf(stderr, "[RINEX Writer] Warning: Invalid satellite ID %d (expected 128-144)\n", sat_id);
    }
    
    /* Store first message timestamp as base */
    if (g_packet_count == 0) {
        g_base_timestamp = (timestamp != 0) ? timestamp : time(NULL);
        g_use_incremental_timestamps = 1;
        printf("[RINEX Writer] Base timestamp set: %ld\n", (long)g_base_timestamp);
    }
    
    /* Calculate timestamp: first message uses input timestamp, rest increment by 1 second each */
    time_t actual_timestamp;
    if (g_use_incremental_timestamps && g_base_timestamp != 0) {
        actual_timestamp = g_base_timestamp + g_packet_count;
    } else {
        actual_timestamp = (timestamp != 0) ? timestamp : time(NULL);
    }
    
    /* Format timestamp */
    char timestamp_str[64];
    format_rinex_timestamp(actual_timestamp, timestamp_str, sizeof(timestamp_str));
    
    /* Write packet header line */
    fprintf(g_rinex_file, "%3d %s  L1    32     0   SBA\n",
            sat_id, timestamp_str);
    
    /* Write hex data lines */
    write_hex_data_lines(g_rinex_file, hex_data);
    
    g_packet_count++;
    
    /* Flush to ensure data is written */
    fflush(g_rinex_file);
    
    return 0;
}

int rinex_writer_close(void)
{
    if (!g_rinex_file) {
        return 0;  /* Already closed */
    }
    
    printf("[RINEX Writer] Closing file. Total packets written: %d\n", g_packet_count);
    
    fclose(g_rinex_file);
    g_rinex_file = NULL;
    g_packet_count = 0;
    g_base_timestamp = 0;
    g_use_incremental_timestamps = 0;
    
    return 0;
}

int rinex_writer_is_active(void)
{
    return (g_rinex_file != NULL) ? 1 : 0;
}
