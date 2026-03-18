#ifndef File_Parsing_H
#define File_Parsing_H

#include <stddef.h>
#include <time.h>
#include "sbas_message_structs.h"

/* Convert parsed timestamp string to Unix timestamp */
time_t sbas_convert_to_unix_timestamp(const char *timestamp_str); 

int sbas_pipeline_process_file(const char *file_buffer, const char *filename, SBAS_MessageArray *out_array);

/* Convenience: free contents of out_array */
void sbas_pipeline_free_array(SBAS_MessageArray *arr);

#endif 

