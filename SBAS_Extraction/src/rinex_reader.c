
#define _CRT_SECURE_NO_WARNINGS
#include "rinex_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*
 * Compatibility shims for MSVC and MinGW: provide strtok_r and strsep which are POSIX.
 * MSVC provides strtok_s which is compatible; implement wrappers when
 * compiling with MSVC to avoid unresolved externals at link time.
 * For MinGW/GCC on Windows, we also need to provide strsep if not available.
 */
#if defined(_MSC_VER)
/* strtok_r wrapper using strtok_s */
static char * strtok_r(char *str, const char *delim, char **saveptr)
{
    return strtok_s(str, delim, saveptr);
}

/* strsep implementation for MSVC (simple) */
static char * strsep(char **stringp, const char *delim)
{
    char *s;
    char *token;

    if (stringp == NULL || *stringp == NULL)
        return NULL;

    s = *stringp;
    /* find first occurrence of any delimiter */
    token = strpbrk(s, delim);
    if (token) {
        *token = '\0';
        *stringp = token + 1;
    } else {
        *stringp = NULL;
    }
    return s;
}
#elif defined(_WIN32) && !defined(_GNU_SOURCE)
/* strsep implementation for MinGW/GCC on Windows */
static char * strsep(char **stringp, const char *delim)
{
    char *s;
    char *token;

    if (stringp == NULL || *stringp == NULL)
        return NULL;

    s = *stringp;
    /* find first occurrence of any delimiter */
    token = strpbrk(s, delim);
    if (token) {
        *token = '\0';
        *stringp = token + 1;
    } else {
        *stringp = NULL;
    }
    return s;
}
#else
/* Provide a portable strsep fallback when the platform doesn't expose one */
static char * strsep(char **stringp, const char *delim)
{
    char *s;
    char *token;

    if (stringp == NULL || *stringp == NULL)
        return NULL;

    s = *stringp;
    token = s;
    /* find first occurrence of any delimiter */
    while (*s && strchr(delim, *s) == NULL) s++;
    if (*s) {
        *s = '\0';
        *stringp = s + 1;
    } else {
        *stringp = NULL;
    }
    return token;
}
#endif





/* Internal helpers - made truly private with static keyword */
static int is_rinex2_format(const char *filename, const char *content);
static int is_sbas_packet_line(const char *line, int isRinex2);
static SBAS_Packet make_empty_packet(int lineNumber);
static void add_hex_to_packet(SBAS_Packet *pkt, const char *line);
static char *clean_hex_data(const char *hex);
static int validate_packet_length(const char *hex);
static int passes_filters(const SBAS_Packet *pkt);
static int finalize_packet(SBAS_Packet *packet, SBAS_Packet **out_packets, size_t *out_count);

/* helper: determine if token is numeric-like (digits with optional sign/decimal point) */
static int numericish(const char *t)
{
    int has_digit = 0;
    if (!t || !*t) return 0;
    const char *p = t;
    if (*p == '+' || *p == '-') p++;
    for (; *p; ++p) {
        if (isdigit((unsigned char)*p)) { has_digit = 1; continue; }
        if (*p == '.') continue;
        return 0;
    }
    return has_digit;
}

