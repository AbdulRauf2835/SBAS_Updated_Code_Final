#include "message_types.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "bitops.h"
#include <math.h> // for pow()


/* helper: compact hex (remove spaces and uppercase) - made private with static */
static void compact_hex(const char *hex, char *out, size_t outlen)
{    size_t o = 0;
    for (size_t i = 0; hex[i] && o + 1 < outlen; ++i) {
        char c = hex[i];
        if (!isspace((unsigned char)c)) out[o++] = (char)toupper((unsigned char)c); }
    out[o] = '\0';
}


/* Into-struct: MT0 (Test Message). Copy raw 212-bit payload and 24-bit CRC. */
int decode_mt0_into(const char *hex, SBAS_MT0_Message *m)
{
    if (!hex || !m) return -1;
    /* compact and convert to bytes */
    char c[512]; compact_hex(hex, c, sizeof(c));
    int nbytes = (int)strlen(c) / 2;
    unsigned char b[64] = {0};
    for (int i = 0; i < nbytes && i < (int)sizeof(b); ++i) {
        unsigned int v = 0; sscanf(&c[i*2], "%2x", &v); b[i] = (unsigned char)v;
    }
    const int base = 14; /* payload start (after 8-bit preamble + 6-bit MT) */
    memset(m->test_data, 0, sizeof(m->test_data));
    for (int i = 0; i < 212; ++i) {
        unsigned bit = (unsigned)sbas_bits_get_u64(b, base + i, 1);
        if (bit) {
            int byte = i >> 3;
            int bit_in_byte = 7 - (i & 7);
            if (byte >= 0 && byte < (int)sizeof(m->test_data)) {
                m->test_data[byte] |= (uint8_t)(1u << bit_in_byte);
            }
        }
    }
    m->crc = (uint32_t)sbas_bits_get_u64(b, base + 212, 24);
    return 0;
}

/* Into-struct: MT1 PRN mask + IODP + CRC
 * Stores: header fields must be set by caller; here we fill prn_mask as packed bits,
 * iodp (2 LSBits), and crc. */
int decode_mt1_into(const char *hex, SBAS_MT1_Message *m)
{
    if (!hex || !m) return -1;
    char c[1024]; compact_hex(hex,c,sizeof(c));
    int nbytes = (int)strlen(c)/2;
    unsigned char b[64]={0};
    for (int i=0;i<nbytes && i<(int)sizeof(b); ++i){ unsigned int v=0; sscanf(&c[i*2],"%2x",&v); b[i]=(unsigned char)v; }
    /* payload starts at bit 14 */
    const int base = 14;
    const int mask_bits = 210; /* per decoder_mt1: 210-bit PRN mask at payload start */
    /* Pack 210 bits MSB-first into 27 bytes to match decoder_mt1() bit view */
    memset(m->prn_mask, 0, sizeof(m->prn_mask));
    for (int i = 0; i < mask_bits; ++i) {
        unsigned bit = (unsigned)sbas_bits_get_u64(b, base + i, 1);
        if (bit) {
            int byte = i >> 3;                 /* i/8 */
            int bit_in_byte = 7 - (i & 7);     /* MSB-first within each byte */
            m->prn_mask[byte] |= (1u << bit_in_byte);}}
    /* Ensure the trailing 6 bits of the last byte are zero (only top 2 bits carry PRN mask bits). */
    m->prn_mask[26] &= 0xC0; /* 1100 0000 */
    /* Next 2 bits are IODP; CRC follows */
    const int iodp_off = base + mask_bits;     /* 14 + 210 = 224 */
    const int crc_off  = iodp_off + 2;         /* 226 */
    m->iodp = (uint8_t)sbas_bits_get_u64(b, iodp_off, 2);
    m->crc  = (uint32_t)sbas_bits_get_u64(b, crc_off, 24);
    return 0;
}





/* Into-struct: MT2..5 minimal population for MT2 request (IODF, IODP, 13 fast corrections, 13 UDREIs, CRC). */
int decode_mt2_into(const char *hex, SBAS_MT2_5_Message *m)
{
    if (!hex || !m) return -1;
    char c[512]; compact_hex(hex,c,sizeof(c)); int nbytes=(int)strlen(c)/2;
    unsigned char b[64]={0}; 
    for (int i=0;i<nbytes && i<(int)sizeof(b);++i)
    { unsigned int v=0; sscanf(&c[i*2],"%2x",&v); 
    b[i]=(unsigned char)v; }
    int base=14;
    m->iodf = (uint8_t)sbas_bits_get_u64(b, base, 2);
    m->iodp = (uint8_t)sbas_bits_get_u64(b, base+2, 2);
    int fc_start = base + 4;
    for (int i=0;i<13;++i) {
        int64_t v = sbas_bits_get_s64(b, fc_start + i*12, 12);
        /* store raw 12-bit signed into 16-bit; no scaling here */
        m->sat_corrections[i].fast_correction = (int16_t)v;
    }
    int ud_start = fc_start + 13*12;
    for (int i=0;i<13;++i) {
        uint64_t u = sbas_bits_get_u64(b, ud_start + i*4, 4);
        m->sat_corrections[i].udrei = (uint8_t)u;
    }
    m->crc = (uint32_t)sbas_bits_get_u64(b, ud_start + 13*4, 24);
    return 0;
}

