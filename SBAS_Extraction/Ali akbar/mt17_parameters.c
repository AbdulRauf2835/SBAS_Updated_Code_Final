#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "File_Parsing.h"

static char* read_text_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
	long sz = ftell(f);
	if (sz < 0) { fclose(f); return NULL; }
	if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
	char *buf = (char*)malloc((size_t)sz + 1);
	if (!buf) { fclose(f); return NULL; }
	size_t n = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	buf[n] = '\0';
	return buf;
}

/* Case-insensitive compare helper for ASCII keys */
static int iequal3(const char *a, const char *b)
{
	/* Compare up to 4 chars (e.g., "Xg1" + NUL) case-insensitively */
	for (int i = 0; i < 4; ++i) {
		unsigned char ca = (unsigned char)a[i];
		unsigned char cb = (unsigned char)b[i];
		if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
		if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
		if (ca != cb) return 0;
		if (ca == '\0') break;
	}
	return 1;
}

typedef enum {
	P_XG1, P_YG1, P_ZG1,
	P_XG2, P_YG2, P_ZG2,
	P_XG3, P_YG3, P_ZG3,
	P_INVALID
} ParamKey;

static ParamKey parse_param_key(const char *s)
{
	if (iequal3(s, "Xg1")) return P_XG1;
	if (iequal3(s, "Yg1")) return P_YG1;
	if (iequal3(s, "Zg1")) return P_ZG1;
	if (iequal3(s, "Xg2")) return P_XG2;
	if (iequal3(s, "Yg2")) return P_YG2;
	if (iequal3(s, "Zg2")) return P_ZG2;
	if (iequal3(s, "Xg3")) return P_XG3;
	if (iequal3(s, "Yg3")) return P_YG3;
	if (iequal3(s, "Zg3")) return P_ZG3;
	return P_INVALID;
}

static const char* key_heading(ParamKey k)
{
	switch (k) {
		case P_XG1: return "Xg1:";
		case P_YG1: return "Yg1:";
		case P_ZG1: return "Zg1:";
		case P_XG2: return "Xg2:";
		case P_YG2: return "Yg2:";
		case P_ZG2: return "Zg2:";
		case P_XG3: return "Xg3:";
		case P_YG3: return "Yg3:";
		case P_ZG3: return "Zg3:";
		default:    return "";
	}
}

int main(int argc, char **argv)
{
	const char *infile = (argc >= 2) ? argv[1] : "data/parsing_file.25B";

	/* Determine the parameter key from argv[2] or prompt the user */
	char keybuf[16] = {0};
	if (argc >= 3) {
		strncpy(keybuf, argv[2], sizeof(keybuf) - 1);
	} else {
		/* Prompt to stderr to keep stdout strictly for requested output */
		fprintf(stderr, "Enter parameter (Xg1/Yg1/Zg1/Xg2/Yg2/Zg2/Xg3/Yg3/Zg3): ");
		if (!fgets(keybuf, sizeof(keybuf), stdin)) {
			fprintf(stderr, "Failed to read parameter selection.\n");
			return 3;
		}
		/* Strip trailing newline */
		size_t L = strlen(keybuf);
		if (L && (keybuf[L-1] == '\n' || keybuf[L-1] == '\r')) keybuf[L-1] = '\0';
	}

	ParamKey key = parse_param_key(keybuf);
	if (key == P_INVALID) {
		fprintf(stderr, "Invalid parameter key: '%s'\n", keybuf);
		return 4;
	}

	char *file_buffer = read_text_file(infile);
	if (!file_buffer) {
		fprintf(stderr, "Failed to read input file: %s\n", infile);
		return 1;
	}

	SBAS_MessageArray arr; memset(&arr, 0, sizeof(arr));
	if (sbas_pipeline_process_file(file_buffer, infile, &arr) != 0) {
		free(file_buffer);
		fprintf(stderr, "Parsing failed for: %s\n", infile);
		return 2;
	}

	/* Print heading then timestamp/value pairs for the selected key */
	printf("%s\n", key_heading(key));
	for (size_t i = 0; i < arr.count; ++i) {
		const SBAS_TypedMessage *tm = &arr.messages[i];
		if (tm->message_type != 17) continue;
		const SBAS_MT17_Message *m = &tm->data.mt17;

		double v = 0.0;
		switch (key) {
			case P_XG1: v = m->geo[0].x_g; break;
			case P_YG1: v = m->geo[0].y_g; break;
			case P_ZG1: v = m->geo[0].z_g; break;
			case P_XG2: v = m->geo[1].x_g; break;
			case P_YG2: v = m->geo[1].y_g; break;
			case P_ZG2: v = m->geo[1].z_g; break;
			case P_XG3: v = m->geo[2].x_g; break;
			case P_YG3: v = m->geo[2].y_g; break;
			case P_ZG3: v = m->geo[2].z_g; break;
			default: continue; /* shouldn't happen */
		}

		/* timestamp above the value - output Unix timestamp (seconds since epoch) */
		time_t unix_ts = m->header.timestamp;
		printf("%ld\n", (long)unix_ts);
		printf("%.0f\n", v);
	}

	sbas_pipeline_free_array(&arr);
	free(file_buffer);
	return 0;
}
