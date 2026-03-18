# Hercules Transmitter: Full Detailed Guide

This document explains how `hercules_transmitter` works from the ground up. It covers:
- Architecture and runtime flow
- Every function in `hercules_transmitter.c` with signature and purpose
- The auto-increment framework used after edits
- Encoder implementations in `src/encoder.c` with bit layouts and LSB scales
- How to run it and what the output looks like

The transmitter is a simple, direct “edit-and-send” system:
- Messages are decoded once from a data file and exposed by a provider.
- The transmitter picks what to send directly from those in-memory structs.
- You can interactively edit a message, encode it, and broadcast it immediately.
- A tiny scheduler keeps the stream going by sending the most “due” types, with MT63 fillers when idle.

---

## Build and run (quick start)

- Build with the repository’s Makefile.
- Run the transmitter and bind a dataset:
  - `hercules_transmitter.exe data\parsing_file.25B`
  - or: `hercules_transmitter.exe --bind-file data\parsing_file.25B`
- Use the menu:
  - 1: Start transmission (continuous or timed)
  - 2: Stop transmission
  - 3: Show client status
  - 4: Edit and send a message (one-shot)

When sending, logs are timestamped like `[HH:MM:SS]`.

---

## High-level data flow

1) Input file -> provider binding
- The file is parsed (CRC-checked and decoded) by the pipeline exposed through `tx_source`.
- Results are stored in memory as an array of `{ timestamp, SBAS_TypedMessage }` pairs.

2) Selection and editing
- For manual sends (menu option 4): you pick satellite ID and message type; the program finds an instance and opens an inline editor for that type.
- For continuous transmission (menu option 1): a direct scheduler chooses the next type to send based on defined update intervals.

3) Encoding
- The chosen typed-struct is encoded into a 250-bit SBAS frame (as hex) using the per-type encoders in `encoder.c`.

4) Broadcast
- Hex is converted to 32 raw bytes and broadcast to all connected TCP clients via the `tx_server` module (default TCP port 23).

If nothing is due, MT63 (null) fillers are transmitted to maintain continuity.

---

## File: `hercules_transmitter.c` (all functions and globals)

### Constants and globals
- `#define TCP_PORT 23` — TCP port for the server (Telnet-friendly default).
- `#define MAX_CLIENTS 1` — Maximum concurrent TCP clients.
- `static const int max_update_interval[64]` — Update intervals per message type (seconds), based on RTCA DO-229D.
- `static time_t g_direct_last_sent[64]` — Last send timestamps per type.
- `static int g_direct_last_idx[64]` — Last sent index per type (for cycling through available messages).

### Helper: current_time_hhmmss
Signature: `static void current_time_hhmmss(char *out, size_t out_sz)`
- Formats local time as `HH:MM:SS` into `out`. Used to prefix log lines.

### Encoder dispatcher: encode_message_from_tm
Signature: `static int encode_message_from_tm(const SBAS_TypedMessage *tm, char *out_hex)`
- Switches on `tm->message_type` and calls the corresponding `encode_mtX_from(...)` function in `encoder.c`.
- Fills `out_hex` with ASCII hex on success; returns 0. Negative return means encoding failed.

### Interactive editor: generalized_edit_prompt
Signature: `static int generalized_edit_prompt(SBAS_TypedMessage *tm)`
- Presents a looped, per-type editor for in-place field edits. Supports: 0,1,2–5,6,7,9,10,17,18,24,25,26,28.
- One-level “back” with `0` or `b`. Returns 0 to go back, negative on input error.
- MT25 has an explicit velocity-code selection prompt (v0 vs v1) before editing fields.

### Auto-increment rules: detect_and_register_increment_rule
Signature: `static void detect_and_register_increment_rule(const SBAS_TypedMessage *before, SBAS_TypedMessage *after)`
- Compares a snapshot before edit to the message after edit, records which fields changed into an `IncrementRule` for that message.
- Sets `suppress_once=true` so the first send after editing uses the exact user-entered values; increments begin on the second send.