/* Into-struct: MT6 Integrity Information (IODF2..IODF5, 51x UDREI, CRC) */
int decode_mt6_into(const char *hex, SBAS_MT6_Message *m)
{
    if (!hex || !m) return -1;
    char c[1024]; compact_hex(hex, c, sizeof(c));
    int nbytes = (int)strlen(c)/2;
    unsigned char b[64] = {0};
    for (int i = 0; i < nbytes && i < (int)sizeof(b); ++i) {
        unsigned int v = 0; sscanf(&c[i*2], "%2x", &v); b[i] = (unsigned char)v;
    }
    const int base = 14;
    m->iodf2 = (uint8_t)sbas_bits_get_u64(b, base, 2);
    m->iodf3 = (uint8_t)sbas_bits_get_u64(b, base+2, 2);
    m->iodf4 = (uint8_t)sbas_bits_get_u64(b, base+4, 2);
    m->iodf5 = (uint8_t)sbas_bits_get_u64(b, base+6, 2);
    const int ud_start = base + 8;
    for (int i = 0; i < 51; ++i) {
        uint64_t u = sbas_bits_get_u64(b, ud_start + i*4, 4);
        m->udrei[i] = (uint8_t)u; /* lower 4 bits used */
    }
    m->crc = (uint32_t)sbas_bits_get_u64(b, ud_start + 51*4, 24);
    return 0;
}






/* Into-struct: MT7 Degradation Parameters (t_lat, iodp, spare, 51x UDRE AI, CRC) */
int decode_mt7_into(const char *hex, SBAS_MT7_Message *m)
{
    if (!hex || !m) return -1;
    char c[1024]; compact_hex(hex, c, sizeof(c));
    int nbytes = (int)strlen(c)/2;
    unsigned char b[64] = {0};
    for (int i = 0; i < nbytes && i < (int)sizeof(b); ++i) { unsigned int v=0; sscanf(&c[i*2], "%2x", &v); b[i] = (unsigned char)v; }
    const int base = 14;
    m->t_lat = (uint8_t)sbas_bits_get_u64(b, base, 4);
    m->iodp  = (uint8_t)sbas_bits_get_u64(b, base+4, 2);
    
    const int ud_start = base + 8;
    for (int i = 0; i < 51; ++i) {
        uint64_t u = sbas_bits_get_u64(b, ud_start + i*4, 4);
        m->udre_ai[i] = (uint8_t)u; /* store raw 4 bits */
    }
    m->crc = (uint32_t)sbas_bits_get_u64(b, ud_start + 51*4, 24);
    return 0;
}





