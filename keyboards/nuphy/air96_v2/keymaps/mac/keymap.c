#include QMK_KEYBOARD_H
#include "../ansi/ansi.h"

// External declarations for RF state (from ansi.h)
extern DEV_INFO_STRUCT dev_info;

enum layers {
    _WINBASE, // Layer 0: Windows base
    _MACBASE, // Layer 1: Mac base (swapped Ctrl/Opt/Cmd)
    _MOD      // Layer 2: Custom mod layer
};

// ============================================================================
// SINGLE LED CACHE - 10 elements: [R, G, B] for each LED
// Index 0-1: Layer LEDs (100, 101)
// Index 2-4: Connection LEDs (102, 103, 104)
// Index 5-9: Battery LEDs (105, 106, 107, 108, 109)
// ============================================================================

static uint8_t led_cache[10][3] = {
    {0xFF, 0xFF, 0xFF}, // 0: LED 100 - Layer (White default Mac)
    {0xFF, 0xFF, 0xFF}, // 1: LED 101 - Layer (White default Mac)
    {0x00, 0x00, 0x00}, // 2: LED 102 - Connection (Off)
    {0x00, 0x00, 0x00}, // 3: LED 103 - Connection (Off)
    {0x00, 0x00, 0x00}, // 4: LED 104 - Connection (Off)
    {0x00, 0x00, 0x00}, // 5: LED 105 - Battery (Off)
    {0x00, 0x00, 0x00}, // 6: LED 106 - Battery (Off)
    {0x00, 0x00, 0x00}, // 7: LED 107 - Battery (Off)
    {0x00, 0x00, 0x00}, // 8: LED 108 - Battery (Off)
    {0xFF, 0x00, 0x00}  // 9: LED 109 - Battery (Red default)
};

// Previous state tracking for event detection
static uint8_t default_layer  = _MACBASE; // Tracks which base layer is default (WIN or MAC)
static uint8_t last_link_mode = 0xFF;
static uint8_t last_rf_state  = 0xFF;

// Timer for battery polling
static uint32_t battery_timer = 0;

// Connection LED animation state
static uint32_t blink_timer         = 0;
static uint8_t  chase_index         = 0;   // 0, 1, 2 for which LED is lit in chase
static uint8_t  chase_pattern       = 0;   // 0=off, 1=fast, 2=slow
static uint8_t  connection_color[3] = {0}; // RGB color for chase animation

// Tap for Escape, Hold for Layer 2
#define ESC_MOD LT(_MOD, KC_ESC)

// ============================================================================
// FRAME LOOP - Apply all cached LED colors every frame
// ============================================================================

// ============================================================================
// CACHE UPDATE FUNCTIONS
// ============================================================================

static void update_layer_cache(bool in_mod) {
    if (in_mod) {
        // MOD layer is active - show red
        led_cache[0][0] = 0xFF;
        led_cache[0][1] = 0x00;
        led_cache[0][2] = 0x00; // Red
        led_cache[1][0] = 0xFF;
        led_cache[1][1] = 0x00;
        led_cache[1][2] = 0x00; // Red
    } else {
        // MOD not active - show color based on default layer
        if (default_layer == _WINBASE) {
            led_cache[0][0] = 0x00;
            led_cache[0][1] = 0x00;
            led_cache[0][2] = 0xFF; // Blue
            led_cache[1][0] = 0x00;
            led_cache[1][1] = 0x00;
            led_cache[1][2] = 0xFF; // Blue
        } else {
            // _MACBASE or anything else
            led_cache[0][0] = 0xFF;
            led_cache[0][1] = 0xFF;
            led_cache[0][2] = 0xFF; // White
            led_cache[1][0] = 0xFF;
            led_cache[1][1] = 0xFF;
            led_cache[1][2] = 0xFF; // White
        }
    }
}

static void update_connection_animation(void) {
    // Chase animation: light up one LED at a time based on chase_index
    // LEDs are indices 2, 3, 4 (corresponding to LEDs 102, 103, 104)
    uint8_t led_offsets[3] = {2, 3, 4}; // Index into led_cache

    // Clear all connection LEDs first
    led_cache[2][0] = 0x00;
    led_cache[2][1] = 0x00;
    led_cache[2][2] = 0x00;
    led_cache[3][0] = 0x00;
    led_cache[3][1] = 0x00;
    led_cache[3][2] = 0x00;
    led_cache[4][0] = 0x00;
    led_cache[4][1] = 0x00;
    led_cache[4][2] = 0x00;

    // Light up current chase LED with the chase color (blue for BT pairing/connecting)
    if (chase_pattern > 0) {
        uint8_t idx       = led_offsets[chase_index % 3];
        led_cache[idx][0] = connection_color[0];
        led_cache[idx][1] = connection_color[1];
        led_cache[idx][2] = connection_color[2];
    }
}