### Auto-increment application: apply_increment_if_rule
Signature: `static void apply_increment_if_rule(SBAS_TypedMessage *tm)`
- Looks up an increment rule for `tm` and applies LSB-accurate increments just before encoding. Clamps or wraps to the on-wire field ranges.
- Skips exactly once immediately after an edit (first send fidelity).

### Direct scheduler reset: direct_scheduler_reset
Signature: `static void direct_scheduler_reset(void)`
- Resets both `g_direct_last_sent` and `g_direct_last_idx` for all types.

### Type priority: direct_type_priority
Signature: `static int direct_type_priority(int mt)`
- Returns 2 for integrity-related types (2–6 and 24); otherwise 1. Used to break ties among overdue types.

### Type filter: is_supported_tx_type
Signature: `static int is_supported_tx_type(int t)`
- Returns 1 if the message type can be encoded/transmitted; otherwise 0.

### Core: direct_scheduler_step
Signature: `static int direct_scheduler_step(void)`
- Chooses the most-overdue supported type, preferring any instance with an active increment rule.
- Steps: select, optionally apply increments, encode, convert hex to bytes, broadcast, update last-sent bookkeeping, log with `[HH:MM:SS]`.
- If nothing is due, sends an MT63 filler to keep continuity.

### Networking shim: accept_new_clients
Signature: `int accept_new_clients(void)`
- Thin wrapper over `tx_server_accept_new_clients()`; non-blocking.

### UI: print_menu
Signature: `void print_menu(void)`
- Prints the main interactive menu (1: start, 2: stop, 3: clients, 4: edit-and-send, 0: exit).

### UI/control loop: handle_user_input
Signature: `void handle_user_input(void)`
- Dispatches menu actions.
- Start (1): asks duration minutes (0=continuous); resets scheduler; warns if no data bound; loops once per second running `direct_scheduler_step()` and `accept_new_clients()`; prints status every 30 seconds; ESC stops early.
- Stop (2): sets `transmitting = 0`.
- Clients (3): prints `tx_server_client_count()/MAX_CLIENTS`.
- Edit/send (4): prompts for satellite ID and message type, finds a matching message with `tx_source_find_first()`, launches `generalized_edit_prompt()`, derives increment rules via `detect_and_register_increment_rule()`, then encodes/broadcasts the edited message immediately.

### Cleanup: cleanup_tcp_server
Signature: `void cleanup_tcp_server(void)`
- Calls `tx_server_cleanup()` to close sockets and free resources. Registered via `atexit()`.

### Program entry: main
Signature: `int main(int argc, char* argv[])`
- Banner; optional `--selftest` sends a single MT63; parses `--bind-file <path>` or a lone path to bind the provider; initializes TCP server; registers cleanup; runs the menu loop.

### Networking init: initialize_tcp_server
Signature: `int initialize_tcp_server(void)`
- Returns `tx_server_init(TCP_PORT, MAX_CLIENTS)`.

---

## Auto-increment framework (after edits)

When you edit a message via menu option 4, the system remembers exactly which fields changed and, starting from the second scheduled send, nudges those fields by their on-wire LSB each time the type is sent. This produces a realistic, smoothly varying stream while preserving your first post-edit packet exactly.

Key points:
- First send after editing: exact user-entered values (suppressed increment once).
- Subsequent sends: LSB-accurate increments, clamped/wrapped to the on-wire ranges.
- If the type has arrays (e.g., MT2–5), masks track which entries changed. Some types promote to “increment all entries” to avoid partial blocks.

