
#ifndef SBAS_MESSAGE_STRUCTS_H
#define SBAS_MESSAGE_STRUCTS_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////Common Message parameter accross every messagetype///////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    uint8_t      preamble;       // 8-bit preamble
    uint8_t      message_type;   // 6-bit message type (lower 6 bits used)
    unsigned int satellite_id;   // satellite ID (e.g., 128, 129, 134, 142, 143, 144)
    // time_t       timestamp;      // Unix timestamp (seconds since epoch)
    
} SBAS_MessageHeader;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////// MT0 - Test Message //////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    SBAS_MessageHeader header;  // common SBAS header
    uint8_t  test_data[27];     // 212-bit test payload (27 bytes)
    uint32_t crc;               // 24-bit CRC (lower 24 bits used)
} SBAS_MT0_Message;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////// MT1 - PRN Mask Assignment ///////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    SBAS_MessageHeader header;  // common SBAS header
    uint8_t  prn_mask[27];      // 210-bit PRN mask (27 bytes)
    uint8_t  iodp;              // 2-bit Issue of Data PRN (lower 2 bits used)
    uint32_t crc;               // 24-bit CRC (lower 24 bits used)
} SBAS_MT1_Message;




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 /////////////////////////////////////////MT 2-5 - Fast Corrections Entry//////////////////////////////////////////////
 ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    int16_t fast_correction; // 12-bit signed fast correction
    uint8_t udrei;           // 4-bit UDREI (0-15)
} SBAS_FastCorrEntry;
typedef struct {
    SBAS_MessageHeader header;     // Common header
    uint8_t iodf;                  // IODF - Fast (2 bits, lower 2 bits used)
    uint8_t iodp;                  // IODP - PRN Mask (2 bits, lower 2 bits used)
    SBAS_FastCorrEntry sat_corrections[13]; // Corrections for 13 satellites
    uint32_t crc;                  // 24-bit CRC (lower 24 bits used)
} SBAS_MT2_5_Message;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////// MT6 - Integrity Information (UDREIs) /////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    SBAS_MessageHeader header; // Common header (preamble + message type 6)

    uint8_t iodf2; // IODF for MT2 (2 bits, lower 2 bits used)
    uint8_t iodf3; // IODF for MT3 (2 bits, lower 2 bits used)
    uint8_t iodf4; // IODF for MT4 (2 bits, lower 2 bits used)
    uint8_t iodf5; // IODF for MT5 (2 bits, lower 2 bits used)
    uint8_t udrei[51]; // 51 UDREI values (4 bits each). Only lower 4 bits of each element are used.
    uint32_t crc; // 24-bit CRC (lower 24 bits used)
} SBAS_MT6_Message;


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////// MT7 - Degradation Parameters //////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    SBAS_MessageHeader header; // Common header (preamble + message type 7)
    uint8_t t_lat;        // System Latency (4 bits, lower 4 bits used)
    uint8_t iodp;         // IODP (2 bits, lower 2 bits used)
    uint8_t udre_ai[51];  // 51 UDRE Degradation Factors (4 bits each). Lower 4 bits used.
    uint32_t crc;         // 24-bit CRC (lower 24 bits used)
} SBAS_MT7_Message;


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////// MT9 - Long-Term Satellite Error Corrections ///////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    SBAS_MessageHeader header; // Common header (preamble + message type 9)

    uint8_t  reserved;        // Unitless, unchanged
    double   t0;              // seconds (raw * 16)
    uint8_t  ura;             // Unitless index
    double   xg_ecef;         // meters (raw * 0.08)
    double   yg_ecef;         // meters (raw * 0.08)
    double   zg_ecef;         // meters (raw * 0.4)
    double   xg_rate;         // m/s (raw * 0.000625)
    double   yg_rate;         // m/s (raw * 0.000625)
    double   zg_rate;         // m/s (raw * 0.004)
    double   xg_accel;        // m/s^2 (raw * 0.0000125)
    double   yg_accel;        // m/s^2 (raw * 0.0000125)
    double   zg_accel;        // m/s^2 (raw * 0.00000625)
    double   ag_f0;           // seconds (raw * 2^-31)
    double   ag_f1;           // s/s (raw * 2^-40)

    uint32_t crc;             // 24-bit CRC (lower 24 bits used)
} SBAS_MT9_Message;




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////// MT10 - Degradation Parameters////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    SBAS_MessageHeader header; // Common header (preamble + message type 10)

    /* Store LSB-scaled physical values (per ICD) */
    double   B_rrc;              // raw*0.002 m
    double   C_ltc_lsb;         // raw*0.002 m
    double   C_ltc_v1;          // raw*0.00005 m/s
    double   I_ltc_v1;          // raw*1.0 s
    double   C_ltc_v0;          // raw*0.002 m
    double   I_ltc_v0;          // raw*1.0 s
    double   C_geo_lsb;         // raw*0.0005 m
    double   C_geo_v;           // raw*0.00005 m/s
    double   I_geo;             // raw*1.0 s
    double   C_er;              // raw*0.5 m
    double   C_iono_step;       // raw*0.001 m
    double   I_iono;            // raw*1.0 s
    double   C_iono_ramp;       // raw*0.000005 m/s
    uint8_t  rss_udre;          // 1-bit UDRE RSS Indicator (0/1)
    uint8_t  rss_iono;          // 1-bit Ionospheric RSS Indicator (0/1)
    double   C_covariance;      // raw*0.1 (dimensionless scale)
    uint8_t  spare[11];         // 81 spare bits (11 bytes = 88 bits, 81 used)

    uint32_t crc;               // 24-bit CRC (lower 24 bits used)
} SBAS_MT10_Message;




