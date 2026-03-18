#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <conio.h>      // For _kbhit() and _getch()
#include <windows.h>    // For Sleep()
#include <limits.h>
#include <stdbool.h>
#include "src/encoder.h"
#include "src/tx_server.h"
#include "src/tx_utils.h"
#include "src/message_types.h"
#include "src/tx_source.h"
#include "src/TLE_implementation.h"
#include "src/stat_model_types.h"                 // <-- ADD THIS
#include "src/statistical_model/stat_model_128.h" // <-- Path from your code
#include "src/statistical_model/stat_model_143.h" // <-- Path from your code
#include "src/rinex_writer.h"                      // <-- RINEX output support


/* ================= Live Auto-increment support (Stateful "Skinny" Ptr) ================ */
/* This new system creates a "Live Rule" for a (SatID, MessageType) pair.
   It stores a pointer to the "master" message instance which holds the
   live, incrementing data. */

typedef struct { 
    int satellite_id;
    int mt;
    bool in_use;       // Is this rule slot active?
    bool suppress_once;   // Skip increment once after edit

    /* This is the key change:
       We store a pointer to the *one* message instance that
       will hold the "live" incrementing data. */
    SBAS_TypedMessage* live_message_ptr; 
    
    /* Flags to know *which* fields to increment */
    /* MT1 */
    bool mt1_inc_iodp;
    /* MT2-5 */
    bool mt2_inc_iodf, mt2_inc_iodp;
    bool mt2_fc_mask[13];
    bool mt2_udrei_mask[13];
    bool mt2_inc_all_entries;
    /* MT6 */
    bool mt6_inc_iodf2, mt6_inc_iodf3, mt6_inc_iodf4, mt6_inc_iodf5;
    bool mt6_udrei_mask[51];
    /* MT7 */
    bool mt7_inc_tlat, mt7_inc_iodp;
    bool mt7_udreai_mask[51];
    /* MT9 */
    bool mt9_inc_t0, mt9_inc_ura, mt9_inc_pos, mt9_inc_rate, mt9_inc_accel, mt9_inc_agf0, mt9_inc_agf1;
    /* MT10 */
    bool mt10_inc_all; bool mt10_toggle_rss_udre; bool mt10_toggle_rss_iono;
    /* MT17 */
    bool mt17_inc_t0; int mt17_geo_idx; bool mt17_inc_geo_pos; bool mt17_inc_geo_rate; bool mt17_inc_geo_hs; bool mt17_inc_geo_data_id; bool mt17_inc_geo_prn;
    /* MT18 */
    bool mt18_inc_bands; bool mt18_inc_bandnum; bool mt18_inc_iodi;
    /* MT24 */
    bool mt24_inc_iodp, mt24_inc_block_id, mt24_inc_iodf;
    bool mt24_fc_mask[6];
    bool mt24_udrei_mask[6];
    /* MT25 */
    int mt25_half; bool mt25_v0;
    bool mt25_inc_iodp; bool mt25_inc_t0;
    bool mt25_inc_dx1, mt25_inc_dy1, mt25_inc_dz1, mt25_inc_da_f0_1;
    bool mt25_inc_dx2, mt25_inc_dy2, mt25_inc_dz2, mt25_inc_da_f0_2;
    bool mt25_inc_dx, mt25_inc_dy, mt25_inc_dz, mt25_inc_da_f0, mt25_inc_dx_rate, mt25_inc_dy_rate, mt25_inc_dz_rate, mt25_inc_da_f1;
    /* MT26 */
    bool mt26_inc_bandnum, mt26_inc_block_id, mt26_inc_iodi;
    bool mt26_delay_mask[SBAS_MT26_IGP_COUNT];
    bool mt26_givei_mask[SBAS_MT26_IGP_COUNT];
    /* MT28 */
    bool mt28_inc_iodp; int mt28_block_idx; bool mt28_inc_prn_mask, mt28_inc_scale_exp, mt28_inc_matrix;
} LiveIncrementRule;

#define MAX_LIVE_RULES 64
static LiveIncrementRule g_live_rules[MAX_LIVE_RULES];
static int g_tx_filter_sat_id = -1; // -1 means no filter (send all)
static uint8_t g_current_iodp = 0;  // This is the system-wide IODP value (for MT1, MT2-5, MT7, MT24, MT25, MT28)
static uint8_t g_current_iodf[4] = {0,0,0,0};  // System-wide IODF values: [0]=MT2, [1]=MT3, [2]=MT4, [3]=MT5 (for MT2-5, MT6, MT24)
static uint8_t g_current_iodi = 0;  // System-wide IODI value (for MT18, MT26)

/* RINEX output configuration */
static char *g_rinex_output_path = NULL;  // NULL means RINEX output disabled

/* Fast correction satellite count configuration
 * This controls which MT2-5 messages are transmitted:
 * - 0 or negative: Send all MT2-5 (no filter)
 * - 1-13 satellites: Send only MT2 (covers 13 satellites)
 * - 14-26 satellites: Send MT2 + MT3 (covers 26 satellites)
 * - 27-39 satellites: Send MT2 + MT3 + MT4 (covers 39 satellites)  
 * - 40-51 satellites: Send MT2 + MT3 + MT4 + MT5 (covers 51 satellites)
 */
static int g_fast_correction_satellites = 0;  // 0 means send all MT2-5


/* New function: Finds a rule by (SatID, MT) */
static LiveIncrementRule* find_live_rule(int sat_id, int mt) {
    for (int i = 0; i < MAX_LIVE_RULES; i++) {
        if (g_live_rules[i].in_use && g_live_rules[i].satellite_id == sat_id && g_live_rules[i].mt == mt) {
            return &g_live_rules[i];
        }
    }
    return NULL;
}

/* New function: Finds or creates a rule for a (SatID, MT) pair */
static LiveIncrementRule* ensure_live_rule(int sat_id, int mt) {
    LiveIncrementRule *r = find_live_rule(sat_id, mt);
    if (r) return r;

    // Not found, find a free slot to create one
    for (int i = 0; i < MAX_LIVE_RULES; i++) {
        if (!g_live_rules[i].in_use) {
            r = &g_live_rules[i];
            memset(r, 0, sizeof(LiveIncrementRule)); // Zero out old data
            r->satellite_id = sat_id;
            r->mt = mt;
            r->in_use = true;
            return r;
        }
    }
    return NULL; // No free slots
}


/* Globals declared near the bottom of this file but referenced earlier; declare externs */
extern char *g_mt9_tle_alloc;
extern const char *g_mt9_tle_path;
extern int g_mt9_sat_id;


/* === NEW GLOBALS FOR ORBIT MODELING === */
typedef enum {
    ORBIT_MODEL_NONE,
    ORBIT_MODEL_TLE,
    ORBIT_MODEL_STAT128,
    ORBIT_MODEL_STAT143
} OrbitModelType;

static OrbitModelType g_orbit_model = ORBIT_MODEL_NONE;
static int g_orbit_model_target_sat_id = -1; // Renamed to fix duplicate

static StatPredictor128 g_stat_model_128; // Global instance for model 128
static StatPredictor143 g_stat_model_143; // Global instance for model 143

/* Multiple TLE support - load all TLEs from file and let user enable/disable */
#define MAX_TLE_ENTRIES 10
static TLE_Entry g_tle_entries[MAX_TLE_ENTRIES];
static int g_tle_count = 0;
static char *g_tle_file_path = NULL;

/* TLE Time Mode: 0 = use current real time, 1 = use simulation time starting from fixed base */
static int g_tle_use_fixed_time = 0;
static time_t g_tle_fixed_time = 0;       // Base time for simulation
static time_t g_tle_simulation_offset = 0; // Seconds elapsed in simulation
/* === END NEW GLOBALS === */


#define TCP_PORT 23
#define MAX_CLIENTS 1
/* BUFFER_SIZE was unused; removed. */

/* Forward declaration for server initializer used before its definition */
int initialize_tcp_server(void);

/* Implementation appears at the bottom of this file. */

/* Utility: format current local time as HH:MM:SS into provided buffer */
static void current_time_hhmmss(char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    if (lt) {
        strftime(out, out_sz, "%H:%M:%S", lt);
    } else {
        /* Fallback */
        out[0] = '\0';
    }
}





/* Clamp helpers */
static int clamp_int(int v,int lo,int hi){ if(v<lo) return lo; if(v>hi) return hi; return v; }
static unsigned clamp_uint(unsigned v,unsigned lo,unsigned hi){ if(v<lo) return lo; if(v>hi) return hi; return v; }
static double clamp_double(double v,double lo,double hi){ if(v<lo) return lo; if(v>hi) return hi; return v; }
/* Detect which fields changed and create/update increment rule (subset for edited fields) */




/*
 * @brief Iterates through the loaded tx_source and trains all stat models.
 */
static void train_statistical_models(void) {
    printf("--- Training statistical models from loaded source data ---\n");
    
    // Initialize/clear old models
    stat_model_128_init(&g_stat_model_128);
    stat_model_143_init(&g_stat_model_143);
    
    SBAS_TypedMessage* tm = NULL;
    int next_idx = -1;
    int last_idx = -1;

    // Find the first MT9 to start the loop
    if (tx_source_next_for_type(9, -1, &tm, &next_idx) != 0) { // Find *any* MT9
         printf("No MT9 messages found in source file. Models not trained.\n");
         return;
    }

    int initial_idx = next_idx;
    int count128 = 0;
    int count143 = 0;
    
    StatModel_MT9_Data temp_data; // Temporary lightweight struct
    
    do {
        // Use helper function from stat_model_types.h to copy data
        copy_msg_to_model_data(&tm->data.mt9, &temp_data);

        if (tm->satellite_id == 128) {
            stat_model_128_add_training_data(&g_stat_model_128, &temp_data);
            count128++;
        } else if (tm->satellite_id == 143) {
            stat_model_143_add_training_data(&g_stat_model_143, &temp_data);
            count143++;
        }
        
        last_idx = next_idx;
    } while (tx_source_next_for_type(9, last_idx, &tm, &next_idx) == 0 && next_idx != initial_idx);

    // Finalize model 128
    if (count128 > 0) {
        stat_model_128_analyze(&g_stat_model_128);
    } else {
        printf("STAT_MODEL_128: No data found, model is not active.\n");
    }
    
    // Finalize model 143
    if (count143 > 0) {
        stat_model_143_analyze(&g_stat_model_143);
    } else {
        printf("STAT_MODEL_143: No data found, model is not active.\n");
    }
    
    printf("--- Statistical model training complete ---\n");
}


/**
 * @brief Applies the global IODP to all relevant messages.
 * This function enforces the ICD rule that all dependent messages
 * must be in sync with the master IODP. It *does not* increment.
 * @param tm The message to apply the IODP to.
 */
static void apply_global_iodp(SBAS_TypedMessage *tm)
{
    if (!tm) return;
    
    // This function now *only* copies the master g_current_iodp
    // to all relevant messages to keep them in sync.
    switch (tm->message_type) {
        case 1:
            tm->data.mt1.iodp = g_current_iodp;
            break;
        case 2: case 3: case 4: case 5:
            tm->data.mt2_5.iodp = g_current_iodp;
            break;
        case 7:
            tm->data.mt7.iodp = g_current_iodp;
            break;
        case 24:
            tm->data.mt24.iodp = g_current_iodp;
            break;
        case 25:
            // MT25 IODP is per-half, but must match the global IODP
            if (tm->data.mt25.half1.raw_velocity_code == 0) {
                tm->data.mt25.half1.u.v0.iodp = g_current_iodp;
            } else {
                tm->data.mt25.half1.u.v1.iodp = g_current_iodp;
            }
            if (tm->data.mt25.half2.raw_velocity_code == 0) {
                tm->data.mt25.half2.u.v0.iodp = g_current_iodp;
            } else {
                tm->data.mt25.half2.u.v1.iodp = g_current_iodp;
            }
            break;
        case 28:
            tm->data.mt28.iodp = g_current_iodp;
            break;
    }
}

/**
 * @brief Applies the global IODF values to all relevant messages.
 * IODF (Issue of Data Fast corrections) is used in MT2-5, MT6, and MT24.
 * MT2-5 each have their own IODF that must match the corresponding entry in MT6.
 * MT6 carries iodf2, iodf3, iodf4, iodf5 for MT2, MT3, MT4, MT5 respectively.
 * MT24 has a single iodf field.
 * @param tm The message to apply the IODF to.
 */
static void apply_global_iodf(SBAS_TypedMessage *tm)
{
    if (!tm) return;
    
    switch (tm->message_type) {
        case 2:
            tm->data.mt2_5.iodf = g_current_iodf[0];
            break;
        case 3:
            tm->data.mt2_5.iodf = g_current_iodf[1];
            break;
        case 4:
            tm->data.mt2_5.iodf = g_current_iodf[2];
            break;
        case 5:
            tm->data.mt2_5.iodf = g_current_iodf[3];
            break;
        case 6:
            tm->data.mt6.iodf2 = g_current_iodf[0];
            tm->data.mt6.iodf3 = g_current_iodf[1];
            tm->data.mt6.iodf4 = g_current_iodf[2];
            tm->data.mt6.iodf5 = g_current_iodf[3];
            break;
        case 24:
            // MT24 uses a single IODF - typically linked to MT2's IODF
            tm->data.mt24.iodf = g_current_iodf[0];
            break;
    }
}

/**
 * @brief Applies the global IODI value to all relevant messages.
 * IODI (Issue of Data Ionospheric) is used in MT18 and MT26.
 * MT18 defines the IGP mask and MT26 provides delay corrections for those IGPs.
 * Both must use the same IODI to ensure consistency.
 * @param tm The message to apply the IODI to.
 */
static void apply_global_iodi(SBAS_TypedMessage *tm)
{
    if (!tm) return;
    
    switch (tm->message_type) {
        case 18:
            tm->data.mt18.iodi = g_current_iodi;
            break;
        case 26:
            tm->data.mt26.iodi = g_current_iodi;
            break;
    }
}