Per-type increments (step sizes and ranges):
- MT1: iodp wraps in 2 bits (0–3).
- MT2–5: iodf/iodp wrap 2 bits; fast_correction increments by 1 (signed 12-bit −2047..+2047); UDREI by +1 (0–15). If any entry or header changed, all 13 entries advance together.
- MT6: iodf2/3/4/5 wrap 2 bits; UDREI[i] +1 (0–15) for edited indices.
- MT7: t_lat +1; iodp wraps; UDRE_AI[i] +1 for edited indices.
- MT9: full coverage; t0 +16 s (13-bit unsigned); URA +1 (0–15); positions X/Y +0.08 m, Z +0.4 m; rates X/Y +0.000625 m/s, Z +0.004 m/s; accelerations X/Y +1.25e-5 m/s^2, Z +6.25e-6 m/s^2; aGf0 +2^-31 s; aGf1 +2^-40 s/s; all with clamping to the bitfield ranges.
- MT10: all numeric parameters increment by their LSBs together; rss_udre and rss_iono toggle if edited.
- MT17: t0 +1 s; per selected GEO block, positions X/Y +2600 m, Z +26000 m; rates X/Y +10 m/s, Z +60 m/s; data_id/prn_number/health wrap in-field.
- MT18: number_of_bands +1, band_number +1, iodi wraps 2 bits.
- MT24: iodp/block_id/iodf wrap 2 bits; edited fast_corrections +1 (−2047..2047); UDREIs +1 (0–15).
- MT25:
  - v0 (two-sat half): dx/dy/dz for each sat +0.125 m (9-bit signed raw −256..+255); da_f0_1/2 +2^-31 s (10-bit signed). iodp wraps if edited.
  - v1 (single-sat with rates): dx/dy/dz +0.125 m (11-bit signed); da_f0 +2^-31 s (11-bit signed); dx/dy/dz_rate +2^-11 m/s (8-bit signed); da_f1 +2^-39 s/s (8-bit signed); t0 +16 s (13-bit unsigned). iodp wraps if edited.
- MT26: band_number/block_id +1; iodi wraps 2 bits; igp_vertical_delay[i] +1 (0..511); givei[i] +1 (0..15).
- MT28: iodp wraps 2 bits; for the selected sat block, prn_mask +1; scale_exponent cycles 0..15; all covariance entries +1.

---

## Encoders in `src/encoder.c` (implementations and bit layouts)

This section summarizes the encoder functions, their signatures, and the on-wire layouts they produce. All encoders write header preamble (8 bits) and message type (6 bits), then the payload per type, and finally CRC-24Q over the data bits.

Common helpers:
- `static void write_bits_to_buffer(unsigned char *buffer, int bit_offset, uint64_t value, int num_bits)` — MSB-first bit writer.
- `uint32_t calculate_sbas_crc24(const unsigned char *data, size_t data_length_bits)` — CRC-24Q (poly 0x1864CFB).
- `int binary_to_hex_string(const unsigned char *binary_data, size_t data_length, char *hex_output)` — hex string builder.

Per-type encoders:
- MT0 — `int encode_mt0_from(const SBAS_MT0_Message *mt0_msg, char *hex_output)`
  - Payload: 212 bits copied from `test_data[]` MSB-first starting at bit 14.
  - CRC-24 appended (24 bits). Output hex covers all bytes up to CRC.

- MT1 — `int encode_mt1_from(const SBAS_MT1_Message *mt1_msg, char *hex_output)`
  - PRN mask: 27 bytes (210 PRN bits used) starting at bit 14.
  - IODP: 2 bits at bit 224; CRC over 226 bits total.

- MT2–5 (unified) — `int encode_mt2_5_from(const SBAS_MT2_5_Message *msg, char *hex_output)` and wrappers `encode_mt2_from`, `encode_mt3_from`, `encode_mt4_from`, `encode_mt5_from`
  - IODF: 2 bits; IODP: 2 bits; 13× fast corrections (12-bit signed each); 13× UDREI (4 bits each). CRC at bit 226.

- MT6 — `int encode_mt6_from(const SBAS_MT6_Message *msg, char *hex_output)`
  - IODF2..IODF5: 2 bits each; 51× UDREI (4 bits each); CRC afterwards.

