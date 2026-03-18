#include "tx_source.h"
#include "File_Parsing.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SBAS_TimestampMessagePair *g_pairs = NULL;
static size_t g_count = 0;
static SBAS_MessageArray g_owned = {0};
static char *g_bound_file_path = NULL;

static int read_whole_file(const char *path, char **out_buf, size_t *out_len) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f); if (sz < 0) { fclose(f); return -1; }
    rewind(f);
    char *buf = (char*)malloc((size_t)sz + 1); if (!buf) { fclose(f); return -1; }
    size_t rd = fread(buf, 1, (size_t)sz, f); fclose(f);
    buf[rd] = '\0';
    if (out_buf) *out_buf = buf; else free(buf);
    if (out_len) *out_len = rd;
    return out_buf ? 0 : -1;
}

int tx_source_bind_file(const char *path) {
    if (!path) return -1;
    // Clear any previous binding
    if (g_owned.pairs) { sbas_pipeline_free_array(&g_owned); memset(&g_owned, 0, sizeof(g_owned)); }
    if (g_bound_file_path) { free(g_bound_file_path); g_bound_file_path = NULL; }
    g_pairs = NULL; g_count = 0;

    char *buffer = NULL; size_t blen = 0;
    if (read_whole_file(path, &buffer, &blen) != 0 || !buffer) return -1;
    memset(&g_owned, 0, sizeof(g_owned));
    int rc = sbas_pipeline_process_file(buffer, path, &g_owned);
    free(buffer);
    if (rc != 0 || g_owned.count == 0) {
        if (g_owned.pairs) { sbas_pipeline_free_array(&g_owned); memset(&g_owned, 0, sizeof(g_owned)); }
        return -1;
    }
    g_pairs = g_owned.pairs;
    g_count = g_owned.count;
    g_bound_file_path = strdup(path);
    return 0;
}

const char *tx_source_get_bound_path(void) {
    return g_bound_file_path;
}

void tx_source_unbind(void) {
    if (g_owned.pairs) { sbas_pipeline_free_array(&g_owned); memset(&g_owned, 0, sizeof(g_owned)); }
    if (g_bound_file_path) { free(g_bound_file_path); g_bound_file_path = NULL; }
    g_pairs = NULL; g_count = 0;
}

size_t tx_source_bound_count(void) { return g_count; }

int tx_source_next_for_type(int mt, int after_idx, SBAS_TypedMessage **out_tm, int *out_idx) {
    if (!g_pairs || g_count == 0 || !out_tm || !out_idx) return -1;
    size_t start = (after_idx < 0) ? 0 : (size_t)after_idx + 1;
    for (size_t i = start; i < g_count; ++i) {
        if (g_pairs[i].message.message_type == mt) { *out_tm = &g_pairs[i].message; *out_idx = (int)i; return 0; }
    }
    for (size_t i = 0; i < g_count && i <= (size_t)after_idx; ++i) {
        if (g_pairs[i].message.message_type == mt) { *out_tm = &g_pairs[i].message; *out_idx = (int)i; return 0; }
    }
    return -1;
}

static unsigned int msg_sat_id(const SBAS_TypedMessage *m) {
    /* All packets parsed by the pipeline populate SBAS_TypedMessage.satellite_id;
       prefer this robust root-level field rather than per-struct headers. */
    return m ? m->satellite_id : 0;
}

int tx_source_find_first(int mt, unsigned int sat_id, SBAS_TypedMessage **out_tm, int *out_idx) {
    if (!g_pairs || g_count == 0 || !out_tm || !out_idx) return -1;
    for (size_t i = 0; i < g_count; ++i) {
        const SBAS_TypedMessage *m = &g_pairs[i].message;
        if (m->message_type == mt && msg_sat_id(m) == sat_id) { *out_tm = (SBAS_TypedMessage*)m; *out_idx = (int)i; return 0; }
    }
    return -1;
}

int tx_source_get_timestamp(int idx, time_t *out_timestamp) {
    if (!g_pairs || idx < 0 || (size_t)idx >= g_count || !out_timestamp) return -1;
    *out_timestamp = g_pairs[idx].timestamp;
    return 0;
}

int tx_source_get_by_index(int idx, SBAS_TypedMessage **out_tm) {
    if (!g_pairs || idx < 0 || (size_t)idx >= g_count || !out_tm) return -1;
    *out_tm = &g_pairs[idx].message;
    return 0;
}