static void detect_and_register_increment_rule(const SBAS_TypedMessage *before, SBAS_TypedMessage *after){
    if(!before||!after) return;
    int mt = after->message_type;
    int sat_id = after->satellite_id;
    // Find or create a rule for this (SatID, MT) pair
    LiveIncrementRule *r = ensure_live_rule(sat_id, mt);
    if(!r) return; // Failed to get a rule slot
    /* This is the key: store a pointer to the message you just edited.
       This message will now be the "master" copy that holds the live data. */
    r->live_message_ptr = (SBAS_TypedMessage*)after; // Cast away const
    /* Ensure first scheduled send after this edit uses the user's exact values */
    r->suppress_once = true;
    /* Now, set all the flags based on what changed. */
    switch(mt){
        case 1: {
            if (before->data.mt1.iodp != after->data.mt1.iodp) r->mt1_inc_iodp = true;
            break;
        }
        case 2: case 3: case 4: case 5: {
            if (before->data.mt2_5.iodf != after->data.mt2_5.iodf) r->mt2_inc_iodf=true;
            if (before->data.mt2_5.iodp != after->data.mt2_5.iodp) r->mt2_inc_iodp=true;
            for(int i=0;i<13;i++){
                bool fc_changed = (before->data.mt2_5.sat_corrections[i].fast_correction != after->data.mt2_5.sat_corrections[i].fast_correction);
                bool u_changed  = (before->data.mt2_5.sat_corrections[i].udrei != after->data.mt2_5.sat_corrections[i].udrei);
                if (fc_changed) r->mt2_fc_mask[i]=true;
                if (u_changed)  r->mt2_udrei_mask[i]=true;
                /* If either field changed for this entry, increment both so the whole entry advances */
                if (fc_changed || u_changed) { r->mt2_fc_mask[i]=true; r->mt2_udrei_mask[i]=true; }
            }
            /* If any entry or header changed, enable all-entries increment to avoid partial blocks */
            if (r->mt2_inc_iodf || r->mt2_inc_iodp) r->mt2_inc_all_entries = true;
            for (int i=0;i<13;i++){ if (r->mt2_fc_mask[i] || r->mt2_udrei_mask[i]) { r->mt2_inc_all_entries = true; break; } }
            break; }
        case 6: {
            if (before->data.mt6.iodf2!=after->data.mt6.iodf2) r->mt6_inc_iodf2=true;
            if (before->data.mt6.iodf3!=after->data.mt6.iodf3) r->mt6_inc_iodf3=true;
            if (before->data.mt6.iodf4!=after->data.mt6.iodf4) r->mt6_inc_iodf4=true;
            if (before->data.mt6.iodf5!=after->data.mt6.iodf5) r->mt6_inc_iodf5=true;
            for(int i=0;i<51;i++) if (before->data.mt6.udrei[i]!=after->data.mt6.udrei[i]){ r->mt6_udrei_mask[i]=true; }
            break; }
        case 7: {
            if (before->data.mt7.t_lat!=after->data.mt7.t_lat) r->mt7_inc_tlat=true;
            if (before->data.mt7.iodp!=after->data.mt7.iodp) r->mt7_inc_iodp=true;
            for(int i=0;i<51;i++) if (before->data.mt7.udre_ai[i]!=after->data.mt7.udre_ai[i]){ r->mt7_udreai_mask[i]=true; }
            break; }
        case 9: {
            if (before->data.mt9.t0!=after->data.mt9.t0) r->mt9_inc_t0=true;
            if (before->data.mt9.ura!=after->data.mt9.ura) r->mt9_inc_ura=true;
            if (before->data.mt9.xg_ecef!=after->data.mt9.xg_ecef || before->data.mt9.yg_ecef!=after->data.mt9.yg_ecef || before->data.mt9.zg_ecef!=after->data.mt9.zg_ecef) r->mt9_inc_pos=true;
            if (before->data.mt9.xg_rate!=after->data.mt9.xg_rate || before->data.mt9.yg_rate!=after->data.mt9.yg_rate || before->data.mt9.zg_rate!=after->data.mt9.zg_rate) r->mt9_inc_rate=true;
            if (before->data.mt9.xg_accel!=after->data.mt9.xg_accel || before->data.mt9.yg_accel!=after->data.mt9.yg_accel || before->data.mt9.zg_accel!=after->data.mt9.zg_accel) r->mt9_inc_accel=true;
            if (before->data.mt9.ag_f0!=after->data.mt9.ag_f0) r->mt9_inc_agf0=true;
            if (before->data.mt9.ag_f1!=after->data.mt9.ag_f1) r->mt9_inc_agf1=true;
            break; }
        case 10: {
            /* Many doubles; if any changed, increment all edited doubles together */
            if (memcmp(&before->data.mt10, &after->data.mt10, sizeof(after->data.mt10))!=0) r->mt10_inc_all=true;
            if (before->data.mt10.rss_udre!=after->data.mt10.rss_udre) r->mt10_toggle_rss_udre=true;
            if (before->data.mt10.rss_iono!=after->data.mt10.rss_iono) r->mt10_toggle_rss_iono=true;
            break; }
        case 17: {
            if (before->data.mt17.t0!=after->data.mt17.t0) r->mt17_inc_t0=true;
            for(int gi=0;gi<3;gi++){
                const SBAS_MT17_GeoBlock *gb=&before->data.mt17.geo[gi], *ga=&after->data.mt17.geo[gi];
                if (memcmp(gb, ga, sizeof(*gb))!=0){
                    r->mt17_geo_idx=gi;
                    if (gb->data_id!=ga->data_id) r->mt17_inc_geo_data_id=true;
                    if (gb->prn_number!=ga->prn_number) r->mt17_inc_geo_prn=true;
                    if (gb->x_g!=ga->x_g||gb->y_g!=ga->y_g||gb->z_g!=ga->z_g) r->mt17_inc_geo_pos=true;
                    if (gb->xg_dot!=ga->xg_dot||gb->yg_dot!=ga->yg_dot||gb->zg_dot!=ga->zg_dot) r->mt17_inc_geo_rate=true;
                    if (gb->health_and_status!=ga->health_and_status) r->mt17_inc_geo_hs=true;
                    break;
                }
            }
            break; }
        case 18: {
            if (before->data.mt18.number_of_bands!=after->data.mt18.number_of_bands) r->mt18_inc_bands=true;
            if (before->data.mt18.band_number!=after->data.mt18.band_number) r->mt18_inc_bandnum=true;
            if (before->data.mt18.iodi!=after->data.mt18.iodi) r->mt18_inc_iodi=true;
            break; }
        case 24: {
            if (before->data.mt24.iodp!=after->data.mt24.iodp) r->mt24_inc_iodp=true;
            if (before->data.mt24.block_id!=after->data.mt24.block_id) r->mt24_inc_block_id=true;
            if (before->data.mt24.iodf!=after->data.mt24.iodf) r->mt24_inc_iodf=true;
            for(int i=0;i<6;i++){
                if (before->data.mt24.fast_corrections[i]!=after->data.mt24.fast_corrections[i]){ r->mt24_fc_mask[i]=true; }
                if (before->data.mt24.udreis[i]!=after->data.mt24.udreis[i]){ r->mt24_udrei_mask[i]=true; }
            }
            break; }
        case 25: {
            /* Track which half and which fields moved */
            int half = 1; bool v0 = true; bool set = false;
            if (memcmp(&before->data.mt25.half1, &after->data.mt25.half1, sizeof(after->data.mt25.half1)) != 0) {
                half = 1;
                v0 = (after->data.mt25.half1.raw_velocity_code == 0);
                set = true;
            }
            if (!set && memcmp(&before->data.mt25.half2, &after->data.mt25.half2, sizeof(after->data.mt25.half2)) != 0) {
                half = 2;
                v0 = (after->data.mt25.half2.raw_velocity_code == 0);
                set = true;
            }
            if (set) {
                r->mt25_half = half;
                r->mt25_v0 = v0;
                if (v0) {
                    const SBAS_MT25_Half_Vel0 *b = (half == 1 ? &before->data.mt25.half1.u.v0 : &before->data.mt25.half2.u.v0);
                    const SBAS_MT25_Half_Vel0 *a = (half == 1 ? &after->data.mt25.half1.u.v0 : &after->data.mt25.half2.u.v0);
                    if (b->dx_1 != a->dx_1) r->mt25_inc_dx1 = true;
                    if (b->dy_1 != a->dy_1) r->mt25_inc_dy1 = true;
                    if (b->dz_1 != a->dz_1) r->mt25_inc_dz1 = true;
                    if (b->da_f0_1 != a->da_f0_1) r->mt25_inc_da_f0_1 = true;
                    if (b->dx_2 != a->dx_2) r->mt25_inc_dx2 = true;
                    if (b->dy_2 != a->dy_2) r->mt25_inc_dy2 = true;
                    if (b->dz_2 != a->dz_2) r->mt25_inc_dz2 = true;
                    if (b->da_f0_2 != a->da_f0_2) r->mt25_inc_da_f0_2 = true;
                    if (b->iodp != a->iodp) r->mt25_inc_iodp = true;
                } else {
                    const SBAS_MT25_Half_Vel1 *b = (half == 1 ? &before->data.mt25.half1.u.v1 : &before->data.mt25.half2.u.v1);
                    const SBAS_MT25_Half_Vel1 *a = (half == 1 ? &after->data.mt25.half1.u.v1 : &after->data.mt25.half2.u.v1);
                    if (b->dx != a->dx) r->mt25_inc_dx = true;
                    if (b->dy != a->dy) r->mt25_inc_dy = true;
                    if (b->dz != a->dz) r->mt25_inc_dz = true;
                    if (b->da_f0 != a->da_f0) r->mt25_inc_da_f0 = true;
                    if (b->dx_rate != a->dx_rate) r->mt25_inc_dx_rate = true;
                    if (b->dy_rate != a->dy_rate) r->mt25_inc_dy_rate = true;
                    if (b->dz_rate != a->dz_rate) r->mt25_inc_dz_rate = true;
                    if (b->da_f1 != a->da_f1) r->mt25_inc_da_f1 = true;
                    if (b->t0 != a->t0) r->mt25_inc_t0 = true;
                    if (b->iodp != a->iodp) r->mt25_inc_iodp = true;
                }
            }
            break; }
        case 26: {
            if (before->data.mt26.band_number!=after->data.mt26.band_number) r->mt26_inc_bandnum=true;
            if (before->data.mt26.block_id!=after->data.mt26.block_id) r->mt26_inc_block_id=true;
            if (before->data.mt26.iodi!=after->data.mt26.iodi) r->mt26_inc_iodi=true;
            for(int i=0;i<SBAS_MT26_IGP_COUNT;i++){
                if (before->data.mt26.igp_vertical_delay[i]!=after->data.mt26.igp_vertical_delay[i]){ r->mt26_delay_mask[i]=true; }
                if (before->data.mt26.givei[i]!=after->data.mt26.givei[i]){ r->mt26_givei_mask[i]=true; }
            }
            break; }
        case 28: {
            if (before->data.mt28.iodp!=after->data.mt28.iodp) r->mt28_inc_iodp=true;
            for(int i=0;i<2;i++){
                const SBAS_MT28_SatBlock *bb=&before->data.mt28.sat[i], *ba=&after->data.mt28.sat[i];
                if (memcmp(bb,ba,sizeof(*bb))!=0){ r->mt28_block_idx=i; if (bb->prn_mask!=ba->prn_mask) r->mt28_inc_prn_mask=true; if (bb->scale_exponent!=ba->scale_exponent) r->mt28_inc_scale_exp=true; if (bb->E1_1!=ba->E1_1||bb->E2_2!=ba->E2_2||bb->E3_3!=ba->E3_3||bb->E4_4!=ba->E4_4||bb->E1_2!=ba->E1_2||bb->E1_3!=ba->E1_3||bb->E1_4!=ba->E1_4||bb->E2_3!=ba->E2_3||bb->E2_4!=ba->E2_4||bb->E3_4!=ba->E3_4) r->mt28_inc_matrix=true; break; }
            }
            break; }
        default: break;
    }
}