- MT7 — `int encode_mt7_from(const SBAS_MT7_Message *msg, char *hex_output)`
  - t_lat (4), iodp (2), spare (2), then 51× UDRE AI (4 bits each); CRC appended.

- MT9 — `int encode_mt9_from(const SBAS_MT9_Message *msg, char *hex_output)`
  - reserved(8), t0(13, LSB 16 s), URA(4), X/Y (30-bit signed, LSB 0.08 m), Z (25-bit signed, LSB 0.4 m);
    rates X/Y (17-bit signed, LSB 0.000625 m/s), Z (18-bit signed, LSB 0.004 m/s);
    accelerations X/Y/Z (10-bit signed with LSBs 1.25e-5/1.25e-5/6.25e-6 m/s^2);
    ag_f0 (12-bit signed, LSB 2^-31 s), ag_f1 (8-bit signed, LSB 2^-40 s/s); CRC over payload.

- MT10 — `int encode_mt10_from(const SBAS_MT10_Message *msg, char *hex_output)`
  - Writes the full parameter table using scales from the decoder; includes two 1-bit toggles `rss_udre` and `rss_iono`, and an 81-bit spare block. CRC appended.

- MT17 — `int encode_mt17_from(const SBAS_MT17_Message *msg, char *hex_output)`
  - Three GEO blocks each containing data_id(2), prn_number(8), health_and_status(8), positions X/Y (15-bit signed @ 2600 m), Z (9-bit signed @ 26000 m), and rates X/Y (3-bit signed @ 10 m/s), Z (4-bit signed @ 60 m/s); then t0(11, LSB 64 s); CRC appended.

- MT18 — `int encode_mt18_from(const SBAS_MT18_Message *msg, char *hex_output)`
  - number_of_bands(4), band_number(4), iodi(2), spare(1), 201-bit IGP mask; CRC appended.

- MT24 — `int encode_mt24_from(const SBAS_MT24_Message *msg, char *hex_output)`
  - 6× fast corrections (12-bit signed), 6× UDREI (4-bit), control (iodp 2, block_id 2, iodf 2, spare 4), 106-bit long-term corrections array; CRC appended.

- MT25 — `int encode_mt25_from(const SBAS_MT25_Message *msg, char *hex_output)`
  - Two halves back-to-back; each half starts with `velocity_code` (1 bit) then either:
    - v0: prn1(6), iod1(8), dx1/dy1/dz1(9-bit signed @ 0.125 m), da_f0_1(10-bit signed @ 2^-31 s), prn2(6), iod2(8), dx2/dy2/dz2(9-bit signed @ 0.125 m), da_f0_2(10-bit signed), iodp(2), spare(1).
    - v1: prn(6), iod(8), dx/dy/dz(11-bit signed @ 0.125 m), da_f0(11-bit signed @ 2^-31 s), dx/dy/dz_rate(8-bit signed @ 2^-11 m/s), da_f1(8-bit signed @ 2^-39 s/s), t0(13, LSB 16 s), iodp(2).
  - CRC over the concatenated halves.

- MT26 — `int encode_mt26_from(const SBAS_MT26_Message *msg, char *hex_output)`
  - band_number(4), block_id(4), 15 IGPs each with vertical_delay(9) and GIVEI(4), iodi(2), spare(7); CRC appended.

- MT28 — `int encode_mt28_from(const SBAS_MT28_Message *msg, char *hex_output)`
  - iodp(2), then two satellite blocks each with prn_mask(6), scale_exponent(3), diagonal terms E1_1..E4_4 (9 bits), and off-diagonals (10 bits); CRC appended.

- MT63 — `int encode_mt63_from(const SBAS_MT63_Message *msg, char *hex_output)`
  - Null message: preamble/type, 212 bits of zeros (or provided `null_data`), CRC appended.

Notes and error modes:
- All encoders return 0 on success and negative on invalid inputs (e.g., null pointers).
- Field scales documented above match the increments used by `apply_increment_if_rule` to stay bit-accurate over time.

