## Makefile for SBAS Extraction Tool (Windows/MSYS2 friendly)
## Improved version with better organization, debugging, and build optimization

# ============================================================================
# CONFIGURATION
# ============================================================================

# Compiler and tools
CC       ?= gcc
RM       ?= rm -f
RMDIR    ?= rmdir /S /Q
MKDIR_P  ?= mkdir -p

# Build flags - can be overridden at command line
OPTIMIZE ?= -O2
DEBUG    ?= 
CFLAGS   ?= -std=c99 $(OPTIMIZE) -Wall -Wextra -I. -Isrc -D_GNU_SOURCE $(DEBUG)
LDFLAGS  ?=

# Platform detection
UNAME_S  := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LDFLAGS_EXTRA := -lm
else
    LDFLAGS_EXTRA := -lws2_32
endif

# ============================================================================
# DIRECTORIES & PATHS
# ============================================================================

SRC_DIR       := src
BUILD_DIR     := build
TEST_DIR1     := Test_for_Decoder
TEST_DIR2     := Test_for_Decoder_into
TOOLS_DIR     := tools
STATS_DIR     := $(SRC_DIR)/Statistical_model

# ============================================================================
# SOURCE FILES & CORE OBJECTS
# ============================================================================

# Core sources (shared across all builds)
CORE_SRCS := \
	$(SRC_DIR)/bitops.c \
	$(SRC_DIR)/message_types.c \
	$(SRC_DIR)/rinex_reader.c

