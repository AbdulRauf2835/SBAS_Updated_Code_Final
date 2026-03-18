#ifndef TLE_IMPLEMENTATION_H
#define TLE_IMPLEMENTATION_H

#include <time.h>
#include <stdint.h>
#include "message_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal TLE elements we need for two-body Kepler propagation */
typedef struct {
    int    epoch_year;      /* full year e.g., 2025 */
    double epoch_day;       /* day-of-year with fraction */
    double inc_deg;         /* inclination (deg) */
    double raan_deg;        /* RAAN (deg) */
    double ecc;             /* eccentricity (unitless) */
    double argp_deg;        /* argument of perigee (deg) */
    double M_deg;           /* mean anomaly at epoch (deg) */
    double n_rev_per_day;   /* mean motion (rev/day) */
} TLE_Elements;

/* TLE entry with name and satellite ID */
typedef struct {
    char name[64];          /* Satellite name from file */
    int sat_id;             /* PRN/Satellite ID */
    TLE_Elements elements;  /* TLE orbital elements */
    int enabled;            /* 1 = enabled, 0 = disabled */
} TLE_Entry;

/* Parse two TLE lines into elements; returns 0 on success */
int tle_parse_lines(const char *line1, const char *line2, TLE_Elements *out);

/* Load first TLE pair from a simple text file (expects two consecutive lines forming a valid TLE). */
int tle_load_first_pair(const char *path, TLE_Elements *out);

/* Load all TLE entries from file with their names. Returns number of entries loaded. */
int tle_load_all_entries(const char *path, TLE_Entry *entries, int max_entries);

/* Propagate using simple Kepler (two-body), from epoch to now_utc, returning ECEF position (m) and velocity (m/s). */
int tle_propagate_ecef(const TLE_Elements *tle, time_t now_utc, double r_ecef[3], double v_ecef[3]);

/* Update MT9 message from TLE elements directly */
int mt9_update_from_tle_elements(SBAS_MT9_Message *mt9, const TLE_Elements *tle, time_t now_utc);

/* Convenience: update only MT9 position and velocity fields from TLE, keeping other fields intact. Returns 0 on success. */
int mt9_update_from_tle_file(SBAS_MT9_Message *mt9, const char *tle_path, time_t now_utc);

#ifdef __cplusplus
}
#endif

#endif /* TLE_IMPLEMENTATION_H */
