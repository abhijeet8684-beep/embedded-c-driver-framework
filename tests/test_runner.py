#!/usr/bin/env python3
"""
test_runner.py -- Unit & Integration Test Suite for Embedded C Firmware Modules
Validates Bitfield Helpers, SPX-100 Peripheral Driver, Atomic Counter, and SPSC Ring Buffer.
"""

import ctypes
import os
import subprocess
import sys
import tempfile

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(PROJECT_ROOT, "src")
INCLUDE_DIR = os.path.join(PROJECT_ROOT, "include")


def build_shared_library():
    so_path = os.path.join(tempfile.gettempdir(), "embedded_driver_test.so")
    cmd = [
        "gcc", "-shared", "-fPIC", "-pthread", "-O2", "-Wall", "-Wextra",
        f"-I{INCLUDE_DIR}", "-o", so_path,
        os.path.join(SRC_DIR, "hw_sim.c"),
        os.path.join(SRC_DIR, "driver.c"),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        print("[ERROR] Compilation failed:\n", proc.stderr)
        sys.exit(1)
    return so_path


def bind_c_functions(lib):
    # Hardware simulator peek & control
    lib.hw_sim_reset.restype = None
    lib.hw_sim_peek.restype = ctypes.c_uint32
    lib.hw_sim_peek.argtypes = [ctypes.c_uint32]
    lib.hw_sim_force_next_error.restype = None

    # Bitfield API
    lib.get_field.restype = ctypes.c_uint32
    lib.get_field.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32]
    lib.set_field.restype = ctypes.c_uint32
    lib.set_field.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32]

    # SPX-100 Driver API
    lib.spx_init.restype = None
    lib.spx_init.argtypes = [ctypes.c_uint8]
    lib.spx_send_byte.restype = ctypes.c_int
    lib.spx_send_byte.argtypes = [ctypes.c_uint8]
    lib.spx_is_busy.restype = ctypes.c_bool
    lib.spx_is_enabled.restype = ctypes.c_bool

    # Atomic Counter API
    lib.counter_isr_increment.restype = None
    lib.counter_read.restype = ctypes.c_uint32

    # SPSC Ring Buffer API
    lib.rb_push.restype = ctypes.c_int
    lib.rb_push.argtypes = [ctypes.c_uint8]
    lib.rb_pop.restype = ctypes.c_int
    lib.rb_pop.argtypes = [ctypes.POINTER(ctypes.c_uint8)]


def assert_test(name, condition):
    status = "PASS" if condition else "FAIL"
    print(f"[{status}] {name}")
    if not condition:
        sys.exit(1)


def main():
    print("=" * 60)
    print("  Embedded C Driver & Concurrency Test Suite")
    print("=" * 60)
    print("Compiling shared library with GCC...")
    so_path = build_shared_library()
    lib = ctypes.CDLL(so_path)
    bind_c_functions(lib)
    print("Build successful.\n")

    # ---- 1. Bitfield Helper Tests ----
    print("--- 1. Bitfield Manipulation ---")
    assert_test("get_field middle extraction", lib.get_field(0b101100, 2, 3) == 0b011)
    assert_test("get_field upper nibble", lib.get_field(0xF0, 4, 4) == 0xF)
    assert_test("get_field full 32-bit width", lib.get_field(0xDEADBEEF, 0, 32) == 0xDEADBEEF)
    assert_test("set_field lower nibble", lib.set_field(0x00, 0, 4, 0xA) == 0x0A)
    assert_test("set_field splice in word", lib.set_field(0xFFFFFFFF, 8, 8, 0x00) == 0xFFFF00FF)
    print()

    # ---- 2. SPX-100 Driver Tests ----
    print("--- 2. SPX-100 Driver & Errata Handling ---")
    lib.hw_sim_reset()
    lib.spx_init(3)
    ctrl = lib.hw_sim_peek(0x00)
    assert_test("spx_init sets CTRL.EN bit", (ctrl & 0x1) == 0x1)
    assert_test("spx_init configures CLKDIV (bits 4:2)", ((ctrl >> 2) & 0x7) == 3)
    assert_test("spx_is_enabled returns true", lib.spx_is_enabled() == True)

    rc = lib.spx_send_byte(0xAB)
    assert_test("spx_send_byte successful transmission", rc == 0)
    assert_test("spx_is_busy returns false when idle", lib.spx_is_busy() == False)

    # Test error recovery
    lib.hw_sim_force_next_error()
    err_rc = lib.spx_send_byte(0xCD)
    assert_test("spx_send_byte returns -1 on hardware error", err_rc == -1)
    print()

    # ---- 3. Atomic Counter Tests ----
    print("--- 3. Atomic Counter ---")
    lib.hw_sim_reset()
    for _ in range(100):
        lib.counter_isr_increment()
    assert_test("counter_read matches 100 increments", lib.counter_read() == 100)
    print()

    # ---- 4. Lock-Free SPSC Ring Buffer Tests ----
    print("--- 4. Lock-Free SPSC Ring Buffer ---")
    lib.hw_sim_reset()
    out = ctypes.c_uint8(0)

    # Empty pop check
    assert_test("rb_pop returns -1 on empty buffer", lib.rb_pop(ctypes.byref(out)) == -1)

    # Fill capacity (capacity is 16, effective max elements in standard ring buffer is capacity - 1)
    pushed = 0
    for i in range(15):
        if lib.rb_push(i + 1) == 0:
            pushed += 1
    assert_test("rb_push enqueues items up to full", pushed == 15)
    assert_test("rb_push rejects when full", lib.rb_push(0xFF) == -1)

    # Dequeue and verify order
    all_matched = True
    for i in range(15):
        if lib.rb_pop(ctypes.byref(out)) != 0 or out.value != (i + 1):
            all_matched = False
            break
    assert_test("rb_pop verifies FIFO ordering", all_matched)
    assert_test("rb_pop returns empty after drain", lib.rb_pop(ctypes.byref(out)) == -1)
    print()

    print("=" * 60)
    print("  ALL MODULE TESTS PASSED (100%)")
    print("=" * 60)


if __name__ == "__main__":
    main()
