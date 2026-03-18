#ifndef RINEX_READER_H
#define RINEX_READER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int satelliteId;    
    int lineNumber;     
    char *hexData;      
    int messageType;    
    int isValid;        
    char timestamp[64]; 
} SBAS_Packet;


int rinex_parse_file(const char *file_buffer, const char *filename,
                     SBAS_Packet **out_packets, size_t *out_count);


void rinex_free_packets(SBAS_Packet *packets, size_t count);


int rinex_extract_message_type_from_hex(const char *hex, int *out_type);

#ifdef __cplusplus
}
#endif

#endif 