/* Apply increments just before encoding (called by scheduler). Keeps values in-safe ranges. */
static void apply_increment_if_rule(SBAS_TypedMessage *tm){
    if(!tm) return;

    // Find the master rule for this message's (SatID, MT)
    LiveIncrementRule *r = find_live_rule(tm->satellite_id, tm->message_type);
    
    if(!r || !r->live_message_ptr) {
        // No rule, or rule is invalid. Send the original file data.
        return;
    }


    /* Check if we should increment the 'live' data *before* we copy it. */
    
    if (r->suppress_once) {
        // This is the first transmission. Don't increment.
        // Just set the flag for next time.
        r->suppress_once = false;
    } else {
        // This is the 2nd transmission or later. Increment the master copy *now*.
        int mt = tm->message_type;
        
        /* NOTE: All logic now applies to 'r->live_message_ptr->data' */
        switch(mt){
            case 1: 
                // if (r->mt1_inc_iodp){ r->live_message_ptr->data.mt1.iodp = (uint8_t)((r->live_message_ptr->data.mt1.iodp + 1) & 3); } 
                // break;
            case 2: case 3: case 4: case 5: {
                if (r->mt2_inc_iodf) r->live_message_ptr->data.mt2_5.iodf = (uint8_t)((r->live_message_ptr->data.mt2_5.iodf + 1) & 3);
                // if (r->mt2_inc_iodp) r->live_message_ptr->data.mt2_5.iodp = (uint8_t)((r->live_message_ptr->data.mt2_5.iodp + 1) & 3);
                for(int i=0;i<13;i++){
                    bool inc_fc = r->mt2_inc_all_entries || r->mt2_fc_mask[i];
                    bool inc_ud = r->mt2_inc_all_entries || r->mt2_udrei_mask[i];
                    if (inc_fc){ int v=r->live_message_ptr->data.mt2_5.sat_corrections[i].fast_correction; v=clamp_int(v+1,-2047,2047); r->live_message_ptr->data.mt2_5.sat_corrections[i].fast_correction=(int16_t)v; }
                    if (inc_ud){ unsigned v=r->live_message_ptr->data.mt2_5.sat_corrections[i].udrei; v=clamp_uint(v+1,0,15); r->live_message_ptr->data.mt2_5.sat_corrections[i].udrei=(uint8_t)v; }
                }
                break; }
            case 6: {
                if (r->mt6_inc_iodf2) r->live_message_ptr->data.mt6.iodf2=(uint8_t)((r->live_message_ptr->data.mt6.iodf2+1)&3);
                if (r->mt6_inc_iodf3) r->live_message_ptr->data.mt6.iodf3=(uint8_t)((r->live_message_ptr->data.mt6.iodf3+1)&3);
                if (r->mt6_inc_iodf4) r->live_message_ptr->data.mt6.iodf4=(uint8_t)((r->live_message_ptr->data.mt6.iodf4+1)&3);
                if (r->mt6_inc_iodf5) r->live_message_ptr->data.mt6.iodf5=(uint8_t)((r->live_message_ptr->data.mt6.iodf5+1)&3);
                for(int i=0;i<51;i++) if (r->mt6_udrei_mask[i]){ unsigned v=r->live_message_ptr->data.mt6.udrei[i]; v=clamp_uint(v+1,0,15); r->live_message_ptr->data.mt6.udrei[i]=(uint8_t)v; }
                break; }
            case 7: {
                if (r->mt7_inc_tlat){ unsigned v=r->live_message_ptr->data.mt7.t_lat; v=clamp_uint(v+1,0,255); r->live_message_ptr->data.mt7.t_lat=(uint8_t)v; }
                // if (r->mt7_inc_iodp) r->live_message_ptr->data.mt7.iodp=(uint8_t)((r->live_message_ptr->data.mt7.iodp+1)&3);
                for(int i=0;i<51;i++) if (r->mt7_udreai_mask[i]){ unsigned v=r->live_message_ptr->data.mt7.udre_ai[i]; v=clamp_uint(v+1,0,15); r->live_message_ptr->data.mt7.udre_ai[i]=(uint8_t)v; }
                break; }
            case 9: {
                const double LSB_T0 = 16.0; const unsigned URA_MAX = 15;
                const double XY_LSB = 0.08; const double Z_LSB = 0.4;
                const double XY_MIN = -536870912.0 * XY_LSB; const double XY_MAX = (536870912.0 - 1.0) * XY_LSB;
                const double Z_MIN  = -16777216.0 * Z_LSB;   const double Z_MAX  = (16777216.0 - 1.0) * Z_LSB;
                const double XR_YR_LSB = 0.000625; const double ZR_LSB = 0.004;
                const double XR_YR_MIN = -65536.0 * XR_YR_LSB; const double XR_YR_MAX = (65536.0 - 1.0) * XR_YR_LSB;
                const double ZR_MIN    = -131072.0 * ZR_LSB;  const double ZR_MAX    = (131072.0 - 1.0) * ZR_LSB;
                const double XA_YA_LSB = 0.0000125; const double ZA_LSB = 0.00000625;
                const double XA_YA_MIN = -512.0 * XA_YA_LSB; const double XA_YA_MAX = (512.0 - 1.0) * XA_YA_LSB;
                const double ZA_MIN    = -512.0 * ZA_LSB; const double ZA_MAX    = (512.0 - 1.0) * ZA_LSB;
                const double AGF0_LSB = 4.6566128730773926e-10; const double AGF1_LSB = 9.0949470177292824e-13;
                const double AGF0_MIN = -2048.0 * AGF0_LSB; const double AGF0_MAX = (2048.0 - 1.0) * AGF0_LSB;
                const double AGF1_MIN = -128.0 * AGF1_LSB; const double AGF1_MAX = (128.0 - 1.0) * AGF1_LSB;

                if (r->mt9_inc_t0) {
                    const double T0_MIN = 0.0; const double T0_MAX = (8192.0 - 1.0) * LSB_T0;
                    r->live_message_ptr->data.mt9.t0 = clamp_double(r->live_message_ptr->data.mt9.t0 + LSB_T0, T0_MIN, T0_MAX);
                }
                if (r->mt9_inc_ura){ unsigned v=r->live_message_ptr->data.mt9.ura; v=clamp_uint(v+1,0,URA_MAX); r->live_message_ptr->data.mt9.ura=(uint8_t)v; }
                if (r->mt9_inc_pos){
                    r->live_message_ptr->data.mt9.xg_ecef = clamp_double(r->live_message_ptr->data.mt9.xg_ecef + XY_LSB, XY_MIN, XY_MAX);
                    r->live_message_ptr->data.mt9.yg_ecef = clamp_double(r->live_message_ptr->data.mt9.yg_ecef + XY_LSB, XY_MIN, XY_MAX);
                    r->live_message_ptr->data.mt9.zg_ecef = clamp_double(r->live_message_ptr->data.mt9.zg_ecef + Z_LSB,  Z_MIN,  Z_MAX);
                }
                if (r->mt9_inc_rate){
                    r->live_message_ptr->data.mt9.xg_rate = clamp_double(r->live_message_ptr->data.mt9.xg_rate + XR_YR_LSB, XR_YR_MIN, XR_YR_MAX);
                    r->live_message_ptr->data.mt9.yg_rate = clamp_double(r->live_message_ptr->data.mt9.yg_rate + XR_YR_LSB, XR_YR_MIN, XR_YR_MAX);
                    r->live_message_ptr->data.mt9.zg_rate = clamp_double(r->live_message_ptr->data.mt9.zg_rate + ZR_LSB,    ZR_MIN,    ZR_MAX);
                }
                if (r->mt9_inc_accel){
                    r->live_message_ptr->data.mt9.xg_accel = clamp_double(r->live_message_ptr->data.mt9.xg_accel + XA_YA_LSB, XA_YA_MIN, XA_YA_MAX);
                    r->live_message_ptr->data.mt9.yg_accel = clamp_double(r->live_message_ptr->data.mt9.yg_accel + XA_YA_LSB, XA_YA_MIN, XA_YA_MAX);
                    r->live_message_ptr->data.mt9.zg_accel = clamp_double(r->live_message_ptr->data.mt9.zg_accel + ZA_LSB,    ZA_MIN,    ZA_MAX);
                }
                if (r->mt9_inc_agf0) r->live_message_ptr->data.mt9.ag_f0 = clamp_double(r->live_message_ptr->data.mt9.ag_f0 + AGF0_LSB, AGF0_MIN, AGF0_MAX);
                if (r->mt9_inc_agf1) r->live_message_ptr->data.mt9.ag_f1 = clamp_double(r->live_message_ptr->data.mt9.ag_f1 + AGF1_LSB, AGF1_MIN, AGF1_MAX);
                break; }
            case 10: {
                if (r->mt10_inc_all){
                    struct { double *p; double step; } incs[] = {
                        { &r->live_message_ptr->data.mt10.B_rrc,        0.002    },
                        { &r->live_message_ptr->data.mt10.C_ltc_lsb,    0.002    },
                        { &r->live_message_ptr->data.mt10.C_ltc_v1,     0.00005  },
                        { &r->live_message_ptr->data.mt10.I_ltc_v1,     1.0      },
                        { &r->live_message_ptr->data.mt10.C_ltc_v0,     0.002    },
                        { &r->live_message_ptr->data.mt10.I_ltc_v0,     1.0      },
                        { &r->live_message_ptr->data.mt10.C_geo_lsb,    0.0005   },
                        { &r->live_message_ptr->data.mt10.C_geo_v,      0.00005  },
                        { &r->live_message_ptr->data.mt10.I_geo,        1.0      },
                        { &r->live_message_ptr->data.mt10.C_er,         0.5      },
                        { &r->live_message_ptr->data.mt10.C_iono_step,  0.001    },
                        { &r->live_message_ptr->data.mt10.I_iono,       1.0      },
                        { &r->live_message_ptr->data.mt10.C_iono_ramp,  0.000005 },
                        { &r->live_message_ptr->data.mt10.C_covariance, 0.1      }
                    };
                    for (size_t i = 0; i < sizeof(incs)/sizeof(incs[0]); ++i) {
                        *(incs[i].p) += incs[i].step;
                    }
                }
                if (r->mt10_toggle_rss_udre) r->live_message_ptr->data.mt10.rss_udre = (uint8_t)(!r->live_message_ptr->data.mt10.rss_udre);
                if (r->mt10_toggle_rss_iono) r->live_message_ptr->data.mt10.rss_iono = (uint8_t)(!r->live_message_ptr->data.mt10.rss_iono);
                break; }
            case 17: {
                if (r->mt17_inc_t0) r->live_message_ptr->data.mt17.t0 += 1.0;
                int gi=r->mt17_geo_idx; if (gi>=0 && gi<3){ SBAS_MT17_GeoBlock *g=&r->live_message_ptr->data.mt17.geo[gi];
                    if (r->mt17_inc_geo_pos){
                        g->x_g += 2600.0;
                        g->y_g += 2600.0;
                        g->z_g += 26000.0;
                    }
                    if (r->mt17_inc_geo_rate){
                        g->xg_dot += 10.0;
                        g->yg_dot += 10.0;
                        g->zg_dot += 60.0;
                    }
                    if (r->mt17_inc_geo_data_id){ g->data_id = (uint8_t)((g->data_id + 1) & 0x03); }
                    if (r->mt17_inc_geo_prn){ g->prn_number = (uint8_t)((g->prn_number + 1) & 0xFF); }
                    if (r->mt17_inc_geo_hs){ g->health_and_status=(uint8_t)((g->health_and_status+1)&0xFF); }
                }
                break; }
            case 18: {
                if (r->mt18_inc_bands) r->live_message_ptr->data.mt18.number_of_bands = (uint8_t)((r->live_message_ptr->data.mt18.number_of_bands+1)&0xFF);
                if (r->mt18_inc_bandnum) r->live_message_ptr->data.mt18.band_number = (uint8_t)((r->live_message_ptr->data.mt18.band_number+1)&0xFF);
                if (r->mt18_inc_iodi) r->live_message_ptr->data.mt18.iodi = (uint8_t)((r->live_message_ptr->data.mt18.iodi+1)&3);
                break; }
            case 24: {
                // if (r->mt24_inc_iodp) r->live_message_ptr->data.mt24.iodp=(uint8_t)((r->live_message_ptr->data.mt24.iodp+1)&3);
                if (r->mt24_inc_block_id) r->live_message_ptr->data.mt24.block_id=(uint8_t)((r->live_message_ptr->data.mt24.block_id+1)&3);
                if (r->mt24_inc_iodf) r->live_message_ptr->data.mt24.iodf=(uint8_t)((r->live_message_ptr->data.mt24.iodf+1)&3);
                for(int i=0;i<6;i++){
                    if (r->mt24_fc_mask[i]){ int v=r->live_message_ptr->data.mt24.fast_corrections[i]; v=clamp_int(v+1,-2047,2047); r->live_message_ptr->data.mt24.fast_corrections[i]=(int16_t)v; }
                    if (r->mt24_udrei_mask[i]){ unsigned v=r->live_message_ptr->data.mt24.udreis[i]; v=clamp_uint(v+1,0,15); r->live_message_ptr->data.mt24.udreis[i]=(uint8_t)v; }
                }
                break; }
            case 25: {
                if (r->mt25_half==1 || r->mt25_half==2){
                    SBAS_MT25_Half *h=(r->mt25_half==1?&r->live_message_ptr->data.mt25.half1:&r->live_message_ptr->data.mt25.half2);
                    if (r->mt25_v0){
                        const double DX_LSB = 0.125;
                        const double AF0_LSB = 4.6566128730773926e-10;
                        const double DX_MIN = -256.0 * DX_LSB;
                        const double DX_MAX = (256.0 - 1.0) * DX_LSB;
                        const double AF0_MIN = -512.0 * AF0_LSB;
                        const double AF0_MAX = (512.0 - 1.0) * AF0_LSB;
                        SBAS_MT25_Half_Vel0 *v=&h->u.v0;
                        if (r->mt25_inc_dx1) v->dx_1 = clamp_double(v->dx_1 + DX_LSB, DX_MIN, DX_MAX);
                        if (r->mt25_inc_dy1) v->dy_1 = clamp_double(v->dy_1 + DX_LSB, DX_MIN, DX_MAX);
                        if (r->mt25_inc_dz1) v->dz_1 = clamp_double(v->dz_1 + DX_LSB, DX_MIN, DX_MAX);
                        if (r->mt25_inc_da_f0_1) v->da_f0_1 = clamp_double(v->da_f0_1 + AF0_LSB, AF0_MIN, AF0_MAX);
                        if (r->mt25_inc_dx2) v->dx_2 = clamp_double(v->dx_2 + DX_LSB, DX_MIN, DX_MAX);
                        if (r->mt25_inc_dy2) v->dy_2 = clamp_double(v->dy_2 + DX_LSB, DX_MIN, DX_MAX);
                        if (r->mt25_inc_dz2) v->dz_2 = clamp_double(v->dz_2 + DX_LSB, DX_MIN, DX_MAX);
                        if (r->mt25_inc_da_f0_2) v->da_f0_2 = clamp_double(v->da_f0_2 + AF0_LSB, AF0_MIN, AF0_MAX);
                        // if (r->mt25_inc_iodp) v->iodp = (uint8_t)((v->iodp + 1) & 0x03);
                    } else {
                        const double D_LSB = 0.125;
                        const double AF0_LSB = 4.6566128730773926e-10;
                        const double DR_LSB = 0.00048828125;
                        const double AF1_LSB = 1.8189894035458565e-12;
                        const double D_MIN = -1024.0 * D_LSB;
                        const double D_MAX = (1024.0 - 1.0) * D_LSB;
                        const double AF0_MIN = -1024.0 * AF0_LSB;
                        const double AF0_MAX = (1024.0 - 1.0) * AF0_LSB;
                        const double DR_MIN = -128.0 * DR_LSB;
                        const double DR_MAX = (128.0 - 1.0) * DR_LSB;
                        const double AF1_MIN = -128.0 * AF1_LSB;
                        const double AF1_MAX = (128.0 - 1.0) * AF1_LSB;
                        const double T0_MIN = 0.0;
                        const double T0_LSB = 16.0;
                        const double T0_MAX = (8192.0 - 1.0) * T0_LSB;
                        SBAS_MT25_Half_Vel1 *v=&h->u.v1;
                        if (r->mt25_inc_dx) v->dx = clamp_double(v->dx + D_LSB, D_MIN, D_MAX);
                        if (r->mt25_inc_dy) v->dy = clamp_double(v->dy + D_LSB, D_MIN, D_MAX);
                        if (r->mt25_inc_dz) v->dz = clamp_double(v->dz + D_LSB, D_MIN, D_MAX);
                        if (r->mt25_inc_da_f0) v->da_f0 = clamp_double(v->da_f0 + AF0_LSB, AF0_MIN, AF0_MAX);
                        if (r->mt25_inc_dx_rate) v->dx_rate = clamp_double(v->dx_rate + DR_LSB, DR_MIN, DR_MAX);
                        if (r->mt25_inc_dy_rate) v->dy_rate = clamp_double(v->dy_rate + DR_LSB, DR_MIN, DR_MAX);
                        if (r->mt25_inc_dz_rate) v->dz_rate = clamp_double(v->dz_rate + DR_LSB, DR_MIN, DR_MAX);
                        if (r->mt25_inc_da_f1) v->da_f1 = clamp_double(v->da_f1 + AF1_LSB, AF1_MIN, AF1_MAX);
                        if (r->mt25_inc_t0) v->t0 = clamp_double(v->t0 + T0_LSB, T0_MIN, T0_MAX);
                        // if (r->mt25_inc_iodp) v->iodp = (uint8_t)((v->iodp + 1) & 0x03);
                    }
                }
                break; }
            case 26: {
                if (r->mt26_inc_bandnum) r->live_message_ptr->data.mt26.band_number=(uint8_t)((r->live_message_ptr->data.mt26.band_number+1)&0xFF);
                if (r->mt26_inc_block_id) r->live_message_ptr->data.mt26.block_id=(uint8_t)((r->live_message_ptr->data.mt26.block_id+1)&0xFF);
                if (r->mt26_inc_iodi) r->live_message_ptr->data.mt26.iodi=(uint8_t)((r->live_message_ptr->data.mt26.iodi+1)&3);
                for(int i=0;i<SBAS_MT26_IGP_COUNT;i++){
                    if (r->mt26_delay_mask[i]){ int v=r->live_message_ptr->data.mt26.igp_vertical_delay[i]; v=clamp_int(v+1,0,511); r->live_message_ptr->data.mt26.igp_vertical_delay[i]=(uint16_t)v; }
                    if (r->mt26_givei_mask[i]){ unsigned v=r->live_message_ptr->data.mt26.givei[i]; v=clamp_uint(v+1,0,15); r->live_message_ptr->data.mt26.givei[i]=(uint8_t)v; }
                }
                break; }
            case 28: {
                // if (r->mt28_inc_iodp) r->live_message_ptr->data.mt28.iodp=(uint8_t)((r->live_message_ptr->data.mt28.iodp+1)&3);
                int bi=r->mt28_block_idx; if (bi>=0 && bi<2){ SBAS_MT28_SatBlock *b=&r->live_message_ptr->data.mt28.sat[bi];
                    if (r->mt28_inc_prn_mask){ unsigned v=b->prn_mask; v=clamp_uint(v+1,0,255); b->prn_mask=(uint8_t)v; }
                    if (r->mt28_inc_scale_exp){ unsigned v=b->scale_exponent; v=(v+1)&0x0F; b->scale_exponent=(uint8_t)v; }
                    if (r->mt28_inc_matrix){ b->E1_1++; b->E2_2++; b->E3_3++; b->E4_4++; b->E1_2++; b->E1_3++; b->E1_4++; b->E2_3++; b->E2_4++; b->E3_4++; }
                }
                break; }
            default: break;
        }
    }

    /* Now, *after* potential increment, OVERWRITE the outgoing message's 
       data with our 'live' data. This will be the user-value on tx 1,
       and user-value+1 on tx 2, etc. */
    tm->data = r->live_message_ptr->data;
}

// Maximum update interval per message type (seconds) - simplified subset from RTCA DO-229D Table A-25
static const int max_update_interval[64] = {
    /* 0 */ 6,    // Type 0 (not used)
    /* 1 */ 120,  // PRN Mask
    /* 2 */ 6,    // Fast corrections / UDREI
    /* 3 */ 6,
    /* 4 */ 6,
    /* 5 */ 6,
    /* 6 */ 6,
    /* 7 */ 120,  // Fast correction degradation
    /* 8 */ 120,
    /* 9 */ 120,  // GEO Navigation
    /*10 */ 120,  // Degradation parameters
    /*11 */ 120,
    /*12 */ 300,  // UTC timing
    /*13 */ 300,
    /*14 */ 300,
    /*15 */ 300,
    /*16 */ 300,
    /*17 */ 300,  // Almanac
    /*18 */ 300,  // Ionospheric grid mask
    /*19 */ 300,
    /*20 */ 300,
    /*21 */ 300,
    /*22 */ 300,
    /*23 */ 300,
    /*24 */ 6,    // Long term corrections also allowed in 6s group
    /*25 */ 120,  // Long term corrections update interval
    /*26 */ 300,  // Ionospheric corrections
    /*27 */ 300,  // Service level
    /*28 */ 120,  // Clock-Ephemeris covariance
    /*29 */ 300,
    /*30 */ 300,
    /*31 */ 300,
    /*32..63 */ 3600
};

// TCP server handled by tx_server module

/* ================= Source-provider based (no internal array) ================= */

/* Provider-only binding: handled inside tx_source. */

/* Encode a typed message to hex; returns 0 on success */
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

