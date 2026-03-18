#define _CRT_SECURE_NO_WARNINGS
#include "TLE_implementation.h"
#include <stdio.h>
#include <string.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <stdlib.h>

/* --- Utility: clamp angle to [-pi, pi) --- */
static double wrap_pi(double x){
    while (x >= M_PI) x -= 2.0*M_PI;
    while (x < -M_PI) x += 2.0*M_PI;
    return x;
}

/* --- Julian date helpers --- */
static int is_leap(int y){ return ( (y%4==0 && y%100!=0) || (y%400==0) ); }
static int doy_to_ymd(int year, int doy, int *out_m, int *out_d){
    static const int mdays_norm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    static const int mdays_leap[12] = {31,29,31,30,31,30,31,31,30,31,30,31};
    const int *md = is_leap(year)?mdays_leap:mdays_norm;
    int m=0, d=doy;
    while (m<12){ if (d>md[m]){ d-=md[m]; m++; } else break; }
    *out_m = m+1; *out_d = d; return 0;
}
static double jd_from_ymd_hms(int Y,int M,int D,int h,int m,double s){
    int a = (14 - M)/12;
    int y = Y + 4800 - a;
    int mm = M + 12*a - 3;
    int JDN = D + (153*mm + 2)/5 + 365*y + y/4 - y/100 + y/400 - 32045;
    double frac = (h - 12)/24.0 + m/1440.0 + s/86400.0; /* JD starts at noon */
    return (double)JDN + frac;
}
static double jd_from_year_doy_frac(int year, double day_of_year){
    int doy_int = (int)floor(day_of_year);
    double day_frac = day_of_year - (double)doy_int;
    int month=1, day=1; doy_to_ymd(year, doy_int, &month, &day);
    double seconds = day_frac * 86400.0;
    int hh = (int)floor(seconds/3600.0);
    seconds -= hh*3600.0;
    int mm = (int)floor(seconds/60.0);
    seconds -= mm*60.0;
    return jd_from_ymd_hms(year, month, day, hh, mm, seconds);
}

/* --- Simple GMST (approx) --- */
static double gmst_rad_from_jd(double jd){
    double T = (jd - 2451545.0)/36525.0; /* centuries since J2000.0 */
    double gmst = 280.46061837 + 360.98564736629*(jd - 2451545.0)
                + 0.000387933*T*T - (T*T*T)/38710000.0; /* degrees */
    double gmst_rad = gmst * (M_PI/180.0);
    gmst_rad = fmod(gmst_rad, 2.0*M_PI);
    if (gmst_rad < 0) gmst_rad += 2.0*M_PI;
    return gmst_rad;
}

/* --- Rotation helpers --- */
static void rz(double a, const double v[3], double out[3]){
    double c=cos(a), s=sin(a);
    out[0] = c*v[0] - s*v[1];
    out[1] = s*v[0] + c*v[1];
    out[2] = v[2];
}
static void rx(double a, const double v[3], double out[3]){
    double c=cos(a), s=sin(a);
    out[0] = v[0];
    out[1] = c*v[1] - s*v[2];
    out[2] = s*v[1] + c*v[2];
}
static void mat_mul_rz_rx_rz(double O,double i,double w,const double v[3], double out[3]){
    double t1[3], t2[3];
    rz(w, v, t1);
    rx(i, t1, t2);
    rz(O, t2, out);
}

/* --- TLE parsing --- */
static int parse_int(const char *s, int n){ char buf[16]; int k = n<15?n:15; memcpy(buf,s,k); buf[k]='\0'; return atoi(buf); }
static double parse_double(const char *s, int n){ char buf[32]; int k=n<31?n:31; memcpy(buf,s,k); buf[k]='\0'; return atof(buf); }

int tle_parse_lines(const char *l1, const char *l2, TLE_Elements *out){
    if (!l1 || !l2 || !out) return -1;
    if (strlen(l1) < 69 || strlen(l2) < 69) return -1;
    /* Epoch year (cols 19-20), epoch day (cols 21-32) per TLE format */
    int yy   = parse_int(l1+18, 2);
    double d = parse_double(l1+20, 12);
    int year = (yy >= 57) ? (1900 + yy) : (2000 + yy);

    /* Line 2 fields (fixed columns per CelesTrak TLE):
       Inc (deg)   cols 9-16
       RAAN (deg)  cols 18-25
       Ecc (no .)  cols 27-33
       ArgP (deg)  cols 35-42
       M (deg)     cols 44-51
       n (rev/d)   cols 53-63
    */
    double inc  = parse_double(l2+8, 8);
    double raan = parse_double(l2+17, 8);
    double ecc7 = parse_double(l2+26, 7); /* e.g., 0001234 */
    double argp = parse_double(l2+34, 8);
    double Mdeg = parse_double(l2+43, 8);
    double nrd  = parse_double(l2+52, 11);

    out->epoch_year = year;
    out->epoch_day = d;
    out->inc_deg = inc;
    out->raan_deg = raan;
    out->ecc = ecc7 * 1e-7;
    out->argp_deg = argp;
    out->M_deg = Mdeg;
    out->n_rev_per_day = nrd;
    return 0;
}

