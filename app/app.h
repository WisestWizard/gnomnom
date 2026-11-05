/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann
 * Copyright (C) 2026 Charles Vestal (fm1-base's template, which this started from) */
/* GNOMNOM: the app's half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "GNOMNOM"
#define APP_SPLASH_RGB RGB(255, 96, 64)
#define APP_PALETTE 3u                   /* red: the platform's own screens (crash, update) */
#define APP_BIG_FONT FONT_M              /* the panel's screens in the medium face: FONT_L's 20 KB stay out */
#define APP_SPLASH                       /* the boot screen is ours (ui.c app_splash: the logo through gfx) */
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_CANVAS_PX (240u * 32u)       /* gfx.c's own strips carry the screen; the platform's canvas only draws its
                                          * one-line screens (crash, update, safe mode) */
#define FM1B_UAC_DEEP 1                  /* the USB audio input keeps a half more in hand: the render runs past 40 %
                                          * at the late stages and a recording must not crackle */
#define APP_BTDEV                        /* Bluetooth MIDI devices (lib/btdev.h) */
#define APP_BLE_AT_BOOT (!gn_ble_off())  /* Bluetooth at power-on: as the player left it (REC held) */
#define APP_OVERLOAD                     /* the render over 90 % of its time: the UI shows CPU */
int gn_ble_off(void);
void app_overload(void);

/* the store: the settings (high score, furthest stage, hat, JAM, flash, Bluetooth), the panel calibration (the
 * platform's), Bluetooth's address and pairing; each an A/B pair of 4 KiB sectors (0xFC000 is Felucca's settings
 * pair) */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}

/* the contract: the platform calls these */
void app_init(void);                     /* boot: the band, plat_cpu2_set(1) (the one place the second core may
                                          * start), the composer, the game, then the saved settings (or defaults) */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: midi_in(); game_tick(n);
                                                                  * band_render(lr, n, master_q12); nothing else */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
void app_ui_init(void);                  /* the first screen, after the splash */
void app_frame(void);                    /* the UI: input and a redraw, about 60 times a second */
void app_input_only(void);               /* between frames: input only */
void app_say(const char *a, const char *b);   /* a short message on the screen and to a screen reader */

/* the host simulator's checks and commands (host/plat_host.c): docs/DESIGN.md section 6 */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
