CMAKE ?= cmake
BUILD_DIR ?= build
REDIS_HOST ?= 127.0.0.1
REDIS_PORT ?= 6379

.PHONY: all configure build test run clean

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR)

build: configure
	$(CMAKE) --build $(BUILD_DIR)

test: build
	./$(BUILD_DIR)/engine_tests

run: build
	REDIS_HOST=$(REDIS_HOST) REDIS_PORT=$(REDIS_PORT) ./$(BUILD_DIR)/engine_app

clean:
	rm -rf $(BUILD_DIR)