int decode_mt9_into(const char *hex, SBAS_MT9_Message *m)
{
    if (!hex || !m) return -1;

    // Compact hex and convert to bytes
    char c[1024]; compact_hex(hex, c, sizeof(c));
    int nbytes = (int)strlen(c) / 2;
    unsigned char b[64] = {0};
    for (int i = 0; i < nbytes && i < (int)sizeof(b); ++i) {
        unsigned int v = 0; sscanf(&c[i*2], "%2x", &v); b[i] = (unsigned char)v;
    }

    int base = 14;
    int off = base;

    // Reserved (8 bits)
    m->reserved = (uint8_t)sbas_bits_get_u64(b, off, 8); off += 8;

    // t0 (13), URA (4)
    uint64_t t0_raw = sbas_bits_get_u64(b, off, 13); off += 13;
    m->t0 = (double)t0_raw * 16.0; /* seconds */
    m->ura = (uint8_t)sbas_bits_get_u64(b, off, 4);   off += 4;

    // Positions (meters)
    int64_t xg_raw = sbas_bits_get_s64(b, off, 30); off += 30;
    int64_t yg_raw = sbas_bits_get_s64(b, off, 30); off += 30;
    int64_t zg_raw = sbas_bits_get_s64(b, off, 25); off += 25;
    m->xg_ecef = (double)xg_raw * 0.08;
    m->yg_ecef = (double)yg_raw * 0.08;
    m->zg_ecef = (double)zg_raw * 0.4;

    // Rates (m/s)
    int64_t xg_rate_raw = sbas_bits_get_s64(b, off, 17); off += 17;
    int64_t yg_rate_raw = sbas_bits_get_s64(b, off, 17); off += 17;
    int64_t zg_rate_raw = sbas_bits_get_s64(b, off, 18); off += 18;
    m->xg_rate = (double)xg_rate_raw * 0.000625;   /* 2^-11 */
    m->yg_rate = (double)yg_rate_raw * 0.000625;
    m->zg_rate = (double)zg_rate_raw * 0.004;      /* 2^-8 */

    // Accelerations (m/s^2)
    int64_t xg_acc_raw = sbas_bits_get_s64(b, off, 10); off += 10;
    int64_t yg_acc_raw = sbas_bits_get_s64(b, off, 10); off += 10;
    int64_t zg_acc_raw = sbas_bits_get_s64(b, off, 10); off += 10;
    m->xg_accel = (double)xg_acc_raw * 0.0000125;   /* 1.25e-5 */
    m->yg_accel = (double)yg_acc_raw * 0.0000125;
    m->zg_accel = (double)zg_acc_raw * 0.00000625;  /* 6.25e-6 */

    // Clock terms
    int64_t agf0_raw = sbas_bits_get_s64(b, off, 12); off += 12;
    int64_t agf1_raw = sbas_bits_get_s64(b, off, 8);  off += 8;
    m->ag_f0 = (double)agf0_raw * pow(2.0, -31);  /* seconds */
    m->ag_f1 = (double)agf1_raw * pow(2.0, -40);  /* s/s */

    // CRC24 (24 bits)
    m->crc = (uint32_t)sbas_bits_get_u64(b, off, 24); off += 24;

    // Header common fields are set by caller (message_type, preamble, timestamp)
    return 0;
}

/* Into-struct version of MT10: populate scaled doubles into SBAS_MT10_Message */
int decode_mt10_into(const char *hex, SBAS_MT10_Message *m)
{
    if (!hex || !m) return -1;
    /* compact and build byte buffer */
    char c[1024]; compact_hex(hex, c, sizeof(c)); int nbytes = (int)strlen(c)/2;
    unsigned char b[128] = {0};
    for (int i=0;i<nbytes && i<(int)sizeof(b); ++i){ unsigned int v=0; sscanf(&c[i*2],"%2x",&v); b[i]=(unsigned char)v; }

    int base = 14;
    /* raw extraction */
    uint64_t brrc_raw       = sbas_bits_get_u64(b, base + 0, 10);
    uint64_t cltc_lsb_raw   = sbas_bits_get_u64(b, base + 10, 10);
    uint64_t cltc_v1_raw    = sbas_bits_get_u64(b, base + 20, 10);
    uint64_t iltc_v1_raw    = sbas_bits_get_u64(b, base + 30, 9);
    uint64_t cltc_v0_raw    = sbas_bits_get_u64(b, base + 39, 10);
    uint64_t iltc_v0_raw    = sbas_bits_get_u64(b, base + 49, 9);
    uint64_t cgeo_lsb_raw   = sbas_bits_get_u64(b, base + 58, 10);
    uint64_t cgeo_v_raw     = sbas_bits_get_u64(b, base + 68, 10);
    uint64_t igeo_raw       = sbas_bits_get_u64(b, base + 78, 9);
    uint64_t cer_raw        = sbas_bits_get_u64(b, base + 87, 6);
    uint64_t ciono_step_raw = sbas_bits_get_u64(b, base + 93, 10);
    uint64_t iono_raw       = sbas_bits_get_u64(b, base + 103, 9);
    uint64_t ciono_ramp_raw = sbas_bits_get_u64(b, base + 112, 10);
    uint64_t rss_udre_raw   = sbas_bits_get_u64(b, base + 122, 1);
    uint64_t rss_iono_raw   = sbas_bits_get_u64(b, base + 123, 1);
    uint64_t ccov_raw       = sbas_bits_get_u64(b, base + 124, 7);

    /* scales (per existing string decoder) */
    m->B_rrc        = (double)brrc_raw * 0.002;       /* m */
    m->C_ltc_lsb    = (double)cltc_lsb_raw * 0.002;   /* m */
    m->C_ltc_v1     = (double)cltc_v1_raw * 0.00005;  /* m/s */
    m->I_ltc_v1     = (double)iltc_v1_raw * 1.0;      /* s */
    m->C_ltc_v0     = (double)cltc_v0_raw * 0.002;    /* m */
    m->I_ltc_v0     = (double)iltc_v0_raw * 1.0;      /* s */
    m->C_geo_lsb    = (double)cgeo_lsb_raw * 0.0005;  /* m */
    m->C_geo_v      = (double)cgeo_v_raw * 0.00005;   /* m/s */
    m->I_geo        = (double)igeo_raw * 1.0;         /* s */
    m->C_er         = (double)cer_raw * 0.5;          /* m */
    m->C_iono_step  = (double)ciono_step_raw * 0.001; /* m */
    m->I_iono       = (double)iono_raw * 1.0;         /* s */
    m->C_iono_ramp  = (double)ciono_ramp_raw * 0.000005; /* m/s */
    m->rss_udre     = (uint8_t)rss_udre_raw;
    m->rss_iono     = (uint8_t)rss_iono_raw;
    m->C_covariance = (double)ccov_raw * 0.1;         /* dimless scale */

    /* Spare: copy 81 bits into 11 bytes (zero the full array first) */
    memset(m->spare, 0, sizeof(m->spare));
    int spare_start = base + 131;
    for (int i = 0; i < 81; ++i) {
        uint64_t bit = sbas_bits_get_u64(b, spare_start + i, 1);
        if (bit) {
            int byte_idx = i / 8;
            int bit_idx  = 7 - (i % 8); /* store MSB-first in each byte for readability */
            if (byte_idx < (int)sizeof(m->spare)) m->spare[byte_idx] |= (uint8_t)(1u << bit_idx);
        }
    }

    /* CRC24 (24 bits) at end of payload */
    int crc_pos = spare_start + 81; /* 131 + 81 = 212 bits from base */
    m->crc = (uint32_t)sbas_bits_get_u64(b, crc_pos, 24);

    return 0;
}