///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////// MT12 - Network time / UTC parameters/////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    SBAS_MessageHeader header; /* preamble + message_type */
    /* Store LSB-scaled physical values per ICD Table 5-7 */
    double  A1SNT;           /* s/s  (raw * 2^-50) */
    double  A0SNT;           /* s    (raw * 2^-30) */
    double  t0t;             /* s    (raw * 2^12)  */
    uint8_t WNt;             /* week (raw) */
    double  delta_t_ls;      /* s    (raw * 1)     */
    uint8_t WN_LSF;          /* week (raw) */
    uint8_t DN;              /* day 1..7 (raw) */
    double  delta_t_lsf;     /* s    (raw * 1)     */
    uint8_t utc_standard_id; /* 3-bit unitless */
    double  TOW;             /* s    (raw * 1)     */
    uint16_t WN;             /* week (raw) */
    uint32_t crc24;          /* 24-bit CRC */
} SBAS_MT12_Message;




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////MT17 - GEO almanac////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define SBAS_MT17_GEOS 3
typedef struct {
    uint8_t data_id;
    uint8_t prn_number;
    uint8_t health_and_status;
    /* Store LSB-scaled physical values per ICD */
    double x_g;     /* meters (raw * 2600) */
    double y_g;     /* meters (raw * 2600) */
    double z_g;     /* meters (raw * 26000) */
    double xg_dot;  /* m/s (raw * 10) */
    double yg_dot;  /* m/s (raw * 10) */
    double zg_dot;  /* m/s (raw * 60) */
} SBAS_MT17_GeoBlock;
typedef struct {
    SBAS_MessageHeader header; /* preamble + message_type */
    SBAS_MT17_GeoBlock geo[SBAS_MT17_GEOS];/* Three GEO almanac blocks */
    double t0; /* seconds (raw * 64) */
    uint32_t crc24;
} SBAS_MT17_Message;




