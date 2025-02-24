/*
 * PicoWifi: Raspberry Pi Pico W Wifi to USB bridge
 *
 * Copyright (c) 2022 - 2023 Christian Zietz <czietz@gmx.net>
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

#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/cyw43_arch.h"
#include "pico/util/queue.h"
#include "pico/bootrom.h"
#include "pico/mutex.h"
#include "pico/binary_info.h"

#include "hardware/vreg.h"
#include "hardware/clocks.h"

#include "bsp/board.h"
#include "tusb.h"
#include "usb_descriptors.h"

#include "wificred.h"
#include "git_rev.h"

#define DEBUG(x) // no output
//#define DEBUG(x) printf x // debug output

#define QSIZE 16
static queue_t qinbound; /* eth -> usb */
static queue_t qoutbound; /* usb -> eth */

#define MTU 1600
#define MAGIC 0xAA55AA55ul

typedef struct {
    uint32_t magic;
    size_t len;
    uint8_t payload[MTU];
} pkt_s;

// === ETHERNET CALLBACKS AND FUNCTIONS ===

#define WLC_GET_RATE                       ( (uint32_t)12 )
#define WLC_GET_RSSI                       ( (uint32_t)127 )

static volatile bool link_up = false;
static int32_t wifi_status;
static int32_t wifi_rssi, wifi_rate;
static uint8_t macaddr[6];

static absolute_time_t next_wifi_status = (absolute_time_t)0;
static volatile absolute_time_t next_wifi_try;
static char wifi_ssid[65]   = {0};
static char wifi_passwd[65] = {0};
static unsigned long wifi_auth = 0;

// pre-stored Wifi credentials at the very last 64k page of flash
#define FLASH_SIZE (2048ul*1024ul)
#define PAGE_SIZE  (64ul*1024ul)
static const wifi_cred_s* const wifi_cred = (wifi_cred_s*)(XIP_NOCACHE_NOALLOC_BASE + FLASH_SIZE - PAGE_SIZE);

// never called but linker needs it
uint16_t pbuf_copy_partial (void *buf, void *dataptr, uint16_t len, uint16_t offset)
{
}

void cyw43_cb_tcpip_set_link_up(cyw43_t *self, int itf) {
    link_up = true;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, link_up);
}

void cyw43_cb_tcpip_set_link_down(cyw43_t *self, int itf) {
    link_up = false;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, link_up);
}

void cyw43_cb_process_ethernet(void *cb_data, int itf, size_t len, const uint8_t *buf) {
    pkt_s in_pkt;

    if (len <= MTU) {
        in_pkt.magic = MAGIC;
        in_pkt.len = len;
        memcpy(in_pkt.payload, buf, len);

        if (!queue_try_add(&qinbound, &in_pkt)) {
            DEBUG(("EQueue full\n"));
        }
    } else {
        DEBUG(("Oversized pkt = %d\n", len));
    }
}

// === USB CALLBACKS AND FUNCTIONS ===
//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

static bool usb_connected = false;
char mac_as_serial[13] = "0000";

// Invoked when device is mounted
void tud_mount_cb(void)
{
  usb_connected = true;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  usb_connected = false;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;

}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{

}

// Firmware commands
enum {
    WIFI_SET_SSID = 0,
    WIFI_SET_PASSWD,
    WIFI_CONNECT,
    WIFI_STATUS,
    FIRMWARE_UPDATE = 0x100,
};

// Invoked when a control transfer occurred on an interface of this class
// Driver response accordingly to the request and the transfer stage (setup/data/ack)
// return false to stall control endpoint (e.g unsupported request)
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request)
{
  struct  {
      int32_t link_up;
      int32_t cyw43_status;
      int32_t last_rssi;
      int32_t last_rate;
  } status;

  // nothing to with DATA & ACK stage
  if (stage != CONTROL_STAGE_SETUP) return true;

  switch (request->bmRequestType_bit.type)
  {
    case TUSB_REQ_TYPE_VENDOR:
      switch (request->bRequest)
      {

        case VENDOR_REQUEST_MICROSOFT:
          if ( request->wIndex == 7 )
          {
            // Get Microsoft OS 2.0 compatible descriptor
            uint16_t total_len;
            memcpy(&total_len, desc_ms_os_20+8, 2);

            return tud_control_xfer(rhport, request, (void*) desc_ms_os_20, total_len);
          }else
          {
            return false;
          }
          break;

        case VENDOR_REQUEST_WIFI:
          switch (request->wIndex) {
              case WIFI_SET_SSID:
              memset(wifi_ssid, 0, sizeof(wifi_ssid));
              return tud_control_xfer(rhport, request, (void*) wifi_ssid, sizeof(wifi_ssid)-1);

              case WIFI_SET_PASSWD:
              memset(wifi_passwd, 0, sizeof(wifi_passwd));
              return tud_control_xfer(rhport, request, (void*) wifi_passwd, sizeof(wifi_passwd)-1);

              case WIFI_CONNECT:
              wifi_auth = (unsigned long)(request->wValue & 0xf) | ((unsigned long)(request->wValue & 0xfff0u) << 12);
              next_wifi_try = make_timeout_time_ms(10);
              return tud_control_xfer(rhport, request, NULL, 0);

              case WIFI_STATUS:
              status.link_up = link_up;
              status.cyw43_status = wifi_status;
              status.last_rssi = wifi_rssi;
              status.last_rate = wifi_rate;
              return tud_control_xfer(rhport, request, (void*) &status, sizeof(status));

              case FIRMWARE_UPDATE:
              tud_control_xfer(rhport, request, NULL, 0);
              reset_usb_boot(0,0);
              break; // not reached

              default: break;
          }

          break;

        default: break;
      }
    break;

    default: break;
  }

  // stall unknown request
  return false;
}