---

## Key external modules used by the transmitter

These APIs are used by the transmitter and are defined under `src/`:

- Provider (`src/tx_source.h`)
  - `int tx_source_bind_file(const char *path)` — Parse and bind messages from a dataset file.
  - `void tx_source_unbind(void)` — Free bound messages.
  - `size_t tx_source_bound_count(void)` — How many messages are available.
  - `int tx_source_find_first(int mt, unsigned sat, SBAS_TypedMessage **out, int *idx)` — Find the first message matching type and satellite.
  - `int tx_source_next_for_type(int mt, int last_idx, SBAS_TypedMessage **out, int *next_idx)` — Cycle through available messages by type.

- Utilities (`src/tx_utils.h`)
  - `size_t tx_hex_to_bin(const char *hex, uint8_t out[32], size_t out_cap)` — Convert ASCII-hex to 32 bytes.

- TCP server (`src/tx_server.h`)
  - `int tx_server_init(int port, int max_clients)` — Start listening.
  - `int tx_server_accept_new_clients(void)` — Accept new connections (non-blocking).
  - `int tx_server_client_count(void)` — Current number of connected clients.
  - `void tx_server_broadcast(const uint8_t *data, size_t len)` — Broadcast to all clients.
  - `void tx_server_cleanup(void)` — Close sockets and free resources.

- Message types (`src/message_types.h`)
  - Defines `SBAS_TypedMessage` and all per-type message structs.

---

## Timing and scheduling details

- The `max_update_interval` table defines how often each type should be sent (in seconds).
- For each type, the scheduler tracks when it was last sent and picks the one that’s most overdue.
- Integrity-related types (2–6 and 24) have higher priority when overdue ties occur.
- If no type is due, the transmitter sends an MT63 filler to maintain continuity.
- The main transmission loop runs once per second (Sleep(1000)) to align with SBAS timing resolution.

---

## What the output looks like

During transmission, every send is logged with a timestamp, the mode, the message type, the index (when using the scheduler), size, and hex.

Examples:
- `[11:29:30] [direct] Sent MT2 (idx 0) 32 bytes | HEX: 53...` (scheduled send)
- `[11:29:45] Sent MT1 for sat 128 (32 bytes)` (manual edit-and-send)
- `[11:30:00] [direct] Sent MT63 filler (32 bytes)` (idle filler)

When you use menu option 4 (edit and send):
1) You pick satellite ID and message type.
2) The per-type editor opens. After edits, the exact values you set are sent once immediately.
3) For subsequent scheduled sends of that type, auto-increments apply from the second send onward (per rules above).

---

## Error handling and edge cases

- If the provider has no data bound, the transmitter warns and will send MT63 fillers.
- If an encoder fails for a message, it logs and skips that send.
- If `tx_source_find_first` fails during manual send, a helpful message is printed.
- Input parsing uses `fgets` to avoid scanf/newline pitfalls.
- Keyboard ESC stops a timed transmission early.

---

## Extending the transmitter

To add support for a new message type X:
1) Implement `encode_mtX_from(...)` in `encoder.c` and declare it in `encoder.h`.
2) In `hercules_transmitter.c`:
   - Extend `encode_message_from_tm` with a new `case X:`.
   - Add `case X` to `is_supported_tx_type` so the scheduler can consider it.
   - Optionally extend `generalized_edit_prompt` with an editor block for X.
3) If X has unique timing requirements, update the `max_update_interval` entry for X.

---

## Glossary

- SBAS: Satellite-Based Augmentation System.
- MT: Message Type (0–63 in SBAS framing).
- MT63: Null/filler message used to maintain continuity.
- IODP/IODF: Issue of Data (PRN/fast correction), per RTCA spec.
- UDREI/GIVEI: Integrity indicators (satellite/ionospheric grid vertical error).

---

If you’d like, we can add diagrams or a sequence chart showing how the provider, scheduler, encoders, and network server interact during a typical second of operation.