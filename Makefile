# RP2350 USB Audio DAC Makefile
# Simplifies build, deployment, and development workflow

.PHONY: help build build-pdm build-096 _build clean deploy monitor shell docker-build rebuild info

# Default target
.DEFAULT_GOAL := help

# Configuration
DOCKER_IMAGE := rp2350-usbaudio
BUILD_DIR := build
UF2_FILE := $(BUILD_DIR)/usb_audio.uf2
PICO_SDK := ../pico-sdk
PICO_EXTRAS := /pico-extras

# Auto-detect RP2350 mount point
MOUNT_POINT_MACOS := $(shell ls -d /Volumes/RP* 2>/dev/null | head -1)
MOUNT_POINT_LINUX := $(shell ls -d /media/*/RPI-RP2 2>/dev/null | head -1)
MOUNT_POINT := $(if $(MOUNT_POINT_MACOS),$(MOUNT_POINT_MACOS),$(MOUNT_POINT_LINUX))

# Auto-detect serial port
SERIAL_PORT_MACOS := $(shell ls /dev/cu.usbmodem* 2>/dev/null | head -1)
SERIAL_PORT_LINUX := $(shell ls /dev/ttyACM* 2>/dev/null | head -1)
SERIAL_PORT := $(if $(SERIAL_PORT_MACOS),$(SERIAL_PORT_MACOS),$(SERIAL_PORT_LINUX))

# Colors for output
COLOR_RESET := \033[0m
COLOR_BOLD := \033[1m
COLOR_GREEN := \033[32m
COLOR_YELLOW := \033[33m
COLOR_BLUE := \033[34m

##@ General

help: ## Display this help message
	@echo "$(COLOR_BOLD)RP2350 USB Audio (I2S/PDM) - Development Commands$(COLOR_RESET)"
	@echo ""
	@awk 'BEGIN {FS = ":.*##"; printf ""} /^[a-zA-Z_-]+:.*?##/ { printf "  $(COLOR_BLUE)%-15s$(COLOR_RESET) %s\n", $$1, $$2 } /^##@/ { printf "\n$(COLOR_BOLD)%s$(COLOR_RESET)\n", substr($$0, 5) } ' $(MAKEFILE_LIST)
	@echo ""

info: ## Show project information
	@echo "$(COLOR_BOLD)Project Information:$(COLOR_RESET)"
	@echo "  Firmware:      $(UF2_FILE)"
	@echo "  Docker image:  $(DOCKER_IMAGE)"
	@echo "  Mount point:   $(if $(MOUNT_POINT),$(COLOR_GREEN)$(MOUNT_POINT)$(COLOR_RESET),$(COLOR_YELLOW)Not detected$(COLOR_RESET))"
	@echo "  Serial port:   $(if $(SERIAL_PORT),$(COLOR_GREEN)$(SERIAL_PORT)$(COLOR_RESET),$(COLOR_YELLOW)Not detected$(COLOR_RESET))"
	@echo "  Pico SDK:      $(PICO_SDK)"
	@echo "  Pico Extras:   $(PICO_EXTRAS) (baked into Docker image)"
	@if [ -f "$(UF2_FILE)" ]; then \
		echo "  Build status:  $(COLOR_GREEN)Built$(COLOR_RESET) ($$(ls -lh $(UF2_FILE) | awk '{print $$5}'))"; \
	else \
		echo "  Build status:  $(COLOR_YELLOW)Not built$(COLOR_RESET)"; \
	fi

##@ Build

docker-build: ## Build Docker image (only needed once)
	@echo "$(COLOR_BOLD)Building Docker image...$(COLOR_RESET)"
	docker build -t $(DOCKER_IMAGE) .
	@echo "$(COLOR_GREEN)Docker image built successfully$(COLOR_RESET)"

build: ## Build firmware (I2S backend, default)
	@$(MAKE) _build CMAKE_EXTRA_FLAGS=""

build-pdm: ## Build firmware with PDM audio output on GP18
	@$(MAKE) _build CMAKE_EXTRA_FLAGS="-DAUDIO_PDM=ON"

build-096: ## Build firmware for RP2350-LCD-0.96 (PDM + 160x80 ST7735S)
	@$(MAKE) _build CMAKE_EXTRA_FLAGS="-DBOARD_096=ON -DAUDIO_PDM=ON"

_build:
	@echo "$(COLOR_BOLD)Building firmware...$(COLOR_RESET)"
	@if ! docker images | grep -q $(DOCKER_IMAGE); then \
		echo "$(COLOR_YELLOW)Docker image not found. Building...$(COLOR_RESET)"; \
		$(MAKE) docker-build; \
	fi
	docker run --rm \
		-v "$$(pwd):/work" \
		-v "$$(cd $(PICO_SDK) && pwd):/pico-sdk" \
		-w /work \
		$(DOCKER_IMAGE) \
		/bin/bash -c " \
			set -e; \
			mkdir -p build; \
			cd build; \
			cmake -DPICO_SDK_PATH=/pico-sdk -DPICO_EXTRAS_PATH=/pico-extras $(CMAKE_EXTRA_FLAGS) ..; \
			make -j$$(nproc); \
		"
	@echo ""
	@echo "$(COLOR_GREEN)Build complete!$(COLOR_RESET)"
	@ls -lh $(UF2_FILE)

rebuild: clean build ## Clean and rebuild firmware

clean: ## Clean build artifacts
	@echo "$(COLOR_BOLD)Cleaning build artifacts...$(COLOR_RESET)"
	rm -rf $(BUILD_DIR)
	@echo "$(COLOR_GREEN)Clean complete$(COLOR_RESET)"

##@ Deployment

deploy: ## Upload firmware to RP2350 board
	@if [ ! -f "$(UF2_FILE)" ]; then \
		echo "$(COLOR_YELLOW)Firmware not built. Building first...$(COLOR_RESET)"; \
		$(MAKE) build; \
	fi
	@echo "$(COLOR_BOLD)Deploying firmware...$(COLOR_RESET)"
	@if [ -z "$(MOUNT_POINT)" ]; then \
		echo "$(COLOR_YELLOW)RP2350 board not detected!$(COLOR_RESET)"; \
		echo ""; \
		echo "Please:"; \
		echo "  1. Hold BOOTSEL button on your RP2350 board"; \
		echo "  2. Connect via USB"; \
		echo "  3. Release BOOTSEL button"; \
		echo "  4. Run 'make deploy' again"; \
		echo ""; \
		echo "Or manually copy: cp $(UF2_FILE) /Volumes/RP2350/"; \
		exit 1; \
	fi
	@echo "Copying to $(MOUNT_POINT)..."
	cp $(UF2_FILE) $(MOUNT_POINT)/
	@echo "Ejecting $(MOUNT_POINT)..."
	@if [ "$(shell uname)" = "Darwin" ]; then \
		diskutil eject $(MOUNT_POINT) 2>/dev/null || true; \
	else \
		umount $(MOUNT_POINT) 2>/dev/null || eject $(MOUNT_POINT) 2>/dev/null || true; \
	fi
	@echo "$(COLOR_GREEN)Deployment complete!$(COLOR_RESET)"
	@echo ""
	@echo "$(COLOR_BOLD)Board will reboot automatically.$(COLOR_RESET)"
	@echo "To monitor serial output, run: $(COLOR_BLUE)make monitor$(COLOR_RESET)"

flash: deploy ## Alias for deploy

##@ Development

monitor: ## Connect to serial console (UART)
	@echo "$(COLOR_BOLD)Connecting to serial console...$(COLOR_RESET)"
	@if [ -z "$(SERIAL_PORT)" ]; then \
		echo "$(COLOR_YELLOW)Serial port not detected!$(COLOR_RESET)"; \
		echo ""; \
		echo "Available ports:"; \
		ls /dev/cu.* 2>/dev/null || ls /dev/ttyACM* 2>/dev/null || echo "  None found"; \
		echo ""; \
		echo "Try: screen /dev/cu.usbserial* 115200"; \
		exit 1; \
	fi
	@echo "Port: $(SERIAL_PORT)"
	@echo "Baud: 115200"
	@echo ""
	@echo "$(COLOR_YELLOW)Press Ctrl+A then K to exit screen$(COLOR_RESET)"
	@sleep 1
	screen $(SERIAL_PORT) 115200

shell: ## Open a shell in Docker container
	@echo "$(COLOR_BOLD)Opening Docker shell...$(COLOR_RESET)"
	@if ! docker images | grep -q $(DOCKER_IMAGE); then \
		echo "$(COLOR_YELLOW)Docker image not found. Building...$(COLOR_RESET)"; \
		$(MAKE) docker-build; \
	fi
	docker run --rm -it \
		-v "$$(pwd):/work" \
		-v "$$(cd $(PICO_SDK) && pwd):/pico-sdk" \
		-w /work \
		$(DOCKER_IMAGE) \
		/bin/bash -c "export PICO_EXTRAS_PATH=/pico-extras && /bin/bash"

##@ Quick Workflows

all: build deploy monitor ## Build, deploy, and monitor (complete workflow)

quick: ## Build and deploy (without monitoring)
	@$(MAKE) build
	@$(MAKE) deploy

watch: ## Rebuild and deploy when source files change (requires entr)
	@if ! command -v entr >/dev/null 2>&1; then \
		echo "$(COLOR_YELLOW)'entr' not found. Install with: brew install entr$(COLOR_RESET)"; \
		exit 1; \
	fi
	@echo "$(COLOR_BOLD)Watching for changes...$(COLOR_RESET)"
	@echo "$(COLOR_YELLOW)Press Ctrl+C to stop$(COLOR_RESET)"
	@find src -name "*.c" -o -name "*.h" | entr -r make quick