/* Prompt to edit parameters for a single message type; loops until user chooses back */
static int generalized_edit_prompt(SBAS_TypedMessage *tm) {
    if (!tm) return -1;
    int mt = tm->message_type;
    unsigned sat = (unsigned)tm->satellite_id;
    printf("Editing MT%d parameters (sat %u). Use 0 or 'b' to go back.\n", mt, sat);

    for (;;) {
        char line[128];
        int choice = 0;
        switch (mt) {
            case 0: {
                printf("1) set bit in test_data (0-211)\n2) clear bit in test_data (0-211)\n3) zero all payload bits\n0/b) back\n> ");
                if (!fgets(line,sizeof(line),stdin)) { return -1; }
                if (line[0]=='b'||line[0]=='B'||line[0]=='0') { return 0; }
                choice = (int)strtol(line,NULL,10);
                if (choice == 1 || choice == 2) {
                    /* Stay in this option to set/clear multiple bits */
                    for(;;){
                        printf("bit index (0-211) or b to back: "); if (!fgets(line,sizeof(line),stdin)) return -1; if (line[0]=='b'||line[0]=='B') break; int bi=(int)strtol(line,NULL,10);
                        if (bi>=0 && bi<212){ int byte=bi/8, bit=7-(bi%8); if (choice==1) { tm->data.mt0.test_data[byte] |= (uint8_t)(1u<<bit); printf("Bit %d set\n", bi); } else { tm->data.mt0.test_data[byte] &= (uint8_t)~(1u<<bit); printf("Bit %d cleared\n", bi); }} }
                }
                else if (choice == 3) { memset(tm->data.mt0.test_data,0,sizeof(tm->data.mt0.test_data)); printf("All payload bits zeroed\n"); }
                continue; }
            case 1: {
                printf("1) toggle PRN in mask (1-210)\n2) set IODP (0-3)\n0/b) back\n> ");
                if (!fgets(line,sizeof(line),stdin)) { return -1; }
                if (line[0]=='b'||line[0]=='B'||line[0]=='0') { return 0; }
                choice=(int)strtol(line,NULL,10);
                if (choice==1){
                    for(;;){ printf("PRN (1-210) or b to back: "); if (!fgets(line,128,stdin)) return -1; if (line[0]=='b'||line[0]=='B') break; int prn=(int)strtol(line,NULL,10); if (prn>=1&&prn<=210){ int b=prn-1; int by=b/8, bi=7-(b%8); tm->data.mt1.prn_mask[by] ^= (uint8_t)(1u<<bi); printf("PRN %d toggled in mask\n", prn);} }
                }
                else if (choice==2){ printf("IODP: "); fgets(line,128,stdin); int v=(int)strtol(line,NULL,10); tm->data.mt1.iodp=(uint8_t)(v&3); printf("IODP set to %d\n", tm->data.mt1.iodp);} 
                continue; }
            case 2: case 3: case 4: case 5: {
                printf("1) set IODF (0-3)\n2) set IODP (0-3)\n3) edit sat entry (0-12): fast_correction & UDREI\n0/b) back\n> ");
                if (!fgets(line,sizeof(line),stdin)) { return -1; }
                if (line[0]=='b'||line[0]=='B'||line[0]=='0') { return 0; }
                choice=(int)strtol(line,NULL,10);
                if (choice==1){ printf("IODF: "); fgets(line,128,stdin); int v=(int)strtol(line,NULL,10); tm->data.mt2_5.iodf=(uint8_t)(v&3); printf("IODF set to %d\n", tm->data.mt2_5.iodf);} 
                else if (choice==2){ printf("IODP: "); fgets(line,128,stdin); int v=(int)strtol(line,NULL,10); tm->data.mt2_5.iodp=(uint8_t)(v&3); printf("IODP set to %d\n", tm->data.mt2_5.iodp);} 
                else if (choice==3){
                    for(;;){ printf("entry index (0-12) or b to back: "); if(!fgets(line,128,stdin)) return -1; if (line[0]=='b'||line[0]=='B') break; int e=(int)strtol(line,NULL,10); if (e>=0&&e<13){ printf("fast_correction (-2047..2047): "); fgets(line,128,stdin); int fc=(int)strtol(line,NULL,10); tm->data.mt2_5.sat_corrections[e].fast_correction=(int16_t)fc; printf("fast_correction set to %d for entry %d\n", fc, e); printf("udrei (0-15): "); fgets(line,128,stdin); int u=(int)strtol(line,NULL,10); tm->data.mt2_5.sat_corrections[e].udrei=(uint8_t)u; printf("UDREI set to %d for entry %d\n", u, e); }}
                }
                continue; }
            case 6: {
                printf("1) set IODF2 (0-3)  2) IODF3  3) IODF4  4) IODF5  5) set UDREI[0-50]\n0/b) back\n> ");
                if (!fgets(line,sizeof(line),stdin)) { return -1; }
                if (line[0]=='b'||line[0]=='B'||line[0]=='0') { return 0; }
                choice=(int)strtol(line,NULL,10);
                if (choice>=1 && choice<=4){ printf("value (0-3): "); fgets(line,128,stdin); int v=(int)strtol(line,NULL,10); if (choice==1) { tm->data.mt6.iodf2=(uint8_t)(v&3); printf("IODF2 set to %d\n", tm->data.mt6.iodf2); } else if (choice==2) { tm->data.mt6.iodf3=(uint8_t)(v&3); printf("IODF3 set to %d\n", tm->data.mt6.iodf3); } else if (choice==3) { tm->data.mt6.iodf4=(uint8_t)(v&3); printf("IODF4 set to %d\n", tm->data.mt6.iodf4); } else { tm->data.mt6.iodf5=(uint8_t)(v&3); printf("IODF5 set to %d\n", tm->data.mt6.iodf5); }} 
                else if (choice==5){ for(;;){ printf("index (0-50) or b to back: "); if(!fgets(line,128,stdin)) return -1; if(line[0]=='b'||line[0]=='B') break; int i=(int)strtol(line,NULL,10); if (i>=0&&i<51){ printf("udrei (0-15): "); fgets(line,128,stdin); int u=(int)strtol(line,NULL,10); tm->data.mt6.udrei[i]=(uint8_t)u; printf("UDREI[%d] set to %d\n", i, u); }} } 
                continue; }
            case 9: {
                printf("1) t0  2) URA  3) X  4) Y  5) Z  6) Vx  7) Vy  8) Vz  9) Ax  10) Ay  11) Az  12) aGf0  13) aGf1  0/b) back\n> ");
                if (!fgets(line,sizeof(line),stdin)) { return -1; }
                if (line[0]=='b'||line[0]=='B'||line[0]=='0') { return 0; }
                choice = (int)strtol(line, NULL, 10);
                double dv; int iv;
                switch (choice){
                    case 1: printf("t0: "); fgets(line,128,stdin); dv=strtod(line,NULL); tm->data.mt9.t0=dv; printf("t0 set to %.6f\n", tm->data.mt9.t0); break;
                    case 2: printf("URA: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); tm->data.mt9.ura=(uint8_t)iv; printf("URA set to %d\n", tm->data.mt9.ura); break;
                    case 3: printf("X: "); fgets(line,128,stdin); tm->data.mt9.xg_ecef=strtod(line,NULL); printf("X set to %.6f\n", tm->data.mt9.xg_ecef); break;
                    case 4: printf("Y: "); fgets(line,128,stdin); tm->data.mt9.yg_ecef=strtod(line,NULL); printf("Y set to %.6f\n", tm->data.mt9.yg_ecef); break;
                    case 5: printf("Z: "); fgets(line,128,stdin); tm->data.mt9.zg_ecef=strtod(line,NULL); printf("Z set to %.6f\n", tm->data.mt9.zg_ecef); break;
                    case 6: printf("Vx: "); fgets(line,128,stdin); tm->data.mt9.xg_rate=strtod(line,NULL); printf("Vx set to %.6f\n", tm->data.mt9.xg_rate); break;
                    case 7: printf("Vy: "); fgets(line,128,stdin); tm->data.mt9.yg_rate=strtod(line,NULL); printf("Vy set to %.6f\n", tm->data.mt9.yg_rate); break;
                    case 8: printf("Vz: "); fgets(line,128,stdin); tm->data.mt9.zg_rate=strtod(line,NULL); printf("Vz set to %.6f\n", tm->data.mt9.zg_rate); break;
                    case 9: printf("Ax: "); fgets(line,128,stdin); tm->data.mt9.xg_accel=strtod(line,NULL); printf("Ax set to %.6f\n", tm->data.mt9.xg_accel); break;
                    case 10: printf("Ay: "); fgets(line,128,stdin); tm->data.mt9.yg_accel=strtod(line,NULL); printf("Ay set to %.6f\n", tm->data.mt9.yg_accel); break;
                    case 11: printf("Az: "); fgets(line,128,stdin); tm->data.mt9.zg_accel=strtod(line,NULL); printf("Az set to %.6f\n", tm->data.mt9.zg_accel); break;
                    case 12: printf("aGf0: "); fgets(line,128,stdin); tm->data.mt9.ag_f0=strtod(line,NULL); printf("aGf0 set to %.6f\n", tm->data.mt9.ag_f0); break;
                    case 13: printf("aGf1: "); fgets(line,128,stdin); tm->data.mt9.ag_f1=strtod(line,NULL); printf("aGf1 set to %.6f\n", tm->data.mt9.ag_f1); break;
                    default: break;
                }
                continue;
            }
            case 10: {
                printf("1) B_rrc 2) C_ltc_lsb 3) C_ltc_v1 4) I_ltc_v1 5) C_ltc_v0 6) I_ltc_v0 7) C_geo_lsb 8) C_geo_v 9) I_geo 10) C_er 11) C_iono_step 12) I_iono 13) C_iono_ramp 14) rss_udre 15) rss_iono 16) C_covariance 0/b) back\n> ");
                if (!fgets(line,sizeof(line),stdin)) { return -1; }
                if (line[0]=='b'||line[0]=='B'||line[0]=='0') { return 0; }
                choice=(int)strtol(line,NULL,10); double dv; int iv;
                switch(choice){
                    case 1: printf("B_rrc: "); fgets(line,128,stdin); dv=strtod(line,NULL); tm->data.mt10.B_rrc=dv; printf("B_rrc set to %.6f\n", tm->data.mt10.B_rrc); break;
                    case 2: printf("C_ltc_lsb: "); fgets(line,128,stdin); dv=strtod(line,NULL); tm->data.mt10.C_ltc_lsb=dv; printf("C_ltc_lsb set to %.6f\n", tm->data.mt10.C_ltc_lsb); break;
                    case 3: printf("C_ltc_v1: "); fgets(line,128,stdin); dv=strtod(line,NULL); tm->data.mt10.C_ltc_v1=dv; printf("C_ltc_v1 set to %.6f\n", tm->data.mt10.C_ltc_v1); break;
                    case 4: printf("I_ltc_v1: "); fgets(line,128,stdin); dv=strtod(line,NULL); tm->data.mt10.I_ltc_v1=dv; printf("I_ltc_v1 set to %.6f\n", tm->data.mt10.I_ltc_v1); break;
                    case 5: printf("C_ltc_v0: "); fgets(line,128,stdin); dv=strtod(line,NULL); tm->data.mt10.C_ltc_v0=dv; printf("C_ltc_v0 set to %.6f\n", tm->data.mt10.C_ltc_v0); break;
                    case 6: printf("I_ltc_v0: "); fgets(line,128,stdin); dv=strtod(line,NULL); tm->data.mt10.I_ltc_v0=dv; printf("I_ltc_v0 set to %.6f\n", tm->data.mt10.I_ltc_v0); break;
                    case 7: printf("C_geo_lsb: "); fgets(line,128,stdin); dv=strtod(line,NULL); tm->data.mt10.C_geo_lsb=dv; printf("C_geo_lsb set to %.6f\n", tm->data.mt10.C_geo_lsb); break;
                    case 8: printf("C_geo_v: "); fgets(line,128,stdin); dv=strtod(line,NULL); tm->data.mt10.C_geo_v=dv; printf("C_geo_v set to %.6f\n", tm->data.mt10.C_geo_v); break;
                    case 9: printf("I_geo: "); fgets(line,128,stdin); dv=strtod(line,NULL); tm->data.mt10.I_geo=dv; printf("I_geo set to %.6f\n", tm->data.mt10.I_geo); break;
                    case 10: printf("C_er: "); fgets(line,128,stdin); dv=strtod(line,NULL); tm->data.mt10.C_er=dv; printf("C_er set to %.6f\n", tm->data.mt10.C_er); break;
                    case 11: printf("C_iono_step: "); fgets(line,128,stdin); dv=strtod(line,NULL); tm->data.mt10.C_iono_step=dv; printf("C_iono_step set to %.6f\n", tm->data.mt10.C_iono_step); break;
                    case 12: printf("I_iono: "); fgets(line,128,stdin); dv=strtod(line,NULL); tm->data.mt10.I_iono=dv; printf("I_iono set to %.6f\n", tm->data.mt10.I_iono); break;
                    case 13: printf("C_iono_ramp: "); fgets(line,128,stdin); dv=strtod(line,NULL); tm->data.mt10.C_iono_ramp=dv; printf("C_iono_ramp set to %.6f\n", tm->data.mt10.C_iono_ramp); break;
                    case 14: printf("rss_udre (0/1): "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); tm->data.mt10.rss_udre=(uint8_t)(iv?1:0); printf("rss_udre set to %d\n", tm->data.mt10.rss_udre); break;
                    case 15: printf("rss_iono (0/1): "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); tm->data.mt10.rss_iono=(uint8_t)(iv?1:0); printf("rss_iono set to %d\n", tm->data.mt10.rss_iono); break;
                    case 16: printf("C_covariance: "); fgets(line,128,stdin); dv=strtod(line,NULL); tm->data.mt10.C_covariance=dv; printf("C_covariance set to %.6f\n", tm->data.mt10.C_covariance); break;
                    default: break; }
                continue; }
            case 17: {
                printf("1) geo[0..2] fields  2) t0  0/b) back\n> ");
                if (!fgets(line,sizeof(line),stdin)) { return -1; }
                if (line[0]=='b'||line[0]=='B'||line[0]=='0') { return 0; }
                choice=(int)strtol(line,NULL,10);
                if (choice==2){ printf("t0: "); fgets(line,128,stdin); double dv=strtod(line,NULL); tm->data.mt17.t0=dv; printf("t0 set to %.6f\n", tm->data.mt17.t0); continue; }
                /* Stay inside geo selection and then inside field selection until user backs out */
                for(;;){
                    printf("geo index (0-2) or b to back: "); if(!fgets(line,128,stdin)) return -1; if (line[0]=='b'||line[0]=='B') break; int gi=(int)strtol(line,NULL,10); if (gi<0||gi>2) continue; SBAS_MT17_GeoBlock *g=&tm->data.mt17.geo[gi];
                    for(;;){
                        printf("1)data_id 2)prn_number 3)health_and_status 4)x_g 5)y_g 6)z_g 7)xg_dot 8)yg_dot 9)zg_dot 0/b) back\n> "); if(!fgets(line,128,stdin)) return -1; if(line[0]=='b'||line[0]=='B'||line[0]=='0') break; int f=(int)strtol(line,NULL,10); double dv; int iv; switch(f){
                            case 1: printf("data_id: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); g->data_id=(uint8_t)iv; printf("data_id set to %d\n", g->data_id); break;
                            case 2: printf("prn_number: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); g->prn_number=(uint8_t)iv; printf("prn_number set to %d\n", g->prn_number); break;
                            case 3: printf("health_and_status: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); g->health_and_status=(uint8_t)iv; printf("health_and_status set to %d\n", g->health_and_status); break;
                            case 4: printf("x_g: "); fgets(line,128,stdin); dv=strtod(line,NULL); g->x_g=dv; printf("x_g set to %.6f\n", g->x_g); break;
                            case 5: printf("y_g: "); fgets(line,128,stdin); dv=strtod(line,NULL); g->y_g=dv; printf("y_g set to %.6f\n", g->y_g); break;
                            case 6: printf("z_g: "); fgets(line,128,stdin); dv=strtod(line,NULL); g->z_g=dv; printf("z_g set to %.6f\n", g->z_g); break;
                            case 7: printf("xg_dot: "); fgets(line,128,stdin); dv=strtod(line,NULL); g->xg_dot=dv; printf("xg_dot set to %.6f\n", g->xg_dot); break;
                            case 8: printf("yg_dot: "); fgets(line,128,stdin); dv=strtod(line,NULL); g->yg_dot=dv; printf("yg_dot set to %.6f\n", g->yg_dot); break;
                            case 9: printf("zg_dot: "); fgets(line,128,stdin); dv=strtod(line,NULL); g->zg_dot=dv; printf("zg_dot set to %.6f\n", g->zg_dot); break;
                            default: break; }
                    }
                }
                continue; }
            case 18: {
                printf("1) number_of_bands  2) band_number  3) iodi  4) toggle IGP mask bit (0-200)  0/b) back\n> ");
                if (!fgets(line,sizeof(line),stdin)) { return -1; }
                if (line[0]=='b'||line[0]=='B'||line[0]=='0') { return 0; }
                choice = (int)strtol(line, NULL, 10);
                int iv;
                if (choice == 1){ printf("number_of_bands: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); tm->data.mt18.number_of_bands=(uint8_t)iv; printf("number_of_bands set to %d\n", tm->data.mt18.number_of_bands); }
                else if (choice == 2){ printf("band_number: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); tm->data.mt18.band_number=(uint8_t)iv; printf("band_number set to %d\n", tm->data.mt18.band_number); }
                else if (choice == 3){ printf("iodi: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); tm->data.mt18.iodi=(uint8_t)iv; printf("iodi set to %d\n", tm->data.mt18.iodi); }
                else if (choice == 4){ for(;;){ printf("IGP bit (0-200) or b to back: "); if(!fgets(line,128,stdin)) return -1; if(line[0]=='b'||line[0]=='B') break; int bi=(int)strtol(line,NULL,10); if (bi>=0&&bi<201){ int by=bi/8; int bt=7-(bi%8); tm->data.mt18.igp_mask[by]^=(uint8_t)(1u<<bt); printf("IGP bit %d toggled\n", bi);} } }
                continue;
            }
            case 24: {
                printf("1) iodp 2) block_id 3) iodf 4) fast correction entry (0-5) 5) UDREI entry (0-5) 6) toggle LTC bit (0-105) 0/b) back\n> ");
                if (!fgets(line,sizeof(line),stdin)) { return -1; }
                if (line[0]=='b'||line[0]=='B'||line[0]=='0') { return 0; }
                choice=(int)strtol(line,NULL,10);
                if (choice==1){ printf("iodp: "); fgets(line,128,stdin); int v=(int)strtol(line,NULL,10); tm->data.mt24.iodp=(uint8_t)(v&3); printf("iodp set to %d\n", tm->data.mt24.iodp);} 
                else if (choice==2){ printf("block_id: "); fgets(line,128,stdin); int v=(int)strtol(line,NULL,10); tm->data.mt24.block_id=(uint8_t)(v&3); printf("block_id set to %d\n", tm->data.mt24.block_id);} 
                else if (choice==3){ printf("iodf: "); fgets(line,128,stdin); int v=(int)strtol(line,NULL,10); tm->data.mt24.iodf=(uint8_t)(v&3); printf("iodf set to %d\n", tm->data.mt24.iodf);} 
                else if (choice==4){ for(;;){ printf("index (0-5) or b to back: "); if(!fgets(line,128,stdin)) return -1; if(line[0]=='b'||line[0]=='B') break; int i=(int)strtol(line,NULL,10); if (i>=0&&i<6){ printf("fast_correction (-2047..2047): "); fgets(line,128,stdin); int fc=(int)strtol(line,NULL,10); tm->data.mt24.fast_corrections[i]=(int16_t)fc; printf("fast_correction[%d] set to %d\n", i, fc); }} }
                else if (choice==5){ for(;;){ printf("index (0-5) or b to back: "); if(!fgets(line,128,stdin)) return -1; if(line[0]=='b'||line[0]=='B') break; int i=(int)strtol(line,NULL,10); if (i>=0&&i<6){ printf("udrei (0-15): "); fgets(line,128,stdin); int u=(int)strtol(line,NULL,10); tm->data.mt24.udreis[i]=(uint8_t)u; printf("udrei[%d] set to %d\n", i, u); }} }
                else if (choice==6){ for(;;){ printf("bit (0-105) or b to back: "); if(!fgets(line,128,stdin)) return -1; if(line[0]=='b'||line[0]=='B') break; int bi=(int)strtol(line,NULL,10); if (bi>=0&&bi<106){ int by=bi/8; int bt=7-(bi%8); tm->data.mt24.long_term_corrections[by]^=(uint8_t)(1u<<bt); printf("LTC bit %d toggled\n", bi);} } }
                continue; }
            case 25: {
                printf("Choose half: 1 or 2  0/b) back\n> ");
                if (!fgets(line,sizeof(line),stdin)) { return -1; }
                if (line[0]=='b'||line[0]=='B'||line[0]=='0') { return 0; }
                int h=(int)strtol(line,NULL,10);
                SBAS_MT25_Half *half=(h==2)?&tm->data.mt25.half2:&tm->data.mt25.half1;
                /* Let user explicitly choose velocity code for editing to avoid ambiguity */
                for(;;){
                    printf("Select velocity code: 0) v0 (two-sat)  1) v1 (single-sat with rates)  b) back\n> ");
                    if (!fgets(line,sizeof(line),stdin)) return -1;
                    if (line[0]=='b'||line[0]=='B') return 0;
                    int vcode=(int)strtol(line,NULL,10);
                    if (vcode==0 || vcode==1){ half->raw_velocity_code=(uint8_t)vcode; break; }
                }
                if (half->raw_velocity_code==0){
                    SBAS_MT25_Half_Vel0 *v=&half->u.v0;
                    for(;;){
                        printf("Edit v0: 1)prn1 2)iod1 3)dx1 4)dy1 5)dz1 6)da_f0_1 7)prn2 8)iod2 9)dx2 10)dy2 11)dz2 12)da_f0_2 13)iodp 0/b) back\n> "); if(!fgets(line,128,stdin)) return -1; if(line[0]=='b'||line[0]=='B'||line[0]=='0') break; int f=(int)strtol(line,NULL,10); double dv; int iv; switch(f){
                            case 1: printf("prn1: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); v->prn_mask_1=(uint8_t)iv; break;
                            case 2: printf("iod1: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); v->iod_1=(uint8_t)iv; break;
                            case 3: printf("dx1: "); fgets(line,128,stdin); dv=strtod(line,NULL); v->dx_1=dv; break;
                            case 4: printf("dy1: "); fgets(line,128,stdin); dv=strtod(line,NULL); v->dy_1=dv; break;
                            case 5: printf("dz1: "); fgets(line,128,stdin); dv=strtod(line,NULL); v->dz_1=dv; break;
                            case 6: printf("da_f0_1: "); fgets(line,128,stdin); dv=strtod(line,NULL); v->da_f0_1=dv; break;
                            case 7: printf("prn2: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); v->prn_mask_2=(uint8_t)iv; break;
                            case 8: printf("iod2: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); v->iod_2=(uint8_t)iv; break;
                            case 9: printf("dx2: "); fgets(line,128,stdin); dv=strtod(line,NULL); v->dx_2=dv; break;
                            case 10: printf("dy2: "); fgets(line,128,stdin); dv=strtod(line,NULL); v->dy_2=dv; break;
                            case 11: printf("dz2: "); fgets(line,128,stdin); dv=strtod(line,NULL); v->dz_2=dv; break;
                            case 12: printf("da_f0_2: "); fgets(line,128,stdin); dv=strtod(line,NULL); v->da_f0_2=dv; break;
                            case 13: printf("iodp: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); v->iodp=(uint8_t)(iv&3); break;
                            default: break; }
                    }
                } else {
                    SBAS_MT25_Half_Vel1 *v=&half->u.v1;
                    for(;;){
                        printf("Edit v1: 1)prn 2)iod 3)dx 4)dy 5)dz 6)da_f0 7)dx_rate 8)dy_rate 9)dz_rate 10)da_f1 11)t0 12)iodp 0/b) back\n> "); if(!fgets(line,128,stdin)) return -1; if(line[0]=='b'||line[0]=='B'||line[0]=='0') break; int f=(int)strtol(line,NULL,10); double dv; int iv; switch(f){
                            case 1: printf("prn: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); v->prn_mask=(uint8_t)iv; break;
                            case 2: printf("iod: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); v->iod=(uint8_t)iv; break;
                            case 3: printf("dx: "); fgets(line,128,stdin); dv=strtod(line,NULL); v->dx=dv; break;
                            case 4: printf("dy: "); fgets(line,128,stdin); dv=strtod(line,NULL); v->dy=dv; break;
                            case 5: printf("dz: "); fgets(line,128,stdin); dv=strtod(line,NULL); v->dz=dv; break;
                            case 6: printf("da_f0: "); fgets(line,128,stdin); dv=strtod(line,NULL); v->da_f0=dv; break;
                            case 7: printf("dx_rate: "); fgets(line,128,stdin); dv=strtod(line,NULL); v->dx_rate=dv; break;
                            case 8: printf("dy_rate: "); fgets(line,128,stdin); dv=strtod(line,NULL); v->dy_rate=dv; break;
                            case 9: printf("dz_rate: "); fgets(line,128,stdin); dv=strtod(line,NULL); v->dz_rate=dv; break;
                            case 10: printf("da_f1: "); fgets(line,128,stdin); dv=strtod(line,NULL); v->da_f1=dv; break;
                            case 11: printf("t0: "); fgets(line,128,stdin); dv=strtod(line,NULL); v->t0=dv; break;
                            case 12: printf("iodp: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); v->iodp=(uint8_t)(iv&3); break;
                            default: break; }
                    }
                }
                continue; }
            case 26: {
                printf("1) band_number  2) block_id  3) iodi  4) set IGP vertical delay  5) set GIVEI  0/b) back\n> ");
                if (!fgets(line,sizeof(line),stdin)) { return -1; }
                if (line[0]=='b'||line[0]=='B'||line[0]=='0') { return 0; }
                choice = (int)strtol(line, NULL, 10);
                int iv;
                if (choice == 1){ printf("band_number: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); tm->data.mt26.band_number=(uint8_t)iv; printf("band_number set to %d\n", tm->data.mt26.band_number); }
                else if (choice == 2){ printf("block_id: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); tm->data.mt26.block_id=(uint8_t)iv; printf("block_id set to %d\n", tm->data.mt26.block_id); }
                else if (choice == 3){ printf("iodi: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); tm->data.mt26.iodi=(uint8_t)iv; printf("iodi set to %d\n", tm->data.mt26.iodi); }
                else if (choice == 4){ for(;;){ printf("index (0-%d) or b to back: ", SBAS_MT26_IGP_COUNT-1); if(!fgets(line,128,stdin)) return -1; if(line[0]=='b'||line[0]=='B') break; int i=(int)strtol(line,NULL,10); if (i>=0&&i<SBAS_MT26_IGP_COUNT){ printf("delay (0..511 raw): "); fgets(line,128,stdin); int v=(int)strtol(line,NULL,10); tm->data.mt26.igp_vertical_delay[i]=(uint16_t)v; printf("IGP vertical delay[%d] set to %d\n", i, v); }} }
                else if (choice == 5){ for(;;){ printf("index (0-%d) or b to back: ", SBAS_MT26_IGP_COUNT-1); if(!fgets(line,128,stdin)) return -1; if(line[0]=='b'||line[0]=='B') break; int i=(int)strtol(line,NULL,10); if (i>=0&&i<SBAS_MT26_IGP_COUNT){ printf("GIVEI (0..15): "); fgets(line,128,stdin); int v=(int)strtol(line,NULL,10); tm->data.mt26.givei[i]=(uint8_t)v; printf("GIVEI[%d] set to %d\n", i, v); }} }
                continue;
            }
            case 28: {
                printf("1) iodp  2) edit sat block (0-1)  0/b) back\n> ");
                if (!fgets(line,sizeof(line),stdin)) { return -1; }
                if (line[0]=='b'||line[0]=='B'||line[0]=='0') { return 0; }
                choice=(int)strtol(line,NULL,10);
                if (choice==1){ printf("iodp: "); fgets(line,128,stdin); int v=(int)strtol(line,NULL,10); tm->data.mt28.iodp=(uint8_t)(v&3); printf("iodp set to %d\n", tm->data.mt28.iodp); continue; }
                for(;;){
                    printf("block index (0-1) or b to back: "); if(!fgets(line,128,stdin)) return -1; if(line[0]=='b'||line[0]=='B') break; int bi=(int)strtol(line,NULL,10); if (bi<0||bi>=2) continue; SBAS_MT28_SatBlock *b=&tm->data.mt28.sat[bi];
                    for(;;){
                        printf("1)prn_mask 2)scale_exp 3)E1_1 4)E2_2 5)E3_3 6)E4_4 7)E1_2 8)E1_3 9)E1_4 10)E2_3 11)E2_4 12)E3_4 0/b) back\n> "); if(!fgets(line,128,stdin)) return -1; if(line[0]=='b'||line[0]=='B'||line[0]=='0') break; int f=(int)strtol(line,NULL,10); int iv2; switch(f){
                            case 1: printf("prn_mask: "); fgets(line,128,stdin); iv2=(int)strtol(line,NULL,10); b->prn_mask=(uint8_t)iv2; printf("prn_mask set to %d\n", b->prn_mask); break;
                            case 2: printf("scale_exp: "); fgets(line,128,stdin); iv2=(int)strtol(line,NULL,10); b->scale_exponent=(uint8_t)iv2; printf("scale_exponent set to %d\n", b->scale_exponent); break;
                            case 3: printf("E1_1: "); fgets(line,128,stdin); iv2=(int)strtol(line,NULL,10); b->E1_1=(uint16_t)iv2; printf("E1_1 set to %d\n", b->E1_1); break;
                            case 4: printf("E2_2: "); fgets(line,128,stdin); iv2=(int)strtol(line,NULL,10); b->E2_2=(uint16_t)iv2; printf("E2_2 set to %d\n", b->E2_2); break;
                            case 5: printf("E3_3: "); fgets(line,128,stdin); iv2=(int)strtol(line,NULL,10); b->E3_3=(uint16_t)iv2; printf("E3_3 set to %d\n", b->E3_3); break;
                            case 6: printf("E4_4: "); fgets(line,128,stdin); iv2=(int)strtol(line,NULL,10); b->E4_4=(uint16_t)iv2; printf("E4_4 set to %d\n", b->E4_4); break;
                            case 7: printf("E1_2: "); fgets(line,128,stdin); iv2=(int)strtol(line,NULL,10); b->E1_2=(int16_t)iv2; printf("E1_2 set to %d\n", b->E1_2); break;
                            case 8: printf("E1_3: "); fgets(line,128,stdin); iv2=(int)strtol(line,NULL,10); b->E1_3=(int16_t)iv2; printf("E1_3 set to %d\n", b->E1_3); break;
                            case 9: printf("E1_4: "); fgets(line,128,stdin); iv2=(int)strtol(line,NULL,10); b->E1_4=(int16_t)iv2; printf("E1_4 set to %d\n", b->E1_4); break;
                            case 10: printf("E2_3: "); fgets(line,128,stdin); iv2=(int)strtol(line,NULL,10); b->E2_3=(int16_t)iv2; printf("E2_3 set to %d\n", b->E2_3); break;
                            case 11: printf("E2_4: "); fgets(line,128,stdin); iv2=(int)strtol(line,NULL,10); b->E2_4=(int16_t)iv2; printf("E2_4 set to %d\n", b->E2_4); break;
                            case 12: printf("E3_4: "); fgets(line,128,stdin); iv2=(int)strtol(line,NULL,10); b->E3_4=(int16_t)iv2; printf("E3_4 set to %d\n", b->E3_4); break;
                            default: break; }
                    }
                }
                continue; }
            case 7: {
                printf("1) t_lat  2) iodp  3) set UDRE_AI[0-50]  0/b) back\n> ");
                if (!fgets(line,sizeof(line),stdin)) { return -1; }
                if (line[0]=='b'||line[0]=='B'||line[0]=='0') { return 0; }
                choice = (int)strtol(line, NULL, 10);
                int iv;
                if (choice == 1){ printf("t_lat: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); tm->data.mt7.t_lat=(uint8_t)iv; printf("t_lat set to %d\n", tm->data.mt7.t_lat); }
                else if (choice == 2){ printf("iodp: "); fgets(line,128,stdin); iv=(int)strtol(line,NULL,10); tm->data.mt7.iodp=(uint8_t)iv; printf("iodp set to %d\n", tm->data.mt7.iodp); }
                else if (choice == 3){ for(;;){ printf("index (0-50) or b to back: "); if(!fgets(line,128,stdin)) return -1; if(line[0]=='b'||line[0]=='B') break; int i=(int)strtol(line,NULL,10); if (i>=0&&i<51){ printf("udre_ai (0-15): "); fgets(line,128,stdin); int v=(int)strtol(line,NULL,10); tm->data.mt7.udre_ai[i]=(uint8_t)v; printf("UDRE_AI[%d] set to %d\n", i, v); }} }
                continue;
            }
            default:
                printf("Generic editor not implemented for MT%d yet.\n", mt);
                return 0;
        }
    }
}



/**
 * @brief Applies the selected orbit model (TLE or STAT) to an MT9 message.
 * @param tm The message to potentially modify.
 */
static void apply_orbit_model_override(SBAS_TypedMessage *tm)
{
    if (!tm) return;

    // A model is selected. Apply the correct one.
    switch (g_orbit_model) 
    {
        case ORBIT_MODEL_TLE:
            // Check all enabled TLE entries for a match with this satellite
            if (tm->message_type == 9) {
                // Calculate current TLE propagation time
                time_t tle_time;
                if (g_tle_use_fixed_time) {
                    // Simulation mode: base time + elapsed seconds since start
                    tle_time = g_tle_fixed_time + g_tle_simulation_offset;
                } else {
                    // Real-time mode: use current system time
                    tle_time = time(NULL);
                }
                
                for (int i = 0; i < g_tle_count; i++) {
                    if (g_tle_entries[i].enabled && 
                        (int)tm->satellite_id == g_tle_entries[i].sat_id) {
                        mt9_update_from_tle_elements(&tm->data.mt9, &g_tle_entries[i].elements, tle_time);
                        break; // Apply first matching TLE only
                    }
                }
            }
            break;
            
        case ORBIT_MODEL_STAT128:
            // Stat models use the g_orbit_model_target_sat_id global
            if (tm->message_type == 9 && (int)tm->satellite_id == g_orbit_model_target_sat_id) 
            {
                if (g_stat_model_128.initialized) {
                    const StatModel_MT9_Data* predicted_data = stat_model_128_predict(&g_stat_model_128);
                    copy_model_data_to_msg(predicted_data, &tm->data.mt9);
                }
            }
            break;
            
        case ORBIT_MODEL_STAT143:
            // Stat models use the g_orbit_model_target_sat_id global
            if (tm->message_type == 9 && (int)tm->satellite_id == g_orbit_model_target_sat_id) 
            {
                if (g_stat_model_143.initialized) {
                    const StatModel_MT9_Data* predicted_data = stat_model_143_predict(&g_stat_model_143);
                    copy_model_data_to_msg(predicted_data, &tm->data.mt9);
                }
            }
            break;
            
        case ORBIT_MODEL_NONE:
        default:
            // Do nothing
            break;
    }
}





/* ================= Direct-from-struct scheduler (no queue) ================= */
static time_t g_direct_last_sent[64];
static int    g_direct_last_idx[64];

static void direct_scheduler_reset(void) {
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

/* Helper: which types we support encoding/transmitting */
static int is_supported_tx_type(int t) {
    /* Filter MT2-5 based on fast correction satellite count */
    if (g_fast_correction_satellites > 0) {
        if (t == 2) {
            /* MT2 always sent if any fast corrections needed (covers satellites 1-13) */
            return 1;
        } else if (t == 3) {
            /* MT3 sent only if > 13 satellites (covers satellites 14-26) */
            return (g_fast_correction_satellites > 13) ? 1 : 0;
        } else if (t == 4) {
            /* MT4 sent only if > 26 satellites (covers satellites 27-39) */
            return (g_fast_correction_satellites > 26) ? 1 : 0;
        } else if (t == 5) {
            /* MT5 sent only if > 39 satellites (covers satellites 40-51) */
            return (g_fast_correction_satellites > 39) ? 1 : 0;
        }
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





/* Single step: choose the most-overdue message type from provider, encode next message of that type, and broadcast. */
static int direct_scheduler_step(void) {
    time_t now = time(NULL);
    int chosen_type = -1;
    int chosen_idx = -1;
    SBAS_TypedMessage *chosen_tm = NULL;
    int best_priority = -1;
    int best_overdue = INT_MIN;

    /* Consider all message types present */
    for (int mt = 0; mt < 64; ++mt) {
        if (!is_supported_tx_type(mt)) continue; // skip unsupported types

        SBAS_TypedMessage *tm = NULL; int next_idx = -1;
        if (tx_source_next_for_type(mt, g_direct_last_idx[mt], &tm, &next_idx) != 0 || next_idx < 0 || !tm) continue; // none present
        
        // --- NEW SATELLITE FILTER LOGIC ---
        if (g_tx_filter_sat_id > 0) {
            int initial_next_idx = next_idx; // Store the starting point
            
            while ((int)tm->satellite_id != g_tx_filter_sat_id) {
                // This message doesn't match our filter. Find the *next* one of this type.
                int current_last_idx = next_idx;
                if (tx_source_next_for_type(mt, current_last_idx, &tm, &next_idx) != 0 || next_idx == initial_next_idx) {
                    // We looped all the way around, or no more messages. No match for this MT.
                    tm = NULL;
                    break;
                }
            }
            if (tm == NULL) continue; // No messages for this (MT, SatID) combo. Skip to next MT.
        }
        // --- END OF NEW LOGIC ---

        // If we're here, 'tm' is a valid candidate (either unfiltered or filtered).
        
        int interval = 0;
        if (mt >= 0 && mt < 64) interval = max_update_interval[mt];
        if (interval <= 0) interval = 3600; // fallback
        
        int overdue;
        if (g_direct_last_sent[mt] == 0) overdue = interval; else overdue = (int)(now - g_direct_last_sent[mt]) - interval;

        if (overdue < 0) continue; // not yet due

        int pri = direct_type_priority(mt);

        if (pri > best_priority || (pri == best_priority && overdue > best_overdue)) {
            best_priority = pri;
            best_overdue = overdue;
            chosen_type = mt;
            chosen_idx = next_idx;
            chosen_tm = tm;
        }
    }

    if (chosen_idx < 0) {
        // No messages are due for our filtered satellite.
        // Send MT63 filler to keep the connection alive.
        char hex_output[64];
        if (encode_mt63_from(NULL, hex_output) < 0) return -1;
        uint8_t filler[32]; size_t len = tx_hex_to_bin(hex_output, filler, sizeof(filler));
        tx_server_broadcast(filler, len);
        g_direct_last_sent[63] = now; // Mark filler as sent
        
        // Log MT63 to RINEX output file if configured
        if (rinex_writer_is_active()) {
            int sat_id = (g_tx_filter_sat_id > 0) ? g_tx_filter_sat_id : 128; // Use filter sat or default 128
            rinex_writer_add_packet(sat_id, 63, hex_output, now);
        }
        
        {
            char ts[16] = {0}; current_time_hhmmss(ts, sizeof(ts));
            printf("[%s] [direct] Sent MT63 filler (No messages for SatID %d)\n", ts, g_tx_filter_sat_id);
        }
        return (int)len;
    }
    

    // Encode and broadcast chosen message
    
/* Apply increments if configured. */
    apply_increment_if_rule(chosen_tm);

    /* Apply the global IOD values (IODP, IODF, IODI) to maintain synchronization */
    apply_global_iodp(chosen_tm);
    apply_global_iodf(chosen_tm);
    apply_global_iodi(chosen_tm);

    apply_orbit_model_override(chosen_tm); // (or tm)



    
    char hex[256] = {0};
    if (encode_message_from_tm(chosen_tm, hex) != 0) {
        printf("[direct] Encoding failed for MT%d at idx %d\n", chosen_type, chosen_idx);
        return -1;
    }
    
    uint8_t bin[32]; size_t blen = tx_hex_to_bin(hex, bin, sizeof(bin));
    if (blen == 0) return -1;
    
    tx_server_broadcast(bin, blen);
    g_direct_last_sent[chosen_type] = now;
    g_direct_last_idx[chosen_type] = chosen_idx;
    
    // Log to RINEX output file if configured
    if (rinex_writer_is_active()) {
        rinex_writer_add_packet(chosen_tm->satellite_id, chosen_type, hex, now);
    }
    
    // Increment TLE simulation time after each transmission (if in simulation mode)
    if (g_orbit_model == ORBIT_MODEL_TLE && g_tle_use_fixed_time) {
        g_tle_simulation_offset++;
    }
    
    {
        char ts[16] = {0}; current_time_hhmmss(ts, sizeof(ts));
        printf("[%s] [direct] Sent MT%d (sat %u, idx %d) %zu bytes | HEX: %s\n", ts, chosen_type, (unsigned)chosen_tm->satellite_id, chosen_idx, blen, hex);
    }
    return (int)blen;
}








// Accept new client connections (thin wrapper)
int accept_new_clients(void) { return tx_server_accept_new_clients(); }

/* Initialize TCP server using tx_server API. Returns 0 on success, non-zero on failure. */
int initialize_tcp_server(void) {
    int rc = tx_server_init(TCP_PORT, MAX_CLIENTS);
    if (rc != 0) {
        fprintf(stderr, "tx_server_init failed with code %d\n", rc);
        return -1;
    }
    return 0;
}


// Print main menu
void print_menu(void) {
    printf("\n=== HERCULES TRANSMITTER SYSTEM ===\n");
    printf("1. Start transmission (with duration timer)\n");
    printf("2. Stop transmission\n");
    printf("3. Show client status\n");
    printf("4. Edit and send a message (generalized, no queue)\n");
    printf("5. Set Orbit Model (TLE / Statistical)\n"); // <-- CHANGED
    printf("6. Edit and transmit a SINGLE message continuously\n");
    printf("7. Configure RINEX Output File\n"); // <-- NEW
    printf("0. Exit\n");
    printf("Choice: ");
    fflush(stdout);
}

// Handle user input
void handle_user_input(void) {
    int choice;
    static int transmitting = 0;
    
    /* Read menu choice using fgets to avoid scanf/newline mix issues */
    {
        char input_line[128];
        if (!fgets(input_line, sizeof(input_line), stdin)) return;
        /* Trim newline */
        char *nl = strchr(input_line, '\n'); if (nl) *nl = '\0';
        choice = (int)strtol(input_line, NULL, 10);
    }
    
    switch (choice) {
        case 1: {
            // --- Ask for SatID filter before starting ---
            {
                char line[64];
                printf("Enter Satellite ID to filter (0 or -1 for all): ");
                if (!fgets(line, sizeof(line), stdin)) {
                    g_tx_filter_sat_id = -1; // Default to all on read error
                } else {
                    int sat_id_filter = (int)strtol(line, NULL, 10);
                    if (sat_id_filter > 0) {
                        g_tx_filter_sat_id = sat_id_filter;
                        printf("--- Starting transmission, FILTERING for SatID %d ---\n", g_tx_filter_sat_id);
                    } else {
                        g_tx_filter_sat_id = -1;
                        printf("--- Starting transmission for ALL satellites ---\n");
                    }
                }
            }
            // --- End of satellite filter logic ---

            // --- Ask for number of satellites for fast corrections (MT2-5 filter) ---
            {
                char line[64];
                printf("\nFast Correction Configuration:\n");
                printf("  MT2 covers satellites 1-13\n");
                printf("  MT3 covers satellites 14-26\n");
                printf("  MT4 covers satellites 27-39\n");
                printf("  MT5 covers satellites 40-51\n");
                printf("Enter number of satellites for fast corrections (0 for all MT2-5): ");
                if (!fgets(line, sizeof(line), stdin)) {
                    g_fast_correction_satellites = 0;
                } else {
                    int num_sats = (int)strtol(line, NULL, 10);
                    if (num_sats > 0 && num_sats <= 51) {
                        g_fast_correction_satellites = num_sats;
                        printf("--- Fast corrections for %d satellites ---\n", num_sats);
                        if (num_sats <= 13) {
                            printf("    Will transmit: MT2 only\n");
                        } else if (num_sats <= 26) {
                            printf("    Will transmit: MT2, MT3\n");
                        } else if (num_sats <= 39) {
                            printf("    Will transmit: MT2, MT3, MT4\n");
                        } else {
                            printf("    Will transmit: MT2, MT3, MT4, MT5\n");
                        }
                    } else {
                        g_fast_correction_satellites = 0;
                        printf("--- Transmitting all fast correction messages (MT2-5) ---\n");
                    }
                }
            }
            // --- End of fast correction satellite config ---

            // *** NEW: Initialize live MT9 data before starting tx ***
            if (g_orbit_model == ORBIT_MODEL_STAT128 && g_stat_model_128.initialized) {
                SBAS_TypedMessage *tm = NULL; int idx = -1;
                if (tx_source_find_first(9, 128, &tm, &idx) == 0) {
                    copy_msg_to_model_data(&tm->data.mt9, &g_stat_model_128.live_data);
                    printf("STAT128 model live state initialized from source file.\n");
                }
            } else if (g_orbit_model == ORBIT_MODEL_STAT143 && g_stat_model_143.initialized) {
                SBAS_TypedMessage *tm = NULL; int idx = -1;
                if (tx_source_find_first(9, 143, &tm, &idx) == 0) {
                    copy_msg_to_model_data(&tm->data.mt9, &g_stat_model_143.live_data);
                    printf("STAT143 model live state initialized from source file.\n");
                }
            }
            
            // Reset TLE simulation time offset when starting transmission
            if (g_orbit_model == ORBIT_MODEL_TLE && g_tle_use_fixed_time) {
                g_tle_simulation_offset = 0;
                struct tm* tm_info = gmtime(&g_tle_fixed_time);
                char time_str[64];
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S UTC", tm_info);
                printf("TLE simulation starting from: %s\n", time_str);
            }

            int duration_minutes = 0;
            printf("Enter transmission duration in minutes (0 for continuous): ");
            {
                char line[64];
                if (fgets(line, sizeof(line), stdin)) {
                    duration_minutes = (int)strtol(line, NULL, 10);
                    if (duration_minutes < 0) { duration_minutes = 0; }
                } else {
                    duration_minutes = 0;
                }
            }
            
            direct_scheduler_reset();

            if (tx_source_bound_count() == 0) {
                printf("Note: No decoded messages are bound. The scheduler will send MT63 filler messages.\n");
                printf("Hint: Start with a data file: .\\hercules_transmitter.exe --bind-file data\\parsing_file.25B\n");
            }
            
            transmitting = 1;
            time_t start_time = time(NULL);
            time_t end_time = (duration_minutes > 0) ? start_time + (duration_minutes * 60) : 0;
            
            if (duration_minutes > 0) {
                printf("Starting transmission for %d minutes...\n", duration_minutes);
                printf("Press ESC to stop early, or wait for automatic stop.\n");
            } else {
                printf("Starting continuous transmission...\n");
                printf("Press ESC to stop transmission.\n");
            }
            
            int message_count = 0;
            time_t last_status_time = start_time;
            
            while (transmitting) {
                direct_scheduler_step();
                accept_new_clients();
                message_count++;
                Sleep(1000); 
                
                time_t current_time = time(NULL);
                
                if (current_time - last_status_time >= 30) {
                    int elapsed_seconds = (int)(current_time - start_time);
                    int elapsed_minutes = elapsed_seconds / 60;
                    
                    if (duration_minutes > 0) {
                        int remaining_seconds = (int)(end_time - current_time);
                        int remaining_minutes = remaining_seconds / 60;
                        printf("=== TRANSMISSION STATUS ===\n");
                        printf("Elapsed: %d:%02d minutes\n", elapsed_minutes, elapsed_seconds % 60);
                        printf("Remaining: %d:%02d minutes\n", remaining_minutes, remaining_seconds % 60);
                        printf("Messages sent: %d\n", message_count);
                        printf("Connected clients: %d\n", tx_server_client_count());
                    } else {
                        printf("=== TRANSMISSION STATUS ===\n");
                        printf("Running: %d:%02d minutes\n", elapsed_minutes, elapsed_seconds % 60);
                        printf("Messages sent: %d\n", message_count);
                        printf("Connected clients: %d\n", tx_server_client_count());
                    }
                    last_status_time = current_time;
                }
                
                if (duration_minutes > 0 && current_time >= end_time) {
                    transmitting = 0;
                    printf("\n=== TRANSMISSION COMPLETED ===\n");
                    printf("Duration: %d minutes\n", duration_minutes);
                    printf("Total messages sent: %d\n", message_count);
                    printf("Average messages per minute: %.1f\n", (float)message_count / duration_minutes);
                    break;
                }
                
                if (_kbhit()) {
                    int ch = _getch();
                    if (ch == 27) {  // ESC key
                        transmitting = 0;
                        int elapsed_seconds = (int)(current_time - start_time);
                        int elapsed_minutes = elapsed_seconds / 60;
                        printf("\n=== TRANSMISSION STOPPED BY USER ===\n");
                        printf("Elapsed time: %d:%02d minutes\n", elapsed_minutes, elapsed_seconds % 60);
                        printf("Messages sent: %d\n", message_count);
                        if (elapsed_minutes > 0) {
                            printf("Average messages per minute: %.1f\n", (float)message_count / elapsed_minutes);
                        }
                    }
                }
            }
            g_tx_filter_sat_id = -1; // Reset filter when transmission stops
            break;
        }
        case 2:
            transmitting = 0;
            g_tx_filter_sat_id = -1; // Reset filter on manual stop
            printf("Transmission stopped\n");
            break;
        case 3:
            printf("Connected clients: %d/%d\n", tx_server_client_count(), MAX_CLIENTS);
            break;
        case 4: {
            /* Ask satellite and message type */
            unsigned int sat = 0; int mt = 0; char line[64];
            printf("Enter satellite ID: "); if (!fgets(line,sizeof(line),stdin)) { break; } sat = (unsigned)strtoul(line,NULL,10);
            printf("Enter message type: "); if (!fgets(line,sizeof(line),stdin)) { break; } mt = (int)strtol(line,NULL,10);
            
            SBAS_TypedMessage *tm = NULL; int idx = -1;
            if (tx_source_find_first(mt, sat, &tm, &idx) != 0 || !tm) { printf("No message found for sat %u type %d.\n", sat, mt); break; }
            
            /* Edit one parameter (track before/after to derive increment rule) */
            SBAS_TypedMessage before = *tm;
            (void)generalized_edit_prompt(tm);
            detect_and_register_increment_rule(&before, tm);

            // --- IODP SYNC LOGIC ---
            uint8_t new_iodp = g_current_iodp;
            switch (mt) {
                case 1:  if (before.data.mt1.iodp != tm->data.mt1.iodp) new_iodp = tm->data.mt1.iodp; break;
                case 2: case 3: case 4: case 5:
                         if (before.data.mt2_5.iodp != tm->data.mt2_5.iodp) new_iodp = tm->data.mt2_5.iodp; break;
                case 7:  if (before.data.mt7.iodp != tm->data.mt7.iodp) new_iodp = tm->data.mt7.iodp; break;
                case 24: if (before.data.mt24.iodp != tm->data.mt24.iodp) new_iodp = tm->data.mt24.iodp; break;
                case 28: if (before.data.mt28.iodp != tm->data.mt28.iodp) new_iodp = tm->data.mt28.iodp; break;
            }
            if (new_iodp != g_current_iodp) {
                g_current_iodp = new_iodp & 3;
                printf("--- System IODP manually set to %d by user edit ---\n", g_current_iodp);
            }

            // --- IODF SYNC LOGIC ---
            // Each MT2-5 has its own IODF that must be synchronized
            bool iodf_changed = false;
            switch (mt) {
                case 2:
                    if (before.data.mt2_5.iodf != tm->data.mt2_5.iodf) {
                        g_current_iodf[0] = tm->data.mt2_5.iodf & 3;
                        printf("--- System IODF for MT2 set to %d by user edit ---\n", g_current_iodf[0]);
                        iodf_changed = true;
                    }
                    break;
                case 3:
                    if (before.data.mt2_5.iodf != tm->data.mt2_5.iodf) {
                        g_current_iodf[1] = tm->data.mt2_5.iodf & 3;
                        printf("--- System IODF for MT3 set to %d by user edit ---\n", g_current_iodf[1]);
                        iodf_changed = true;
                    }
                    break;
                case 4:
                    if (before.data.mt2_5.iodf != tm->data.mt2_5.iodf) {
                        g_current_iodf[2] = tm->data.mt2_5.iodf & 3;
                        printf("--- System IODF for MT4 set to %d by user edit ---\n", g_current_iodf[2]);
                        iodf_changed = true;
                    }
                    break;
                case 5:
                    if (before.data.mt2_5.iodf != tm->data.mt2_5.iodf) {
                        g_current_iodf[3] = tm->data.mt2_5.iodf & 3;
                        printf("--- System IODF for MT5 set to %d by user edit ---\n", g_current_iodf[3]);
                        iodf_changed = true;
                    }
                    break;
                case 6:
                    // MT6 carries all four IODF values
                    if (before.data.mt6.iodf2 != tm->data.mt6.iodf2) {
                        g_current_iodf[0] = tm->data.mt6.iodf2 & 3;
                        printf("--- System IODF2 (for MT2) set to %d by user edit ---\n", g_current_iodf[0]);
                        iodf_changed = true;
                    }
                    if (before.data.mt6.iodf3 != tm->data.mt6.iodf3) {
                        g_current_iodf[1] = tm->data.mt6.iodf3 & 3;
                        printf("--- System IODF3 (for MT3) set to %d by user edit ---\n", g_current_iodf[1]);
                        iodf_changed = true;
                    }
                    if (before.data.mt6.iodf4 != tm->data.mt6.iodf4) {
                        g_current_iodf[2] = tm->data.mt6.iodf4 & 3;
                        printf("--- System IODF4 (for MT4) set to %d by user edit ---\n", g_current_iodf[2]);
                        iodf_changed = true;
                    }
                    if (before.data.mt6.iodf5 != tm->data.mt6.iodf5) {
                        g_current_iodf[3] = tm->data.mt6.iodf5 & 3;
                        printf("--- System IODF5 (for MT5) set to %d by user edit ---\n", g_current_iodf[3]);
                        iodf_changed = true;
                    }
                    break;
                case 24:
                    if (before.data.mt24.iodf != tm->data.mt24.iodf) {
                        // MT24 uses a single IODF - we treat it as linked to MT2's IODF
                        g_current_iodf[0] = tm->data.mt24.iodf & 3;
                        printf("--- System IODF (via MT24) set to %d by user edit ---\n", g_current_iodf[0]);
                        iodf_changed = true;
                    }
                    break;
            }

            // --- IODI SYNC LOGIC ---
            bool iodi_changed = false;
            switch (mt) {
                case 18:
                    if (before.data.mt18.iodi != tm->data.mt18.iodi) {
                        g_current_iodi = tm->data.mt18.iodi & 3;
                        printf("--- System IODI set to %d by user edit (via MT18) ---\n", g_current_iodi);
                        iodi_changed = true;
                    }
                    break;
                case 26:
                    if (before.data.mt26.iodi != tm->data.mt26.iodi) {
                        g_current_iodi = tm->data.mt26.iodi & 3;
                        printf("--- System IODI set to %d by user edit (via MT26) ---\n", g_current_iodi);
                        iodi_changed = true;
                    }
                    break;
            }
            
            /* Encode and transmit */
            char hex[256] = {0};
            
            /* Force this message to use the (new or old) global IOD values */
            apply_global_iodp(tm);
            apply_global_iodf(tm);
            apply_global_iodi(tm);
            
            /* Apply ORBIT MODEL override if it matches */
            apply_orbit_model_override(tm); // <-- UPDATED
            
            if (encode_message_from_tm(tm, hex) == 0) {
                uint8_t bin[32]; size_t blen = tx_hex_to_bin(hex, bin, sizeof(bin));
                if (blen > 0) {
                    tx_server_broadcast(bin, blen);
                    char ts[16] = {0}; current_time_hhmmss(ts, sizeof(ts));
                    printf("[%s] Sent MT%d for sat %u (%zu bytes)\n", ts, mt, sat, blen);
                }
                else { printf("Failed to convert hex to binary.\n"); }
            } else {
                printf("Encoding failed for MT%d.\n", mt);
            }
            break;
        }
        case 5: {
            // --- NEW ORBIT MODEL MENU ---
            char line[512];
            printf("=== Orbit Model Configuration ===\n");
            
            const char* model_name = "None";
            if (g_orbit_model == ORBIT_MODEL_TLE) model_name = "TLE";
            else if (g_orbit_model == ORBIT_MODEL_STAT128) model_name = "Statistical (128)";
            else if (g_orbit_model == ORBIT_MODEL_STAT143) model_name = "Statistical (143)";
            
            printf("Current Model: %s\n", model_name);
            if (g_orbit_model == ORBIT_MODEL_TLE && g_tle_file_path) {
                printf("TLE File: %s (%d entries loaded)\n", g_tle_file_path, g_tle_count);
                printf("Time Mode: %s", g_tle_use_fixed_time ? "Simulation Mode" : "Real-Time");
                if (g_tle_use_fixed_time) {
                    struct tm* tm_info = gmtime(&g_tle_fixed_time);
                    char time_str[64];
                    strftime(time_str, sizeof(time_str), " (Base: %Y-%m-%d %H:%M:%S UTC)", tm_info);
                    printf("%s", time_str);
                }
                printf("\n");
            }
            printf("-----------------------------------\n");
            printf("1. Load TLE File and Configure Satellites\n");
            printf("2. Set Statistical Model (Sat 128)\n");
            printf("3. Set Statistical Model (Sat 143)\n");
            printf("4. Configure TLE Time Mode\n");
            printf("5. Disable Orbit Model\n");
            printf("0. Back to Main Menu\n> ");
            
            if (!fgets(line, sizeof(line), stdin)) break;
            if (line[0]=='b' || line[0]=='B' || line[0]=='0') break;

            int sub = (int)strtol(line, NULL, 10);

            if (sub == 1) { // Load TLE File
                printf("\nEnter TLE file path (e.g., data/beidou.txt): ");
                if (!fgets(line, sizeof(line), stdin)) break;
                char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
                char *cr = strchr(line, '\r'); if (cr) *cr = '\0';
                
                if (line[0] == '\0') {
                    printf("No file specified.\n");
                    break;
                }
                
                // Load all TLE entries from file
                int loaded = tle_load_all_entries(line, g_tle_entries, MAX_TLE_ENTRIES);
                if (loaded <= 0) {
                    printf("ERROR: Failed to load TLE file or no valid entries found.\n");
                    break;
                }
                
                g_tle_count = loaded;
                if (g_tle_file_path) free(g_tle_file_path);
                g_tle_file_path = strdup(line);
                
                printf("\n=== TLE Entries Found ===\n");
                for (int i = 0; i < g_tle_count; i++) {
                    printf("%d. %s\n", i+1, g_tle_entries[i].name);
                }
                
                // Now let user configure satellites
                printf("\n=== Configure Satellites ===\n");
                printf("For each entry, enter the PRN/Satellite ID to use,\n");
                printf("or press Enter to skip that entry.\n\n");
                
                for (int i = 0; i < g_tle_count; i++) {
                    printf("%s\n", g_tle_entries[i].name);
                    printf("  Enter PRN (128, 129, 143, etc.) or press Enter to skip: ");
                    if (!fgets(line, sizeof(line), stdin)) continue;
                    
                    if (line[0] == '\n' || line[0] == '\r' || line[0] == '\0') {
                        g_tle_entries[i].enabled = 0;
                        g_tle_entries[i].sat_id = -1;
                        printf("  Skipped.\n\n");
                    } else {
                        int prn = (int)strtol(line, NULL, 10);
                        if (prn >= 120 && prn <= 158) {
                            g_tle_entries[i].sat_id = prn;
                            g_tle_entries[i].enabled = 1;
                            printf("  ✓ Enabled for PRN %d\n\n", prn);
                        } else {
                            printf("  Invalid PRN (must be 120-158). Skipped.\n\n");
                            g_tle_entries[i].enabled = 0;
                            g_tle_entries[i].sat_id = -1;
                        }
                    }
                }
                
                // Show summary
                printf("\n=== Configuration Summary ===\n");
                int enabled_count = 0;
                for (int i = 0; i < g_tle_count; i++) {
                    if (g_tle_entries[i].enabled) {
                        printf("✓ PRN %d: %s\n", g_tle_entries[i].sat_id, g_tle_entries[i].name);
                        enabled_count++;
                    }
                }
                
                if (enabled_count > 0) {
                    g_orbit_model = ORBIT_MODEL_TLE;
                    printf("\nTLE Model activated with %d satellite(s).\n", enabled_count);
                } else {
                    printf("\nNo satellites enabled. TLE model not activated.\n");
                }
            } 
            else if (sub == 2) { // Set Stat 128
                if (g_stat_model_128.initialized) {
                    g_orbit_model = ORBIT_MODEL_STAT128;
                    g_orbit_model_target_sat_id = 128; // Default target
                    printf("Orbit Model set to STAT128 (trained).\n");
                    printf("Target SatID automatically set to 128.\n");
                } else {
                    printf("ERROR: STAT128 model was not trained. Load a .25B file on startup.\n");
                }
            } 
            else if (sub == 3) { // Set Stat 143
                if (g_stat_model_143.initialized) {
                    g_orbit_model = ORBIT_MODEL_STAT143;
                    g_orbit_model_target_sat_id = 143; // Default target
                    printf("Orbit Model set to STAT143 (trained).\n");
                    printf("Target SatID automatically set to 143.\n");
                } else {
                    printf("ERROR: STAT143 model was not trained. Load a .25B file on startup.\n");
                }
            }
            else if (sub == 4) { // Configure TLE Time Mode
                if (g_orbit_model != ORBIT_MODEL_TLE) {
                    printf("\nWARNING: TLE Model is not currently active.\n");
                    printf("This setting will apply when TLE model is enabled.\n\n");
                }
                
                printf("\n=== Configure TLE Time Mode ===\n");
                printf("Current Mode: %s\n", g_tle_use_fixed_time ? "Simulation Mode" : "Real-Time");
                if (g_tle_use_fixed_time) {
                    struct tm* tm_info = gmtime(&g_tle_fixed_time);
                    char time_str[64];
                    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S UTC", tm_info);
                    printf("Base Time: %s\n", time_str);
                    printf("(Time advances with each message: +1 second per transmission)\n");
                }
                printf("-------------------------------\n");
                printf("1. Use Real-Time (current date/time)\n");
                printf("2. Use Simulation Mode (fixed start date/time)\n");
                printf("0. Cancel\n> ");
                
                if (!fgets(line, sizeof(line), stdin)) break;
                int time_choice = (int)strtol(line, NULL, 10);
                
                if (time_choice == 1) {
                    g_tle_use_fixed_time = 0;
                    g_tle_simulation_offset = 0;  // Reset offset (not used in real-time mode)
                    printf("\n✓ TLE Time Mode set to Real-Time\n");
                    printf("  TLE predictions will use current system time.\n");
                }
                else if (time_choice == 2) {
                    printf("\nEnter date and time (UTC):\n");
                    
                    int year, month, day, hour, minute, second;
                    
                    printf("  Year (e.g., 2025): ");
                    if (!fgets(line, sizeof(line), stdin)) break;
                    year = (int)strtol(line, NULL, 10);
                    
                    printf("  Month (1-12): ");
                    if (!fgets(line, sizeof(line), stdin)) break;
                    month = (int)strtol(line, NULL, 10);
                    
                    printf("  Day (1-31): ");
                    if (!fgets(line, sizeof(line), stdin)) break;
                    day = (int)strtol(line, NULL, 10);
                    
                    printf("  Hour (0-23): ");
                    if (!fgets(line, sizeof(line), stdin)) break;
                    hour = (int)strtol(line, NULL, 10);
                    
                    printf("  Minute (0-59): ");
                    if (!fgets(line, sizeof(line), stdin)) break;
                    minute = (int)strtol(line, NULL, 10);
                    
                    printf("  Second (0-59): ");
                    if (!fgets(line, sizeof(line), stdin)) break;
                    second = (int)strtol(line, NULL, 10);
                    
                    // Validate input ranges
                    if (year < 1900 || year > 2100 || 
                        month < 1 || month > 12 ||
                        day < 1 || day > 31 ||
                        hour < 0 || hour > 23 ||
                        minute < 0 || minute > 59 ||
                        second < 0 || second > 59) {
                        printf("\nERROR: Invalid date/time values.\n");
                        break;
                    }
                    
                    // Convert to time_t
                    struct tm tm_input = {0};
                    tm_input.tm_year = year - 1900;
                    tm_input.tm_mon = month - 1;
                    tm_input.tm_mday = day;
                    tm_input.tm_hour = hour;
                    tm_input.tm_min = minute;
                    tm_input.tm_sec = second;
                    tm_input.tm_isdst = -1;
                    
                    time_t input_time = mktime(&tm_input);
                    if (input_time == -1) {
                        printf("\nERROR: Failed to convert date/time.\n");
                        break;
                    }
                    
                    g_tle_fixed_time = input_time;
                    g_tle_use_fixed_time = 1;
                    g_tle_simulation_offset = 0;  // Reset simulation to start from this time
                    
                    struct tm* tm_confirm = gmtime(&g_tle_fixed_time);
                    char confirm_str[64];
                    strftime(confirm_str, sizeof(confirm_str), "%Y-%m-%d %H:%M:%S UTC", tm_confirm);
                    
                    printf("\n✓ TLE Time Mode set to Fixed Time (Simulation Mode)\n");
                    printf("  Base Time: %s\n", confirm_str);
                    printf("  TLE predictions will start from this time and advance with each message.\n");
                    printf("  (Time will increment by 1 second per transmission)\n");
                }
            }
            else if (sub == 5) { // Disable
                 g_orbit_model = ORBIT_MODEL_NONE;
                 g_orbit_model_target_sat_id = -1;
                 printf("All orbit models disabled.\n");
            }
            break;
        }
        case 6: {
            unsigned int sat = 0; int mt = 0; char line[64];
            printf("Enter satellite ID for single-message tx: "); 
            if (!fgets(line,sizeof(line),stdin)) { break; } 
            sat = (unsigned)strtoul(line,NULL,10);

            printf("Enter message type for single-message tx: "); 
            if (!fgets(line,sizeof(line),stdin)) { break; } 
            mt = (int)strtol(line,NULL,10);

            SBAS_TypedMessage *tm = NULL; int idx = -1;
            if (tx_source_find_first(mt, sat, &tm, &idx) != 0 || !tm) { 
                printf("No message found for sat %u type %d.\n", sat, mt); 
                break; 
            }

            // --- Edit the message and set up the increment rule ---
            printf("--- Editing parameters for (Sat %u, MT %d) ---\n", sat, mt);
            SBAS_TypedMessage before = *tm;
            (void)generalized_edit_prompt(tm);
            detect_and_register_increment_rule(&before, tm);

            // --- IODP SYNC LOGIC (for Case 6) ---
            uint8_t new_iodp = g_current_iodp;
            switch (mt) {
                case 1:  if (before.data.mt1.iodp != tm->data.mt1.iodp) new_iodp = tm->data.mt1.iodp; break;
                case 2: case 3: case 4: case 5:
                         if (before.data.mt2_5.iodp != tm->data.mt2_5.iodp) new_iodp = tm->data.mt2_5.iodp; break;
                case 7:  if (before.data.mt7.iodp != tm->data.mt7.iodp) new_iodp = tm->data.mt7.iodp; break;
                case 24: if (before.data.mt24.iodp != tm->data.mt24.iodp) new_iodp = tm->data.mt24.iodp; break;
                case 28: if (before.data.mt28.iodp != tm->data.mt28.iodp) new_iodp = tm->data.mt28.iodp; break;
            }
            if (new_iodp != g_current_iodp) {
                g_current_iodp = new_iodp & 3;
                printf("--- System IODP manually set to %d by user edit ---\n", g_current_iodp);
            }
            
            // *** NEW: Initialize live MT9 data before starting tx ***
            if (mt == 9 && g_orbit_model == ORBIT_MODEL_STAT128 && g_stat_model_128.initialized) {
                copy_msg_to_model_data(&tm->data.mt9, &g_stat_model_128.live_data);
                printf("STAT128 model live state initialized from user-edited data.\n");
            } else if (mt == 9 && g_orbit_model == ORBIT_MODEL_STAT143 && g_stat_model_143.initialized) {
                copy_msg_to_model_data(&tm->data.mt9, &g_stat_model_143.live_data);
                printf("STAT143 model live state initialized from user-edited data.\n");
            }

            printf("--- Finished editing. Starting continuous transmission... ---\n");
            printf("Press ESC to stop transmission and return to menu.\n");
            
            bool keep_sending_this = true;
            char hex[256] = {0};
            uint8_t bin[32];
            size_t blen;
            char ts[16] = {0};

            while (keep_sending_this) {
                // 1. Apply the parameter increment rule
                apply_increment_if_rule(tm);
                
                // 1b. Force the global IOD values (IODP, IODF, IODI)
                apply_global_iodp(tm);
                apply_global_iodf(tm);
                apply_global_iodi(tm);
                
                // 2. Apply ORBIT MODEL override
                apply_orbit_model_override(tm); // <-- UPDATED

                // 3. Encode
                if (encode_message_from_tm(tm, hex) != 0) {
                    printf("Encoding failed for MT%d. Stopping transmission.\n", mt);
                    break;
                }
                
                // 4. Convert
                blen = tx_hex_to_bin(hex, bin, sizeof(bin));
                if (blen == 0) {
                    printf("Failed to convert hex to binary. Stopping transmission.\n");
                    break;
                }

                // 5. Broadcast
                tx_server_broadcast(bin, blen);
                
                // Log to RINEX output file if configured
                if (rinex_writer_is_active()) {
                    rinex_writer_add_packet(sat, mt, hex, time(NULL));
                }
                
                // Increment TLE simulation time after each transmission (if in simulation mode)
                if (g_orbit_model == ORBIT_MODEL_TLE && g_tle_use_fixed_time) {
                    g_tle_simulation_offset++;
                }
                
                // 6. Log
                current_time_hhmmss(ts, sizeof(ts));
                printf("[%s] [SingleTX] Sent MT%d (sat %u) %zu bytes | HEX: %s\n", ts, mt, sat, blen, hex);

                // 7. Accept clients
                accept_new_clients();
                
                // 8. Wait
                Sleep(1000);

                // 9. Check for stop
                if (_kbhit()) {
                    if (_getch() == 27) { // ESC key
                        keep_sending_this = false;
                    }
                }
            }
            printf("--- Stopped single message transmission. Returning to menu. ---\n");
            break;
        }
        case 7: {
            // Configure RINEX output file
            char line[256];
            printf("\n=== RINEX OUTPUT CONFIGURATION ===\n");
            
            if (rinex_writer_is_active()) {
                printf("RINEX output is currently ENABLED\n");
                printf("Output file: %s\n", g_rinex_output_path ? g_rinex_output_path : "(unknown)");
                printf("\n1. Disable RINEX output\n");
                printf("2. Change output file path\n");
                printf("0. Cancel\n");
                printf("Choice: ");
                
                if (!fgets(line, sizeof(line), stdin)) break;
                int sub_choice = (int)strtol(line, NULL, 10);
                
                if (sub_choice == 1) {
                    // Disable RINEX output
                    rinex_writer_close();
                    free(g_rinex_output_path);
                    g_rinex_output_path = NULL;
                    printf("RINEX output disabled.\n");
                } else if (sub_choice == 2) {
                    // Change output file path
                    rinex_writer_close();
                    free(g_rinex_output_path);
                    g_rinex_output_path = NULL;
                    
                    printf("Enter RINEX output file path: ");
                    if (fgets(line, sizeof(line), stdin)) {
                        char *nl = strchr(line, '\n');
                        if (nl) *nl = '\0';
                        
                        if (strlen(line) > 0) {
                            const char *template_path = tx_source_get_bound_path();
                            int init_result = template_path 
                                ? rinex_writer_init_from_template(template_path, line)
                                : rinex_writer_init(line);
                            if (init_result == 0) {
                                g_rinex_output_path = strdup(line);
                                printf("RINEX output enabled: %s\n", g_rinex_output_path);
                            } else {
                                printf("Failed to initialize RINEX writer.\n");
                            }
                        }
                    }
                }
            } else {
                printf("RINEX output is currently DISABLED\n");
                printf("\nEnter RINEX output file path (or press Enter to cancel): ");
                
                if (fgets(line, sizeof(line), stdin)) {
                    char *nl = strchr(line, '\n');
                    if (nl) *nl = '\0';
                    
                    if (strlen(line) > 0) {
                        const char *template_path = tx_source_get_bound_path();
                        int init_result = template_path 
                            ? rinex_writer_init_from_template(template_path, line)
                            : rinex_writer_init(line);
                        if (init_result == 0) {
                            g_rinex_output_path = strdup(line);
                            printf("RINEX output enabled: %s\n", g_rinex_output_path);
                        } else {
                            printf("Failed to initialize RINEX writer.\n");
                        }
                    } else {
                        printf("RINEX output remains disabled.\n");
                    }
                }
            }
            break;
        }
        case 0:
            printf("Exiting...\n");
            exit(0);
            break;
        default:
            printf("Invalid choice\n");
            break;
    }
}


// Cleanup TCP server
void cleanup_tcp_server(void) {
    tx_server_cleanup();
}

// Cleanup RINEX writer
void cleanup_rinex_writer(void) {
    if (rinex_writer_is_active()) {
        rinex_writer_close();
    }
    if (g_rinex_output_path) {
        free(g_rinex_output_path);
        g_rinex_output_path = NULL;
    }
}

/* Cleanup wrappers for atexit: stat_model free functions expect a pointer to the
   model instance, but atexit handlers must be void(void). Provide small wrappers
   that forward to the proper free functions using the global instances. */
static void cleanup_stat_model_128_wrapper(void) {
    stat_model_128_free(&g_stat_model_128);
}
static void cleanup_stat_model_143_wrapper(void) {
    stat_model_143_free(&g_stat_model_143);
}

/* Cleanup TLE file path */
static void cleanup_tle_data(void) {
    if (g_tle_file_path) {
        free(g_tle_file_path);
        g_tle_file_path = NULL;
    }
}


// Main function
int main(int argc, char* argv[]) {
    /* Optional MT9-from-TLE path */
    static const char *s_tle_path = NULL;
    printf("HERCULES TRANSMITTER SYSTEM\n");
    printf("===========================\n");
    
    // === NEW: INITIALIZE MODELS ===
    stat_model_128_init(&g_stat_model_128);
    stat_model_143_init(&g_stat_model_143);
    
    // Quick self-test mode: if '--selftest' passed, send one MT63 directly and exit
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        printf("Running selftest (no TCP server initialization)\n");
        char hex_output[64] = {0}; uint8_t filler[32];
        if (encode_mt63_from(NULL, hex_output) == 0) {
            size_t len = tx_hex_to_bin(hex_output, filler, sizeof(filler));
            tx_server_broadcast(filler, len);
            char ts[16] = {0}; current_time_hhmmss(ts, sizeof(ts));
            printf("[%s] Selftest: sent MT63 filler (%zu bytes)\n", ts, len);
        }
        return 0;
    }

    /* Optional: if a file path is provided... */
    bool file_bound = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--selftest") == 0) continue; 
        if (strcmp(argv[i], "--bind-file") == 0 && i + 1 < argc) {
            if (tx_source_bind_file(argv[++i]) == 0) {
                printf("Provider bound from file. Messages available: %zu\n", tx_source_bound_count());
                file_bound = true;
            } else {
                fprintf(stderr, "Failed to bind provider from file.\n");
            }
            continue;
        }
        if (strcmp(argv[i], "--mt9-from-tle") == 0 && i + 1 < argc) {
            s_tle_path = argv[++i];
            printf("MT9 position/velocity will be sourced from TLE: %s\n", s_tle_path);
            continue;
        }
        /* Treat a lone non-switch arg as a file path */
        if (argv[i][0] != '-') {
            if (tx_source_bind_file(argv[i]) == 0) {
                printf("Provider bound from file. Messages available: %zu\n", tx_source_bound_count());
                file_bound = true;
            } else {
                fprintf(stderr, "Failed to bind provider from file.\n");
            }
        }
    }
    
    // --- NEW: TRAIN MODELS IF FILE WAS LOADED ---
    if (file_bound) {
        train_statistical_models();
    }
    // --- END NEW LOGIC ---

    /* publish global pointer... */
    {
        extern const char *g_mt9_tle_path; /* defined below */
        extern int g_mt9_sat_id; /* defined below */
        
        // This logic is for pre-allocating the TLE path from command line
        if (s_tle_path && s_tle_path[0]) {
            extern char *g_mt9_tle_alloc; /* defined below */
            g_mt9_tle_alloc = strdup(s_tle_path); // Use strdup
            g_mt9_tle_path = (const char*)g_mt9_tle_alloc;
            printf("TLE file provided on command line: %s\n", g_mt9_tle_path);
            printf("Use menu option 5 to select TLE model and target satellite ID.\n");
        } else {
            g_mt9_tle_path = NULL; // Ensure it's NULL if not provided
        }
        g_mt9_sat_id = -1; // Default TLE satID to -1
    }

    // Initialize TCP server
    if (initialize_tcp_server() != 0) {
        fprintf(stderr, "Failed to initialize TCP server\n");
        return 1;
    }
    
    // Setup cleanup on exit
    atexit(cleanup_tcp_server);
    atexit(tx_source_unbind);
    atexit(cleanup_tle_data);
    atexit(cleanup_rinex_writer);
    /* Register zero-arg wrapper functions that free the global stat-models */
    atexit(cleanup_stat_model_128_wrapper);
    atexit(cleanup_stat_model_143_wrapper);

    while (1) {
        print_menu();
        handle_user_input();
        accept_new_clients();  // Always check for new connections
    }
    
    return 0;
}

/* Global TLE path used to drive MT9 pos/vel updates (NULL when inactive) */
/* If the user sets a TLE path from the interactive menu we allocate it here; this
   pointer owns that allocation and is freed on overwrite. */
char *g_mt9_tle_alloc = NULL;
const char *g_mt9_tle_path = NULL;
/* Satellite ID selected by the user for MT9 TLE overrides. -1 means no override. */
int g_mt9_sat_id = -1;