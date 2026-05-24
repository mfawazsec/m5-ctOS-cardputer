/**
 * boot_banner.h — ctOS splash screen (display, no heap)
 */
#pragma once

/** Render the ctOS boot banner on M5.Display and block briefly.
 *  Call immediately after M5.begin() / ui_menu_init().
 *  All string data lives in flash (.rodata); zero heap allocation. */
void boot_banner_show(void);