/* Into-struct: MT12 (Network time / UTC parameters) */
int decode_mt12_into(const char *hex, SBAS_MT12_Message *m)
{
    if (!hex || !m) return -1;

    /* compact and convert to byte buffer */
    char c[1024]; compact_hex(hex, c, sizeof(c));
    int nbytes = (int)strlen(c)/2;
    unsigned char b[128] = {0};
    for (int i=0; i<nbytes && i<(int)sizeof(b); ++i){ unsigned int v=0; sscanf(&c[i*2],"%2x",&v); b[i]=(unsigned char)v; }

    int base = 14; /* payload start */
    int off = base;

    int64_t A1SNT_raw = sbas_bits_get_s64(b, off, 24); off += 24;
    int64_t A0SNT_raw = sbas_bits_get_s64(b, off, 32); off += 32;
    uint64_t t0t_raw  = sbas_bits_get_u64(b, off, 8);  off += 8;
    uint64_t WNt_raw  = sbas_bits_get_u64(b, off, 8);  off += 8;
    int64_t d_tLS_raw = sbas_bits_get_s64(b, off, 8);  off += 8;
    uint64_t WN_LSF_raw = sbas_bits_get_u64(b, off, 8); off += 8;
    uint64_t DN_raw   = sbas_bits_get_u64(b, off, 8);  off += 8;
    int64_t d_tLSF_raw= sbas_bits_get_s64(b, off, 8);  off += 8;
    uint64_t utc_id_raw = sbas_bits_get_u64(b, off, 3); off += 3;

    uint64_t TOW_raw  = sbas_bits_get_u64(b, off, 20); off += 20;
    uint64_t WN_raw   = sbas_bits_get_u64(b, off, 10); off += 10;

    /* skip 75 spare bits */
    off += 75;

    /* CRC24 (24 bits) */
    uint32_t crc = (uint32_t)sbas_bits_get_u64(b, off, 24); off += 24;

    /* Apply ICD scales and populate struct (store physical values) */
    m->A1SNT = (double)A1SNT_raw * pow(2.0, -50); /* s/s */
    m->A0SNT = (double)A0SNT_raw * pow(2.0, -30); /* s */
    m->t0t   = (double)t0t_raw * pow(2.0, 12);    /* seconds */
    m->WNt   = (uint8_t)WNt_raw;                  /* week */
    m->delta_t_ls = (double)d_tLS_raw * 1.0;      /* s */
    m->WN_LSF = (uint8_t)WN_LSF_raw;              /* week */
    m->DN     = (uint8_t)DN_raw;                  /* day (1..7) */
    m->delta_t_lsf = (double)d_tLSF_raw * 1.0;    /* s */
    m->utc_standard_id = (uint8_t)utc_id_raw;     /* 3-bit */
    m->TOW   = (double)TOW_raw * 1.0;             /* seconds */
    m->WN    = (uint16_t)WN_raw;                  /* week */
    m->crc24 = crc & 0xFFFFFFu;                   /* 24-bit */

    return 0;
}








