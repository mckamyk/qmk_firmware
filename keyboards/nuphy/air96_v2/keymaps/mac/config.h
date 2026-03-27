#pragma once

// 200ms timeout for tap vs hold (matches your Karabiner)
#define TAPPING_TERM 200

// Instant layer activation when pressing another key
#define HOLD_ON_OTHER_KEY_PRESS

// Helps with rolling keys
#define PERMISSIVE_HOLD

// Increase I2C timeout from 1ms to 10ms to prevent LED flickering during UART/RF interrupts
// #undef ISSI_TIMEOUT
// #define ISSI_TIMEOUT 10