static void set_connection_leds(uint8_t r, uint8_t g, uint8_t b) {
    led_cache[2][0] = r;
    led_cache[2][1] = g;
    led_cache[2][2] = b;
    led_cache[3][0] = r;
    led_cache[3][1] = g;
    led_cache[3][2] = b;
    led_cache[4][0] = r;
    led_cache[4][1] = g;
    led_cache[4][2] = b;
}

static void update_connection_cache(void) {
    // Always read fresh values from dev_info
    uint8_t rf_state  = dev_info.rf_state;
    uint8_t link_mode = dev_info.link_mode;

    // USB is special - it uses RF_IDLE but should show as connected
    if (link_mode == LINK_USB) {
        chase_pattern = 0; // No animation for USB
        // USB: All 3 steady yellow
        set_connection_leds(0xFF, 0xFF, 0x00);
        return;
    }

    // Check if BT is in pairing or connecting state - show chase animation
    // with the respective target color
    if (link_mode >= LINK_BT_1 && link_mode <= LINK_BT_3) {
        // Set chase color based on which BT device we're connecting to
        switch (link_mode) {
            case LINK_BT_1:
                connection_color[0] = 0x00; // Blue
                connection_color[1] = 0x00;
                connection_color[2] = 0xFF;
                break;
            case LINK_BT_2:
                connection_color[0] = 0xFF; // White
                connection_color[1] = 0xFF;
                connection_color[2] = 0xFF;
                break;
            case LINK_BT_3:
                connection_color[0] = 0x00; // Green
                connection_color[1] = 0xFF;
                connection_color[2] = 0x00;
                break;
        }

        if (rf_state == RF_PAIRING) {
            // BT Pairing: Fast chase with target color
            chase_pattern = 1; // Fast
            chase_index   = 0;
            blink_timer   = timer_read32();
            update_connection_animation();
            return;
        } else if (rf_state == RF_LINKING) {
            // BT Connecting: Slow chase with target color
            chase_pattern = 2; // Slow
            chase_index   = 0;
            blink_timer   = timer_read32();
            update_connection_animation();
            return;
        }
    }

    // Not in pairing/linking state - disable animation
    chase_pattern = 0;

    // Steady state - use link_mode directly (no dependency on rf_state)
    // This fixes timing issues with fast connections
    switch (link_mode) {
        case LINK_BT_1:
            // BT1: All 3 steady blue + auto-switch to Windows
            set_connection_leds(0x00, 0x00, 0xFF);
            if (default_layer != _WINBASE) {
                set_single_persistent_default_layer(_WINBASE);
                default_layer = _WINBASE;
                update_layer_cache(false);
            }
            break;
        case LINK_BT_2:
            // BT2: All 3 steady white + auto-switch to Mac
            set_connection_leds(0xFF, 0xFF, 0xFF);
            if (default_layer != _MACBASE) {
                set_single_persistent_default_layer(_MACBASE);
                default_layer = _MACBASE;
                update_layer_cache(false);
            }
            break;
        case LINK_BT_3:
            // BT3: All 3 steady green, no layer change
            set_connection_leds(0x00, 0xFF, 0x00);
            break;
        case LINK_RF_24:
            // RF: All 3 steady red, no layer change
            set_connection_leds(0xFF, 0x00, 0x00);
            break;
        default:
            // Unknown state - LEDs off
            set_connection_leds(0x00, 0x00, 0x00);
            break;
    }
}