//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////// MT18 - Ionospheric Grid Point (IGP) Mask //////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    SBAS_MessageHeader header; // Common header (preamble + message type 18)
    uint8_t  number_of_bands;  // 4-bit Number of Bands
    uint8_t  band_number;      // 4-bit Band Number
    uint8_t  iodi;             // 2-bit IODI
    uint8_t  spare;            // 1-bit Spare
    uint8_t  igp_mask[26];     // 201-bit IGP Mask (26 bytes = 208 bits, 201 used
    uint32_t crc;              // 24-bit CRC (lower 24 bits used)
} SBAS_MT18_Message;



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////MT24 - Mixed Fast & Long-Term Corrections/////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// 
typedef struct {
    SBAS_MessageHeader header; // Common header (preamble + message type 24)
    uint8_t  iodp;             // 2-bit IODP
    uint8_t  block_id;         // 2-bit Block ID
    uint8_t  iodf;             // 2-bit IODF
    uint8_t  spare;            // 4-bit Spare
    int16_t  fast_corrections[6];  // 6 x 12-bit signed fast corrections
    uint8_t  udreis[6];            // 6 x 4-bit UDREIs
    uint8_t  long_term_corrections[14]; // 106-bit Long-Term Corrections (14 bytes = 112 bits, 106 used)
    uint32_t crc;              // 24-bit CRC (lower 24 bits used)
} SBAS_MT24_Message;



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////  MT 25  //////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    uint8_t velocity_code;    // 1 bit(0)
    /* Satellite 1 */
    uint8_t prn_mask_1;      // 6 bits
    uint8_t iod_1;           // 8 bits
    double  dx_1;            // scaled (m), raw 9 bits * 0.125 m
    double  dy_1;            // scaled (m)
    double  dz_1;            // scaled (m)
    double  da_f0_1;         // scaled (s), raw 10 bits * 2^-31 s
    /* Satellite 2 */
    uint8_t prn_mask_2;      // 6 bits
    uint8_t iod_2;           // 8 bits
    double  dx_2;            // scaled (m), raw 9 bits * 0.125 m
    double  dy_2;            // scaled (m)
    double  dz_2;            // scaled (m)
    double  da_f0_2;         // scaled (s), raw 10 bits * 2^-31 s
    uint8_t iodp;            // 2 bits (IODP)
    uint8_t spare;           // 1 bit spare (kept as a byte)
} SBAS_MT25_Half_Vel0;

typedef struct {
    uint8_t velocity_code;    // 1 bit(1)
    uint8_t prn_mask;        // 6 bits
    uint8_t iod;             // 8 bits
    double  dx;              // scaled (m), raw 11 bits * 0.125 m
    double  dy;              // scaled (m)
    double  dz;              // scaled (m)
    double  da_f0;           // scaled (s), raw 11 bits * 2^-31 s

    double  dx_rate;         // scaled (m/s), raw 8 bits * 2^-11 m/s
    double  dy_rate;         // scaled (m/s)
    double  dz_rate;         // scaled (m/s)
    double  da_f1;           // scaled (s/s), raw 8 bits * 2^-39 s/s

    double  t0;              // scaled (s), raw 13 bits * 16 s
    uint8_t iodp;           // 2 bits (IODP)
} SBAS_MT25_Half_Vel1;

typedef struct {
    uint8_t raw_velocity_code; // raw 1-bit velocity code as stored by parser
    union {
        SBAS_MT25_Half_Vel0 v0;
        SBAS_MT25_Half_Vel1 v1;
    } u;
} SBAS_MT25_Half;

/* Full MT25 message: two 106-bit halves + 24-bit CRC */
typedef struct {
    SBAS_MessageHeader header; /* preamble + message_type */
    SBAS_MT25_Half half1;     /* first 106-bit half */
    SBAS_MT25_Half half2;     /* second 106-bit half */
    uint32_t crc24;           /* 24-bit CRC (stored in lower 24 bits) */
} SBAS_MT25_Message;






///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////// MT26 - Ionospheric delay corrections //////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define SBAS_MT26_IGP_COUNT 15
typedef struct {
    SBAS_MessageHeader header;   /* common SBAS header */
    /* Basic identifiers */
    uint8_t band_number;         /* 4 bits used (LSB) */
    uint8_t block_id;            /* 4 bits used (LSB) */
    uint16_t igp_vertical_delay[SBAS_MT26_IGP_COUNT]; /* store raw 9-bit values */
    uint8_t  givei[SBAS_MT26_IGP_COUNT];              /* store raw 4-bit GIVEI values */
    /* Control and spare fields */
    uint8_t iodi;               /* 2 bits used (LSB) */
    uint8_t spare;              /* 7-bit spare (unused) */
    uint32_t crc;               /* 24-bit CRC (stored in lower 24 bits) */
} SBAS_MT26_Message;




