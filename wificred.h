/*
 * PicoWifi: Raspberry Pi Pico W Wifi to USB bridge
 *
 * Copyright (c) 2022 Christian Zietz <czietz@gmx.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef WIFICRED_H_
#define WIFICRED_H_

#include <stdint.h>

#define WIFI_CRED_MAGIC 0x55AAAA55ul

typedef struct {
    uint32_t magic;
    char wifi_ssid[64];
    char wifi_passwd[64];
    uint32_t wifi_auth;
} wifi_cred_s;

#endif