static void update_battery_cache(void) {
    uint8_t battery = dev_info.rf_baterry;

    if (battery < 20) {
        // Low battery: Only bottom LED (105) lit red
        led_cache[5][0] = 0xFF;
        led_cache[5][1] = 0x00;
        led_cache[5][2] = 0x00; // Red
        led_cache[6][0] = 0x00;
        led_cache[6][1] = 0x00;
        led_cache[6][2] = 0x00; // Off
        led_cache[7][0] = 0x00;
        led_cache[7][1] = 0x00;
        led_cache[7][2] = 0x00; // Off
        led_cache[8][0] = 0x00;
        led_cache[8][1] = 0x00;
        led_cache[8][2] = 0x00; // Off
        led_cache[9][0] = 0x00;
        led_cache[9][1] = 0x00;
        led_cache[9][2] = 0x00; // Off
    } else {
        // Normal battery display
        // LED 105 (index 5): Green if >80%
        if (battery > 80) {
            led_cache[5][0] = 0x00;
            led_cache[5][1] = 0xFF;
            led_cache[5][2] = 0x00; // Green
        } else {
            led_cache[5][0] = 0x00;
            led_cache[5][1] = 0x00;
            led_cache[5][2] = 0x00; // Off
        }

        // LED 106 (index 6): Yellow if >60%
        if (battery > 60) {
            led_cache[6][0] = 0xFF;
            led_cache[6][1] = 0xFF;
            led_cache[6][2] = 0x00; // Yellow
        } else {
            led_cache[6][0] = 0x00;
            led_cache[6][1] = 0x00;
            led_cache[6][2] = 0x00; // Off
        }

        // LED 107 (index 7): Yellow if >40%
        if (battery > 40) {
            led_cache[7][0] = 0xFF;
            led_cache[7][1] = 0xFF;
            led_cache[7][2] = 0x00; // Yellow
        } else {
            led_cache[7][0] = 0x00;
            led_cache[7][1] = 0x00;
            led_cache[7][2] = 0x00; // Off
        }

        // LED 108 (index 8): Red if >20%
        if (battery > 20) {
            led_cache[8][0] = 0xFF;
            led_cache[8][1] = 0x00;
            led_cache[8][2] = 0x00; // Red
        } else {
            led_cache[8][0] = 0x00;
            led_cache[8][1] = 0x00;
            led_cache[8][2] = 0x00; // Off
        }

        // LED 109 (index 9): Always red
        led_cache[9][0] = 0xFF;
        led_cache[9][1] = 0x00;
        led_cache[9][2] = 0x00; // Red
    }
}

// ============================================================================
// FRAME LOOP - Just applies cached colors (NO logic here)
// ============================================================================

bool rgb_matrix_indicators_user(void) {
    // Just apply all 10 cached LED colors every frame - no logic, no decisions
    for (int i = 0; i < 10; i++) {
        rgb_matrix_set_color(100 + i, led_cache[i][0], led_cache[i][1], led_cache[i][2]);
    }
    return true;
}

// ============================================================================
// EVENT-DRIVEN CALLBACKS - These update the cache
// ============================================================================

// Called when active layer changes (QMK callback)
layer_state_t layer_state_set_user(layer_state_t state) {
    // Check if MOD layer (_MOD = 2) is currently active
    bool in_mod = layer_state_cmp(state, _MOD);
    update_layer_cache(in_mod);
    return state;
}

// Called when default layer changes (QMK callback)
layer_state_t default_layer_state_set_user(layer_state_t state) {
    default_layer = get_highest_layer(state);
    // Default layer change also clears active layers, so MOD won't be active
    update_layer_cache(false);
    return state;
}

// Called when connection mode changes (USB/BT/RF) - Hooked from switch_dev_link()
void led_mode_changed_callback(uint8_t mode) {
    if (mode != last_link_mode) {
        last_link_mode = mode;
        update_connection_cache();
    }
}

// Called when RF state changes (Pairing/Linking/Connected) - Hooked from RF_Protocol_Receive()
void led_rf_status_callback(uint8_t rf_state) {
    // Always update - don't check for state change
    // When switching BT devices, rf_state may stay as RF_CONNECT but link_mode changes
    last_rf_state = rf_state;
    update_connection_cache();
}

// ============================================================================
// INITIALIZATION & POLLING
// ============================================================================

void keyboard_post_init_user(void) {
    // Set default layer to Mac
    set_single_persistent_default_layer(_MACBASE);

    // Initialize LEDs immediately
    last_link_mode = dev_info.link_mode;
    last_rf_state  = dev_info.rf_state;
    update_layer_cache(false); // MOD not active on startup
    update_connection_cache();
    update_battery_cache();

    // Initialize battery timer
    battery_timer = timer_read32();
}

// Polling for battery and connection LED animation
void housekeeping_task_user(void) {
    // Update battery every 30 seconds
    if (timer_elapsed32(battery_timer) > 30000) {
        battery_timer = timer_read32();
        update_battery_cache();
    }

    // Handle connection LED chase animation
    if (chase_pattern > 0) {
        uint16_t interval = (chase_pattern == 1) ? 100 : 300; // Fast: 100ms, Slow: 300ms
        if (timer_elapsed32(blink_timer) > interval) {
            blink_timer = timer_read32();
            chase_index = (chase_index + 1) % 3;
            update_connection_animation();
        }
    }
}