/* Public API */
int rinex_parse_file(const char *file_buffer, const char *filename,
                     SBAS_Packet **out_packets, size_t *out_count)//Check out_packet and out_count.
{
    if (!file_buffer || !out_packets || !out_count) return -1;

    *out_packets = NULL;
    *out_count = 0;

    int isRinex2 = is_rinex2_format(filename, file_buffer);

    /* make a writable copy to tokenize by lines */
    char *buf = strdup(file_buffer);
    if (!buf) return -1;

    char *saveptr = NULL;
    char *line = NULL;
    size_t line_idx = 0;
    int headerEnded = 0;

    SBAS_Packet current = make_empty_packet(0);
    int haveCurrent = 0;

    for (line = strtok_r(buf, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        line_idx++;

        if (!headerEnded) {
            if (strstr(line, "END OF HEADER")) headerEnded = 1;
            continue;
        }

        /* skip blank lines */
        const char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0') continue;

        if (is_sbas_packet_line(line, isRinex2)) {
            /* finalize previous packet if present */
            if (haveCurrent && current.hexData && current.hexData[0] != '\0') {
                if (finalize_packet(&current, out_packets, out_count) != 0) {
                    free(current.hexData);
                    free(buf);
                    return -1;
                }
                haveCurrent = 0;
                current = make_empty_packet(0);
            }

            /* start a new packet */
            current = make_empty_packet((int)line_idx);
            haveCurrent = 1;

            /* Parse tokens once, gather numeric-like tokens, detect sat id, then build timestamp
               from the next six numeric tokens after an optional leading sat-id. */
            char tmp[512];
            strncpy(tmp, line, sizeof(tmp)-1);
            tmp[sizeof(tmp)-1] = '\0';

            char *numeric[16]; int nnum = 0;
            int first_numeric_is_sat = 0;
            char *token = NULL; char *str = tmp;
            while ((token = strsep(&str, " \t")) != NULL) {
                if (*token == '\0') continue;
                /* S### sat id */
                if ((token[0] == 'S' || token[0] == 's') && isdigit((unsigned char)token[1])) {
                    current.satelliteId = atoi(token+1);
                    continue;
                }
                if (numericish(token)) {
                    if (nnum < (int)(sizeof(numeric)/sizeof(numeric[0]))) {
                        numeric[nnum++] = token;
                        if (nnum == 1) {
                            int v = atoi(token);
                            if (v >= 120 && v <= 158) {
                                first_numeric_is_sat = 1;
                                if (current.satelliteId < 0) current.satelliteId = v;
                            }
                        }
                    }
                }
            }

            int start = first_numeric_is_sat ? 1 : 0;
            if (nnum - start >= 6) {
                current.timestamp[0] = '\0';
                for (int i = 0; i < 6; ++i) {
                    if (i) strncat(current.timestamp, " ", sizeof(current.timestamp)-strlen(current.timestamp)-1);
                    strncat(current.timestamp, numeric[start + i], sizeof(current.timestamp)-strlen(current.timestamp)-1);
                }
            }

            continue;
        }

        /* accumulate hex data lines */
        if (haveCurrent) add_hex_to_packet(&current, line);
    }

    /* finalize last packet */
    if (haveCurrent && current.hexData && current.hexData[0] != '\0') {
        if (finalize_packet(&current, out_packets, out_count) != 0) {
            free(current.hexData);
            free(buf);
            return -1;
        }
    }

    free(buf);
    return 0;
}

void rinex_free_packets(SBAS_Packet *packets, size_t count)
{
    if (!packets) return;
    for (size_t i = 0; i < count; ++i) free(packets[i].hexData);
    free(packets);
}

int rinex_extract_message_type_from_hex(const char *hex, int *out_type)
{
    if (!hex || !out_type) return -1;
    
    /* compact: remove spaces */
    char compact[256]; size_t w = 0;
    for (size_t i = 0; hex[i] && w + 1 < sizeof(compact); ++i) {
        if (!isspace((unsigned char)hex[i])) {
            compact[w++] = (char)toupper((unsigned char)hex[i]);
        }
    }
    compact[w] = '\0';
    
    if (w < 4) return -2; // Need at least 2 bytes
    
    /* SBAS Message Format (250 bits total):
     * Bits 0-7:   Preamble (0x53, 0x9A, or 0xC6)
     * Bits 8-13:  Message Type (6 bits)
     * Bits 14-225: Data Field (212 bits)
     * Bits 226-249: CRC (24 bits)
     */
    
    // Convert first two hex bytes to get preamble and message type
    char first_byte[3], second_byte[3];
    memcpy(first_byte, compact, 2);
    first_byte[2] = '\0';
    memcpy(second_byte, compact + 2, 2);
    second_byte[2] = '\0';
    
    unsigned int preamble_val = 0, second_val = 0;
    if (sscanf(first_byte, "%x", &preamble_val) != 1) return -3;
    if (sscanf(second_byte, "%x", &second_val) != 1) return -4;
    
    /* Check for valid SBAS preamble */
    if (preamble_val != 0x53 && preamble_val != 0x9A && preamble_val != 0xC6) {
        return -5; // Invalid preamble
    }
    
    /* Extract message type from bits 8-13
     * The message type is in the upper 6 bits of the second byte
     * Second byte format: MMMMMMDD (M=message type bits, D=data bits)
     */
    *out_type = (int)((second_val >> 2) & 0x3F);
    
    return 0;
}

/* ---------- private helpers ---------- */
static SBAS_Packet make_empty_packet(int lineNumber)
{
    SBAS_Packet p;
    p.satelliteId = -1;
    p.lineNumber = lineNumber;
    p.hexData = strdup("");
    p.messageType = -1;
    p.isValid = 0;
    p.timestamp[0] = '\0';
    return p;
}

static void add_hex_to_packet(SBAS_Packet *pkt, const char *line)
{
    if (!pkt || !line) return;

    /* Tokenize by whitespace and accept only proper hex tokens (pairs),
       to avoid nibble loss when a line starts with a hex digit. */
    char tmp[2048];
    strncpy(tmp, line, sizeof(tmp)-1);
    tmp[sizeof(tmp)-1] = '\0';

    /* Collect 2-char hex tokens (also split longer even-length tokens). We will
       later decide where to start appending based on preamble alignment. */
    const char *preambles[] = { "53", "9A", "C6" };
    char tokens[512][3];
    int tok_count = 0;
    char *save = NULL;
    for (char *tok = strtok_r(tmp, " \t\r\n", &save); tok; tok = strtok_r(NULL, " \t\r\n", &save)) {
        size_t L = strlen(tok);
        int all_hex = 1;
        for (size_t i = 0; i < L; ++i) {
            if (!isxdigit((unsigned char)tok[i])) { all_hex = 0; break; }
        }
        if (!all_hex) continue;

        /* Uppercase in-place for consistency */
        for (size_t i = 0; i < L; ++i) tok[i] = (char)toupper((unsigned char)tok[i]);

        if (L == 2) {
            if (tok_count < (int)(sizeof(tokens)/sizeof(tokens[0]))) {
                strncpy(tokens[tok_count], tok, 3); tokens[tok_count][2] = '\0';
                tok_count++;
            }
        } else if (L > 2 && (L % 2) == 0) {
            /* Split long contiguous hex into 2-char pairs */
            for (size_t i = 0; i + 1 < L; i += 2) {
                char pair[3] = { tok[i], tok[i+1], '\0' };
                if (tok_count < (int)(sizeof(tokens)/sizeof(tokens[0]))) {
                    strncpy(tokens[tok_count], pair, 3);
                    tok_count++;
                }
            }
        } else {
            /* Ignore odd-length tokens (likely counters like "2"). */
            continue;
        }
    }

    if (tok_count > 0) {
        /* When starting a new packet (no prior hex), align to the first preamble
           and drop any tokens before it (e.g., leading counters like "2" or "10"). */
        int start_idx = 0;
        size_t cur_len = pkt->hexData ? strlen(pkt->hexData) : 0;
        if (cur_len == 0) {
            int found = -1;
            for (int i = 0; i < tok_count; ++i) {
                for (int j = 0; j < 3; ++j) {
                    if (strcmp(tokens[i], preambles[j]) == 0) { found = i; break; }
                }
                if (found >= 0) break;
            }
            if (found >= 0) start_idx = found; else return; /* wait for preamble */
        }

        /* Build accumulation string from start_idx..end */
        char accum[2048] = ""; int added = 0;
        for (int i = start_idx; i < tok_count; ++i) {
            if (added > 0) strncat(accum, " ", sizeof(accum)-strlen(accum)-1);
            strncat(accum, tokens[i], sizeof(accum)-strlen(accum)-1);
            added++;
        }
        if (added == 0) return;

        size_t new_len = cur_len + (cur_len > 0 ? 1 : 0) + strlen(accum) + 1;
        char *tmp2 = malloc(new_len);
        if (!tmp2) return;

        if (cur_len > 0) {
            snprintf(tmp2, new_len, "%s %s", pkt->hexData, accum);
        } else {
            snprintf(tmp2, new_len, "%s", accum);
        }

        free(pkt->hexData);
        pkt->hexData = tmp2;
    }
}

static char *clean_hex_data(const char *hex)
{
    if (!hex) return strdup("");
    size_t len = strlen(hex);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    size_t o = 0; int in_space = 0;
    for (size_t i = 0; hex[i]; ++i) {
        char c = hex[i];
        if (isspace((unsigned char)c)) {
            if (!in_space && o > 0) { out[o++] = ' '; in_space = 1; }
        } else {
            in_space = 0; out[o++] = (char)toupper((unsigned char)c);
        }
    }
    while (o > 0 && out[o-1] == ' ') o--;
    out[o] = '\0';
    return out;
}

static int validate_packet_length(const char *hex)
{
    if (!hex) return 0;
    size_t chars = 0;
    for (size_t i = 0; hex[i]; ++i) if (!isspace((unsigned char)hex[i])) chars++;
    /* Expect 64 bytes = 128 hex chars for a full 250-bit SBAS frame (preamble+MT+212-bit payload+CRC). */
    if (chars == 128) return 1;   /* ideal */
    if (chars == 126 || chars == 130) return 1; /* tolerate +/- 1 byte anomalies seen in some files */
    return 0;
}

static int is_rinex2_format(const char *filename, const char *content)
{
    if (filename) {
        const char *exts[] = { ".25B", ".25O", ".25N", ".24B", ".24O", ".24N", ".26B" };
        size_t flen = strlen(filename);
        for (size_t i = 0; i < sizeof(exts)/sizeof(exts[0]); ++i) {
            size_t elen = strlen(exts[i]);
            if (flen >= elen) {
                const char *p = filename + flen - elen;
                int match = 1;
                for (size_t j = 0; j < elen; ++j) if (toupper((unsigned char)p[j]) != toupper((unsigned char)exts[i][j])) { match = 0; break; }
                if (match) return 1;
            }
        }
    }
    if (!content) return 0;
    /* look for a version number like ' 2.x ' or ' 3.x ' near start */
    const char *p = content; size_t scanned = 0;
    while (*p && scanned < 2000) {
        if ((p[0] == '2' || p[0] == '3') && p[1] == '.' && isdigit((unsigned char)p[2])) {
            return (p[0] == '2') ? 1 : 0;
        }
        p++; scanned++;
    }
    return 0;
}

static int is_sbas_packet_line(const char *line, int isRinex2)
{
    if (!line) return 0;
    if (!isRinex2) {
        if (strstr(line, "SBA") != NULL) return 1;
    } else {
        const char *p = line;
        while (*p) { if ((*p == 'S' || *p == 's') && isdigit((unsigned char)*(p+1))) return 1; p++; }
        char tmp[512]; strncpy(tmp, line, sizeof(tmp)-1); tmp[sizeof(tmp)-1] = '\0';
        char *s = tmp; char *tok;
        while ((tok = strsep(&s, " \t")) != NULL) {
            if (*tok == '\0') continue;
            int v = atoi(tok);
            if (v >= 120 && v <= 158) return 1;
        }
    }
    return 0;
}

static int passes_filters(const SBAS_Packet *pkt)
{
    if (!pkt) return 0;
    // Accept all SBAS satellites (PRN 120-158)
    int validSat = (pkt->satelliteId >= 120 && pkt->satelliteId <= 158);
    int validMsg = (pkt->messageType >= 0 && pkt->messageType <= 63);
    return (validSat || validMsg);
}

/* Finalize a packet - this eliminates duplication */
static int finalize_packet(SBAS_Packet *packet, SBAS_Packet **out_packets, size_t *out_count)
{
    if (!packet || !out_packets || !out_count) {
        return -1;
    }

    char *clean = clean_hex_data(packet->hexData);
    free(packet->hexData);
    packet->hexData = clean;

    if (!clean) {
        return -1;
    }

    packet->messageType = -1;
    rinex_extract_message_type_from_hex(packet->hexData, &packet->messageType);
    packet->isValid = validate_packet_length(packet->hexData);

    if (passes_filters(packet)) { //check vakidity caase, and instead of or
        SBAS_Packet *tmp = realloc(*out_packets, ((*out_count) + 1) * sizeof(SBAS_Packet));
        if (!tmp) {
            return -1;
        }
        *out_packets = tmp;
        (*out_packets)[*out_count] = *packet;
        (*out_count)++;
        
        /* Clear the original packet since we've moved it */
        memset(packet, 0, sizeof(SBAS_Packet));
        packet->satelliteId = -1;
        packet->messageType = -1;
        packet->hexData = strdup("");
    } else {
        free(packet->hexData);
        packet->hexData = strdup("");
    }

    return 0;
}