


////JUST FOR OUTPUT IT IS NOT THE MAIN SOURCE FILE////////



#ifndef RINEX_WRITER_H
#define RINEX_WRITER_H

#include <stdio.h>
#include <time.h>

/**
 * @brief Initialize RINEX writer and create output file
 * @param output_path Path to output RINEX file
 * @return 0 on success, -1 on error
 */
int rinex_writer_init(const char *output_path);

/**
 * Initialize RINEX writer copying header from a template RINEX file.
 * The template's header is copied verbatim except the PGM/DATE line is
 * updated to the current date/time. Returns 0 on success.
 */
int rinex_writer_init_from_template(const char *template_path, const char *output_path);

/**
 * @brief Add an SBAS packet to the RINEX output file
 * @param sat_id Satellite ID (128-144)
 * @param msg_type Message type (0-63)
 * @param hex_data Full hex string (64 bytes = 128 hex chars)
 * @param timestamp Unix timestamp of the message
 * @return 0 on success, -1 on error
 */
int rinex_writer_add_packet(int sat_id, int msg_type, const char *hex_data, time_t timestamp);

/**
 * @brief Close RINEX writer and finalize file
 * @return 0 on success, -1 on error
 */
int rinex_writer_close(void);

/**
 * @brief Check if RINEX writer is currently active
 * @return 1 if active, 0 if not
 */
int rinex_writer_is_active(void);

#endif /* RINEX_WRITER_H */
