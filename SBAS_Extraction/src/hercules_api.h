/**
 * @file hercules_api.h
 * @brief Public API for Hercules Transmitter DLL
 * 
 * This header defines the C API exported by hercules_transmitter.dll
 * for integration with GUI applications (PyQt6, WinForms, etc.)
 */

#ifndef HERCULES_API_H
#define HERCULES_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
  #ifdef HERCULES_BUILD_DLL
    #define HERCULES_API __declspec(dllexport)
  #else
    #define HERCULES_API __declspec(dllimport)
  #endif
#else
  #define HERCULES_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * INITIALIZATION & CLEANUP
 * ========================================================================== */

/**
 * @brief Initialize the Hercules transmitter system
 * @param tcp_port Port for TCP server (e.g., 5000)
 * @return 0 on success, non-zero on failure
 */
HERCULES_API int hercules_init(int tcp_port);

/**
 * @brief Clean up and shut down the transmitter
 */
HERCULES_API void hercules_cleanup(void);

/* ============================================================================
 * FILE BINDING & DATA SOURCE
 * ========================================================================== */

/**
 * @brief Bind a RINEX .25B file as the message source
 * @param file_path Absolute path to the .25B file
 * @return 0 on success, -1 on failure
 */
HERCULES_API int hercules_bind_file(const char* file_path);

/**
 * @brief Get count of loaded messages
 * @return Number of messages loaded from bound file
 */
HERCULES_API size_t hercules_get_message_count(void);

/**
 * @brief Get the path of the currently bound file
 * @return Path string (do not free), or NULL if none bound
 */
HERCULES_API const char* hercules_get_bound_file(void);

/* ============================================================================
 * TRANSMISSION CONTROL (Menu Option 1 & 2)
 * ========================================================================== */

/**
 * @brief Start transmission
 * @param satellite_filter Satellite ID to filter (0 or -1 for all)
 * @param fast_correction_satellites Number of satellites for MT2-5 (0 for all)
 * @param duration_seconds Duration in seconds (0 for continuous)
 * @return 0 on success, -1 on failure
 */
HERCULES_API int hercules_start_transmission(int satellite_filter, 
                                               int fast_correction_satellites,
                                               int duration_seconds);

/**
 * @brief Stop ongoing transmission
 */
HERCULES_API void hercules_stop_transmission(void);

/**
 * @brief Process one iteration of transmission (accept clients, send messages)
 * Call this periodically (e.g., every 100ms) when transmission is active
 * @return Number of messages sent, -1 on error
 */
HERCULES_API int hercules_process_transmission(void);

/**
 * @brief Check if transmission is currently active
 * @return 1 if active, 0 if stopped
 */
HERCULES_API int hercules_is_transmitting(void);

/* ============================================================================
 * CLIENT STATUS (Menu Option 3)
 * ========================================================================== */

/**
 * @brief Get number of connected TCP clients
 * @return Client count
 */
HERCULES_API int hercules_get_client_count(void);

/* ============================================================================
 * MESSAGE EDITING (Menu Option 4 & 6)
 * ========================================================================== */

/**
 * @brief Get a message by satellite ID and type
 * @param satellite_id Satellite PRN
 * @param message_type Message type (1-28, 62, 63, etc.)
 * @param out_data Buffer to receive message data (JSON format)
 * @param buffer_size Size of output buffer
 * @return 0 on success, -1 if message not found
 */
HERCULES_API int hercules_get_message(int satellite_id, 
                                        int message_type,
                                        char* out_data,
                                        size_t buffer_size);

/**
 * @brief Update a message with new field values
 * @param satellite_id Satellite PRN
 * @param message_type Message type
 * @param field_name Field name (e.g., "iodp", "fast_correction")
 * @param value_str Value as string (will be parsed based on field type)
 * @return 0 on success, -1 on failure
 */
