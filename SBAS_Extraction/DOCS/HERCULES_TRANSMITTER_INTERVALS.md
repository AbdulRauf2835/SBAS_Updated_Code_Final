# Hercules Transmitter - Message Update Intervals

This document describes the transmission intervals used by the Hercules Transmitter system, based on RTCA DO-229D Table A-25 specifications.

## Message Type Transmission Intervals

| Data | Associated Message Types | Maximum Update Interval (seconds) |
|------|--------------------------|-----------------------------------|
| Don't Use for Safety Applications | 0 | 6 |
| PRN Mask | 1 | 120 |
| UDREI | 2 to 6, 24 | 6 |
| Fast Corrections | 2 to 5, 24 | 6 |
| Long Term Corrections | 24, 25 | 120 |
| GEO Navigation Data | 9 | 120 |
| Fast Correction Degradation | 7 | 120 |
| Degradation Parameters | 10 | 120 |
| Ionospheric Grid Mask | 18 | 300 |
| Ionospheric Corrections | 26 | 300 |
| UTC Timing Data | 12 | 300 |
| Almanac Data | 17 | 300 |
| Service Level | 27 | 300 |
| Clock-Ephemeris Covariance Matrix | 28 | 120 |
| Null Message (Filler) | 63 | As needed |

## Detailed Message Type Intervals

| MT | Description | Interval (sec) | Notes |
|----|-------------|----------------|-------|
| 0 | Don't Use for Safety | 6 | Not used in safety applications |
| 1 | PRN Mask | 120 | Defines which satellites are monitored |
| 2 | Fast Corrections (Slot 1-13) | 6 | High priority - integrity critical |
| 3 | Fast Corrections (Slot 14-26) | 6 | High priority - integrity critical |
| 4 | Fast Corrections (Slot 27-39) | 6 | High priority - integrity critical |
| 5 | Fast Corrections (Slot 40-51) | 6 | High priority - integrity critical |
| 6 | Integrity Information | 6 | High priority - integrity critical |
| 7 | Fast Correction Degradation | 120 | Degradation factor for fast corrections |
| 8 | Reserved | 120 | - |
| 9 | GEO Navigation Message | 120 | GEO satellite ephemeris |
| 10 | Degradation Parameters | 120 | System degradation parameters |
| 11 | Reserved | 120 | - |
| 12 | UTC Timing Data | 300 | UTC offset parameters |
| 13-16 | Reserved | 300 | - |
| 17 | GEO Almanac | 300 | GEO satellite almanac |
| 18 | Ionospheric Grid Mask | 300 | Defines ionospheric grid points |
| 19-23 | Reserved | 300 | - |
| 24 | Mixed Fast/Long Term Corrections | 6 | High priority |
| 25 | Long Term Corrections | 120 | Satellite long-term corrections |
| 26 | Ionospheric Delay Corrections | 300 | Grid ionospheric delays |
| 27 | SBAS Service Message | 300 | Service level information |
| 28 | Clock-Ephemeris Covariance | 120 | Covariance matrix data |
| 29-62 | Reserved | 3600 | Not currently used |
| 63 | Null Message | As needed | Filler when no other messages due |

## Fast Correction Satellite Configuration

The transmitter allows configuring how many satellites need fast corrections:

| Satellites | Message Types Transmitted |
|------------|---------------------------|
| 1-13 | MT2 only |
| 14-26 | MT2 + MT3 |
| 27-39 | MT2 + MT3 + MT4 |
| 40-51 | MT2 + MT3 + MT4 + MT5 |
| 0 (all) | MT2 + MT3 + MT4 + MT5 |

## Priority System

The transmitter uses a priority system for message scheduling:

| Priority | Message Types | Description |
|----------|---------------|-------------|
| High (2) | 2, 3, 4, 5, 6, 24 | Integrity-critical messages |
| Normal (1) | All others | Standard priority messages |

## Transmission Logic

1. The scheduler checks all supported message types each second
2. Messages are selected based on:
   - Whether they are overdue (time since last transmission > interval)
   - Priority level (higher priority preferred)
   - How overdue they are (more overdue preferred)
3. If no messages are due, MT63 (Null Message) is sent as filler
4. Satellite ID filtering can be applied to transmit only for specific satellites

## Source Code Reference

The intervals are defined in `src/hercules_transmitter.c`:

```c
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
    /*13-16 */ 300,
    /*17 */ 300,  // Almanac
    /*18 */ 300,  // Ionospheric grid mask
    /*19-23 */ 300,
    /*24 */ 6,    // Mixed fast/long term corrections
    /*25 */ 120,  // Long term corrections
    /*26 */ 300,  // Ionospheric corrections
    /*27 */ 300,  // Service level
    /*28 */ 120,  // Clock-Ephemeris covariance
    /*29-31 */ 300,
    /*32-63 */ 3600
};
```

---
*Document generated: November 27, 2025*
*Based on RTCA DO-229D specifications and Hercules Transmitter implementation*