CORE_OBJS := $(CORE_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Additional commonly needed objects
COMMON_OBJS := \
	$(BUILD_DIR)/encoder.o \
	$(BUILD_DIR)/File_Parsing.o

# Hercules Transmitter sources
HERC_TX_SRCS := \
	$(SRC_DIR)/tx_utils.c \
	$(SRC_DIR)/tx_server.c \
	$(SRC_DIR)/tx_source.c \
	$(SRC_DIR)/TLE_implementation.c \
	$(SRC_DIR)/rinex_writer.c \
	$(STATS_DIR)/stat_model_128.c \
	$(STATS_DIR)/stat_model_143.c

# Correctly map transmitter sources to object files without leaving raw .c paths
HERC_TX_SRCS_SRC  := $(filter-out $(STATS_DIR)/%,$(filter $(SRC_DIR)/%,$(HERC_TX_SRCS)))
HERC_TX_SRCS_STAT := $(filter $(STATS_DIR)/%,$(HERC_TX_SRCS))
HERC_TX_OBJS := \
	$(HERC_TX_SRCS_SRC:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o) \
	$(HERC_TX_SRCS_STAT:$(STATS_DIR)/%.c=$(BUILD_DIR)/Statistical_model/%.o)

# Main program
MAIN_SRC  := $(SRC_DIR)/main.c
MAIN_OBJ  := $(BUILD_DIR)/main.o
TARGET    := sbas_extractor.exe

# Hercules API DLL
HERCULES_API_SRC := $(SRC_DIR)/hercules_api.c
HERCULES_API_OBJ := $(BUILD_DIR)/hercules_api.o
HERCULES_DLL     := hercules_transmitter.dll

# Test runners (auto-discovered)
TEST_SRCS       := $(filter-out %/tmp_run_mt7\ copy.c,$(wildcard $(addsuffix /*.c,$(TEST_DIR1) $(TEST_DIR2))))
TEST_EXES       := $(TEST_SRCS:.c=.exe)

# Default target
.DEFAULT_GOAL := tests
all: tests hercules_transmitter.exe

# ============================================================================
# PHONY TARGETS (must be declared early)
# ============================================================================

.PHONY: all tests clean distclean rebuild help info debug-build \
        run run-hercules run-mt9 run-test-mt9 run-test-mt10 run-test-mt12 \
        run-test-mt17 run-test-mt1_mt2 run-scan-hist run-mt17-params \
        run-mt2-query run-mt1-display run-test-mt6 run-test-mt7 \
        run-test-mt17_18_24_25_26_28 run-hercules-test test-stat-models \
        print-vars verbose

## Build rules
$(BUILD_DIR):
	@echo "[MKDIR] Creating build directory..."
	$(MKDIR_P) $(BUILD_DIR) $(BUILD_DIR)/Statistical_model

# Pattern rule: compile src/*.c into build/*.o
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@echo "[CC] Compiling $<..."
	$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Statistical model objects
$(BUILD_DIR)/Statistical_model/%.o: $(STATS_DIR)/%.c | $(BUILD_DIR)
	@echo "[CC] Compiling $<..."
	$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Link main executable (sbas_extractor.c linked only here)
$(TARGET): $(CORE_OBJS) $(BUILD_DIR)/sbas_extractor.o $(MAIN_OBJ)
	@echo "[LINK] Building $@..."
	$(CC) $(CFLAGS) $^ $(LDFLAGS) $(LDFLAGS_EXTRA) -o $@
	@echo "[OK] $@ built successfully"

# Object for sbas_extractor.c
$(BUILD_DIR)/sbas_extractor.o: $(SRC_DIR)/sbas_extractor.c | $(BUILD_DIR)
	@echo "[CC] Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Object for File_Parsing.c used by Test_for_file_parser
$(BUILD_DIR)/File_Parsing.o: $(SRC_DIR)/File_Parsing.c | $(BUILD_DIR)
	@echo "[CC] Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

## Test rules
# Generic rule: build each test .exe by linking its .c with core objects
$(TEST_DIR1)/%.exe: $(TEST_DIR1)/%.c $(CORE_OBJS)
	@echo "[CC] Building test $@..."
	$(CC) $(CFLAGS) -Isrc $< $(CORE_OBJS) $(LDFLAGS) $(LDFLAGS_EXTRA) -o $@
	@echo "[OK] Test built: $@"

$(TEST_DIR2)/%.exe: $(TEST_DIR2)/%.c $(CORE_OBJS)
	@echo "[CC] Building test $@..."
	$(CC) $(CFLAGS) -Isrc $< $(CORE_OBJS) $(LDFLAGS) $(LDFLAGS_EXTRA) -o $@
	@echo "[OK] Test built: $@

# Curated tests that are known to build
TESTS_CURATED := \
	$(TEST_DIR1)/tmp_run_mt9.exe \
	$(TEST_DIR2)/tmp_test_mt9.exe \
	$(TEST_DIR2)/tmp_test_mt10.exe \
	$(TEST_DIR2)/tmp_test_mt12.exe \
	$(TEST_DIR2)/tmp_test_mt17.exe \
	$(TEST_DIR2)/tmp_test_mt7.exe \
	$(TEST_DIR2)/tmp_test_mt6.exe \
	$(TEST_DIR2)/tmp_test_mt1_mt2.exe \
	$(TEST_DIR2)/tmp_scan_mt_hist.exe \
	$(TEST_DIR2)/tmp_test_mt17_18_24_25_26_28.exe

tests: $(TESTS_CURATED)
	@echo ""
	@echo "=========================================="
	@echo "  CURATED TESTS BUILT SUCCESSFULLY"
	@echo "=========================================="
	@echo "$(TESTS_CURATED)" | tr ' ' '\n' | sed 's/^/  ✓ /'
	@echo ""

# Build the interactive parser tester at repo root
Test_for_file_parser.exe: Test_for_file_parser.c $(CORE_OBJS) $(BUILD_DIR)/File_Parsing.o
	@echo "[CC] Building parser tester..."
	$(CC) $(CFLAGS) -Isrc $< $(CORE_OBJS) $(BUILD_DIR)/File_Parsing.o $(LDFLAGS) $(LDFLAGS_EXTRA) -o $@
	@echo "[OK] Parser tester built"

# MT17 parameters extractor (root-level program)
mt17_parameters.exe: mt17_parameters.c $(CORE_OBJS) $(BUILD_DIR)/File_Parsing.o
	@echo "[CC] Building MT17 parameters extractor..."
	$(CC) $(CFLAGS) -Isrc $< $(CORE_OBJS) $(BUILD_DIR)/File_Parsing.o $(LDFLAGS) $(LDFLAGS_EXTRA) -o $@

# MT2 Query Test with Raw Data Display
test_mt2_query_raw_data.exe: test_mt2_query_raw_data.c $(CORE_OBJS) $(BUILD_DIR)/File_Parsing.o
	@echo "[CC] Building MT2 query test..."
	$(CC) $(CFLAGS) -Isrc $< $(CORE_OBJS) $(BUILD_DIR)/File_Parsing.o $(LDFLAGS) $(LDFLAGS_EXTRA) -o $@

# MT1 Complete Display Test
test_mt1_complete_display.exe: test_mt1_complete_display.c $(CORE_OBJS) $(BUILD_DIR)/File_Parsing.o
	@echo "[CC] Building MT1 display test..."
	$(CC) $(CFLAGS) -Isrc $< $(CORE_OBJS) $(BUILD_DIR)/File_Parsing.o $(LDFLAGS) $(LDFLAGS_EXTRA) -o $@

run-mt17-params: mt17_parameters.exe
	@if [ -z "$(FILE)" ]; then \
	  echo "Please specify a RINEX file using FILE=, e.g., make $@ FILE=data/parsing_file.25B"; \
	else \
	  ./mt17_parameters.exe "$(FILE)"; \
	fi

run-mt2-query: test_mt2_query_raw_data.exe
	./test_mt2_query_raw_data.exe

run-mt1-display: test_mt1_complete_display.exe
	./test_mt1_complete_display.exe

# Combined tester using the File_Parsing pipeline for MT17/18/24/25/26/28
$(TEST_DIR2)/tmp_test_mt17_18_24_25_26_28.exe: $(TEST_DIR2)/tmp_test_mt17_18_24_25_26_28.c $(CORE_OBJS) $(BUILD_DIR)/File_Parsing.o
	@echo "[CC] Building MT17/18/24/25/26/28 test..."
	$(CC) $(CFLAGS) -Isrc $< $(CORE_OBJS) $(BUILD_DIR)/File_Parsing.o $(LDFLAGS) $(LDFLAGS_EXTRA) -o $@

# ============================================================================
# HERCULES TRANSMITTER SYSTEM
# ============================================================================

hercules_transmitter.exe: $(SRC_DIR)/hercules_transmitter.c $(CORE_OBJS) $(COMMON_OBJS) $(HERC_TX_OBJS)
	@echo "[LINK] Building Hercules Transmitter..."
	$(CC) $(CFLAGS) -Isrc $< $(CORE_OBJS) $(COMMON_OBJS) $(HERC_TX_OBJS) $(LDFLAGS) $(LDFLAGS_EXTRA) -o $@
	@echo "[OK] Hercules Transmitter built successfully"
	@echo ""
	@echo "Run with: ./hercules_transmitter.exe <input_file.25B>"
	@echo ""

# Hercules Transmitter DLL for GUI integration
$(HERCULES_DLL): $(HERCULES_API_SRC) $(CORE_OBJS) $(COMMON_OBJS) $(HERC_TX_OBJS)
	@echo "[LINK] Building Hercules Transmitter DLL..."
	$(CC) $(CFLAGS) -shared -DHERCULES_BUILD_DLL -Isrc $(HERCULES_API_SRC) $(CORE_OBJS) $(COMMON_OBJS) $(HERC_TX_OBJS) $(LDFLAGS) $(LDFLAGS_EXTRA) -o $@
	@echo "[OK] Hercules DLL built successfully: $(HERCULES_DLL)"
	@echo ""

# Statistical Models Test Suite
$(TEST_DIR1)/test_statistical_models.exe: $(TEST_DIR1)/test_statistical_models.c $(CORE_OBJS) $(COMMON_OBJS) $(HERC_TX_OBJS)
	@echo "[CC] Building statistical models test..."
	$(CC) $(CFLAGS) -Isrc $< $(CORE_OBJS) $(COMMON_OBJS) $(HERC_TX_OBJS) $(LDFLAGS) $(LDFLAGS_EXTRA) -lm -o $@

# Hercules feature test
test_hercules_features.exe: test_hercules_features.c $(CORE_OBJS) $(BUILD_DIR)/encoder.o
	@echo "[CC] Building Hercules features test..."
	$(CC) $(CFLAGS) -Isrc $< $(CORE_OBJS) $(BUILD_DIR)/encoder.o $(LDFLAGS) $(LDFLAGS_EXTRA) -o $@

# ============================================================================
# RUN TARGETS
# ============================================================================

run: $(TARGET)
	@if [ -z "$(FILE)" ]; then \
	  echo "ERROR: Please specify a RINEX file using FILE="; \
	  echo "Example: make run FILE=data/parsing_file.25B"; \
	else \
	  ./$(TARGET) "$(FILE)"; \
	fi

run-mt9: $(TEST_DIR1)/tmp_run_mt9.exe
	@if [ -z "$(FILE)" ]; then \
	  echo "ERROR: Please specify a RINEX file using FILE="; \
	  echo "Example: make run-mt9 FILE=data/parsing_file.25B"; \
	else \
	  ./$(TEST_DIR1)/tmp_run_mt9.exe "$(FILE)"; \
	fi

run-test-mt9: $(TEST_DIR2)/tmp_test_mt9.exe
	@if [ -z "$(FILE)" ]; then \
	  echo "ERROR: Please specify a RINEX file using FILE="; \
	  echo "Example: make run-test-mt9 FILE=data/parsing_file.25B"; \
	else \
	  ./$(TEST_DIR2)/tmp_test_mt9.exe "$(FILE)"; \
	fi

run-test-mt10: $(TEST_DIR2)/tmp_test_mt10.exe
	@if [ -z "$(FILE)" ]; then \
	  echo "ERROR: Please specify a RINEX file using FILE="; \
	  echo "Example: make run-test-mt10 FILE=data/parsing_file.25B"; \
	else \
	  ./$(TEST_DIR2)/tmp_test_mt10.exe "$(FILE)"; \
	fi

run-test-mt12: $(TEST_DIR2)/tmp_test_mt12.exe
	@if [ -z "$(FILE)" ]; then \
	  echo "ERROR: Please specify a RINEX file using FILE="; \
	  echo "Example: make run-test-mt12 FILE=data/parsing_file.25B"; \
	else \
	  ./$(TEST_DIR2)/tmp_test_mt12.exe "$(FILE)"; \
	fi

run-test-mt17: $(TEST_DIR2)/tmp_test_mt17.exe
	@if [ -z "$(FILE)" ]; then \
	  echo "ERROR: Please specify a RINEX file using FILE="; \
	  echo "Example: make run-test-mt17 FILE=data/parsing_file.25B"; \
	else \
	  ./$(TEST_DIR2)/tmp_test_mt17.exe "$(FILE)"; \
	fi

run-test-mt1_mt2: $(TEST_DIR2)/tmp_test_mt1_mt2.exe
	@if [ -z "$(FILE)" ]; then \
	  echo "ERROR: Please specify a RINEX file using FILE="; \
	  echo "Example: make run-test-mt1_mt2 FILE=data/parsing_file.25B"; \
	else \
	  ./$(TEST_DIR2)/tmp_test_mt1_mt2.exe "$(FILE)"; \
	fi

run-scan-hist: $(TEST_DIR2)/tmp_scan_mt_hist.exe
	@if [ -z "$(FILE)" ]; then \
	  echo "ERROR: Please specify a RINEX file using FILE="; \
	  echo "Example: make run-scan-hist FILE=data/parsing_file.25B"; \
	else \
	  ./$(TEST_DIR2)/tmp_scan_mt_hist.exe "$(FILE)"; \
	fi

run-test-mt6: $(TEST_DIR2)/tmp_test_mt6.exe
	@if [ -z "$(FILE)" ]; then \
	  echo "ERROR: Please specify a RINEX file using FILE="; \
	  echo "Example: make run-test-mt6 FILE=data/parsing_file.25B"; \
	else \
	  ./$(TEST_DIR2)/tmp_test_mt6.exe "$(FILE)"; \
	fi

run-test-mt7: $(TEST_DIR2)/tmp_test_mt7.exe
	@if [ -z "$(FILE)" ]; then \
	  echo "ERROR: Please specify a RINEX file using FILE="; \
	  echo "Example: make run-test-mt7 FILE=data/parsing_file.25B"; \
	else \
	  ./$(TEST_DIR2)/tmp_test_mt7.exe "$(FILE)"; \
	fi

run-test-mt17_18_24_25_26_28: $(TEST_DIR2)/tmp_test_mt17_18_24_25_26_28.exe
	@if [ -z "$(FILE)" ]; then \
	  echo "ERROR: Please specify a RINEX file using FILE="; \
	  echo "Example: make run-test-mt17_18_24_25_26_28 FILE=data/parsing_file.25B"; \
	else \
	  ./$(TEST_DIR2)/tmp_test_mt17_18_24_25_26_28.exe "$(FILE)"; \
	fi

run-hercules: hercules_transmitter.exe
		@echo "Starting Hercules Transmitter..."
		@if [ -z "$(FILE)" ]; then \
			echo "ERROR: Please specify a RINEX file using FILE="; \
			echo "Example: make run-hercules FILE=data/parsing_file.25B"; \
		else \
			./hercules_transmitter.exe "$(FILE)"; \
		fi

test-stat-models: $(TEST_DIR1)/test_statistical_models.exe
	./$(TEST_DIR1)/test_statistical_models.exe

run-hercules-test: test_hercules_features.exe
	./test_hercules_features.exe

## Housekeeping
clean:
	@echo "[CLEAN] Removing object files and executables..."
	$(RM) $(TARGET)
	$(RM) $(CORE_OBJS) $(MAIN_OBJ) $(BUILD_DIR)/sbas_extractor.o
	$(RM) $(BUILD_DIR)/*.o 2> /dev/null || true
	$(RM) $(TESTS_CURATED)
	@echo "[OK] Clean complete"

distclean: clean
	@echo "[DISTCLEAN] Removing build directory..."
	-$(RMDIR) $(BUILD_DIR) 2> /dev/null || $(RM) -r $(BUILD_DIR)
	@echo "[OK] Distclean complete"

rebuild: distclean all
	@echo "[OK] Rebuild complete"

# ============================================================================
# DEBUG & INFORMATIONAL TARGETS
# ============================================================================

help:
	@echo ""
	@echo "=========================================="
	@echo "  SBAS Extraction & Hercules Transmitter"
	@echo "=========================================="
	@echo ""
	@echo "Main Targets:"
	@echo "  make tests                 - Build all curated tests"
	@echo "  make hercules_transmitter.exe - Build main transmitter"
	@echo "  make all                   - Build tests + transmitter"
	@echo ""
	@echo "Run Targets (with FILE=<path> parameter):"
	@echo "  make run FILE=...          - Run main extractor"
	@echo "  make run-mt9 FILE=...      - Run MT9 test"
	@echo "  make run-test-mt{9,10,12,17} FILE=... - Run specific MT tests"
	@echo "  make run-hercules          - Run Hercules Transmitter"
	@echo ""
	@echo "Utility Targets:"
	@echo "  make clean                 - Remove object files & executables"
	@echo "  make distclean             - Remove all generated files"
	@echo "  make rebuild               - Clean rebuild"
	@echo "  make print-vars            - Show build configuration"
	@echo "  make info                  - Show detailed build info"
	@echo "  make debug-build           - Build with debug symbols"
	@echo "  make verbose               - Show detailed build output"
	@echo ""

info: print-vars
	@echo ""
	@echo "Paths:"
	@echo "  SRC_DIR=$(SRC_DIR)"
	@echo "  BUILD_DIR=$(BUILD_DIR)"
	@echo "  TEST_DIR1=$(TEST_DIR1)"
	@echo "  TEST_DIR2=$(TEST_DIR2)"
	@echo ""
	@echo "Curated Tests:"
	@for test in $(TESTS_CURATED); do echo "  $$test"; done
	@echo ""

print-vars:
	@echo ""
	@echo "Build Configuration:"
	@echo "  CC=$(CC)"
	@echo "  CFLAGS=$(CFLAGS)"
	@echo "  LDFLAGS=$(LDFLAGS)"
	@echo "  LDFLAGS_EXTRA=$(LDFLAGS_EXTRA)"
	@echo ""
	@echo "Source Files:"
	@echo "  CORE_SRCS: $(words $(CORE_SRCS)) files"
	@echo "  HERC_TX_SRCS: $(words $(HERC_TX_SRCS)) files"
	@echo ""
	@echo "Objects:"
	@echo "  CORE_OBJS=$(CORE_OBJS)"
	@echo "  HERC_TX_OBJS=$(HERC_TX_OBJS)"
	@echo ""

debug-build: DEBUG=-g -O0 -DDEBUG
debug-build: OPTIMIZE=
debug-build: clean all
	@echo "[OK] Debug build complete with symbols"

verbose:
	@echo "Next build will show detailed compilation output"
	@make all | sed 's/^/  /'

# ============================================================================
# PHONY TARGETS DECLARATION
# ============================================================================

.PHONY: all tests clean distclean rebuild help info print-vars debug-build verbose \
        run run-mt9 run-test-mt9 run-test-mt10 run-test-mt12 run-test-mt17 \
        run-test-mt1_mt2 run-test-mt6 run-test-mt7 run-scan-hist \
        run-mt17-params run-mt2-query run-mt1-display run-test-mt17_18_24_25_26_28 \
        run-hercules test-stat-models run-hercules-test