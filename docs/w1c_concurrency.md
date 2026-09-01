# Concurrency & Hardware Register Architecture

This document details the low-level synchronization models and hardware register access semantics used across this project.

---

## 1. Write-1-to-Clear (W1C) Status Registers

In embedded hardware peripherals, status registers frequently employ **Write-1-to-Clear (W1C)** semantics for interrupt and completion flags.

### The Race Condition with Read-Modify-Write (RMW)
A classic firmware anti-pattern is using Read-Modify-Write to clear status bits:

```c
/* BUGGY: Vulnerable to lost hardware events */
uint32_t status = reg_read(STATUS_OFFSET);
status &= ~(1u << DONE_BIT); // Trying to clear DONE
reg_write(STATUS_OFFSET, status);
```

If another hardware event (such as `ERR`) occurs between the `reg_read()` and `reg_write()`, the write-back will inadvertently clear or corrupt pending flags that occurred in that window.

### The Isolated W1C Solution
With W1C hardware:
- Writing `1` clears that specific bit.
- Writing `0` has **no effect** and leaves the bit untouched.

Therefore, different execution contexts (e.g., an ISR handling `DONE` and a main background task handling `ERR`) can clear their owned flags with isolated single-bit writes:

```c
/* ISR Context: Safely clears only DONE */
reg_write(SPX_STATUS_OFFSET, (1u << SPX_STATUS_DONE_BIT));

/* Main Loop Context: Safely clears only ERR */
reg_write(SPX_STATUS_OFFSET, (1u << SPX_STATUS_ERR_BIT));
```

Because each write only sets its target bit, the hardware leaves all other bits undisturbed, allowing concurrent flag management without mutexes or critical sections.

---

## 2. Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer

The circular ring buffer enables asynchronous data transfer between an ISR producer and a main loop consumer without disabling interrupts.

### Memory Ordering Semantics

- **Producer (`rb_push`)**:
  1. Loads `rb_tail` using `__ATOMIC_ACQUIRE` to ensure up-to-date visibility of the consumer’s state.
  2. Writes data payload into `rb_buf[head]`.
  3. Updates `rb_head` using `__ATOMIC_RELEASE`. The release barrier guarantees that the payload write is fully committed to memory before the updated head index becomes visible to the consumer.

- **Consumer (`rb_pop`)**:
  1. Loads `rb_head` using `__ATOMIC_ACQUIRE` to ensure the consumer sees the newly written data before reading from the buffer.
  2. Reads data payload from `rb_buf[tail]`.
  3. Updates `rb_tail` using `__ATOMIC_RELEASE`. The release barrier guarantees that the data read completes before the updated tail index signals the slot as free.