void usb_rx(void)
{
    static pkt_s out_pkt;
    static int pktoffs = -1;

    if (tud_vendor_available()) {

        if (pktoffs < 0) { // no on-going packet
            tud_vendor_read(&out_pkt, offsetof(pkt_s, payload)); // read header
            if ((out_pkt.magic == MAGIC) && (out_pkt.len <= MTU)) { // valid packet
                pktoffs = tud_vendor_read(out_pkt.payload, out_pkt.len);
            } else {
                DEBUG(("Invalid packet: magic = %x, len = %d\n", out_pkt.magic, out_pkt.len));
            }
        } else { // pkt_ongoing
            pktoffs += tud_vendor_read(&out_pkt.payload[pktoffs], out_pkt.len - pktoffs);
        }

        if (pktoffs >= out_pkt.len) { // completed packet
            if (!queue_try_add(&qoutbound, &out_pkt)) {
                DEBUG(("UQueue full\n"));
            }
            pktoffs = -1;
        }
    }

}

mutex_t usb_ready;

// === Core 1 main: mainly USB ===

void core1_entry(void)
{
    static pkt_s temp_pkt; // to save stack size

    // Init USB
    board_init();
    tusb_init();
    mutex_enter_blocking(&usb_ready);

    while (1)
    {
        tud_task();

        usb_rx();

        while (queue_try_peek(&qinbound, &temp_pkt)) {
            DEBUG(("Recv pkt: len = %d, src = ", temp_pkt.len));
            for (int k=6;k<12;k++) {
                DEBUG(("%02X:", temp_pkt.payload[k]));
            }
            DEBUG(("\n"));


            if (usb_connected && (tud_vendor_write_available() >= (temp_pkt.len + offsetof(pkt_s, payload)))) {
                queue_remove_blocking(&qinbound, NULL); // data is already in temp_pkt
                tud_vendor_write(&temp_pkt, temp_pkt.len + offsetof(pkt_s, payload));
            } else {
                break;
            }
        }
        tud_vendor_flush();
    }
}

// === Core 0 main: Ethernet and housekeeping ===

int main() {
    int res;
    pkt_s temp_pkt;

    // binary info for picotool
    bi_decl(bi_program_name("PicoWifi"));
    bi_decl(bi_program_version_string(GIT_REVISION));

    stdio_init_all();

    set_sys_clock_khz(200000,1);

    queue_init(&qinbound, sizeof(pkt_s), QSIZE);
    queue_init(&qoutbound, sizeof(pkt_s), QSIZE);
    mutex_init(&usb_ready);
    mutex_enter_blocking(&usb_ready);

    // launch USB
    multicore_launch_core1(core1_entry);

    // Init Ethernet
    cyw43_arch_init();
    cyw43_arch_enable_sta_mode();

    // get our MAC address
    cyw43_hal_get_mac(0, macaddr);
    sprintf(mac_as_serial, "%02x%02x%02x%02x%02x%02x", macaddr[0],macaddr[1],macaddr[2],
                                        macaddr[3],macaddr[4],macaddr[5]);
    mutex_exit(&usb_ready);

    cyw43_wifi_pm(&cyw43_state, CYW43_PERFORMANCE_PM);

    if (wifi_cred->magic == WIFI_CRED_MAGIC) {
        // immediately try to connect if credentials are found in flash
        strlcpy(wifi_ssid, wifi_cred->wifi_ssid, sizeof(wifi_ssid));
        strlcpy(wifi_passwd, wifi_cred->wifi_passwd, sizeof(wifi_passwd));
        wifi_auth = wifi_cred->wifi_auth;
        next_wifi_try = make_timeout_time_ms(10);
    } else {
        // wait for setup before trying to connect
        next_wifi_try = at_the_end_of_time;
    }


    while(true) {

        if (!link_up) {
            if (absolute_time_diff_us(get_absolute_time(), next_wifi_try) < 0) {
                DEBUG(("Retrying wifi connection %s/%s/%lx\n", wifi_ssid, wifi_passwd, wifi_auth));
                cyw43_arch_wifi_connect_async(wifi_ssid, wifi_passwd, wifi_auth);
                next_wifi_try = make_timeout_time_ms(10000);
            }
        }

        if (absolute_time_diff_us(get_absolute_time(), next_wifi_status) < 0) {
            wifi_status = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
            cyw43_ioctl(&cyw43_state, (uint32_t)WLC_GET_RSSI<<1, sizeof(wifi_rssi), (uint8_t*)&wifi_rssi, CYW43_ITF_STA);
            cyw43_ioctl(&cyw43_state, (uint32_t)WLC_GET_RATE<<1, sizeof(wifi_rate), (uint8_t*)&wifi_rate, CYW43_ITF_STA);
            next_wifi_status = make_timeout_time_ms(500);
        }

        if (queue_try_remove(&qoutbound, &temp_pkt)) {


            DEBUG(("Send pkt: len = %d, src = ", temp_pkt.len));
            for (int k=6;k<12;k++) {
                DEBUG(("%02X:", temp_pkt.payload[k]));
            }
            DEBUG(("\n"));

            if (link_up)
                cyw43_send_ethernet(&cyw43_state, CYW43_ITF_STA, temp_pkt.len, temp_pkt.payload, false);
        }

        //cyw43_poll(); // for max. speed poll and don't wait for background timer

    }

    cyw43_arch_deinit();
    return 0;
}