HERCULES_API int hercules_update_message_field(int satellite_id,
                                                 int message_type,
                                                 const char* field_name,
                                                 const char* value_str);

/**
 * @brief Enable field increment mode for continuous transmission
 * 
 * When enabled, the first transmission will use exact field values.
 * Subsequent transmissions will auto-increment changed fields (like console app).
 * 
 * Call this after updating message fields with hercules_update_message_field().
 * 
 * @param satellite_id Satellite PRN
 * @param message_type Message type
 * @return 0 on success, -1 on failure
 */
HERCULES_API int hercules_enable_field_increment(int satellite_id, int message_type);

/**
 * @brief Encode and send a single message once
 * @param satellite_id Satellite PRN
 * @param message_type Message type
 * @return 0 on success, -1 on failure
 */
HERCULES_API int hercules_send_message_once(int satellite_id, int message_type);

/**
 * @brief Start continuous transmission of a single message
 * @param satellite_id Satellite PRN
 * @param message_type Message type
 * @param interval_ms Interval between transmissions in milliseconds
 * @return 0 on success, -1 on failure
 */
HERCULES_API int hercules_start_single_message_tx(int satellite_id, 
                                                    int message_type,
                                                    int interval_ms);

/**
 * @brief Stop continuous single message transmission
 */
HERCULES_API void hercules_stop_single_message_tx(void);

/* ============================================================================
 * ORBIT MODEL CONFIGURATION (Menu Option 5)
 * ========================================================================== */

/**
 * @brief Set orbit model to TLE
 * @param tle_file_path Path to TLE file
 * @param satellite_mappings JSON array of {"sat_id": 128, "tle_index": 0} mappings
 * @return 0 on success, -1 on failure
 */
HERCULES_API int hercules_set_orbit_model_tle(const char* tle_file_path,
                                                const char* satellite_mappings);

/**
 * @brief Set orbit model to statistical (Sat 128 or 143)
 * @param satellite_id 128 or 143
 * @return 0 on success, -1 on failure
 */
HERCULES_API int hercules_set_orbit_model_statistical(int satellite_id);

/**
 * @brief Disable orbit model
 */
HERCULES_API void hercules_disable_orbit_model(void);

/**
 * @brief Set TLE time mode
 * @param use_simulation 1 for simulation mode, 0 for real-time
 * @param base_time_utc Base time in UTC (YYYY-MM-DD HH:MM:SS) for simulation, NULL for real-time
 * @return 0 on success, -1 on failure
 */
HERCULES_API int hercules_set_tle_time_mode(int use_simulation, const char* base_time_utc);

/* ============================================================================
 * RINEX OUTPUT CONFIGURATION (Menu Option 7)
 * ========================================================================== */

/**
 * @brief Configure RINEX output file
 * @param output_path Path to output .25B file, or NULL to disable
 * @param template_path Path to template file (usually the input file)
 * @return 0 on success, -1 on failure
 */
HERCULES_API int hercules_configure_rinex_output(const char* output_path,
                                                   const char* template_path);

/**
 * @brief Check if RINEX output is active
 * @return 1 if active, 0 if disabled
 */
HERCULES_API int hercules_is_rinex_output_active(void);

/* ============================================================================
 * STATUS & LOGGING
 * ========================================================================== */

/**
 * @brief Get last error message
 * @return Error string (do not free), or NULL if no error
 */
HERCULES_API const char* hercules_get_last_error(void);

/**
 * @brief Get transmission statistics (JSON format)
 * @param out_stats Buffer to receive stats
 * @param buffer_size Size of buffer
 * @return 0 on success, -1 on failure
 */
HERCULES_API int hercules_get_transmission_stats(char* out_stats, size_t buffer_size);

/**
 * @brief Set callback for status updates
 * @param callback Function pointer (void callback(const char* message))
 */
typedef void (*hercules_status_callback_t)(const char* message);
HERCULES_API void hercules_set_status_callback(hercules_status_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif /* HERCULES_API_H */
