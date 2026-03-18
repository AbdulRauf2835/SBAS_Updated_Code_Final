#ifndef TX_SOURCE_H
#define TX_SOURCE_H

#include "sbas_message_structs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Return the next decoded message of type `mt` after `after_idx` (wraps around provider's store).
// On success: returns 0 and sets *out_tm to a mutable pointer and *out_idx to provider index.
// On failure or if no such message exists: returns -1, *out_tm untouched.
// Bind the provider to a decoded store owned by the caller. The store must
// remain valid for the lifetime of transmission. Pass the array of pairs and
// its count; no copying occurs.
// Bind the provider by parsing a RINEX-like file into a decoded array that the provider owns.
// Returns 0 on success (and makes messages available); -1 on failure.
int tx_source_bind_file(const char *path);

// Unbind and free any provider-owned decoded array; after this, bound count becomes 0.
void tx_source_unbind(void);

// Return how many decoded pairs are currently bound (0 if none)
size_t tx_source_bound_count(void);

// Return the path of the currently bound file (NULL if none)
const char *tx_source_get_bound_path(void);

// Return the next decoded message of type `mt` after `after_idx` (wraps around provider's store).
// On success: returns 0 and sets *out_tm to a mutable pointer and *out_idx to provider index.
int tx_source_next_for_type(int mt, int after_idx, SBAS_TypedMessage **out_tm, int *out_idx);

// Find the first decoded message matching (mt, sat_id).
// On success: returns 0 and sets *out_tm/*out_idx; otherwise returns -1.
int tx_source_find_first(int mt, unsigned int sat_id, SBAS_TypedMessage **out_tm, int *out_idx);

// Get timestamp for a message at given index
// Returns 0 if index is valid, -1 otherwise
int tx_source_get_timestamp(int idx, time_t *out_timestamp);

// Get message by absolute index (for sequential transmission)
// Returns 0 if index is valid, -1 otherwise
int tx_source_get_by_index(int idx, SBAS_TypedMessage **out_tm);

#ifdef __cplusplus
}
#endif

#endif // TX_SOURCE_H
