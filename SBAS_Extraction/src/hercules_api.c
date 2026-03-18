/**
 * @file hercules_api.c
 * @brief Implementation of Hercules Transmitter DLL API
 * 
 * This file wraps the existing transmitter functionality into a clean
 * C API suitable for GUI integration via ctypes/FFI.
 */

#define HERCULES_BUILD_DLL
#include "hercules_api.h"
#include "tx_server.h"
#include "tx_source.h"
#include "tx_utils.h"
#include "encoder.h"
#include "message_types.h"
#include "TLE_implementation.h"
#include "rinex_writer.h"
#include "stat_model_types.h"
#include "statistical_model/stat_model_128.h"
#include "statistical_model/stat_model_143.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

/* Forward declarations for increment rule system */
typedef struct {
    bool in_use;
    int satellite_id;
    int mt;
    void *live_message_ptr;  // SBAS_TypedMessage*
    bool suppress_once;
    /* MT-specific increment flags would be here but we only need basic structure */
} LiveIncrementRule;

/* Extern variables and functions from the transmitter (not available in DLL build) 
   These increment functions are console-only, so we implement a simplified version */
#define MAX_LIVE_RULES 64
static LiveIncrementRule g_increment_rules[MAX_LIVE_RULES] = {0};

static LiveIncrementRule* ensure_live_rule(int sat_id, int mt) {
    /* Find existing rule */
    for (int i = 0; i < MAX_LIVE_RULES; i++) {
        if (g_increment_rules[i].in_use && 
            g_increment_rules[i].satellite_id == sat_id && 
            g_increment_rules[i].mt == mt) {
            return &g_increment_rules[i];
        }
    }
    
    /* Create new rule */
    for (int i = 0; i < MAX_LIVE_RULES; i++) {
        if (!g_increment_rules[i].in_use) {
            memset(&g_increment_rules[i], 0, sizeof(LiveIncrementRule));
            g_increment_rules[i].in_use = true;
            g_increment_rules[i].satellite_id = sat_id;
            g_increment_rules[i].mt = mt;
            return &g_increment_rules[i];
        }
    }
    
    return NULL;  /* No free slots */
}

/* ============================================================================
 * GLOBAL STATE
 * ========================================================================== */

static char g_last_error[512] = {0};
static int g_initialized = 0;
static int g_transmitting = 0;
static int g_single_msg_transmitting = 0;
static int g_single_msg_satellite_id = -1;
static int g_single_msg_type = -1;
static int g_single_msg_interval_ms = 1000;
static hercules_status_callback_t g_status_callback = NULL;

/* Transmission parameters */
static int g_tx_satellite_filter = -1;
static int g_tx_fast_corr_sats = 0;
static int g_tx_duration = 0;

/* Sequential transmission mode (preserves input file order) */
static int g_sequential_mode = 0;  /* 1 = sequential, 0 = scheduler-based */
static int g_sequential_index = -1;  /* Current index in sequential mode */
static time_t g_sequential_last_sent = 0;  /* Last send time in sequential mode */

/* Orbit model state */
typedef enum {
    ORBIT_NONE,
    ORBIT_TLE,
    ORBIT_STAT128,
    ORBIT_STAT143
} OrbitMode;

static OrbitMode g_orbit_mode = ORBIT_NONE;
static int g_orbit_target_sat = -1;

/* Statistical models (defined here for DLL) */
static StatPredictor128 g_stat_model_128;
static StatPredictor143 g_stat_model_143;

/* TLE state */
#define MAX_TLE_ENTRIES 10
static TLE_Entry g_tle_entries[MAX_TLE_ENTRIES];
static int g_tle_count = 0;

/* Scheduler state */
static time_t g_direct_last_sent[64];
static int g_direct_last_idx[64];
static uint8_t g_current_iodp = 0;
static uint8_t g_current_iodf = 0;
static uint8_t g_current_iodi = 0;

/* Message update intervals (seconds) */
static const int max_update_interval[64] = {
    6, 6, 6, 6, 6, 6,     // MT0-MT5
    6, 120, 0, 120, 120,  // MT6-MT10
    0, 120, 0, 0, 0,      // MT11-MT15
    0, 300, 300, 0, 0,    // MT16-MT20
    0, 0, 0, 6, 120,      // MT21-MT25
    300, 0, 120,          // MT26-MT28
    [29 ... 62] = 3600,   // MT29-MT62
    3600                  // MT63
};
static int g_tle_use_simulation = 0;
static time_t g_tle_base_time = 0;

/* ============================================================================
 * HELPER FUNCTIONS
 * ========================================================================== */

static void set_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error), fmt, args);
    va_end(args);
}