int tle_load_first_pair(const char *path, TLE_Elements *out){
    if (!path || !out) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char l1[128]={0}, l2[128]={0};
    /* Skip blank/comment lines until we find line starting with '1 ' then read next as line 2 */
    char buf[256];
    int found = 0;
    while (fgets(buf, sizeof(buf), f)){
        if (buf[0]=='1' && (buf[1]==' ' || buf[1]=='0')){
            strncpy(l1, buf, sizeof(l1)-1);
            if (fgets(l2, sizeof(l2), f)){
                if (l2[0]=='2' && (l2[1]==' ' || l2[1]=='0')){ found = 1; break; }
            }
        }
    }
    fclose(f);
    if (!found) return -1;
    return tle_parse_lines(l1, l2, out);
}

/* Two-body (Kepler) propagation, adequate for GEO and short durations. */
int tle_propagate_ecef(const TLE_Elements *tle, time_t now_utc, double r_ecef[3], double v_ecef[3]){
    if (!tle || !r_ecef || !v_ecef) return -1;
    /* Constants */
    const double mu = 3.986004418e14;      /* m^3/s^2 */
    const double deg2rad = M_PI/180.0;
    const double OMEGA_E = 7.2921159e-5;   /* rad/s Earth rotation */

    /* Epoch JD */
    double jd_epoch = jd_from_year_doy_frac(tle->epoch_year, tle->epoch_day);
    /* now JD (UTC) */
    struct tm *gmt = gmtime(&now_utc);
    int Y = gmt->tm_year + 1900, Mo = gmt->tm_mon + 1, D = gmt->tm_mday;
    int h = gmt->tm_hour, m = gmt->tm_min; double s = (double)gmt->tm_sec;
    double jd_now = jd_from_ymd_hms(Y, Mo, D, h, m, s);
    double tsince = (jd_now - jd_epoch) * 86400.0; /* seconds */

    /* Elements */
    double i = tle->inc_deg * deg2rad;
    double O = tle->raan_deg * deg2rad;
    double e = tle->ecc; if (e < 0) e = 0; if (e >= 1.0) e = 0.999999;
    double w = tle->argp_deg * deg2rad;
    double M0 = tle->M_deg * deg2rad;
    double n = tle->n_rev_per_day * (2.0*M_PI/86400.0); /* rad/s */

    /* Semi-major axis from mean motion */
    double a = cbrt(mu / (n*n)); /* meters */
    double M = wrap_pi(M0 + n*tsince);

    /* Solve Kepler for E */
    double E = M; /* initial guess */
    for (int it=0; it<10; ++it){
        double f = E - e*sin(E) - M;
        double fp = 1.0 - e*cos(E);
        double dE = -f/fp;
        E += dE;
        if (fabs(dE) < 1e-12) break;
    }
    double cosE = cos(E), sinE = sin(E);
    double sqrt1me2 = sqrt(1.0 - e*e);
    double nu = atan2(sqrt1me2 * sinE, cosE - e);

    /* Parameters */
    double r = a * (1.0 - e*cosE);
    double p = a * (1.0 - e*e);
    double sqrt_mu_p = sqrt(mu / p);

    /* Perifocal position and velocity */
    double r_pf[3] = { r * cos(nu), r * sin(nu), 0.0 };
    double v_pf[3] = { -sqrt_mu_p * sin(nu), sqrt_mu_p * (e + cos(nu)), 0.0 };

    /* Rotate to ECI */
    double r_eci[3], v_eci[3];
    mat_mul_rz_rx_rz(O, i, w, r_pf, r_eci);
    mat_mul_rz_rx_rz(O, i, w, v_pf, v_eci);

    /* ECI -> ECEF with GMST */
    double gmst = gmst_rad_from_jd(jd_now);
    /* Rotate by -gmst about Z for both r and v */
    double r_ecef_tmp[3], v_rot[3];
    rz(-gmst, r_eci, r_ecef_tmp);
    rz(-gmst, v_eci, v_rot);
    /* Account for Earth rotation: v_ecef = R*v_eci - omega x r_ecef */
    double omega[3] = {0.0, 0.0, OMEGA_E};
    double wxr[3] = { omega[1]*r_ecef_tmp[2] - omega[2]*r_ecef_tmp[1],
                      omega[2]*r_ecef_tmp[0] - omega[0]*r_ecef_tmp[2],
                      omega[0]*r_ecef_tmp[1] - omega[1]*r_ecef_tmp[0] };
    r_ecef[0] = r_ecef_tmp[0]; r_ecef[1] = r_ecef_tmp[1]; r_ecef[2] = r_ecef_tmp[2];
    v_ecef[0] = v_rot[0] - wxr[0];
    v_ecef[1] = v_rot[1] - wxr[1];
    v_ecef[2] = v_rot[2] - wxr[2];

    return 0;
}

