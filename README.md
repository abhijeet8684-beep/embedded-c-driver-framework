# Embedded C Concurrency & Device Driver Framework

![C Language](https://img.shields.io/badge/Language-C99-blue.svg)
![Build](https://img.shields.io/badge/Build-Passing-brightgreen.svg)
![Platform](https://img.shields.io/badge/Platform-Bare--Metal%20%7C%20RTOS-orange.svg)
![Concurrency](https://img.shields.io/badge/Concurrency-Lock--Free%20Atomics-purple.svg)
![License](https://img.shields.io/badge/License-MIT-lightgrey.svg)

A high-performance embedded systems firmware codebase in C99. This project showcases low-level hardware driver development, memory-mapped I/O (MMIO), register-level bitfield manipulation, lock-free interrupt synchronization, and an ISR-safe Single-Producer Single-Consumer (SPSC) circular ring buffer.

---

## 📋 Table of Contents

- [Overview](#-overview)
- [Project Architecture](#-project-architecture)
- [Core Firmware Modules](#-core-firmware-modules)
  - [1. Generic Bitfield Engine](#1-generic-bitfield-engine)
  - [2. SPX-100 Serial Bus Driver](#2-spx-100-serial-bus-driver)
  - [3. Lock-Free Atomic Counter](#3-lock-free-atomic-counter)
  - [4. SPSC Lock-Free Ring Buffer](#4-spsc-lock-free-ring-buffer)
  - [5. Write-1-to-Clear (W1C) Concurrency](#5-write-1-to-clear-w1c-concurrency)
- [Technical Highlights](#-technical-highlights)
- [Test Suite & Verification](#-test-suite--verification)
- [Build & Execution](#-build--execution)
- [Author](#-author)

---

## 🔍 Overview

Bare-metal firmware requires race-free communication between asynchronous hardware peripherals, Interrupt Service Routines (ISRs), and main scheduling loops. This project provides:
- **Bit-Accurate Register Slicing**: Safe extraction and insertion of multi-bit fields preventing 32-bit shift overflows.
- **Robust Peripheral Driver**: Implements asynchronous transmit flows, register latch arming, and bounded polling.
- **Lock-Free Synchronization**: Eliminates mutexes in ISRs using GCC atomic builtins with explicit acquire-release memory barriers.
- **Hardware Simulation Testbed**: Validated against an asynchronous multi-threaded hardware simulator via Python `ctypes`.

---

## 🗂 Project Architecture

```text
embedded-c-concurrency-driver/
├── include/
│   ├── spx_peripheral.h      # SPX-100 register definitions & driver API
│   ├── ring_buffer.h         # Ring buffer & atomic synchronization prototypes
│   └── hw_sim.h              # Hardware simulator interface
├── src/
│   ├── driver.c              # Core driver, bitfield, and concurrency implementation
│   └── hw_sim.c              # Asynchronous hardware simulation engine
├── tests/
│   └── test_runner.py        # Python ctypes automated integration test suite
├── docs/
│   └── w1c_concurrency.md    # Detailed guide on W1C registers & memory ordering
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
- `spx_send_byte(data)`: Clears stale `DONE`/`ERR` flags via Write-1-to-Clear, loads the `TXDATA` FIFO, asserts `START`, and polls completion with timeout protection.
- `spx_is_busy()`: Non-blocking check on active transfer status.

### 3. Lock-Free Atomic Counter
- Solves read-modify-write (RMW) data races in multithreaded / ISR contexts.
- `counter_isr_increment()` uses `__atomic_fetch_add(&g_counter, 1, __ATOMIC_SEQ_CST)`.
- `counter_read()` uses `__atomic_load_n(&g_counter, __ATOMIC_SEQ_CST)`.

### 4. SPSC Lock-Free Ring Buffer
- Implements a 16-byte Single-Producer Single-Consumer circular queue.
- **Producer (`rb_push`)**: Reads tail with `__ATOMIC_ACQUIRE`, writes payload, and updates head with `__ATOMIC_RELEASE`.
- **Consumer (`rb_pop`)**: Reads head with `__ATOMIC_ACQUIRE`, reads payload, and updates tail with `__ATOMIC_RELEASE`.
- **Zero Mutex Overhead**: Deterministic execution time, safe for high-frequency interrupts.

### 5. Write-1-to-Clear (W1C) Concurrency
- Demonstrates how independent status bits (`DONE` and `ERR`) can be cleared simultaneously from distinct contexts (ISR vs. Main loop) using isolated bit writes without read-modify-write conflicts. *(See [docs/w1c_concurrency.md](docs/w1c_concurrency.md) for full details)*.

---

## ⚡ Technical Highlights

| Feature | Implementation | Benefit |
| :--- | :--- | :--- |
| **ISR Safety** | Lock-Free Atomics (`__atomic_*`) | Eliminates deadlocks & priority inversion in interrupt context |
| **Memory Consistency** | Acquire-Release Barriers | Prevents CPU instruction reordering across thread boundaries |
| **Hardware Robustness** | Latch Arming & Bounded Polling | Handles hardware initialization timing and avoids watchdog resets |
| **Portability** | Standard C99 & POSIX | Cross-platform compatibility across bare-metal microcontrollers and RTOS |

---

## 🧪 Test Suite & Verification

Run the automated integration test suite:

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

--- 3. Atomic Counter ---
[PASS] counter_read matches 100 increments

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
make test       # Runs the automated test runner
make clean      # Cleans build artifacts
```

---

## 👤 Author

- **Abhijeet**
- GitHub: [@abhijeet8684-beep](https://github.com/abhijeet8684-beep)
- Email: abhijeet8684@gmail.com