static void send_status(const char* fmt, ...) {
    if (g_status_callback) {
        char buffer[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        g_status_callback(buffer);
    }
}

/* ============================================================================
 * SCHEDULER HELPERS
 * ========================================================================== */

static void scheduler_reset(void) {
    for (int i = 0; i < 64; ++i) {
        g_direct_last_sent[i] = 0;
        g_direct_last_idx[i] = -1;
    }
}

static int direct_type_priority(int mt) {
    switch (mt) {
        case 2: case 3: case 4: case 5: case 6: case 24: return 2; // integrity types higher
        default: return 1;
    }
}

static int is_supported_tx_type(int t) {
    /* Filter MT2-5 based on fast correction satellite count */
    if (g_tx_fast_corr_sats > 0) {
        if (t == 2) return 1; // MT2 always sent if any fast corrections needed
        else if (t == 3) return (g_tx_fast_corr_sats > 13) ? 1 : 0;
        else if (t == 4) return (g_tx_fast_corr_sats > 26) ? 1 : 0;
        else if (t == 5) return (g_tx_fast_corr_sats > 39) ? 1 : 0;
    }
    
    switch (t) {
        case 0: case 1: case 2: case 3: case 4: case 5:
        case 6: case 7: case 9: case 10:
        case 17: case 18: case 24: case 25:
        case 26: case 28: case 63:
            return 1;
        default:
            return 0;
    }
}

static int encode_message_from_tm(const SBAS_TypedMessage *tm, char *out_hex) {
    if (!tm) return -1;
    out_hex[0] = '\0';
    switch (tm->message_type) {
        case 0: return encode_mt0_from(&tm->data.mt0, out_hex);
        case 1: return encode_mt1_from(&tm->data.mt1, out_hex);
        case 2: return encode_mt2_from(&tm->data.mt2_5, out_hex);
        case 3: return encode_mt3_from(&tm->data.mt2_5, out_hex);
        case 4: return encode_mt4_from(&tm->data.mt2_5, out_hex);
        case 5: return encode_mt5_from(&tm->data.mt2_5, out_hex);
        case 6: return encode_mt6_from(&tm->data.mt6, out_hex);
        case 7: return encode_mt7_from(&tm->data.mt7, out_hex);
        case 9: return encode_mt9_from(&tm->data.mt9, out_hex);
        case 10: return encode_mt10_from(&tm->data.mt10, out_hex);
        case 17: return encode_mt17_from(&tm->data.mt17, out_hex);
        case 18: return encode_mt18_from(&tm->data.mt18, out_hex);
        case 24: return encode_mt24_from(&tm->data.mt24, out_hex);
        case 25: return encode_mt25_from(&tm->data.mt25, out_hex);
        case 26: return encode_mt26_from(&tm->data.mt26, out_hex);
        case 28: return encode_mt28_from(&tm->data.mt28, out_hex);
        case 63: return encode_mt63_from(NULL, out_hex);
        default: return -1;
    }
}

static void apply_orbit_model_override(SBAS_TypedMessage *tm) {
    if (!tm) return;
    
    switch (g_orbit_mode) {
        case ORBIT_TLE:
            // TLE orbit model not fully implemented in DLL yet
            break;
            
        case ORBIT_STAT128:
            if (tm->message_type == 9 && (int)tm->satellite_id == g_orbit_target_sat) {
                if (g_stat_model_128.initialized) {
                    const StatModel_MT9_Data* predicted_data = stat_model_128_predict(&g_stat_model_128);
                    if (predicted_data) {
                        copy_model_data_to_msg(predicted_data, &tm->data.mt9);
                    }
                }
            }
            break;
            
        case ORBIT_STAT143:
            if (tm->message_type == 9 && (int)tm->satellite_id == g_orbit_target_sat) {
                if (g_stat_model_143.initialized) {
                    const StatModel_MT9_Data* predicted_data = stat_model_143_predict(&g_stat_model_143);
                    if (predicted_data) {
                        copy_model_data_to_msg(predicted_data, &tm->data.mt9);
                    }
                }
            }
            break;
            
        case ORBIT_NONE:
        default:
            break;
    }
}

static void apply_global_iodp(SBAS_TypedMessage *tm) {
    if (!tm) return;
    switch (tm->message_type) {
        case 1:  tm->data.mt1.iodp = g_current_iodp; break;
        case 2: case 3: case 4: case 5: tm->data.mt2_5.iodp = g_current_iodp; break;
        case 7:  tm->data.mt7.iodp = g_current_iodp; break;
    }
}

static void apply_global_iodf(SBAS_TypedMessage *tm) {
    if (!tm) return;
    switch (tm->message_type) {
        case 2: case 3: case 4: case 5: tm->data.mt2_5.iodf = g_current_iodf; break;
        case 6:
            tm->data.mt6.iodf2 = g_current_iodf;
            tm->data.mt6.iodf3 = g_current_iodf;
            tm->data.mt6.iodf4 = g_current_iodf;
            tm->data.mt6.iodf5 = g_current_iodf;
            break;
    }
}

static void apply_global_iodi(SBAS_TypedMessage *tm) {
    if (!tm) return;
    switch (tm->message_type) {
        case 18: tm->data.mt18.iodi = g_current_iodi; break;
        case 26: tm->data.mt26.iodi = g_current_iodi; break;
    }
}

/* ============================================================================
 * INITIALIZATION & CLEANUP
 * ========================================================================== */

HERCULES_API int hercules_init(int tcp_port) {
    if (g_initialized) {
        set_error("Already initialized");
        return -1;
    }

    /* Initialize statistical models */
    stat_model_128_init(&g_stat_model_128);
    stat_model_143_init(&g_stat_model_143);

    /* Initialize TCP server */
    if (tx_server_init(tcp_port, 10) != 0) {
        set_error("Failed to initialize TCP server on port %d", tcp_port);
        return -1;
    }

    g_initialized = 1;
    scheduler_reset();
    stat_model_128_init(&g_stat_model_128);
    stat_model_143_init(&g_stat_model_143);
    
    send_status("Hercules Transmitter initialized on port %d", tcp_port);
    return 0;
}

HERCULES_API void hercules_cleanup(void) {
    if (!g_initialized) return;

    hercules_stop_transmission();
    hercules_stop_single_message_tx();
    tx_source_unbind();
    tx_server_cleanup();
    
    if (rinex_writer_is_active()) {
        rinex_writer_close();
    }

    g_initialized = 0;
    send_status("Hercules Transmitter shut down");
}

/* ============================================================================
 * FILE BINDING & DATA SOURCE
 * ========================================================================== */

HERCULES_API int hercules_bind_file(const char* file_path) {
    if (!g_initialized) {
        set_error("Not initialized");
        return -1;
    }

    if (!file_path) {
        set_error("NULL file path");
        return -1;
    }

    if (tx_source_bind_file(file_path) != 0) {
        set_error("Failed to bind file: %s", file_path);
        return -1;
    }

    send_status("Bound file: %s (%zu messages)", file_path, tx_source_bound_count());
    
    /* Train statistical models if messages available */
    // TODO: Call train_statistical_models() here if needed
    
    return 0;
}

HERCULES_API size_t hercules_get_message_count(void) {
    return tx_source_bound_count();
}

HERCULES_API const char* hercules_get_bound_file(void) {
    return tx_source_get_bound_path();
}

/* ============================================================================
 * TRANSMISSION CONTROL
 * ========================================================================== */

HERCULES_API int hercules_start_transmission(int satellite_filter, 
                                               int fast_correction_satellites,
                                               int duration_seconds) {
    if (!g_initialized) {
        set_error("Not initialized");
        return -1;
    }

    if (g_transmitting) {
        set_error("Already transmitting");
        return -1;
    }

    if (tx_source_bound_count() == 0) {
        set_error("No messages loaded. Bind a file first.");
        return -1;
    }

    g_tx_satellite_filter = satellite_filter;
    g_tx_fast_corr_sats = fast_correction_satellites;
    g_tx_duration = duration_seconds;
    g_transmitting = 1;
    
    /* Enable sequential mode when All Satellites (-1 or 0) is selected */
    g_sequential_mode = (satellite_filter <= 0) ? 1 : 0;
    g_sequential_index = -1;  /* Start from beginning */
    g_sequential_last_sent = 0;  /* Reset timing */

    send_status("Transmission started (filter: %d, FC sats: %d, duration: %d, mode: %s)",
                satellite_filter, fast_correction_satellites, duration_seconds,
                g_sequential_mode ? "sequential" : "scheduler");
    
    /* TODO: Actual transmission loop should be in a separate thread
     * For now, this just sets the flag. The actual scheduler logic
     * from hercules_transmitter.c should be extracted here. */
    
    return 0;
}

HERCULES_API void hercules_stop_transmission(void) {
    if (g_transmitting) {
        g_transmitting = 0;
        send_status("Transmission stopped");
    }
}

HERCULES_API int hercules_process_transmission(void) {
    if (!g_transmitting) {
        return 0;
    }

    // Accept any new clients
    tx_server_accept_new_clients();

    // If in single message mode, send only that message
    if (g_single_msg_transmitting) {
        static time_t last_single_msg_sent = 0;
        time_t now = time(NULL);
        
        // Check if enough time has elapsed (convert ms to seconds)
        int interval_sec = g_single_msg_interval_ms / 1000;
        if (interval_sec <= 0) interval_sec = 1;
        
        if (now - last_single_msg_sent < interval_sec) {
            return 0;  // Not time yet
        }
        
        SBAS_TypedMessage* tm = NULL;
        int idx = -1;
        if (tx_source_find_first(g_single_msg_type, (unsigned int)g_single_msg_satellite_id, &tm, &idx) != 0 || !tm) {
            // Message not found, disable single message mode
            g_single_msg_transmitting = 0;
            return -1;
        }
        
        // Apply orbit model and IOD overrides
        apply_orbit_model_override(tm);
        apply_global_iodp(tm);
        apply_global_iodf(tm);
        apply_global_iodi(tm);
        
        // Encode and broadcast
        char hex[256] = {0};
        if (encode_message_from_tm(tm, hex) != 0) {
            return -1;
        }
        
        uint8_t bin[32];
        size_t blen = tx_hex_to_bin(hex, bin, sizeof(bin));
        if (blen == 0) return -1;
        
        tx_server_broadcast(bin, blen);
        last_single_msg_sent = now;
        
        // Log to RINEX if active
        if (rinex_writer_is_active()) {
            time_t msg_timestamp = 0;
            if (tx_source_get_timestamp(idx, &msg_timestamp) != 0 || msg_timestamp == 0) {
                msg_timestamp = time(NULL);  // Fallback to current time
            }
            rinex_writer_add_packet(tm->satellite_id, g_single_msg_type, hex, msg_timestamp);
        }
        
        return (int)blen;
    }

    // Run the direct scheduler to send next message
    time_t now = time(NULL);
    int chosen_type = -1;
    int chosen_idx = -1;
    SBAS_TypedMessage *chosen_tm = NULL;
    int best_priority = -1;
    int best_overdue = INT_MIN;
    
    /* Sequential mode: go through messages in input file order */
    if (g_sequential_mode) {
        /* Add 1 second delay between messages */
        if (g_sequential_last_sent > 0 && (now - g_sequential_last_sent) < 1) {
            return 0;  /* Not time yet */
        }
        
        size_t total_count = tx_source_bound_count();
        if (total_count == 0) {
            return 0;
        }
        
        /* Get next message in sequence */
        g_sequential_index++;
        if (g_sequential_index < 0 || (size_t)g_sequential_index >= total_count) {
            g_sequential_index = 0;  /* Wrap around */
        }
        
        /* Get message at current index */
        if (tx_source_get_by_index(g_sequential_index, &chosen_tm) == 0 && chosen_tm) {
            chosen_type = chosen_tm->message_type;
            chosen_idx = g_sequential_index;
            /* Debug: log what message we're sending */
            printf("[Sequential] Index %d: SAT %d, MT %d\n", g_sequential_index, chosen_tm->satellite_id, chosen_type);
        }
        
        g_sequential_last_sent = now;  /* Update last send time */
    } else {
        /* Original scheduler mode: priority-based selection */
    for (int mt = 0; mt < 64; ++mt) {
        if (!is_supported_tx_type(mt)) continue;

        SBAS_TypedMessage *tm = NULL;
        int next_idx = -1;
        if (tx_source_next_for_type(mt, g_direct_last_idx[mt], &tm, &next_idx) != 0 || next_idx < 0 || !tm) 
            continue;
        
        // Apply satellite filter if set
        if (g_tx_satellite_filter > 0) {
            int initial_idx = next_idx;
            while ((int)tm->satellite_id != g_tx_satellite_filter) {
                int current_idx = next_idx;
                if (tx_source_next_for_type(mt, current_idx, &tm, &next_idx) != 0 || next_idx == initial_idx) {
                    tm = NULL;
                    break;
                }
            }
            if (tm == NULL) continue;
        }

        int interval = (mt >= 0 && mt < 64) ? max_update_interval[mt] : 3600;
        if (interval <= 0) interval = 3600;
        
        int overdue;
        if (g_direct_last_sent[mt] == 0) 
            overdue = interval;
        else 
            overdue = (int)(now - g_direct_last_sent[mt]) - interval;

        if (overdue < 0) continue;

        int pri = direct_type_priority(mt);

        if (pri > best_priority || (pri == best_priority && overdue > best_overdue)) {
            best_priority = pri;
            best_overdue = overdue;
            chosen_type = mt;
            chosen_idx = next_idx;
            chosen_tm = tm;
        }
    }
    }  /* End of scheduler mode else block */

    // If no message is due, send MT63 filler
    if (chosen_idx < 0) {
        char hex_output[64];
        if (encode_mt63_from(NULL, hex_output) >= 0) {
            uint8_t filler[32];
            size_t len = tx_hex_to_bin(hex_output, filler, sizeof(filler));
            if (len > 0) {
                tx_server_broadcast(filler, len);
                g_direct_last_sent[63] = now;
                
                if (rinex_writer_is_active()) {
                    int sat_id = (g_tx_satellite_filter > 0) ? g_tx_satellite_filter : 128;
                    rinex_writer_add_packet(sat_id, 63, hex_output, now);
                }
            }
        }
        return 0;
    }

    // Apply orbit model overrides, global IODs, etc.
    apply_orbit_model_override(chosen_tm);
    apply_global_iodp(chosen_tm);
    apply_global_iodf(chosen_tm);
    apply_global_iodi(chosen_tm);

    // Encode and broadcast
    char hex[256] = {0};
    if (encode_message_from_tm(chosen_tm, hex) != 0) {
        return -1;
    }
    
    uint8_t bin[32];
    size_t blen = tx_hex_to_bin(hex, bin, sizeof(bin));
    if (blen == 0) return -1;
    
    tx_server_broadcast(bin, blen);
    g_direct_last_sent[chosen_type] = now;
    g_direct_last_idx[chosen_type] = chosen_idx;
    
    // Log to RINEX if active
    if (rinex_writer_is_active()) {
        time_t msg_timestamp = 0;
        if (tx_source_get_timestamp(chosen_idx, &msg_timestamp) != 0 || msg_timestamp == 0) {
            msg_timestamp = time(NULL);  // Fallback to current time
        }
        rinex_writer_add_packet(chosen_tm->satellite_id, chosen_type, hex, msg_timestamp);
    }
    
    return (int)blen;
}

HERCULES_API int hercules_is_transmitting(void) {
    return g_transmitting;
}

/* ============================================================================
 * CLIENT STATUS
 * ========================================================================== */

HERCULES_API int hercules_get_client_count(void) {
    if (!g_initialized) return 0;
    return tx_server_client_count();
}

/* ============================================================================
 * MESSAGE EDITING (Stub - needs full implementation)
 * ========================================================================== */

HERCULES_API int hercules_get_message(int satellite_id, 
                                        int message_type,
                                        char* out_data,
                                        size_t buffer_size) {
    if (!g_initialized || !out_data || buffer_size == 0) {
        set_error("Invalid parameters");
        return -1;
    }

    SBAS_TypedMessage* tm = NULL;
    int idx = -1;
    
    if (tx_source_find_first(message_type, (unsigned int)satellite_id, &tm, &idx) != 0 || !tm) {
        set_error("Message not found: sat=%d, type=%d", satellite_id, message_type);
        return -1;
    }

    /* Serialize message fields to JSON format based on type */
    int written = 0;
    written = snprintf(out_data, buffer_size, 
                      "{\"satellite_id\":%d,\"message_type\":%d,\"fields\":{", 
                      satellite_id, message_type);
    
    switch (message_type) {
        case 0:
            written += snprintf(out_data + written, buffer_size - written, "\"type\":\"test_message\"}}");
            break;
        case 1:
            written += snprintf(out_data + written, buffer_size - written,
                              "\"iodp\":%d,\"prn_mask\":[", tm->data.mt1.iodp);
            for (int i = 0; i < 27; i++) {
                written += snprintf(out_data + written, buffer_size - written,
                                  "%s%d", i > 0 ? "," : "", tm->data.mt1.prn_mask[i]);
            }
            written += snprintf(out_data + written, buffer_size - written, "]}}");
            break;
        case 2: case 3: case 4: case 5:
            written += snprintf(out_data + written, buffer_size - written,
                              "\"iodf\":%d,\"iodp\":%d,\"sat_corrections\":[", 
                              tm->data.mt2_5.iodf, tm->data.mt2_5.iodp);
            for (int i = 0; i < 13; i++) {
                written += snprintf(out_data + written, buffer_size - written,
                                  "%s{\"fc\":%d,\"udrei\":%d}",
                                  i > 0 ? "," : "",
                                  tm->data.mt2_5.sat_corrections[i].fast_correction,
                                  tm->data.mt2_5.sat_corrections[i].udrei);
            }
            written += snprintf(out_data + written, buffer_size - written, "]}}");
            break;
        case 6:
            written += snprintf(out_data + written, buffer_size - written,
                              "\"iodf2\":%d,\"iodf3\":%d,\"iodf4\":%d,\"iodf5\":%d,\"udrei\":[",
                              tm->data.mt6.iodf2, tm->data.mt6.iodf3,
                              tm->data.mt6.iodf4, tm->data.mt6.iodf5);
            for (int i = 0; i < 51; i++) {
                written += snprintf(out_data + written, buffer_size - written,
                                  "%s%d", i > 0 ? "," : "", tm->data.mt6.udrei[i]);
            }
            written += snprintf(out_data + written, buffer_size - written, "]}}");
            break;
        case 7:
            written += snprintf(out_data + written, buffer_size - written,
                              "\"t_lat\":%d,\"iodp\":%d,\"udre_ai\":[",
                              tm->data.mt7.t_lat, tm->data.mt7.iodp);
            for (int i = 0; i < 51; i++) {
                written += snprintf(out_data + written, buffer_size - written,
                                  "%s%d", i > 0 ? "," : "", tm->data.mt7.udre_ai[i]);
            }
            written += snprintf(out_data + written, buffer_size - written, "]}}");
            break;
        case 9:
            written += snprintf(out_data + written, buffer_size - written,
                              "\"t0\":%.6f,\"ura\":%d,\"xg_ecef\":%.6f,\"yg_ecef\":%.6f,"
                              "\"zg_ecef\":%.6f,\"xg_rate\":%.6f,\"yg_rate\":%.6f,"
                              "\"zg_rate\":%.6f,\"xg_accel\":%.6f,\"yg_accel\":%.6f,"
                              "\"zg_accel\":%.6f,\"ag_f0\":%.6f,\"ag_f1\":%.6f}}",
                              tm->data.mt9.t0, tm->data.mt9.ura,
                              tm->data.mt9.xg_ecef, tm->data.mt9.yg_ecef, tm->data.mt9.zg_ecef,
                              tm->data.mt9.xg_rate, tm->data.mt9.yg_rate, tm->data.mt9.zg_rate,
                              tm->data.mt9.xg_accel, tm->data.mt9.yg_accel, tm->data.mt9.zg_accel,
                              tm->data.mt9.ag_f0, tm->data.mt9.ag_f1);
            break;
        case 10:
            written += snprintf(out_data + written, buffer_size - written,
                              "\"B_rrc\":%.6f,\"C_ltc_lsb\":%.6f,\"C_ltc_v1\":%.6f,"
                              "\"I_ltc_v1\":%.6f,\"C_ltc_v0\":%.6f,\"I_ltc_v0\":%.6f,"
                              "\"C_geo_lsb\":%.6f,\"C_geo_v\":%.6f,\"I_geo\":%.6f,"
                              "\"C_er\":%.6f,\"C_iono_step\":%.6f,\"I_iono\":%.6f,"
                              "\"C_iono_ramp\":%.6f,\"rss_udre\":%d,\"rss_iono\":%d,"
                              "\"C_covariance\":%.6f}}",
                              tm->data.mt10.B_rrc, tm->data.mt10.C_ltc_lsb, tm->data.mt10.C_ltc_v1,
                              tm->data.mt10.I_ltc_v1, tm->data.mt10.C_ltc_v0, tm->data.mt10.I_ltc_v0,
                              tm->data.mt10.C_geo_lsb, tm->data.mt10.C_geo_v, tm->data.mt10.I_geo,
                              tm->data.mt10.C_er, tm->data.mt10.C_iono_step, tm->data.mt10.I_iono,
                              tm->data.mt10.C_iono_ramp, tm->data.mt10.rss_udre, tm->data.mt10.rss_iono,
                              tm->data.mt10.C_covariance);
            break;
        case 12:
            written += snprintf(out_data + written, buffer_size - written,
                              "\"A1SNT\":%.10e,\"A0SNT\":%.10e,\"t0t\":%.6f,\"WNt\":%d,"
                              "\"delta_t_ls\":%.6f,\"WN_LSF\":%d,\"DN\":%d,\"delta_t_lsf\":%.6f,"
                              "\"utc_standard_id\":%d,\"TOW\":%.6f,\"WN\":%d}}",
                              tm->data.mt12.A1SNT, tm->data.mt12.A0SNT, tm->data.mt12.t0t, tm->data.mt12.WNt,
                              tm->data.mt12.delta_t_ls, tm->data.mt12.WN_LSF, tm->data.mt12.DN, 
                              tm->data.mt12.delta_t_lsf, tm->data.mt12.utc_standard_id, 
                              tm->data.mt12.TOW, tm->data.mt12.WN);
            break;
        case 17:
            written += snprintf(out_data + written, buffer_size - written,
                              "\"t0\":%.6f,\"geo\":[", tm->data.mt17.t0);
            for (int i = 0; i < 3; i++) {
                written += snprintf(out_data + written, buffer_size - written,
                                  "%s{\"data_id\":%d,\"prn\":%d,\"health\":%d,\"x_g\":%.6f,"
                                  "\"y_g\":%.6f,\"z_g\":%.6f,\"xg_dot\":%.6f,\"yg_dot\":%.6f,\"zg_dot\":%.6f}",
                                  i > 0 ? "," : "",
                                  tm->data.mt17.geo[i].data_id, tm->data.mt17.geo[i].prn_number,
                                  tm->data.mt17.geo[i].health_and_status, tm->data.mt17.geo[i].x_g,
                                  tm->data.mt17.geo[i].y_g, tm->data.mt17.geo[i].z_g,
                                  tm->data.mt17.geo[i].xg_dot, tm->data.mt17.geo[i].yg_dot, 
                                  tm->data.mt17.geo[i].zg_dot);
            }
            written += snprintf(out_data + written, buffer_size - written, "]}}");
            break;
        case 18:
            written += snprintf(out_data + written, buffer_size - written,
                              "\"number_of_bands\":%d,\"band_number\":%d,\"iodi\":%d}}",
                              tm->data.mt18.number_of_bands, tm->data.mt18.band_number,
                              tm->data.mt18.iodi);
            break;
        case 24:
            written += snprintf(out_data + written, buffer_size - written,
                              "\"iodp\":%d,\"block_id\":%d,\"iodf\":%d,\"fast_corrections\":[",
                              tm->data.mt24.iodp, tm->data.mt24.block_id, tm->data.mt24.iodf);
            for (int i = 0; i < 6; i++) {
                written += snprintf(out_data + written, buffer_size - written,
                                  "%s%d", i > 0 ? "," : "", tm->data.mt24.fast_corrections[i]);
            }
            written += snprintf(out_data + written, buffer_size - written, "],\"udreis\":[");
            for (int i = 0; i < 6; i++) {
                written += snprintf(out_data + written, buffer_size - written,
                                  "%s%d", i > 0 ? "," : "", tm->data.mt24.udreis[i]);
            }
            written += snprintf(out_data + written, buffer_size - written, "]}}");
            break;
        case 25: {
            // MT25 has two halves, each with velocity code
            written += snprintf(out_data + written, buffer_size - written, "\"half1\":{\"velocity_code\":%d,",
                              tm->data.mt25.half1.raw_velocity_code);
            if (tm->data.mt25.half1.raw_velocity_code == 0) {
                SBAS_MT25_Half_Vel0 *v = &tm->data.mt25.half1.u.v0;
                written += snprintf(out_data + written, buffer_size - written,
                                  "\"prn_mask_1\":%d,\"iod_1\":%d,\"dx_1\":%.6f,\"dy_1\":%.6f,\"dz_1\":%.6f,\"da_f0_1\":%.10e,"
                                  "\"prn_mask_2\":%d,\"iod_2\":%d,\"dx_2\":%.6f,\"dy_2\":%.6f,\"dz_2\":%.6f,\"da_f0_2\":%.10e,\"iodp\":%d",
                                  v->prn_mask_1, v->iod_1, v->dx_1, v->dy_1, v->dz_1, v->da_f0_1,
                                  v->prn_mask_2, v->iod_2, v->dx_2, v->dy_2, v->dz_2, v->da_f0_2, v->iodp);
            } else {
                SBAS_MT25_Half_Vel1 *v = &tm->data.mt25.half1.u.v1;
                written += snprintf(out_data + written, buffer_size - written,
                                  "\"prn_mask\":%d,\"iod\":%d,\"dx\":%.6f,\"dy\":%.6f,\"dz\":%.6f,\"da_f0\":%.10e,"
                                  "\"dx_rate\":%.6f,\"dy_rate\":%.6f,\"dz_rate\":%.6f,\"da_f1\":%.10e,\"t0\":%.6f,\"iodp\":%d",
                                  v->prn_mask, v->iod, v->dx, v->dy, v->dz, v->da_f0,
                                  v->dx_rate, v->dy_rate, v->dz_rate, v->da_f1, v->t0, v->iodp);
            }
            written += snprintf(out_data + written, buffer_size - written, "},\"half2\":{\"velocity_code\":%d,",
                              tm->data.mt25.half2.raw_velocity_code);
            if (tm->data.mt25.half2.raw_velocity_code == 0) {
                SBAS_MT25_Half_Vel0 *v = &tm->data.mt25.half2.u.v0;
                written += snprintf(out_data + written, buffer_size - written,
                                  "\"prn_mask_1\":%d,\"iod_1\":%d,\"dx_1\":%.6f,\"dy_1\":%.6f,\"dz_1\":%.6f,\"da_f0_1\":%.10e,"
                                  "\"prn_mask_2\":%d,\"iod_2\":%d,\"dx_2\":%.6f,\"dy_2\":%.6f,\"dz_2\":%.6f,\"da_f0_2\":%.10e,\"iodp\":%d",
                                  v->prn_mask_1, v->iod_1, v->dx_1, v->dy_1, v->dz_1, v->da_f0_1,
                                  v->prn_mask_2, v->iod_2, v->dx_2, v->dy_2, v->dz_2, v->da_f0_2, v->iodp);
            } else {
                SBAS_MT25_Half_Vel1 *v = &tm->data.mt25.half2.u.v1;
                written += snprintf(out_data + written, buffer_size - written,
                                  "\"prn_mask\":%d,\"iod\":%d,\"dx\":%.6f,\"dy\":%.6f,\"dz\":%.6f,\"da_f0\":%.10e,"
                                  "\"dx_rate\":%.6f,\"dy_rate\":%.6f,\"dz_rate\":%.6f,\"da_f1\":%.10e,\"t0\":%.6f,\"iodp\":%d",
                                  v->prn_mask, v->iod, v->dx, v->dy, v->dz, v->da_f0,
                                  v->dx_rate, v->dy_rate, v->dz_rate, v->da_f1, v->t0, v->iodp);
            }
            written += snprintf(out_data + written, buffer_size - written, "}}}");
            break;
        }
        case 26:
            written += snprintf(out_data + written, buffer_size - written,
                              "\"band_number\":%d,\"block_id\":%d,\"iodi\":%d,\"igp_vertical_delay\":[",
                              tm->data.mt26.band_number, tm->data.mt26.block_id, tm->data.mt26.iodi);
            for (int i = 0; i < 15; i++) {
                written += snprintf(out_data + written, buffer_size - written,
                                  "%s%d", i > 0 ? "," : "", tm->data.mt26.igp_vertical_delay[i]);
            }
            written += snprintf(out_data + written, buffer_size - written, "],\"givei\":[");
            for (int i = 0; i < 15; i++) {
                written += snprintf(out_data + written, buffer_size - written,
                                  "%s%d", i > 0 ? "," : "", tm->data.mt26.givei[i]);
            }
            written += snprintf(out_data + written, buffer_size - written, "]}}");
            break;
        case 28:
            written += snprintf(out_data + written, buffer_size - written,
                              "\"iodp\":%d,\"sat\":[", tm->data.mt28.iodp);
            for (int i = 0; i < 2; i++) {
                written += snprintf(out_data + written, buffer_size - written,
                                  "%s{\"prn\":%d,\"scale_exp\":%d,\"E11\":%d,\"E22\":%d,\"E33\":%d,"
                                  "\"E44\":%d,\"E12\":%d,\"E13\":%d,\"E14\":%d,\"E23\":%d,\"E24\":%d,\"E34\":%d}",
                                  i > 0 ? "," : "", tm->data.mt28.sat[i].prn_mask, tm->data.mt28.sat[i].scale_exponent,
                                  tm->data.mt28.sat[i].E1_1, tm->data.mt28.sat[i].E2_2, tm->data.mt28.sat[i].E3_3,
                                  tm->data.mt28.sat[i].E4_4, tm->data.mt28.sat[i].E1_2, tm->data.mt28.sat[i].E1_3,
                                  tm->data.mt28.sat[i].E1_4, tm->data.mt28.sat[i].E2_3, tm->data.mt28.sat[i].E2_4,
                                  tm->data.mt28.sat[i].E3_4);
            }
            written += snprintf(out_data + written, buffer_size - written, "]}}");
            break;
        default:
            written += snprintf(out_data + written, buffer_size - written, "}}");
            break;
    }
    
    return 0;
}

HERCULES_API int hercules_update_message_field(int satellite_id,
                                                 int message_type,
                                                 const char* field_name,
                                                 const char* value_str) {
    if (!g_initialized || !field_name || !value_str) {
        set_error("Invalid parameters");
        return -1;
    }

    SBAS_TypedMessage* tm = NULL;
    int idx = -1;
    
    if (tx_source_find_first(message_type, (unsigned int)satellite_id, &tm, &idx) != 0 || !tm) {
        set_error("Message not found: sat=%d, type=%d", satellite_id, message_type);
        return -1;
    }

    /* Parse field name and update corresponding field */
    double dval = strtod(value_str, NULL);
    int ival = (int)strtol(value_str, NULL, 10);
    
    switch (message_type) {
        case 1:
            if (strcmp(field_name, "iodp") == 0) {
                tm->data.mt1.iodp = (uint8_t)(ival & 3);
            } else if (strncmp(field_name, "prn_mask[", 9) == 0) {
                /* Parse prn_mask[N] */
                int idx = atoi(field_name + 9);
                if (idx >= 0 && idx < 27) {
                    tm->data.mt1.prn_mask[idx] = (uint8_t)ival;
                } else {
                    set_error("Invalid prn_mask index: %d", idx);
                    return -1;
                }
            } else {
                set_error("Unknown field for MT1: %s", field_name);
                return -1;
            }
            break;
            
        case 2: case 3: case 4: case 5:
            if (strcmp(field_name, "iodf") == 0) {
                tm->data.mt2_5.iodf = (uint8_t)(ival & 3);
            } else if (strcmp(field_name, "iodp") == 0) {
                tm->data.mt2_5.iodp = (uint8_t)(ival & 3);
            } else if (strncmp(field_name, "sat_corrections[", 16) == 0) {
                /* Parse sat_corrections[N].field */
                int idx = atoi(field_name + 16);
                if (idx >= 0 && idx < 13) {
                    const char *dot = strchr(field_name + 16, '.');
                    if (dot && strcmp(dot + 1, "fc") == 0) {
                        tm->data.mt2_5.sat_corrections[idx].fast_correction = (int16_t)ival;
                    } else if (dot && strcmp(dot + 1, "udrei") == 0) {
                        tm->data.mt2_5.sat_corrections[idx].udrei = (uint8_t)(ival & 15);
                    } else {
                        set_error("Unknown sat_corrections field: %s", field_name);
                        return -1;
                    }
                } else {
                    set_error("Invalid sat_corrections index: %d", idx);
                    return -1;
                }
            } else {
                set_error("Unknown field for MT2-5: %s", field_name);
                return -1;
            }
            break;
            
        case 6:
            if (strcmp(field_name, "iodf2") == 0) {
                tm->data.mt6.iodf2 = (uint8_t)(ival & 3);
            } else if (strcmp(field_name, "iodf3") == 0) {
                tm->data.mt6.iodf3 = (uint8_t)(ival & 3);
            } else if (strcmp(field_name, "iodf4") == 0) {
                tm->data.mt6.iodf4 = (uint8_t)(ival & 3);
            } else if (strcmp(field_name, "iodf5") == 0) {
                tm->data.mt6.iodf5 = (uint8_t)(ival & 3);
            } else if (strncmp(field_name, "udrei[", 6) == 0) {
                int idx = atoi(field_name + 6);
                if (idx >= 0 && idx < 51) {
                    tm->data.mt6.udrei[idx] = (uint8_t)(ival & 15);
                } else {
                    set_error("Invalid udrei index: %d", idx);
                    return -1;
                }
            } else {
                set_error("Unknown field for MT6: %s", field_name);
                return -1;
            }
            break;
            
        case 7:
            if (strcmp(field_name, "t_lat") == 0) {
                tm->data.mt7.t_lat = (uint8_t)(ival & 15);
            } else if (strcmp(field_name, "iodp") == 0) {
                tm->data.mt7.iodp = (uint8_t)(ival & 3);
            } else if (strncmp(field_name, "udre_ai[", 8) == 0) {
                int idx = atoi(field_name + 8);
                if (idx >= 0 && idx < 51) {
                    tm->data.mt7.udre_ai[idx] = (uint8_t)(ival & 15);
                } else {
                    set_error("Invalid udre_ai index: %d", idx);
                    return -1;
                }
            } else {
                set_error("Unknown field for MT7: %s", field_name);
                return -1;
            }
            break;
            
        case 9:
            if (strcmp(field_name, "t0") == 0) {
                tm->data.mt9.t0 = dval;
            } else if (strcmp(field_name, "ura") == 0) {
                tm->data.mt9.ura = (uint8_t)ival;
            } else if (strcmp(field_name, "xg_ecef") == 0) {
                tm->data.mt9.xg_ecef = dval;
            } else if (strcmp(field_name, "yg_ecef") == 0) {
                tm->data.mt9.yg_ecef = dval;
            } else if (strcmp(field_name, "zg_ecef") == 0) {
                tm->data.mt9.zg_ecef = dval;
            } else if (strcmp(field_name, "xg_rate") == 0) {
                tm->data.mt9.xg_rate = dval;
            } else if (strcmp(field_name, "yg_rate") == 0) {
                tm->data.mt9.yg_rate = dval;
            } else if (strcmp(field_name, "zg_rate") == 0) {
                tm->data.mt9.zg_rate = dval;
            } else if (strcmp(field_name, "xg_accel") == 0) {
                tm->data.mt9.xg_accel = dval;
            } else if (strcmp(field_name, "yg_accel") == 0) {
                tm->data.mt9.yg_accel = dval;
            } else if (strcmp(field_name, "zg_accel") == 0) {
                tm->data.mt9.zg_accel = dval;
            } else if (strcmp(field_name, "ag_f0") == 0) {
                tm->data.mt9.ag_f0 = dval;
            } else if (strcmp(field_name, "ag_f1") == 0) {
                tm->data.mt9.ag_f1 = dval;
            } else {
                set_error("Unknown field for MT9: %s", field_name);
                return -1;
            }
            break;
            
        case 10:
            if (strcmp(field_name, "B_rrc") == 0) {
                tm->data.mt10.B_rrc = dval;
            } else if (strcmp(field_name, "C_ltc_lsb") == 0) {
                tm->data.mt10.C_ltc_lsb = dval;
            } else if (strcmp(field_name, "C_ltc_v1") == 0) {
                tm->data.mt10.C_ltc_v1 = dval;
            } else if (strcmp(field_name, "I_ltc_v1") == 0) {
                tm->data.mt10.I_ltc_v1 = dval;
            } else if (strcmp(field_name, "C_ltc_v0") == 0) {
                tm->data.mt10.C_ltc_v0 = dval;
            } else if (strcmp(field_name, "I_ltc_v0") == 0) {
                tm->data.mt10.I_ltc_v0 = dval;
            } else if (strcmp(field_name, "C_geo_lsb") == 0) {
                tm->data.mt10.C_geo_lsb = dval;
            } else if (strcmp(field_name, "C_geo_v") == 0) {
                tm->data.mt10.C_geo_v = dval;
            } else if (strcmp(field_name, "I_geo") == 0) {
                tm->data.mt10.I_geo = dval;
            } else if (strcmp(field_name, "C_er") == 0) {
                tm->data.mt10.C_er = dval;
            } else if (strcmp(field_name, "C_iono_step") == 0) {
                tm->data.mt10.C_iono_step = dval;
            } else if (strcmp(field_name, "I_iono") == 0) {
                tm->data.mt10.I_iono = dval;
            } else if (strcmp(field_name, "C_iono_ramp") == 0) {
                tm->data.mt10.C_iono_ramp = dval;
            } else if (strcmp(field_name, "rss_udre") == 0) {
                tm->data.mt10.rss_udre = (uint8_t)(ival ? 1 : 0);
            } else if (strcmp(field_name, "rss_iono") == 0) {
                tm->data.mt10.rss_iono = (uint8_t)(ival ? 1 : 0);
            } else if (strcmp(field_name, "C_covariance") == 0) {
                tm->data.mt10.C_covariance = dval;
            } else {
                set_error("Unknown field for MT10: %s", field_name);
                return -1;
            }
            break;
            
        case 17:
            if (strcmp(field_name, "t0") == 0) {
                tm->data.mt17.t0 = dval;
            } else if (strncmp(field_name, "geo[", 4) == 0) {
                int idx = atoi(field_name + 4);
                if (idx >= 0 && idx < 3) {
                    const char *dot = strchr(field_name + 4, '.');
                    if (dot) {
                        const char *subfield = dot + 1;
                        if (strcmp(subfield, "data_id") == 0) tm->data.mt17.geo[idx].data_id = (uint8_t)(ival & 3);
                        else if (strcmp(subfield, "prn") == 0) tm->data.mt17.geo[idx].prn_number = (uint8_t)ival;
                        else if (strcmp(subfield, "health") == 0) tm->data.mt17.geo[idx].health_and_status = (uint8_t)ival;
                        else if (strcmp(subfield, "x_g") == 0) tm->data.mt17.geo[idx].x_g = dval;
                        else if (strcmp(subfield, "y_g") == 0) tm->data.mt17.geo[idx].y_g = dval;
                        else if (strcmp(subfield, "z_g") == 0) tm->data.mt17.geo[idx].z_g = dval;
                        else if (strcmp(subfield, "xg_dot") == 0) tm->data.mt17.geo[idx].xg_dot = dval;
                        else if (strcmp(subfield, "yg_dot") == 0) tm->data.mt17.geo[idx].yg_dot = dval;
                        else if (strcmp(subfield, "zg_dot") == 0) tm->data.mt17.geo[idx].zg_dot = dval;
                        else { set_error("Unknown geo field: %s", subfield); return -1; }
                    } else {
                        set_error("Invalid geo field format: %s", field_name);
                        return -1;
                    }
                } else {
                    set_error("Invalid geo index: %d", idx);
                    return -1;
                }
            } else {
                set_error("Unknown field for MT17: %s", field_name);
                return -1;
            }
            break;
            
        case 18:
            if (strcmp(field_name, "number_of_bands") == 0) {
                tm->data.mt18.number_of_bands = (uint8_t)ival;
            } else if (strcmp(field_name, "band_number") == 0) {
                tm->data.mt18.band_number = (uint8_t)ival;
            } else if (strcmp(field_name, "iodi") == 0) {
                tm->data.mt18.iodi = (uint8_t)ival;
            } else {
                set_error("Unknown field for MT18: %s", field_name);
                return -1;
            }
            break;
            
        case 24:
            if (strcmp(field_name, "iodp") == 0) {
                tm->data.mt24.iodp = (uint8_t)(ival & 3);
            } else if (strcmp(field_name, "block_id") == 0) {
                tm->data.mt24.block_id = (uint8_t)(ival & 3);
            } else if (strcmp(field_name, "iodf") == 0) {
                tm->data.mt24.iodf = (uint8_t)(ival & 3);
            } else if (strncmp(field_name, "fast_corrections[", 17) == 0) {
                int idx = atoi(field_name + 17);
                if (idx >= 0 && idx < 6) {
                    tm->data.mt24.fast_corrections[idx] = (int16_t)ival;
                } else {
                    set_error("Invalid fast_corrections index: %d", idx);
                    return -1;
                }
            } else if (strncmp(field_name, "udreis[", 7) == 0) {
                int idx = atoi(field_name + 7);
                if (idx >= 0 && idx < 6) {
                    tm->data.mt24.udreis[idx] = (uint8_t)(ival & 15);
                } else {
                    set_error("Invalid udreis index: %d", idx);
                    return -1;
                }
            } else {
                set_error("Unknown field for MT24: %s", field_name);
                return -1;
            }
            break;
            
        case 25: {
            // Field names: half1.velocity_code, half1.prn_mask_1, half1.dx_1, etc.
            // or half2.velocity_code, half2.prn_mask, half2.dx_rate, etc.
            if (strncmp(field_name, "half1.", 6) == 0) {
                const char *subfield = field_name + 6;
                SBAS_MT25_Half *half = &tm->data.mt25.half1;
                if (strcmp(subfield, "velocity_code") == 0) {
                    half->raw_velocity_code = (uint8_t)(ival & 1);
                } else if (half->raw_velocity_code == 0) {
                    SBAS_MT25_Half_Vel0 *v = &half->u.v0;
                    if (strcmp(subfield, "prn_mask_1") == 0) v->prn_mask_1 = (uint8_t)ival;
                    else if (strcmp(subfield, "iod_1") == 0) v->iod_1 = (uint8_t)ival;
                    else if (strcmp(subfield, "dx_1") == 0) v->dx_1 = dval;
                    else if (strcmp(subfield, "dy_1") == 0) v->dy_1 = dval;
                    else if (strcmp(subfield, "dz_1") == 0) v->dz_1 = dval;
                    else if (strcmp(subfield, "da_f0_1") == 0) v->da_f0_1 = dval;
                    else if (strcmp(subfield, "prn_mask_2") == 0) v->prn_mask_2 = (uint8_t)ival;
                    else if (strcmp(subfield, "iod_2") == 0) v->iod_2 = (uint8_t)ival;
                    else if (strcmp(subfield, "dx_2") == 0) v->dx_2 = dval;
                    else if (strcmp(subfield, "dy_2") == 0) v->dy_2 = dval;
                    else if (strcmp(subfield, "dz_2") == 0) v->dz_2 = dval;
                    else if (strcmp(subfield, "da_f0_2") == 0) v->da_f0_2 = dval;
                    else if (strcmp(subfield, "iodp") == 0) v->iodp = (uint8_t)(ival & 3);
                    else { set_error("Unknown v0 field for MT25 half1: %s", subfield); return -1; }
                } else {
                    SBAS_MT25_Half_Vel1 *v = &half->u.v1;
                    if (strcmp(subfield, "prn_mask") == 0) v->prn_mask = (uint8_t)ival;
                    else if (strcmp(subfield, "iod") == 0) v->iod = (uint8_t)ival;
                    else if (strcmp(subfield, "dx") == 0) v->dx = dval;
                    else if (strcmp(subfield, "dy") == 0) v->dy = dval;
                    else if (strcmp(subfield, "dz") == 0) v->dz = dval;
                    else if (strcmp(subfield, "da_f0") == 0) v->da_f0 = dval;
                    else if (strcmp(subfield, "dx_rate") == 0) v->dx_rate = dval;
                    else if (strcmp(subfield, "dy_rate") == 0) v->dy_rate = dval;
                    else if (strcmp(subfield, "dz_rate") == 0) v->dz_rate = dval;
                    else if (strcmp(subfield, "da_f1") == 0) v->da_f1 = dval;
                    else if (strcmp(subfield, "t0") == 0) v->t0 = dval;
                    else if (strcmp(subfield, "iodp") == 0) v->iodp = (uint8_t)(ival & 3);
                    else { set_error("Unknown v1 field for MT25 half1: %s", subfield); return -1; }
                }
            } else if (strncmp(field_name, "half2.", 6) == 0) {
                const char *subfield = field_name + 6;
                SBAS_MT25_Half *half = &tm->data.mt25.half2;
                if (strcmp(subfield, "velocity_code") == 0) {
                    half->raw_velocity_code = (uint8_t)(ival & 1);
                } else if (half->raw_velocity_code == 0) {
                    SBAS_MT25_Half_Vel0 *v = &half->u.v0;
                    if (strcmp(subfield, "prn_mask_1") == 0) v->prn_mask_1 = (uint8_t)ival;
                    else if (strcmp(subfield, "iod_1") == 0) v->iod_1 = (uint8_t)ival;
                    else if (strcmp(subfield, "dx_1") == 0) v->dx_1 = dval;
                    else if (strcmp(subfield, "dy_1") == 0) v->dy_1 = dval;
                    else if (strcmp(subfield, "dz_1") == 0) v->dz_1 = dval;
                    else if (strcmp(subfield, "da_f0_1") == 0) v->da_f0_1 = dval;
                    else if (strcmp(subfield, "prn_mask_2") == 0) v->prn_mask_2 = (uint8_t)ival;
                    else if (strcmp(subfield, "iod_2") == 0) v->iod_2 = (uint8_t)ival;
                    else if (strcmp(subfield, "dx_2") == 0) v->dx_2 = dval;
                    else if (strcmp(subfield, "dy_2") == 0) v->dy_2 = dval;
                    else if (strcmp(subfield, "dz_2") == 0) v->dz_2 = dval;
                    else if (strcmp(subfield, "da_f0_2") == 0) v->da_f0_2 = dval;
                    else if (strcmp(subfield, "iodp") == 0) v->iodp = (uint8_t)(ival & 3);
                    else { set_error("Unknown v0 field for MT25 half2: %s", subfield); return -1; }
                } else {
                    SBAS_MT25_Half_Vel1 *v = &half->u.v1;
                    if (strcmp(subfield, "prn_mask") == 0) v->prn_mask = (uint8_t)ival;
                    else if (strcmp(subfield, "iod") == 0) v->iod = (uint8_t)ival;
                    else if (strcmp(subfield, "dx") == 0) v->dx = dval;
                    else if (strcmp(subfield, "dy") == 0) v->dy = dval;
                    else if (strcmp(subfield, "dz") == 0) v->dz = dval;
                    else if (strcmp(subfield, "da_f0") == 0) v->da_f0 = dval;
                    else if (strcmp(subfield, "dx_rate") == 0) v->dx_rate = dval;
                    else if (strcmp(subfield, "dy_rate") == 0) v->dy_rate = dval;
                    else if (strcmp(subfield, "dz_rate") == 0) v->dz_rate = dval;
                    else if (strcmp(subfield, "da_f1") == 0) v->da_f1 = dval;
                    else if (strcmp(subfield, "t0") == 0) v->t0 = dval;
                    else if (strcmp(subfield, "iodp") == 0) v->iodp = (uint8_t)(ival & 3);
                    else { set_error("Unknown v1 field for MT25 half2: %s", subfield); return -1; }
                }
            } else {
                set_error("MT25 fields must start with 'half1.' or 'half2.': %s", field_name);
                return -1;
            }
            break;
        }
            
        case 26:
            if (strcmp(field_name, "band_number") == 0) {
                tm->data.mt26.band_number = (uint8_t)ival;
            } else if (strcmp(field_name, "block_id") == 0) {
                tm->data.mt26.block_id = (uint8_t)(ival & 15);
            } else if (strcmp(field_name, "iodi") == 0) {
                tm->data.mt26.iodi = (uint8_t)(ival & 3);
            } else if (strncmp(field_name, "igp_vertical_delay[", 19) == 0) {
                int idx = atoi(field_name + 19);
                if (idx >= 0 && idx < 15) {
                    tm->data.mt26.igp_vertical_delay[idx] = (int16_t)ival;
                } else {
                    set_error("Invalid igp_vertical_delay index: %d", idx);
                    return -1;
                }
            } else if (strncmp(field_name, "givei[", 6) == 0) {
                int idx = atoi(field_name + 6);
                if (idx >= 0 && idx < 15) {
                    tm->data.mt26.givei[idx] = (uint8_t)(ival & 15);
                } else {
                    set_error("Invalid givei index: %d", idx);
                    return -1;
                }
            } else {
                set_error("Unknown field for MT26: %s", field_name);
                return -1;
            }
            break;
            
        case 28:
            if (strcmp(field_name, "iodp") == 0) {
                tm->data.mt28.iodp = (uint8_t)(ival & 3);
            } else {
                set_error("Unknown field for MT28: %s", field_name);
                return -1;
            }
            break;
            
        default:
            set_error("Message type %d not supported for editing", message_type);
            return -1;
    }
    
    send_status("Updated field %s = %s for MT%d (sat %d)", 
                field_name, value_str, message_type, satellite_id);
    return 0;
}

HERCULES_API int hercules_send_message_once(int satellite_id, int message_type) {
    if (!g_initialized) {
        set_error("Not initialized");
        return -1;
    }

    SBAS_TypedMessage* tm = NULL;
    int idx = -1;
    
    if (tx_source_find_first(message_type, (unsigned int)satellite_id, &tm, &idx) != 0 || !tm) {
        set_error("Message not found");
        return -1;
    }

    /* Apply orbit model and IOD overrides if applicable */
    apply_orbit_model_override(tm);
    apply_global_iodp(tm);
    apply_global_iodf(tm);
    apply_global_iodi(tm);

    /* Encode message */
    char hex[256] = {0};
    if (encode_message_from_tm(tm, hex) != 0) {
        set_error("Failed to encode MT%d", message_type);
        return -1;
    }
    
    /* Convert to binary and broadcast */
    uint8_t bin[32];
    size_t blen = tx_hex_to_bin(hex, bin, sizeof(bin));
    if (blen == 0) {
        set_error("Failed to convert hex to binary");
        return -1;
    }

    tx_server_broadcast(bin, blen);
    send_status("Sent MT%d (sat %d) once", message_type, satellite_id);
    
    /* Log to RINEX if active */
    if (rinex_writer_is_active()) {
        time_t msg_timestamp = 0;
        if (tx_source_get_timestamp(idx, &msg_timestamp) != 0 || msg_timestamp == 0) {
            msg_timestamp = time(NULL);  // Fallback to current time
        }
        rinex_writer_add_packet(satellite_id, message_type, hex, msg_timestamp);
    }
    
    return 0;
}

HERCULES_API int hercules_start_single_message_tx(int satellite_id, 
                                                    int message_type,
                                                    int interval_ms) {
    if (!g_initialized) {
        set_error("Not initialized");
        return -1;
    }
    
    /* Verify message exists */
    SBAS_TypedMessage* tm = NULL;
    int idx = -1;
    if (tx_source_find_first(message_type, (unsigned int)satellite_id, &tm, &idx) != 0 || !tm) {
        set_error("Message not found: sat=%d, type=%d", satellite_id, message_type);
        return -1;
    }
    
    /* Set single message mode parameters */
    g_single_msg_transmitting = 1;
    g_single_msg_satellite_id = satellite_id;
    g_single_msg_type = message_type;
    g_single_msg_interval_ms = (interval_ms > 0) ? interval_ms : 1000;
    
    send_status("Started continuous TX: MT%d (sat %d), interval %d ms", 
                message_type, satellite_id, g_single_msg_interval_ms);
    return 0;
}

HERCULES_API void hercules_stop_single_message_tx(void) {
    g_single_msg_transmitting = 0;
}

/* ============================================================================
 * ORBIT MODEL CONFIGURATION
 * ========================================================================== */

HERCULES_API int hercules_set_orbit_model_tle(const char* tle_file_path,
                                                const char* satellite_mappings) {
    if (!tle_file_path) {
        set_error("NULL TLE file path");
        return -1;
    }

    /* Load TLE entries */
    int loaded = tle_load_all_entries(tle_file_path, g_tle_entries, MAX_TLE_ENTRIES);
    if (loaded <= 0) {
        set_error("Failed to load TLE file: %s", tle_file_path);
        return -1;
    }

    g_tle_count = loaded;
    g_orbit_mode = ORBIT_TLE;
    
    /* TODO: Parse satellite_mappings JSON and enable TLE entries */
    
    send_status("TLE orbit model set (%d entries loaded)", loaded);
    return 0;
}

HERCULES_API int hercules_set_orbit_model_statistical(int satellite_id) {
    if (satellite_id == 128) {
        if (!g_stat_model_128.initialized) {
            set_error("Statistical model 128 not trained");
            return -1;
        }
        g_orbit_mode = ORBIT_STAT128;
        g_orbit_target_sat = 128;
    } else if (satellite_id == 143) {
        if (!g_stat_model_143.initialized) {
            set_error("Statistical model 143 not trained");
            return -1;
        }
        g_orbit_mode = ORBIT_STAT143;
        g_orbit_target_sat = 143;
    } else {
        set_error("Invalid satellite ID for statistical model: %d", satellite_id);
        return -1;
    }

    send_status("Statistical orbit model set for satellite %d", satellite_id);
    return 0;
}

HERCULES_API void hercules_disable_orbit_model(void) {
    g_orbit_mode = ORBIT_NONE;
    g_orbit_target_sat = -1;
    send_status("Orbit model disabled");
}

HERCULES_API int hercules_set_tle_time_mode(int use_simulation, const char* base_time_utc) {
    g_tle_use_simulation = use_simulation;
    
    if (use_simulation && base_time_utc) {
        /* Parse base_time_utc string (YYYY-MM-DD HH:MM:SS) */
        struct tm tm_input = {0};
        if (sscanf(base_time_utc, "%d-%d-%d %d:%d:%d",
                   &tm_input.tm_year, &tm_input.tm_mon, &tm_input.tm_mday,
                   &tm_input.tm_hour, &tm_input.tm_min, &tm_input.tm_sec) != 6) {
            set_error("Invalid time format. Use: YYYY-MM-DD HH:MM:SS");
            return -1;
        }
        
        tm_input.tm_year -= 1900;
        tm_input.tm_mon -= 1;
        tm_input.tm_isdst = -1;
        
        g_tle_base_time = mktime(&tm_input);
        if (g_tle_base_time == -1) {
            set_error("Failed to convert time");
            return -1;
        }
        
        send_status("TLE simulation mode enabled (base: %s)", base_time_utc);
    } else {
        send_status("TLE real-time mode enabled");
    }
    
    return 0;
}

/* ============================================================================
 * RINEX OUTPUT CONFIGURATION
 * ========================================================================== */

HERCULES_API int hercules_configure_rinex_output(const char* output_path,
                                                   const char* template_path) {
    if (!output_path) {
        /* Disable RINEX output */
        if (rinex_writer_is_active()) {
            rinex_writer_close();
        }
        send_status("RINEX output disabled");
        return 0;
    }

    if (!template_path) {
        set_error("Template path required for RINEX output");
        return -1;
    }

    if (rinex_writer_init_from_template(template_path, output_path) != 0) {
        set_error("Failed to initialize RINEX writer");
        return -1;
    }

    send_status("RINEX output enabled: %s", output_path);
    return 0;
}

HERCULES_API int hercules_is_rinex_output_active(void) {
    return rinex_writer_is_active();
}

HERCULES_API int hercules_enable_field_increment(int satellite_id, int message_type) {
    /* Find the message in the source */
    SBAS_TypedMessage *tm = NULL;
    int idx = -1;
    
    if (tx_source_find_first(message_type, satellite_id, &tm, &idx) != 0 || !tm) {
        set_error("Message not found for increment enable");
        return -1;
    }
    
    /* 
     * For increment mode, we need to mark that the next transmission
     * should use exact values (suppress_once = true), then subsequent
     * transmissions will apply increments.
     * 
     * The increment rule is created/updated based on which fields changed.
     * Since we're calling this after field updates via hercules_update_message_field,
     * we can create a rule that marks the message as modified.
     */
    
    LiveIncrementRule *rule = ensure_live_rule(satellite_id, message_type);
    if (!rule) {
        set_error("Failed to create increment rule (max rules reached)");
        return -1;
    }
    
    /* Mark that the live message pointer points to this message */
    rule->live_message_ptr = (void*)tm;
    
    /* 
     * Set suppress_once to true so the first transmission uses exact values,
     * then subsequent transmissions will increment
     */
    rule->suppress_once = true;
    
    send_status("Field increment mode enabled for MT%d Sat %d (first TX uses exact values, then increments)",
                message_type, satellite_id);
    
    return 0;
}

/* ============================================================================
 * STATUS & LOGGING
 * ========================================================================== */

HERCULES_API const char* hercules_get_last_error(void) {
    return g_last_error[0] ? g_last_error : NULL;
}

HERCULES_API int hercules_get_transmission_stats(char* out_stats, size_t buffer_size) {
    if (!out_stats || buffer_size == 0) return -1;

    snprintf(out_stats, buffer_size,
             "{\"is_transmitting\": %d, \"client_count\": %d, \"message_count\": %zu}",
             g_transmitting, hercules_get_client_count(), tx_source_bound_count());
    
    return 0;
}

HERCULES_API void hercules_set_status_callback(hercules_status_callback_t callback) {
    g_status_callback = callback;
}