/* Load all TLE entries from a file with their names */
int tle_load_all_entries(const char *path, TLE_Entry *entries, int max_entries){
    if (!path || !entries || max_entries <= 0) return -1;
    
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    char buf[256];
    char name_line[128] = {0};
    char l1[128] = {0};
    char l2[128] = {0};
    int count = 0;
    int state = 0; /* 0=looking for name, 1=have name looking for line1, 2=have line1 looking for line2 */
    
    while (fgets(buf, sizeof(buf), f) && count < max_entries) {
        /* Remove newline */
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
        if (len > 1 && buf[len-2] == '\r') buf[len-2] = '\0';
        
        /* Skip empty lines */
        if (strlen(buf) == 0) continue;
        
        if (state == 0) {
            /* Looking for name line (not starting with '1' or '2') */
            if (buf[0] != '1' && buf[0] != '2') {
                strncpy(name_line, buf, sizeof(name_line)-1);
                name_line[sizeof(name_line)-1] = '\0';
                state = 1;
            }
        } else if (state == 1) {
            /* Looking for line 1 */
            if (buf[0] == '1' && (buf[1] == ' ' || buf[1] == '0')) {
                strncpy(l1, buf, sizeof(l1)-1);
                l1[sizeof(l1)-1] = '\0';
                state = 2;
            } else {
                /* Not a TLE line, treat as new name */
                strncpy(name_line, buf, sizeof(name_line)-1);
                name_line[sizeof(name_line)-1] = '\0';
            }
        } else if (state == 2) {
            /* Looking for line 2 */
            if (buf[0] == '2' && (buf[1] == ' ' || buf[1] == '0')) {
                strncpy(l2, buf, sizeof(l2)-1);
                l2[sizeof(l2)-1] = '\0';
                
                /* Parse and store */
                if (tle_parse_lines(l1, l2, &entries[count].elements) == 0) {
                    strncpy(entries[count].name, name_line, sizeof(entries[count].name)-1);
                    entries[count].name[sizeof(entries[count].name)-1] = '\0';
                    entries[count].sat_id = -1; /* User will set this */
                    entries[count].enabled = 0; /* Disabled by default */
                    count++;
                }
                
                /* Reset for next entry */
                state = 0;
                name_line[0] = '\0';
                l1[0] = '\0';
                l2[0] = '\0';
            } else {
                /* Not line 2, restart */
                state = 0;
                if (buf[0] != '1' && buf[0] != '2') {
                    strncpy(name_line, buf, sizeof(name_line)-1);
                    name_line[sizeof(name_line)-1] = '\0';
                    state = 1;
                }
            }
        }
    }
    
    fclose(f);
    return count;
}

/* Update MT9 message from TLE elements directly */
int mt9_update_from_tle_elements(SBAS_MT9_Message *mt9, const TLE_Elements *tle, time_t now_utc){
    if (!mt9 || !tle) return -1;
    double r_ecef[3], v_ecef[3];
    if (tle_propagate_ecef(tle, now_utc, r_ecef, v_ecef) != 0) return -1;
    /* Update only position and velocity fields; leave others untouched */
    mt9->xg_ecef = r_ecef[0];
    mt9->yg_ecef = r_ecef[1];
    mt9->zg_ecef = r_ecef[2];
    mt9->xg_rate = v_ecef[0];
    mt9->yg_rate = v_ecef[1];
    mt9->zg_rate = v_ecef[2];
    return 0;
}

int mt9_update_from_tle_file(SBAS_MT9_Message *mt9, const char *tle_path, time_t now_utc){
    if (!mt9 || !tle_path) return -1;
    TLE_Elements tle;
    if (tle_load_first_pair(tle_path, &tle) != 0) return -1;
    return mt9_update_from_tle_elements(mt9, &tle, now_utc);
}
