#ifndef MESSAGE_TYPES_H
#define MESSAGE_TYPES_H

#include <stddef.h>
#include "sbas_message_structs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Into-struct decoders (raw fields, no formatting) - used by File_Parsing.c */
int decode_mt0_into(const char *hex, SBAS_MT0_Message *m);
int decode_mt1_into(const char *hex, SBAS_MT1_Message *m);
int decode_mt2_into(const char *hex, SBAS_MT2_5_Message *m);
int decode_mt6_into(const char *hex, SBAS_MT6_Message *m);
int decode_mt7_into(const char *hex, SBAS_MT7_Message *m);
int decode_mt9_into(const char *hex, SBAS_MT9_Message *m);
int decode_mt10_into(const char *hex, SBAS_MT10_Message *m);
int decode_mt12_into(const char *hex, SBAS_MT12_Message *m);
int decode_mt17_into(const char *hex, SBAS_MT17_Message *m);
int decode_mt18_into(const char *hex, SBAS_MT18_Message *m);
int decode_mt24_into(const char *hex, SBAS_MT24_Message *m);
int decode_mt25_into(const char *hex, SBAS_MT25_Message *m);
int decode_mt26_into(const char *hex, SBAS_MT26_Message *m);
int decode_mt28_into(const char *hex, SBAS_MT28_Message *m);
int decode_mt62_into(const char *hex, SBAS_MT62_Message *m);
int decode_mt63_into(const char *hex, SBAS_MT63_Message *m);

#ifdef __cplusplus
}
#endif

#endif /* MESSAGE_TYPES_H */
