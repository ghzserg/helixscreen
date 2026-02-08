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
	$(Q)if [ -x "$(VENV_PYTHON_TRANS)" ] && $(VENV_PYTHON_TRANS) -c "import yaml" 2>/dev/null; then \
		$(VENV_PYTHON_TRANS) $(TRANS_SCRIPT); \
		echo "$(GREEN)✓ Translations generated$(RESET)"; \
	elif python3 -c "import yaml" 2>/dev/null; then \
		python3 $(TRANS_SCRIPT); \
		echo "$(GREEN)✓ Translations generated (system python3)$(RESET)"; \
	elif [ -f "$(TRANS_GEN_C)" ] && [ -f "$(TRANS_GEN_H)" ]; then \
		echo "$(YELLOW)⚠ Python venv not available - using existing generated translations$(RESET)"; \
		touch $(TRANS_GEN_C) $(TRANS_GEN_H) $(TRANS_XML); \
	else \
		echo "$(RED)✗ Python with yaml not available and no generated files - run 'make venv-setup'$(RESET)"; \
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

# Translation sync tool - manage translation string lifecycle
TRANS_SYNC_SCRIPT := scripts/translation_sync.py

# Sync translations: extract from XML/C++, merge new keys to YAML
# Use translation-sync-dry-run first to preview changes
.PHONY: translation-sync
translation-sync:
	$(ECHO) "$(CYAN)Syncing translation strings...$(RESET)"
	$(Q)if [ -x "$(VENV_PYTHON_TRANS)" ]; then \
		$(VENV_PYTHON_TRANS) $(TRANS_SYNC_SCRIPT) sync; \
	else \
		echo "$(RED)✗ Python venv not available - run 'make venv-setup'$(RESET)"; \
		exit 1; \
	fi

# Preview sync without modifying files
.PHONY: translation-sync-dry-run
translation-sync-dry-run:
	$(ECHO) "$(CYAN)Previewing translation sync...$(RESET)"
	$(Q)if [ -x "$(VENV_PYTHON_TRANS)" ]; then \
		$(VENV_PYTHON_TRANS) $(TRANS_SYNC_SCRIPT) sync --dry-run; \
	else \
		echo "$(RED)✗ Python venv not available - run 'make venv-setup'$(RESET)"; \
		exit 1; \
	fi

# Show translation coverage statistics
.PHONY: translation-coverage
translation-coverage:
	$(ECHO) "$(CYAN)Translation coverage report...$(RESET)"
	$(Q)if [ -x "$(VENV_PYTHON_TRANS)" ]; then \
		$(VENV_PYTHON_TRANS) $(TRANS_SYNC_SCRIPT) coverage --show-missing; \
	else \
		echo "$(RED)✗ Python venv not available - run 'make venv-setup'$(RESET)"; \
		exit 1; \
	fi

# Find obsolete translation keys (not used in XML)
.PHONY: translation-obsolete
translation-obsolete:
	$(ECHO) "$(CYAN)Finding obsolete translation keys...$(RESET)"
	$(Q)if [ -x "$(VENV_PYTHON_TRANS)" ]; then \
		$(VENV_PYTHON_TRANS) $(TRANS_SYNC_SCRIPT) obsolete; \
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
