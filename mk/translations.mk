# Copyright (c) 2025 Preston Brown <pbrown@brown-house.net>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# HelixScreen UI Prototype - Translation Generation Module
# Handles i18n translation file generation from YAML master files

# Python venv for translation generator
VENV_PYTHON_TRANS := .venv/bin/python3

# Generated translation files
TRANS_GEN_DIR := src/generated
TRANS_GEN_C := $(TRANS_GEN_DIR)/lv_i18n_translations.c
TRANS_GEN_H := $(TRANS_GEN_DIR)/lv_i18n_translations.h
TRANS_XML := ui_xml/translations/translations.xml

# Source files for translations
TRANS_YAML := $(wildcard translations/*.yml)
TRANS_SCRIPT := scripts/generate_translations.py

# Add generated translation source to build
TRANS_SRCS := $(TRANS_GEN_C)
TRANS_OBJS := $(patsubst $(TRANS_GEN_DIR)/%.c,$(OBJ_DIR)/generated/%.o,$(TRANS_SRCS))

# Generate translations from YAML master files
# Creates: translations.xml, lv_i18n_translations.c, lv_i18n_translations.h
$(TRANS_GEN_C) $(TRANS_GEN_H) $(TRANS_XML): $(TRANS_YAML) $(TRANS_SCRIPT)
	$(ECHO) "$(CYAN)Generating translations from YAML...$(RESET)"
	$(Q)mkdir -p $(TRANS_GEN_DIR)
	$(Q)if [ -x "$(VENV_PYTHON_TRANS)" ]; then \
		$(VENV_PYTHON_TRANS) $(TRANS_SCRIPT); \
		echo "$(GREEN)✓ Translations generated$(RESET)"; \
	else \
		echo "$(RED)✗ Python venv not available - run 'make venv-setup'$(RESET)"; \
		exit 1; \
	fi

# Phony target for manual regeneration
.PHONY: translations
translations:
	$(ECHO) "$(CYAN)Regenerating translations...$(RESET)"
	$(Q)mkdir -p $(TRANS_GEN_DIR)
	$(Q)if [ -x "$(VENV_PYTHON_TRANS)" ]; then \
		$(VENV_PYTHON_TRANS) $(TRANS_SCRIPT); \
		echo "$(GREEN)✓ Translations regenerated$(RESET)"; \
	else \
		echo "$(RED)✗ Python venv not available - run 'make venv-setup'$(RESET)"; \
		exit 1; \
	fi

# Compile generated translation source
# Uses SUBMODULE_CFLAGS since it's generated code
$(OBJ_DIR)/generated/%.o: $(TRANS_GEN_DIR)/%.c
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(GREEN)[TRANS]$(RESET) $<"
	$(Q)$(CC) $(SUBMODULE_CFLAGS) $(INCLUDES) $(LV_CONF) -c $< -o $@ || { \
		echo "$(RED)✗ Translation compilation failed:$(RESET) $<"; \
		exit 1; \
	}
	$(call emit-compile-command,$(CC),$(SUBMODULE_CFLAGS) $(INCLUDES) $(LV_CONF),$<,$@)