// ============================================================================
// KEYMAPS
// ============================================================================

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    /* Layer 0: Windows Base
     */
    [_WINBASE] = LAYOUT(KC_ESC, KC_F1, KC_F2, KC_F3, KC_F4, KC_F5, KC_F6, KC_F7, KC_F8, KC_F9, KC_F10, KC_F11, KC_F12, KC_PSCR, KC_DEL, KC_HOME, KC_END, KC_PGUP, KC_PGDN, KC_GRV, KC_1, KC_2, KC_3, KC_4, KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_MINS, KC_EQL, KC_BSPC, KC_NUM, KC_PSLS, KC_PAST, KC_PMNS, KC_TAB, KC_Q, KC_W, KC_E, KC_R, KC_T, KC_Y, KC_U, KC_I, KC_O, KC_P, KC_LBRC, KC_RBRC, KC_BSLS, KC_P7, KC_P8, KC_P9, KC_PPLS, ESC_MOD, KC_A, KC_S, KC_D, KC_F, KC_G, KC_H, KC_J, KC_K, KC_L, KC_SCLN, KC_QUOT, KC_ENT, KC_P4, KC_P5, KC_P6, KC_LSFT, KC_Z, KC_X, KC_C, KC_V, KC_B, KC_N, KC_M, KC_COMM, KC_DOT, KC_SLSH, KC_RSFT, KC_UP, KC_P1, KC_P2, KC_P3, KC_PENT, KC_LCTL, KC_LGUI, KC_LALT, KC_SPC, KC_RALT, KC_RGUI, KC_RCTL, KC_LEFT, KC_DOWN, KC_RGHT, KC_P0, KC_PDOT),

    /* Layer 1: Mac Base
     * Swapped Ctrl/Opt/Cmd
     */
    [_MACBASE] = LAYOUT(KC_ESC, KC_F1, KC_F2, KC_F3, KC_F4, KC_F5, KC_F6, KC_F7, KC_F8, KC_F9, KC_F10, KC_F11, KC_F12, KC_PSCR, KC_DEL, KC_HOME, KC_END, KC_PGUP, KC_PGDN, KC_GRV, KC_1, KC_2, KC_3, KC_4, KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_MINS, KC_EQL, KC_BSPC, KC_NUM, KC_PSLS, KC_PAST, KC_PMNS, KC_TAB, KC_Q, KC_W, KC_E, KC_R, KC_T, KC_Y, KC_U, KC_I, KC_O, KC_P, KC_LBRC, KC_RBRC, KC_BSLS, KC_P7, KC_P8, KC_P9, KC_PPLS, ESC_MOD, KC_A, KC_S, KC_D, KC_F, KC_G, KC_H, KC_J, KC_K, KC_L, KC_SCLN, KC_QUOT, KC_ENT, KC_P4, KC_P5, KC_P6, KC_LSFT, KC_Z, KC_X, KC_C, KC_V, KC_B, KC_N, KC_M, KC_COMM, KC_DOT, KC_SLSH, KC_RSFT, KC_UP, KC_P1, KC_P2, KC_P3, KC_PENT, KC_LCTL, KC_LALT, KC_LGUI, KC_SPC, KC_RGUI, KC_RALT, KC_RCTL, KC_LEFT, KC_DOWN, KC_RGHT, KC_P0, KC_PDOT),

    /* Layer 2: Mod Layer
     * hjkl -> arrows, q -> `, 1/2/3 -> BT, 4 -> RF, space -> Caps toggle
     * F1 -> Windows base, F2 -> Mac base
     */
    [_MOD] = LAYOUT(_______, DF(_WINBASE), DF(_MACBASE), _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, LNK_BLE1, LNK_BLE2, LNK_BLE3, LNK_RF, _______, _______, _______, _______, _______, _______, _______, _______, KC_DEL, _______, _______, _______, _______, _______, KC_GRV, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, KC_LEFT, KC_DOWN, KC_UP, KC_RGHT, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, KC_CAPS, _______, _______, _______, _______, _______, _______, _______, _______)};

