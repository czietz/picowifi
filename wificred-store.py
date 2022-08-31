#!/usr/bin/env python3
#
# PicoWifi: Raspberry Pi Pico W Wifi to USB bridge
#
# Helper script to store Wifi credentials in flash ROM
#
# Copyright (c) 2022 Christian Zietz <czietz@gmx.net>
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

import struct

# get data
wifi_ssid = input("WIFI SSID: ")
wifi_pass = input("WIFI Password: ")

# build UF2
header = struct.pack("<4sLLLLLLL", b"UF2\n", 0x9E5D5157, # header
                     0x2000, 0x101F0000, 256, # flags, address, len
                     0, 1, 0xe48bff56) # block number, total blocks, RP2040 ID
_magic   = 0x55AAAA55
_wpaauth = 0x00400004
payload = struct.pack("<L64s64sL", _magic, wifi_ssid.encode("latin1"), wifi_pass.encode("latin1"), _wpaauth)
filler  = (476 - len(payload)) * b"\00"
footer  = struct.pack("<L", 0x0AB16F30) # final magic

# write UF2
try:
	with open("wificred.uf2", "wb") as uf2:
		uf2.write(header)
		uf2.write(payload)
		uf2.write(filler)
		uf2.write(footer)
except BaseException as err:
	print("Could not create 'wificred.uf2':")
	print(err)
	
