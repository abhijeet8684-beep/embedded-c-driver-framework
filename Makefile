CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -fPIC -Iinclude
PYTHON = python3

SRC = src/driver.c src/hw_sim.c
BUILD_DIR = build
TARGET_LIB = $(BUILD_DIR)/libembedded_driver.so

.PHONY: all test clean

all: $(TARGET_LIB)

$(TARGET_LIB): $(SRC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -shared -o $@ $^
	@echo "[BUILD] Successfully built $(TARGET_LIB)"

test:
	$(PYTHON) tests/test_runner.py

clean:
	rm -rf $(BUILD_DIR) /tmp/embedded_driver_test.so
	@echo "[CLEAN] Cleaned build artifacts."
