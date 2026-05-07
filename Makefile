.PHONY: help install install-dev lint format test firmware-build esp-idf-setup clean

help:
	@echo "Bluetooth Jieli Research — Developer Makefile"
	@echo ""
	@echo "Usage: make [target]"
	@echo ""
	@echo "Setup Targets:"
	@echo "  install              Install runtime dependencies (requirements.txt)"
	@echo "  install-dev          Install with dev tools (pylint, black, pytest)"
	@echo "  esp-idf-setup        One-command ESP-IDF v6.1 installation to ~/esp-idf"
	@echo ""
	@echo "Code Quality Targets:"
	@echo "  lint                 Run pylint, black (check), and flake8 on scripts/"
	@echo "  format               Auto-format Python code with black"
	@echo "  test                 Run pytest (if tests/ exists)"
	@echo ""
	@echo "Firmware Targets:"
	@echo "  firmware-build       Compile ESP32 firmware (idf.py build)"
	@echo "  firmware-clean       Remove build/ and sdkconfig from firmware/"
	@echo ""
	@echo "Maintenance:"
	@echo "  clean                Remove __pycache__, *.pyc, build/, dist/"
	@echo ""
	@echo "Examples:"
	@echo "  make install && python3 scripts/avrcp/avrcp_pause.py"
	@echo "  make install-dev && make lint"
	@echo "  make esp-idf-setup && make firmware-build"
	@echo ""

# ============================================================================
# SETUP TARGETS
# ============================================================================

install:
	@echo "📦 Installing runtime dependencies..."
	pip install -r requirements.txt
	@echo "✅ Done! Run: python3 scripts/avrcp/avrcp_pause.py"

install-dev:
	@echo "🔧 Installing development dependencies..."
	pip install -e ".[dev]"
	@echo "✅ Done! Run: make lint"

esp-idf-setup:
	@echo "📥 Installing ESP-IDF v6.1 to ~/esp-idf..."
	@if [ -d ~/esp-idf ]; then \
		echo "⚠️  ~/esp-idf already exists. Skipping clone."; \
	else \
		git clone --branch v6.1 https://github.com/espressif/esp-idf.git ~/esp-idf; \
	fi
	@cd ~/esp-idf && ./install.sh
	@echo ""
	@echo "✅ ESP-IDF v6.1 installed!"
	@echo "📌 Next: source ~/esp-idf/export.sh"
	@echo "   Then: make firmware-build"
	@echo ""

# ============================================================================
# CODE QUALITY TARGETS
# ============================================================================

lint:
	@echo "🔍 Linting Python scripts..."
	@echo ""
	@echo "Black (style check):"
	@black --check scripts/ || true
	@echo ""
	@echo "Pylint (code quality):"
	@pylint scripts/ || true
	@echo ""
	@echo "Flake8 (PEP8):"
	@flake8 scripts/ || true
	@echo ""
	@echo "✅ Lint complete"

format:
	@echo "🎨 Auto-formatting Python code with black..."
	black scripts/
	@echo "✅ Formatting complete"

test:
	@echo "🧪 Running tests..."
	@if [ -d tests ]; then \
		pytest tests/ -v; \
	else \
		echo "⚠️  No tests/ directory found. Skipping."; \
	fi
	@echo "✅ Test run complete"

# ============================================================================
# FIRMWARE TARGETS
# ============================================================================

firmware-build:
	@echo "🔨 Building ESP32 firmware..."
	@if [ -z "$$IDF_PATH" ]; then \
		echo "⚠️  IDF_PATH not set. Run: source ~/esp-idf/export.sh"; \
		exit 1; \
	fi
	cd firmware/esp32_avrcp_console && idf.py build
	@echo "✅ Firmware built! Location: firmware/esp32_avrcp_console/build/"
	@echo "📌 Next: idf.py -p /dev/ttyUSB0 flash monitor"

firmware-clean:
	@echo "🧹 Cleaning firmware build artifacts..."
	rm -rf firmware/esp32_avrcp_console/build/
	rm -f firmware/esp32_avrcp_console/sdkconfig
	rm -f firmware/esp32_avrcp_console/sdkconfig.old
	@echo "✅ Firmware directory cleaned"

# ============================================================================
# MAINTENANCE TARGETS
# ============================================================================

clean:
	@echo "🧹 Cleaning Python cache and build artifacts..."
	find . -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null || true
	find . -type f -name "*.pyc" -delete
	find . -type f -name "*.pyo" -delete
	find . -type d -name "*.egg-info" -exec rm -rf {} + 2>/dev/null || true
	rm -rf build/ dist/ .eggs/
	@echo "✅ Clean complete"

# ============================================================================
# QUICK START RECIPES
# ============================================================================

.PHONY: quick-start-python quick-start-firmware

quick-start-python: install
	@echo ""
	@echo "🚀 Python Quick Start"
	@echo "Run an AVRCP test:"
	@echo "  python3 scripts/avrcp/avrcp_pause.py"
	@echo ""

quick-start-firmware: esp-idf-setup
	@echo ""
	@echo "🚀 Firmware Quick Start"
	@echo "Next steps:"
	@echo "  source ~/esp-idf/export.sh"
	@echo "  make firmware-build"
	@echo "  idf.py -p /dev/ttyUSB0 flash monitor"
	@echo ""
