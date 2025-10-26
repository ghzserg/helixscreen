# HelixScreen UI Prototype Makefile
# LVGL 9 + SDL2 simulator

# Compilers
CC := clang
CXX := clang++
CFLAGS := -std=c11 -Wall -Wextra -O2 -g
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -g

# Directories
SRC_DIR := src
INC_DIR := include
BUILD_DIR := build
BIN_DIR := $(BUILD_DIR)/bin
OBJ_DIR := $(BUILD_DIR)/obj

# LVGL
LVGL_DIR := lvgl
LVGL_INC := -I$(LVGL_DIR) -I$(LVGL_DIR)/src
LVGL_SRCS := $(shell find $(LVGL_DIR)/src -name "*.c")
LVGL_OBJS := $(patsubst $(LVGL_DIR)/%.c,$(OBJ_DIR)/lvgl/%.o,$(LVGL_SRCS))

# ThorVG sources (.cpp files for SVG support)
THORVG_SRCS := $(shell find $(LVGL_DIR)/src/libs/thorvg -name "*.cpp" 2>/dev/null)
THORVG_OBJS := $(patsubst $(LVGL_DIR)/%.cpp,$(OBJ_DIR)/lvgl/%.o,$(THORVG_SRCS))

# LVGL Demos (separate target)
LVGL_DEMO_SRCS := $(shell find $(LVGL_DIR)/demos -name "*.c")
LVGL_DEMO_OBJS := $(patsubst $(LVGL_DIR)/%.c,$(OBJ_DIR)/lvgl/%.o,$(LVGL_DEMO_SRCS))