/* Into-struct: MT17 (GEO Almanac) - store LSB-scaled physical values */
int decode_mt17_into(const char *hex, SBAS_MT17_Message *m)
{
    if (!hex || !m) return -1;

    char c[1024]; compact_hex(hex, c, sizeof(c));
    int nbytes = (int)strlen(c)/2;
    unsigned char b[128] = {0};
    for (int i=0; i<nbytes && i<(int)sizeof(b); ++i) {
        unsigned int v=0; sscanf(&c[i*2], "%2x", &v); b[i] = (unsigned char)v;
    }

    const int base = 14; /* payload start */
    for (int i = 0; i < 3; ++i) {
        int blk_off = base + i*67;
        uint64_t data_id = sbas_bits_get_u64(b, blk_off + 0, 2);
        uint64_t prn     = sbas_bits_get_u64(b, blk_off + 2, 8);
        uint64_t health  = sbas_bits_get_u64(b, blk_off +10, 8);
        int64_t  xg_raw  = sbas_bits_get_s64(b, blk_off +18, 15);
        int64_t  yg_raw  = sbas_bits_get_s64(b, blk_off +33, 15);
        int64_t  zg_raw  = sbas_bits_get_s64(b, blk_off +48, 9);
        int64_t  xg_dot_raw = sbas_bits_get_s64(b, blk_off +57, 3);
        int64_t  yg_dot_raw = sbas_bits_get_s64(b, blk_off +60, 3);
        int64_t  zg_dot_raw = sbas_bits_get_s64(b, blk_off +63, 4);

        m->geo[i].data_id = (uint8_t)data_id;
        m->geo[i].prn_number = (uint8_t)prn;
        m->geo[i].health_and_status = (uint8_t)health;
        /* Apply ICD LSB scales */
        m->geo[i].x_g = (double)xg_raw * 2600.0;   /* meters */
        m->geo[i].y_g = (double)yg_raw * 2600.0;   /* meters */
        m->geo[i].z_g = (double)zg_raw * 26000.0;  /* meters */
        m->geo[i].xg_dot = (double)xg_dot_raw * 10.0;  /* m/s */
        m->geo[i].yg_dot = (double)yg_dot_raw * 10.0;  /* m/s */
        m->geo[i].zg_dot = (double)zg_dot_raw * 60.0;  /* m/s */
    }
    int off = base + 3*67;
    uint64_t t0_raw = sbas_bits_get_u64(b, off, 11); off += 11;
    uint64_t crc24  = sbas_bits_get_u64(b, off, 24); off += 24;

    m->t0 = (double)t0_raw * 64.0; /* seconds */
    m->crc24 = (uint32_t)(crc24 & 0xFFFFFFu);
    return 0;
}

int decode_mt18_into(const char *hex, SBAS_MT18_Message *m)
{
    if (!hex || !m) return -1;
    /* compact hex and build byte buffer */
    char c[1024]; compact_hex(hex, c, sizeof(c));
    int nbytes = (int)strlen(c)/2;
    unsigned char b[128] = {0};
    for (int i=0; i<nbytes && i<(int)sizeof(b); ++i){ unsigned int v=0; sscanf(&c[i*2], "%2x", &v); b[i]=(unsigned char)v; }

    const int base = 14; /* payload start */

    /* Header fields */
    m->number_of_bands = (uint8_t)sbas_bits_get_u64(b, base + 0, 4);
    m->band_number     = (uint8_t)sbas_bits_get_u64(b, base + 4, 4);
    m->iodi            = (uint8_t)sbas_bits_get_u64(b, base + 8, 2);
    m->spare           = (uint8_t)sbas_bits_get_u64(b, base + 10, 1);

    /* IGP mask: 201 bits starting at base+11; pack MSB-first into 26 bytes */
    memset(m->igp_mask, 0, sizeof(m->igp_mask));
    const int igp_start = base + 11;
    const int igp_bits = 201;
    for (int i = 0; i < igp_bits; ++i) {
        unsigned bit = (unsigned)sbas_bits_get_u64(b, igp_start + i, 1);
        if (bit) {
            int byte = i >> 3;             /* i/8 */
            int bit_in_byte = 7 - (i & 7); /* MSB-first */
            if (byte >= 0 && byte < (int)sizeof(m->igp_mask)) {
                m->igp_mask[byte] |= (uint8_t)(1u << bit_in_byte);
            }
        }
    }
    /* trailing unused bits in last byte remain 0 by memset above */

    /* CRC (24 bits) follows */
    const int crc_off = igp_start + igp_bits;
    m->crc = (uint32_t)sbas_bits_get_u64(b, crc_off, 24);
    return 0;
}