// LED configuration for RGB matrix
const is31_led PROGMEM g_is31_leds[RGB_MATRIX_LED_COUNT] = {{0, A_16, B_16, C_16}, {0, A_15, B_15, C_15}, {0, A_14, B_14, C_14}, {0, A_13, B_13, C_13}, {0, A_12, B_12, C_12}, {0, D_16, E_16, F_16}, {0, D_15, E_15, F_15}, {0, D_14, E_14, F_14}, {0, D_13, E_13, F_13}, {0, D_12, E_12, F_12}, {1, D_16, E_16, F_16}, {1, D_15, E_15, F_15}, {1, D_14, E_14, F_14}, {1, D_13, E_13, F_13}, {1, G_4, H_4, I_4}, {1, D_5, E_5, F_5}, {1, G_5, H_5, I_5}, {1, D_6, E_6, F_6}, {1, G_6, H_6, I_6}, {0, A_1, B_1, C_1}, {0, A_2, B_2, C_2},    {0, A_3, B_3, C_3},    {0, A_4, B_4, C_4},    {0, A_5, B_5, C_5},    {0, A_6, B_6, C_6},    {0, A_7, B_7, C_7},    {0, A_8, B_8, C_8},    {0, A_9, B_9, C_9},    {0, A_10, B_10, C_10}, {0, A_11, B_11, C_11}, {1, D_1, E_1, F_1},    {1, D_2, E_2, F_2},    {1, D_3, E_3, F_3},    {1, D_7, E_7, F_7},
                                                            {1, D_8, E_8, F_8},    {1, D_9, E_9, F_9},    {1, D_10, E_10, F_10}, {0, D_1, E_1, F_1},    {0, D_2, E_2, F_2},    {0, D_3, E_3, F_3},    {0, D_4, E_4, F_4},    {0, D_5, E_5, F_5},    {0, D_6, E_6, F_6},    {0, D_7, E_7, F_7},    {0, D_8, E_8, F_8},    {0, D_9, E_9, F_9},    {0, D_10, E_10, F_10}, {0, D_11, E_11, F_11}, {1, G_1, H_1, I_1}, {1, G_2, H_2, I_2}, {1, G_3, H_3, I_3}, {1, G_7, H_7, I_7}, {1, G_8, H_8, I_8}, {1, G_9, H_9, I_9}, {1, G_10, H_10, I_10}, {0, G_1, H_1, I_1},    {0, G_2, H_2, I_2},    {0, G_3, H_3, I_3},    {0, G_4, H_4, I_4},    {0, G_5, H_5, I_5},    {0, G_6, H_6, I_6},    {0, G_7, H_7, I_7},    {0, G_8, H_8, I_8},    {0, G_9, H_9, I_9},    {0, G_10, H_10, I_10}, {0, G_11, H_11, I_11}, {1, G_16, H_16, I_16}, {1, G_14, H_14, I_14},
                                                            {1, G_13, H_13, I_13}, {1, G_12, H_12, I_12}, {1, G_11, H_11, I_11}, {0, J_1, K_1, L_1},    {0, J_3, K_3, L_3},    {0, J_4, K_4, L_4},    {0, J_5, K_5, L_5},    {0, J_6, K_6, L_6},    {0, J_7, K_7, L_7},    {0, J_8, K_8, L_8},    {0, J_9, K_9, L_9},    {0, J_10, K_10, L_10}, {0, J_11, K_11, L_11}, {1, J_1, K_1, L_1},    {1, J_3, K_3, L_3}, {1, J_4, K_4, L_4}, {1, J_5, K_5, L_5}, {1, J_6, K_6, L_6}, {1, J_7, K_7, L_7}, {1, J_8, K_8, L_8}, {0, J_16, K_16, L_16}, {0, J_15, K_15, L_15}, {0, J_14, K_14, L_14}, {0, J_13, K_13, L_13}, {0, J_12, K_12, L_12}, {1, J_16, K_16, L_16}, {1, J_14, K_14, L_14}, {1, J_13, K_13, L_13}, {1, J_12, K_12, L_12}, {1, J_11, K_11, L_11}, {1, J_10, K_10, L_10}, {1, J_9, K_9, L_9},

                                                            {1, A_5, B_5, C_5},    {1, A_4, B_4, C_4},    {1, A_3, B_3, C_3},    {1, A_2, B_2, C_2},    {1, A_1, B_1, C_1},

                                                            {1, A_6, B_6, C_6},    {1, A_7, B_7, C_7},    {1, A_8, B_8, C_8},    {1, A_9, B_9, C_9},    {1, A_10, B_10, C_10}};