# Application C++ sources (exclude test binaries)
APP_SRCS := $(filter-out $(SRC_DIR)/test_dynamic_cards.cpp,$(wildcard $(SRC_DIR)/*.cpp))
APP_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(APP_SRCS))

# Fonts
FONT_SRCS := assets/fonts/fa_icons_64.c assets/fonts/fa_icons_48.c assets/fonts/fa_icons_32.c assets/fonts/fa_icons_24.c assets/fonts/fa_icons_16.c assets/fonts/arrows_64.c assets/fonts/arrows_48.c assets/fonts/arrows_32.c
FONT_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(FONT_SRCS))

# Material Design Icons
MATERIAL_ICON_SRCS := $(wildcard assets/images/material/*.c)
MATERIAL_ICON_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(MATERIAL_ICON_SRCS))

# SDL2
SDL2_CFLAGS := $(shell sdl2-config --cflags)
SDL2_LIBS := $(shell sdl2-config --libs)

# Include paths
INCLUDES := -I. -I$(INC_DIR) $(LVGL_INC) $(SDL2_CFLAGS)

# Linker flags
LDFLAGS := $(SDL2_LIBS) -lm -lpthread

# Binary
TARGET := $(BIN_DIR)/helix-ui-proto

# LVGL configuration
LV_CONF := -DLV_CONF_INCLUDE_SIMPLE

# Parallel build (auto-detect CPU cores and enable by default)
NPROC := $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
MAKEFLAGS += -j$(NPROC)

# Test configuration
TEST_DIR := tests
TEST_UNIT_DIR := $(TEST_DIR)/unit
TEST_FRAMEWORK_DIR := $(TEST_DIR)/framework
TEST_BIN := $(BIN_DIR)/run_tests
TEST_SRCS := $(wildcard $(TEST_UNIT_DIR)/*.cpp)
TEST_OBJS := $(patsubst $(TEST_UNIT_DIR)/%.cpp,$(OBJ_DIR)/tests/%.o,$(TEST_SRCS))

.PHONY: all clean run test test-cards test-print-select

all: $(TARGET)

# Link binary
$(TARGET): $(APP_OBJS) $(LVGL_OBJS) $(THORVG_OBJS) $(FONT_OBJS) $(MATERIAL_ICON_OBJS)
	@mkdir -p $(BIN_DIR)
	@echo "Linking $@..."
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Build complete: $@"

# Collect all header dependencies
HEADERS := $(shell find $(INC_DIR) -name "*.h" 2>/dev/null)

# Compile app C++ sources (depend on headers)
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) $(INCLUDES) $(LV_CONF) -c $< -o $@

# Compile LVGL sources
$(OBJ_DIR)/lvgl/%.o: $(LVGL_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling LVGL: $<..."
	@$(CC) $(CFLAGS) $(INCLUDES) $(LV_CONF) -c $< -o $@

# Compile LVGL C++ sources (ThorVG)
$(OBJ_DIR)/lvgl/%.o: $(LVGL_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling LVGL C++: $<..."
	@$(CXX) $(CXXFLAGS) $(INCLUDES) $(LV_CONF) -c $< -o $@

# Compile font sources
$(OBJ_DIR)/assets/fonts/%.o: assets/fonts/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling font: $<..."
	@$(CC) $(CFLAGS) $(INCLUDES) $(LV_CONF) -c $< -o $@

# Compile Material Design icon sources
$(OBJ_DIR)/assets/images/material/%.o: assets/images/material/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling Material icon: $<..."
	@$(CC) $(CFLAGS) $(INCLUDES) $(LV_CONF) -c $< -o $@

# Run the prototype
run: $(TARGET)
	@echo "Running UI prototype..."
	@$(TARGET)

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete"

# Parallel build target
build:
	@echo "Building with $(NPROC) parallel jobs..."
	@$(MAKE) -j$(NPROC) all


# Demo widgets target
demo: $(LVGL_OBJS) $(LVGL_DEMO_OBJS)
	@echo "Compiling demo..."
	$(CXX) -o $(BIN_DIR)/lvgl-demo demo-widgets.cpp $(LVGL_OBJS) $(LVGL_DEMO_OBJS) \
	  $(LVGL_INC) $(SDL2_CFLAGS) $(CXXFLAGS) $(SDL2_LIBS)
	@echo "✓ Demo ready: ./build/bin/lvgl-demo"
	@echo ""
	@echo "Run: ./build/bin/lvgl-demo"

$(OBJ_DIR)/lvgl/demos/%.o: $(LVGL_DIR)/demos/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling LVGL demo: $<..."
	$(CC) -c $< -o $@ $(LVGL_INC) $(CFLAGS)

# Test targets
test: $(TEST_BIN)
	@echo "Running unit tests..."
	@$(TEST_BIN)

$(TEST_BIN): $(TEST_OBJS) $(LVGL_OBJS) $(OBJ_DIR)/ui_nav.o
	@mkdir -p $(BIN_DIR)
	@echo "Linking test binary..."
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Test binary ready: $@"

$(OBJ_DIR)/tests/%.o: $(TEST_UNIT_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling test: $<..."
	@$(CXX) $(CXXFLAGS) -I$(TEST_FRAMEWORK_DIR) $(INCLUDES) $(LV_CONF) -c $< -o $@

# Dynamic card instantiation test
TEST_CARDS_BIN := $(BIN_DIR)/test_dynamic_cards
TEST_CARDS_OBJ := $(OBJ_DIR)/test_dynamic_cards.o

test-cards: $(TEST_CARDS_BIN)
	@echo "Running dynamic card test..."
	@$(TEST_CARDS_BIN)

$(TEST_CARDS_BIN): $(TEST_CARDS_OBJ) $(LVGL_OBJS) $(FONT_OBJS)
	@mkdir -p $(BIN_DIR)
	@echo "Linking test_dynamic_cards..."
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "✓ Test binary ready: $@"

$(TEST_CARDS_OBJ): $(SRC_DIR)/test_dynamic_cards.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) $(INCLUDES) $(LV_CONF) -c $< -o $@

# Print select panel test with mock data
TEST_PRINT_SELECT_BIN := $(BIN_DIR)/test_print_select_panel
TEST_PRINT_SELECT_OBJ := $(OBJ_DIR)/tests/test_print_select_panel.o
MOCK_FILES_OBJ := $(OBJ_DIR)/tests/mock_print_files.o

test-print-select: $(TEST_PRINT_SELECT_BIN)
	@echo "Running print select panel test..."
	@$(TEST_PRINT_SELECT_BIN)

$(TEST_PRINT_SELECT_BIN): $(TEST_PRINT_SELECT_OBJ) $(MOCK_FILES_OBJ) $(LVGL_OBJS)
	@mkdir -p $(BIN_DIR)
	@echo "Linking test_print_select_panel..."
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "✓ Test binary ready: $@"

$(TEST_PRINT_SELECT_OBJ): $(TEST_DIR)/test_print_select_panel.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) -I$(TEST_DIR) $(INCLUDES) $(LV_CONF) -c $< -o $@

$(MOCK_FILES_OBJ): $(TEST_DIR)/mock_print_files.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) -I$(TEST_DIR) $(INCLUDES) $(LV_CONF) -c $< -o $@

