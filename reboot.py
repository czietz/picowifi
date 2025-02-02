#!/usr/bin/env python3
#
# PicoWifi: Raspberry Pi Pico W Wifi to USB bridge
#
# Helper script to reboot PicoWifi into bootloader mode.
# An alternative to plugging in the Pi Pico pressing the BOOTSEL button.
#
# Copyright (c) 2025 Christian Zietz <czietz@gmx.net>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#

import usb.core
import usb.util

# find PicoWifi
dev = usb.core.find(idVendor=0x20a0, idProduct=0x42ec)

# was it found?
if dev is None:
    raise ValueError('Device not found')

# set the active configuration. With no arguments, the first
# configuration will be the active one
dev.set_configuration()

# send reboot request
VENDOR_REQUEST_WIFI = 2
FIRMWARE_UPDATE = 0x100

dev.ctrl_transfer(0x40, VENDOR_REQUEST_WIFI, 0, FIRMWARE_UPDATE, None)
