#ifndef TX_UTILS_H
#define TX_UTILS_H

#include <stddef.h>
#include <stdint.h>

/* Format binary buffer to uppercase hex string (no spaces), null-terminated */
void tx_format_hex(const uint8_t *data, size_t len, char *out, size_t out_size);

/* Convert hex string (no spaces) to binary bytes. Returns bytes written. */
size_t tx_hex_to_bin(const char *hex, uint8_t *out, size_t max_out);

#endif /* TX_UTILS_H */


