#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "tx_utils.h"

void tx_format_hex(const uint8_t *data, size_t len, char *out, size_t out_size)
{
	if (!out || out_size == 0) return;
	out[0] = '\0';
	for (size_t i = 0; i < len && (strlen(out) + 3) < out_size; i++) {
		char b[4];
		snprintf(b, sizeof(b), "%02X", data[i]);
		strncat(out, b, out_size - strlen(out) - 1);
	}
}

size_t tx_hex_to_bin(const char *hex, uint8_t *out, size_t max_out)
{
	if (!hex || !out || max_out == 0) return 0;
	size_t hlen = strlen(hex);
	size_t bytes = 0;
	for (size_t i = 0; i + 1 < hlen && bytes < max_out; i += 2) {
		char tmp[3] = { hex[i], hex[i+1], '\0' };
		out[bytes++] = (uint8_t)strtol(tmp, NULL, 16);
	}
	return bytes;
}


