# NuPhy Air96 V2 - Keyboard Freeze Analysis

## Symptom

After 5-10 minutes of use on Bluetooth (and possibly 2.4GHz RF), the keyboard
completely locks up. No keys register, no layer/connection changes work, and the
LEDs freeze in place (they do not turn off). A power cycle via the physical
switch is required to recover. The issue has not been observed on wired USB.

---

## Root Cause: RF Sync Loss Cascade

**File:** `rf.c:457-527` (`dev_sts_sync()`)

Every 200ms, the firmware sends `CMD_RF_STS_SYSC` to the nRF RF module over
UART and increments a `sync_lost` counter. The counter is only cleared when a
valid UART response is parsed (`rf.c:195`). If 5 consecutive syncs go
unanswered (~1 second), the firmware triggers a hard reset of the RF module:

```c
// rf.c:466-473
if (f_rf_reset) {
    f_rf_reset = 0;
    wait_ms(100);
    writePinLow(NRF_RESET_PIN);
    wait_ms(50);
    writePinHigh(NRF_RESET_PIN);
    wait_ms(50);
}
```

This is **200ms of hard blocking** inside `housekeeping_task_kb()`. During this
time the entire keyboard is frozen -- no matrix scan, no key processing, no LED
updates. After the reset, the nRF module needs time to reinitialize. Meanwhile,
further sync commands fail, `sync_lost` accumulates again, and the reset fires
again. This creates a **cascading reset loop** that effectively locks the
keyboard.

The `sync_lost` counter resets to 0 inside the reset branch (`rf.c:523`), but
the 200ms blocking delay means the next sync attempt at 200ms later still won't
get a response from the still-reinitializing module, so `sync_lost` climbs back
to 5 within ~1 second and the cycle repeats indefinitely.

### Why LEDs freeze instead of turning off

The sleep logic (`sleep.c:98-102`) only triggers LED power-off when
`rf_state == RF_CONNECT` and `no_act_time >= SLEEP_TIME_DELAY`. During the
reset cascade, `rf_state` oscillates between `RF_DISCONNECT` and other
transient states, so the sleep handler's LED-off path never executes cleanly.
The LEDs remain in whatever state they were last set to.

---

## Contributing Factors

### 1. `uart_receive_pro()` has no iteration bound

**File:** `rf.c:607-633`

```c
while (uart_available()) {
    // ... read bytes ...
    if (!uart_available()) {
        wait_us(200);  // then re-check
    }
}
```

No maximum iteration count. If the UART buffer fills with noise or corrupted
data (which can happen after an RF module reset at 460800 baud), this loop runs
until fully drained, potentially for many milliseconds, further stalling the
main loop.

### 2. I2C and UART interrupt contention

- I2C runs at 1MHz **without DMA** (`mcuconf.h`: `STM32_I2C_USE_DMA FALSE`)
- UART runs at 460800 baud
- Both rely on ChibiOS interrupts on the same MCU
- `ISSI_TIMEOUT` is set to just **1ms** (in `is31fl3763.c`)

Every RGB matrix frame requires ~108 I2C transactions across 2 IS31FL3763 LED
drivers. If a UART interrupt delays an I2C interrupt, the 1ms timeout fires,
ChibiOS resets the I2C bus (`i2cStop()`/`i2cStart()`), and the next frame
retries. This creates periodic I2C bus disruptions that can degrade system
timing and correlate with RF sync failures.

### 3. Sleep timer alignment with freeze window

`no_act_time` is `uint16_t`, incremented every 10ms, capped at `0xFFFF`.
`SLEEP_TIME_DELAY` is `(uint16_t)(100 * 360)` = 36000 ticks = **6 minutes**.
This aligns with the observed 5-10 minute freeze window. When sleep triggers on
wireless, it sends `CMD_SET_CONFIG` or `CMD_SLEEP` to the RF module
(`sleep.c:47-50`). If the module is already in a degraded state from sync
issues, this adds another layer of failed UART communication.

### 4. Triple-send blocking in UART_Send_Bytes

**File:** `rf.c:534-557`

When sending key reports wirelessly, `UART_Send_Bytes()` transmits each packet
**3 times** with busy-wait GPIO toggling (~3ms total blocking per report). During
this time, I2C interrupts may be delayed and incoming UART RX data may be lost,
contributing to sync failures.

---

## Diagnostic Steps

### 1. Confirm USB is unaffected

If the freeze **never** happens on wired USB, the RF sync cascade is confirmed
as the root cause.

### 2. Disable sleep mode and test

Press `Fn + Sleep` to toggle sleep mode off. If the freeze stops occurring, the
sleep handler's interaction with the RF module is the trigger point. The
`SLEEP_TIME_DELAY` of 6 minutes fits the observed timeline.

### 3. Add a cooldown to prevent RF reset cascade

In `dev_sts_sync()` (`rf.c`), add a minimum interval between RF resets to break
the cascade:

```c
static uint32_t last_reset_time = 0;
if (f_rf_reset) {
    if (timer_elapsed32(last_reset_time) < 5000) {
        f_rf_reset = 0;  // skip reset, too soon after last one
    } else {
        f_rf_reset = 0;
        last_reset_time = timer_read32();
        wait_ms(100);
        writePinLow(NRF_RESET_PIN);
        wait_ms(50);
        writePinHigh(NRF_RESET_PIN);
        wait_ms(50);
    }
}
```

### 4. Add an iteration limit to uart_receive_pro()

In `rf.c`, bound the receive loop to prevent unbounded stalls:

```c
uint8_t max_reads = UART_MAX_LEN;
while (uart_available() && max_reads--) {
    // ... existing read logic ...
}
```

### 5. Increase ISSI_TIMEOUT

Change `ISSI_TIMEOUT` from `1` to `10` (or higher) to reduce spurious I2C bus
resets caused by interrupt contention with the UART driver.

---

## Key File References

| File | Purpose |
|------|---------|
| `ansi/rf.c:457-527` | `dev_sts_sync()` -- RF sync + reset cascade |
| `ansi/rf.c:607-633` | `uart_receive_pro()` -- unbounded UART read loop |
| `ansi/rf.c:534-557` | `UART_Send_Bytes()` -- 3x repeat blocking send |
| `ansi/rf.c:190-291` | `RF_Protocol_Receive()` -- sync_lost reset at line 195 |
| `ansi/ansi.c:746-763` | `housekeeping_task_kb()` -- main loop calling all subsystems |
| `ansi/ansi.c:657-684` | `timer_pro()` -- 10ms tick, no_act_time increment |
| `ansi/sleep.c:34-113` | `Sleep_Handle()` -- sleep triggers at 6 min inactivity |
| `ansi/config.h` | Pin definitions, I2C/UART config |
| `ansi/mcuconf.h` | MCU config -- I2C DMA disabled |