int decode_mt24_into(const char *hex, SBAS_MT24_Message *m)
{
    if (!hex || !m) return -1;
    char c[1024]; compact_hex(hex, c, sizeof(c));
    int nbytes = (int)strlen(c)/2;
    unsigned char b[256] = {0};
    for (int i=0;i<nbytes && i<(int)sizeof(b); ++i){ unsigned int v=0; sscanf(&c[i*2],"%2x",&v); b[i]=(unsigned char)v; }

    const int base = 14;
    int off = base;

    /* Fast Corrections */
    for (int i=0; i<6; ++i) {
        m->fast_corrections[i] = (int16_t)sbas_bits_get_s64(b, off, 12);
        off += 12;
    }

    /* UDREIs */
    for (int i=0; i<6; ++i) {
        m->udreis[i] = (uint8_t)sbas_bits_get_u64(b, off, 4);
        off += 4;
    }

    /* Control bits */
    m->iodp    = (uint8_t)sbas_bits_get_u64(b, off, 2); off += 2;
    m->block_id= (uint8_t)sbas_bits_get_u64(b, off, 2); off += 2;
    m->iodf    = (uint8_t)sbas_bits_get_u64(b, off, 2); off += 2;
    m->spare   = (uint8_t)sbas_bits_get_u64(b, off, 4); off += 4;

    /* Long-term corrections: 106 bits packed MSB-first into 14 bytes */
    memset(m->long_term_corrections, 0, sizeof(m->long_term_corrections));
    for (int i=0; i<106; ++i) {
        unsigned bit = (unsigned)sbas_bits_get_u64(b, off + i, 1);
        if (bit) {
            int byte = i >> 3;
            int bit_in_byte = 7 - (i & 7);
            if (byte >= 0 && byte < (int)sizeof(m->long_term_corrections)) {
                m->long_term_corrections[byte] |= (uint8_t)(1u << bit_in_byte);
            }
        }
    }
    off += 106;

    /* CRC */
    m->crc = (uint32_t)sbas_bits_get_u64(b, off, 24);
    return 0;
}



/* Into-struct decoder for MT25 (Long-term satellite error corrections)
 * Parses two 106-bit halves with velocity_code-dependent layouts and a trailing 24-bit CRC.
 * Applies ICD LSB scales and stores physical values in SBAS_MT25_Message.
 */
/* helper to parse one 106-bit half of MT25 */
static void parse_mt25_half(const unsigned char *b, int bit_off, SBAS_MT25_Half *half)
{
    memset(half, 0, sizeof(*half));
    unsigned int vcode = (unsigned int)sbas_bits_get_u64(b, bit_off + 0, 1);
    half->raw_velocity_code = (uint8_t)vcode;
    if (vcode == 0) {
        SBAS_MT25_Half_Vel0 *h = &half->u.v0;
        h->velocity_code = 0;
        h->prn_mask_1 = (uint8_t)sbas_bits_get_u64(b, bit_off + 1, 6);
        h->iod_1      = (uint8_t)sbas_bits_get_u64(b, bit_off + 7, 8);
        {
            int16_t dx_raw = (int16_t)sbas_bits_get_s64(b, bit_off + 15, 9);
            int16_t dy_raw = (int16_t)sbas_bits_get_s64(b, bit_off + 24, 9);
            int16_t dz_raw = (int16_t)sbas_bits_get_s64(b, bit_off + 33, 9);
            int16_t da_f0_raw = (int16_t)sbas_bits_get_s64(b, bit_off + 42, 10);
            h->dx_1    = (double)dx_raw * 0.125;              /* meters */
            h->dy_1    = (double)dy_raw * 0.125;              /* meters */
            h->dz_1    = (double)dz_raw * 0.125;              /* meters */
            h->da_f0_1 = (double)da_f0_raw * pow(2.0, -31);   /* seconds */
        }
        h->prn_mask_2 = (uint8_t)sbas_bits_get_u64(b, bit_off + 52, 6);
        h->iod_2      = (uint8_t)sbas_bits_get_u64(b, bit_off + 58, 8);
        {
            int16_t dx_raw = (int16_t)sbas_bits_get_s64(b, bit_off + 66, 9);
            int16_t dy_raw = (int16_t)sbas_bits_get_s64(b, bit_off + 75, 9);
            int16_t dz_raw = (int16_t)sbas_bits_get_s64(b, bit_off + 84, 9);
            int16_t da_f0_raw = (int16_t)sbas_bits_get_s64(b, bit_off + 93, 10);
            h->dx_2    = (double)dx_raw * 0.125;              /* meters */
            h->dy_2    = (double)dy_raw * 0.125;              /* meters */
            h->dz_2    = (double)dz_raw * 0.125;              /* meters */
            h->da_f0_2 = (double)da_f0_raw * pow(2.0, -31);   /* seconds */
        }
        h->iodp       = (uint8_t)sbas_bits_get_u64(b, bit_off + 103, 2);
        h->spare      = (uint8_t)sbas_bits_get_u64(b, bit_off + 105, 1);
    } else {
        SBAS_MT25_Half_Vel1 *h = &half->u.v1;
        h->velocity_code = 1;
        h->prn_mask = (uint8_t)sbas_bits_get_u64(b, bit_off + 1, 6);
        h->iod      = (uint8_t)sbas_bits_get_u64(b, bit_off + 7, 8);
        {
            int16_t dx_raw    = (int16_t)sbas_bits_get_s64(b, bit_off + 15, 11);
            int16_t dy_raw    = (int16_t)sbas_bits_get_s64(b, bit_off + 26, 11);
            int16_t dz_raw    = (int16_t)sbas_bits_get_s64(b, bit_off + 37, 11);
            int16_t da_f0_raw = (int16_t)sbas_bits_get_s64(b, bit_off + 48, 11);
            h->dx    = (double)dx_raw * 0.125;               /* m */
            h->dy    = (double)dy_raw * 0.125;               /* m */
            h->dz    = (double)dz_raw * 0.125;               /* m */
            h->da_f0 = (double)da_f0_raw * pow(2.0, -31);    /* s */
        }
        {
            int8_t dx_rate_raw = (int8_t) sbas_bits_get_s64(b, bit_off + 59, 8);
            int8_t dy_rate_raw = (int8_t) sbas_bits_get_s64(b, bit_off + 67, 8);
            int8_t dz_rate_raw = (int8_t) sbas_bits_get_s64(b, bit_off + 75, 8);
            int8_t da_f1_raw   = (int8_t) sbas_bits_get_s64(b, bit_off + 83, 8);
            h->dx_rate = (double)dx_rate_raw * pow(2.0, -11); /* m/s */
            h->dy_rate = (double)dy_rate_raw * pow(2.0, -11); /* m/s */
            h->dz_rate = (double)dz_rate_raw * pow(2.0, -11); /* m/s */
            h->da_f1   = (double)da_f1_raw   * pow(2.0, -39); /* s/s */
        }
        {
            uint16_t t0_raw = (uint16_t)sbas_bits_get_u64(b, bit_off + 91, 13);
            h->t0 = (double)t0_raw * 16.0;                   /* s */
        }
        h->iodp     = (uint8_t) sbas_bits_get_u64(b, bit_off + 104, 2);
    }
}

