# Embedded C Concurrency & Device Driver Framework

![C Language](https://img.shields.io/badge/Language-C99-blue.svg)
[![Embedded Driver CI](https://github.com/abhijeet8684-beep/embedded-c-driver-framework/actions/workflows/test.yml/badge.svg)](https://github.com/abhijeet8684-beep/embedded-c-driver-framework/actions/workflows/test.yml)
![Platform](https://img.shields.io/badge/Platform-Bare--Metal%20%7C%20RTOS-orange.svg)
![Concurrency](https://img.shields.io/badge/Concurrency-Lock--Free%20Atomics-purple.svg)
![License](https://img.shields.io/badge/License-MIT-lightgrey.svg)

A production-grade bare-metal embedded firmware codebase in C99. This framework showcases memory-mapped I/O (MMIO) register drivers, safe bitfield manipulation, lock-free interrupt synchronization, wall-clock bounded hardware polling, and an ISR-safe Single-Producer Single-Consumer (SPSC) circular ring buffer.

---

## 📋 Table of Contents

- [Overview](#-overview)
- [Project Architecture](#-project-architecture)
- [Core Firmware Modules](#-core-firmware-modules)
  - [1. Generic Bitfield Engine](#1-generic-bitfield-engine)
  - [2. SPX-100 Serial Bus Driver](#2-spx-100-serial-bus-driver)
  - [3. High-Performance Relaxed Atomic Counter](#3-high-performance-relaxed-atomic-counter)
  - [4. SPSC Lock-Free Ring Buffer](#4-spsc-lock-free-ring-buffer)
  - [5. Write-1-to-Clear (W1C) Concurrency](#5-write-1-to-clear-w1c-concurrency)
- [Known Limitations & Design Tradeoffs](#-known-limitations--design-tradeoffs)
- [Test Suite & Verification](#-test-suite--verification)
- [Build & Execution](#-build--execution)
- [Author](#-author)

---

## 🔍 Overview

Bare-metal firmware requires deterministic, race-free communication between asynchronous hardware peripherals, Interrupt Service Routines (ISRs), and main scheduling loops. This project provides:
- **Bit-Accurate Register Slicing**: Safe extraction and insertion of multi-bit fields preventing 32-bit shift overflows.
- **Robust Peripheral Driver**: Implements asynchronous transmit flows, register latch arming, wall-clock timeout bounds, and resilience to split hardware status updates.
- **Lock-Free Synchronization**: Eliminates mutexes in ISRs using GCC atomic builtins with explicit memory models (`__ATOMIC_RELAXED` for metrics and acquire-release for queues).
- **Hardware Simulation Testbed**: Validated against an asynchronous multi-threaded hardware simulator via Python `ctypes`.

---

## 🗂 Project Architecture

```text
embedded-c-driver-framework/
├── include/
│   ├── spx_peripheral.h      # SPX-100 register definitions & driver API
│   ├── ring_buffer.h         # Ring buffer & atomic synchronization prototypes
│   └── hw_sim.h              # Hardware simulator interface
├── src/
│   ├── driver.c              # Core driver, bitfield, and concurrency implementation
│   └── hw_sim.c              # Asynchronous hardware simulation engine
├── tests/
│   └── test_runner.py        # Automated unit and integration test suite
├── docs/
│   └── w1c_concurrency.md    # Architectural guide on W1C & memory ordering
├── .github/
│   └── workflows/test.yml    # Continuous Integration (CI) test workflow
├── Makefile                  # Build automation for shared libraries & testing
└── README.md
```

---

## 🛠 Core Firmware Modules

### 1. Generic Bitfield Engine
Functions operating on 32-bit register words with boundary protection:
- `get_field(reg_val, bit_offset, width)`: Extracts a `width`-bit field starting at `bit_offset`.
- `set_field(reg_val, bit_offset, width, value)`: Clears and inserts a new value into a field without modifying surrounding bits.
- **Shift Safety**: Uses `(width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u)` to avoid C undefined behavior when `width == 32`.

### 2. SPX-100 Serial Bus Driver
- `spx_init(clkdiv)`: Arms the post-reset `CTRL` write latch via a dummy read on `STATUS`, sets the clock prescaler, and enables the peripheral.
- `spx_send_byte_timeout(data, timeout_ms)`: Clears stale `DONE`/`ERR` flags via Write-1-to-Clear, loads the `TXDATA` FIFO, asserts `START`, and polls completion bounded by a wall-clock timeout.
- `spx_is_busy()`: Non-blocking check on active transfer status.

### 3. High-Performance Relaxed Atomic Counter
- Solves read-modify-write (RMW) data races in multithreaded / ISR contexts.
- Uses `__ATOMIC_RELAXED` on both `counter_isr_increment()` and `counter_read()` for maximum efficiency without full-barrier overhead.

### 4. SPSC Lock-Free Ring Buffer
- Implements a 16-byte Single-Producer Single-Consumer circular queue.
- **Producer (`rb_push`)**: Reads tail with `__ATOMIC_ACQUIRE`, writes payload, and updates head with `__ATOMIC_RELEASE`.
- **Consumer (`rb_pop`)**: Reads head with `__ATOMIC_ACQUIRE`, reads payload, and updates tail with `__ATOMIC_RELEASE`.

### 5. Write-1-to-Clear (W1C) Concurrency
- Demonstrates how independent status bits (`DONE` and `ERR`) can be cleared simultaneously from distinct contexts (ISR vs. Main loop) using isolated bit writes without read-modify-write conflicts. *(See [docs/w1c_concurrency.md](docs/w1c_concurrency.md))*.

---

## ⚖️ Known Limitations & Design Tradeoffs

### 1. SPSC Ring Buffer Slot Utilization (Capacity - 1)
- **Tradeoff**: The ring buffer has a fixed capacity of 16 bytes but holds a maximum of 15 elements.
- **Rationale**: To disambiguate between *Full* and *Empty* states without introducing a shared `count` variable (which would require atomic RMW operations on both push and pop), the queue reserves one empty slot when full (`(head + 1) % CAP == tail`). This enables lock-free SPSC operation with only single-variable ownership.

### 2. Atomic Counter Memory Ordering (`__ATOMIC_RELAXED`)
- **Tradeoff**: `counter_isr_increment` and `counter_read` use relaxed memory ordering rather than sequential consistency (`__ATOMIC_SEQ_CST`) or acquire-release.
- **Rationale**: The counter serves as an independent statistical metric / event count. Because it does not publish or guard other memory locations (no happens-before relationship required), relaxed ordering guarantees atomic read-modify-write while avoiding pipeline stalls and hardware memory barrier instructions (`DMB` on ARM, locked bus on x86).

### 3. Wall-Clock Timeout Dependency (`spx_get_tick_ms`)
- **Tradeoff**: Bounding driver loops by wall-clock time requires an underlying system tick source.
- **Rationale**: Iteration-count loops (`for (i=0; i<2000000; i++)`) vary wildly with CPU frequency, compiler optimization levels (`-O0` vs `-O3`), and cache state. The driver uses a weak symbol `spx_get_tick_ms()` defaulting to monotonic POSIX time on host builds, which can be overridden by bare-metal targets using `SysTick` or a hardware timer.

### 4. SPSC Single-Context Invariance
- **Warning**: The ring buffer is strictly Single-Producer Single-Consumer. Calling `rb_push` from multiple interrupt priorities or `rb_pop` from multiple worker threads without external locking is undefined behavior.

---

## 🧪 Test Suite & Verification

The integration test suite in `tests/test_runner.py` validates normal operation, error injection, wall-clock timeout thresholds, and decoupled status updates:

```bash
python3 tests/test_runner.py
```

### Test Output:
```text
============================================================
  Embedded C Driver & Concurrency Test Suite
============================================================
Compiling shared library with GCC...
Build successful.

--- 1. Bitfield Manipulation ---
[PASS] get_field middle extraction
[PASS] get_field upper nibble
[PASS] get_field full 32-bit width
[PASS] set_field lower nibble
[PASS] set_field splice in word

--- 2. SPX-100 Driver & Errata Handling ---
[PASS] spx_init sets CTRL.EN bit
[PASS] spx_init configures CLKDIV (bits 4:2)
[PASS] spx_is_enabled returns true
[PASS] spx_send_byte successful transmission
[PASS] spx_is_busy returns false when idle
[PASS] spx_send_byte returns -1 on hardware error
[PASS] spx_send_byte_timeout returns -2 on hardware stall
[PASS] spx_send_byte_timeout honors wall-clock threshold (~30ms)
[PASS] spx_send_byte succeeds during decoupled/split status updates

--- 3. Atomic Counter ---
[PASS] counter_read matches 100 increments with __ATOMIC_RELAXED

--- 4. Lock-Free SPSC Ring Buffer ---
[PASS] rb_pop returns -1 on empty buffer
[PASS] rb_push enqueues items up to full
[PASS] rb_push rejects when full
[PASS] rb_pop verifies FIFO ordering
[PASS] rb_pop returns empty after drain

============================================================
  ALL MODULE TESTS PASSED (100%)
============================================================
```

---

## 🚀 Build & Execution

### Build with Make:
```bash
make all        # Builds the shared library build/libembedded_driver.so
make test       # Runs the automated integration test runner
make clean      # Cleans build artifacts
```

---

## 👤 Author

- **Abhijeet**
- GitHub: [@abhijeet8684-beep](https://github.com/abhijeet8684-beep)
- Email: abhijeet8684@gmail.com