///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////// MT28 - Clock-ephemeris covariance matrix //////////////////////////////////////////////////////
 //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define SBAS_MT28_SAT_BLOCKS 2
typedef struct {
    uint8_t prn_mask;        /* 6 bits used (LSB) */
    uint8_t scale_exponent;  /* 3 bits used (LSB) */
    /* Diagonal covariance elements (unsigned 9-bit, 0..511) */
    uint16_t E1_1;            /* E1,1 */
    uint16_t E2_2;            /* E2,2 */
    uint16_t E3_3;            /* E3,3 */
    uint16_t E4_4;            /* E4,4 */
    /* Off-diagonal covariance elements (signed 10-bit, stored in int16_t) */
    int16_t  E1_2;            /* E1,2 */
    int16_t  E1_3;            /* E1,3 */
    int16_t  E1_4;            /* E1,4 */
    int16_t  E2_3;            /* E2,3 */
    int16_t  E2_4;            /* E2,4 */
    int16_t  E3_4;            /* E3,4 */
} SBAS_MT28_SatBlock;
typedef struct {
    SBAS_MessageHeader header; /* preamble + message_type */
    uint8_t iodp;              /* 2 bits used (LSB) */
    /* Two satellite covariance blocks per message */
    SBAS_MT28_SatBlock sat[SBAS_MT28_SAT_BLOCKS];
    uint32_t crc;              /* 24-bit CRC (stored in lower 24 bits) */
} SBAS_MT28_Message;



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////// MT62 - Internal Testing //////////////////////////////////////////////////////////////////// 
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    SBAS_MessageHeader header; // Common header (preamble + message type 62)
    
    uint8_t  test_data[28];    // 212-bit test data (28 bytes = 224 bits, 212 used)
    uint32_t crc;              // 24-bit CRC (lower 24 bits used)
} SBAS_MT62_Message;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////// MT63 - Null message ///////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    SBAS_MessageHeader header;
    uint8_t null_data[226]; // 212 bits payload + 24 bits CRC + padding
    uint32_t crc;              // 24-bit CRC (lower 24 bits used)
} SBAS_MT63_Message;



// Union of all message types 
typedef struct {
    int message_type;
    int source_line;   // source RINEX line number of the packet
    unsigned int satellite_id;  // source satellite ID (PRN) of the packet
    union {
        SBAS_MT0_Message mt0;
        SBAS_MT1_Message mt1;
        SBAS_MT2_5_Message mt2_5;
        SBAS_MT6_Message mt6;
        SBAS_MT7_Message mt7;
        SBAS_MT9_Message mt9;
        SBAS_MT10_Message mt10;
        SBAS_MT12_Message mt12;
        SBAS_MT17_Message mt17;
        SBAS_MT18_Message mt18;
        SBAS_MT24_Message mt24;
        SBAS_MT25_Message mt25;
        SBAS_MT26_Message mt26;
        SBAS_MT28_Message mt28;
        SBAS_MT62_Message mt62;
        SBAS_MT63_Message mt63;
    } data;
} SBAS_TypedMessage;

// 2D Array element: timestamp + message pair
typedef struct {
    time_t timestamp;           // First dimension: Unix timestamp
    SBAS_TypedMessage message;  // Second dimension: Message data  
} SBAS_TimestampMessagePair;

// 2D Array of timestamp+message pairs  
typedef struct {
    SBAS_TimestampMessagePair* pairs;  // Array of [timestamp, message] pairs
    size_t count;                      // Number of pairs
    size_t capacity;                   // Allocated capacity for efficient growth
} SBAS_MessageArray;

#endif /* SBAS_MESSAGE_STRUCTS_H */