int decode_mt25_into(const char *hex, SBAS_MT25_Message *m)
{
    if (!hex || !m) return -1;
    char c[1024]; compact_hex(hex, c, sizeof(c));
    int nbytes = (int)strlen(c)/2;
    unsigned char b[256] = {0};
    for (int i=0;i<nbytes && i<(int)sizeof(b); ++i){ unsigned int v=0; sscanf(&c[i*2],"%2x",&v); b[i]=(unsigned char)v; }
    const int base = 14;
    parse_mt25_half(b, base, &m->half1);
    parse_mt25_half(b, base + 106, &m->half2);
    m->crc24 = (uint32_t)sbas_bits_get_u64(b, base + 212, 24);
    return 0;
}




int decode_mt26_into(const char *hex, SBAS_MT26_Message *m)
{
    if (!hex || !m) return -1;
    char c[1024]; compact_hex(hex, c, sizeof(c));
    int nbytes = (int)strlen(c)/2;
    unsigned char b[256] = {0};
    for (int i=0; i<nbytes && i<(int)sizeof(b); ++i){ unsigned int v=0; sscanf(&c[i*2], "%2x", &v); b[i]=(unsigned char)v; }

    int off = 14; /* payload start */

    /* Headers */
    m->band_number = (uint8_t)sbas_bits_get_u64(b, off, 4); off += 4;
    m->block_id    = (uint8_t)sbas_bits_get_u64(b, off, 4); off += 4;

    /* 15 IGPs: each 9-bit delay then 4-bit GIVEI */
    for (int i = 0; i < SBAS_MT26_IGP_COUNT; ++i) {
        m->igp_vertical_delay[i] = (uint16_t)sbas_bits_get_u64(b, off, 9); off += 9;
        m->givei[i]              = (uint8_t) sbas_bits_get_u64(b, off, 4); off += 4;
    }

    /* Control + spare */
    m->iodi  = (uint8_t)sbas_bits_get_u64(b, off, 2); off += 2;
    m->spare = (uint8_t)sbas_bits_get_u64(b, off, 7); off += 7;

    /* CRC */
    m->crc = (uint32_t)sbas_bits_get_u64(b, off, 24); off += 24;
    (void)off; /* keep compiler happy */
    return 0;
}


int decode_mt28_into(const char *hex, SBAS_MT28_Message *m)
{
    if (!hex || !m) return -1;
    char c[1024]; compact_hex(hex, c, sizeof(c));
    int nbytes = (int)strlen(c)/2;
    unsigned char b[256] = {0};
    for (int i=0; i<nbytes && i<(int)sizeof(b); ++i){ unsigned int v=0; sscanf(&c[i*2], "%2x", &v); b[i]=(unsigned char)v; }

    const int base = 14;
    int off = base;

    /* IODP */
    m->iodp = (uint8_t)sbas_bits_get_u64(b, off, 2); off += 2;

    /* Two 105-bit satellite blocks */
    for (int s = 0; s < 2; ++s) {
        int so = off + s*105;
        SBAS_MT28_SatBlock *dst = &m->sat[s];
        dst->prn_mask      = (uint8_t)sbas_bits_get_u64(b, so + 0, 6);
        dst->scale_exponent= (uint8_t)sbas_bits_get_u64(b, so + 6, 3);
        int diag_off = so + 9;
        dst->E1_1 = (uint16_t)sbas_bits_get_u64(b, diag_off + 0, 9);
        dst->E2_2 = (uint16_t)sbas_bits_get_u64(b, diag_off + 9, 9);
        dst->E3_3 = (uint16_t)sbas_bits_get_u64(b, diag_off + 18, 9);
        dst->E4_4 = (uint16_t)sbas_bits_get_u64(b, diag_off + 27, 9);
        int offd = diag_off + 36;
        dst->E1_2 = (int16_t)sbas_bits_get_s64(b, offd + 0, 10);
        dst->E1_3 = (int16_t)sbas_bits_get_s64(b, offd + 10, 10);
        dst->E1_4 = (int16_t)sbas_bits_get_s64(b, offd + 20, 10);
        dst->E2_3 = (int16_t)sbas_bits_get_s64(b, offd + 30, 10);
        dst->E2_4 = (int16_t)sbas_bits_get_s64(b, offd + 40, 10);
        dst->E3_4 = (int16_t)sbas_bits_get_s64(b, offd + 50, 10);
    }

    /* CRC after IODP + 2 blocks */
    int crc_off = base + 2 + 105 + 105;
    m->crc = (uint32_t)sbas_bits_get_u64(b, crc_off, 24);
    return 0;
}

/* Into-struct: MT62 (internal test). We don't interpret payload fields; copy raw 212 bits and CRC. */
int decode_mt62_into(const char *hex, SBAS_MT62_Message *m)
{
    if (!hex || !m) return -1;
    char c[1024]; compact_hex(hex, c, sizeof(c));
    int nbytes = (int)strlen(c)/2;
    unsigned char b[256] = {0};
    for (int i=0; i<nbytes && i<(int)sizeof(b); ++i){ unsigned int v=0; sscanf(&c[i*2],"%2x",&v); b[i]=(unsigned char)v; }

    const int base = 14; /* payload start */
    /* Copy 212 payload bits MSB-first into 28 bytes; last 4 bits of the last byte are unused */
    memset(m->test_data, 0, sizeof(m->test_data));
    for (int i = 0; i < 212; ++i) {
        unsigned bit = (unsigned)sbas_bits_get_u64(b, base + i, 1);
        if (bit) {
            int byte = i >> 3;            /* i/8 */
            int bit_in_byte = 7 - (i & 7); /* MSB-first */
            if (byte >= 0 && byte < (int)sizeof(m->test_data)) {
                m->test_data[byte] |= (uint8_t)(1u << bit_in_byte);
            }
        }
    }
    /* CRC (24 bits) after payload */
    m->crc = (uint32_t)sbas_bits_get_u64(b, base + 212, 24);
    return 0;
}

/* Into-struct: MT63 (null/filler). Same treatment: stash 212-bit payload and CRC. */
int decode_mt63_into(const char *hex, SBAS_MT63_Message *m)
{
    if (!hex || !m) return -1;
    char c[1024]; compact_hex(hex, c, sizeof(c));
    int nbytes = (int)strlen(c)/2;
    unsigned char b[256] = {0};
    for (int i=0; i<nbytes && i<(int)sizeof(b); ++i){ unsigned int v=0; sscanf(&c[i*2],"%2x",&v); b[i]=(unsigned char)v; }

    const int base = 14; /* payload start */
    memset(m->null_data, 0, sizeof(m->null_data));
    for (int i = 0; i < 212; ++i) {
        unsigned bit = (unsigned)sbas_bits_get_u64(b, base + i, 1);
        if (bit) {
            int byte = i >> 3;
            int bit_in_byte = 7 - (i & 7);
            if (byte >= 0 && byte < (int)sizeof(m->null_data)) {
                m->null_data[byte] |= (uint8_t)(1u << bit_in_byte);
            }
        }
    }
    m->crc = (uint32_t)sbas_bits_get_u64(b, base + 212, 24);
    return 0;
}

