/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Scott Shawcroft for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "supervisor/port.h"
#include "shared-bindings/wifi/Radio.h"
#include "shared-bindings/wifi/Network.h"

#include <math.h>
#include <string.h>

#include "common-hal/wifi/__init__.h"
#include "shared/runtime/interrupt_char.h"
#include "py/gc.h"
#include "py/runtime.h"
#include "shared-bindings/ipaddress/IPv4Address.h"
#include "shared-bindings/wifi/ScannedNetworks.h"
#include "shared-bindings/wifi/AuthMode.h"
#include "shared-bindings/time/__init__.h"
#include "shared-module/ipaddress/__init__.h"

#if CIRCUITPY_MDNS
#include "components/mdns/include/mdns.h"
#endif

#include "lwip/dns.h"
#include "lwip/icmp.h"
#include "lwip/raw.h"
#include "lwip_src/ping.h"

#ifndef PING_ID
#define PING_ID        0xAFAF
#endif

#define MAC_ADDRESS_LENGTH 6

#define NETIF_STA (&cyw43_state.netif[CYW43_ITF_STA])
#define NETIF_AP (&cyw43_state.netif[CYW43_ITF_AP])

NORETURN static void ro_attribute(int attr) {
    mp_raise_msg_varg(&mp_type_AttributeError, MP_ERROR_TEXT("'%s' object has no attribute '%q'"), "Radio", attr);
}

bool common_hal_wifi_radio_get_enabled(wifi_radio_obj_t *self) {
    return true;
}

void common_hal_wifi_radio_set_enabled(wifi_radio_obj_t *self, bool enabled) {
    if (!enabled) {
        ro_attribute(MP_QSTR_enabled);
    }
}

mp_obj_t common_hal_wifi_radio_get_hostname(wifi_radio_obj_t *self) {
    if (!NETIF_STA->hostname) {
        return mp_const_none;
    }
    return mp_obj_new_str(NETIF_STA->hostname, strlen(NETIF_STA->hostname));
}

void common_hal_wifi_radio_set_hostname(wifi_radio_obj_t *self, const char *hostname) {
    ro_attribute(MP_QSTR_hostname);
}

mp_obj_t common_hal_wifi_radio_get_mac_address(wifi_radio_obj_t *self) {
    return mp_obj_new_bytes(cyw43_state.mac, MAC_ADDRESS_LENGTH);
}

void common_hal_wifi_radio_set_mac_address(wifi_radio_obj_t *self, const uint8_t *mac) {
    ro_attribute(MP_QSTR_mac_address);
}

mp_float_t common_hal_wifi_radio_get_tx_power(wifi_radio_obj_t *self) {
    return MICROPY_FLOAT_CONST(0.);
}

void common_hal_wifi_radio_set_tx_power(wifi_radio_obj_t *self, const mp_float_t tx_power) {
    ro_attribute(MP_QSTR_tx_power);

}

mp_obj_t common_hal_wifi_radio_get_mac_address_ap(wifi_radio_obj_t *self) {
    return common_hal_wifi_radio_get_mac_address(self);
}

void common_hal_wifi_radio_set_mac_address_ap(wifi_radio_obj_t *self, const uint8_t *mac) {
    ro_attribute(MP_QSTR_mac_address_ap);
}

mp_obj_t common_hal_wifi_radio_start_scanning_networks(wifi_radio_obj_t *self) {
    if (self->current_scan) {
        mp_raise_RuntimeError(translate("Already scanning for wifi networks"));
    }
    if (!common_hal_wifi_radio_get_enabled(self)) {
        mp_raise_RuntimeError(translate("wifi is not enabled"));
    }
    wifi_scannednetworks_obj_t *scan = m_new_obj(wifi_scannednetworks_obj_t);
    scan->base.type = &wifi_scannednetworks_type;
    mp_obj_t args[] = { mp_const_empty_tuple, MP_OBJ_NEW_SMALL_INT(16) };
    scan->results = mp_type_deque.make_new(&mp_type_deque, 2, 0, args);
    self->current_scan = scan;
    wifi_scannednetworks_start_scan(scan);
    return scan;
}

void common_hal_wifi_radio_stop_scanning_networks(wifi_radio_obj_t *self) {
    self->current_scan = NULL;
}

void common_hal_wifi_radio_start_station(wifi_radio_obj_t *self) {
    cyw43_arch_enable_sta_mode();
}

void common_hal_wifi_radio_stop_station(wifi_radio_obj_t *self) {
}

void common_hal_wifi_radio_start_ap(wifi_radio_obj_t *self, uint8_t *ssid, size_t ssid_len, uint8_t *password, size_t password_len, uint8_t channel, uint8_t authmode, uint8_t max_connections) {
    mp_raise_NotImplementedError(NULL);
}

void common_hal_wifi_radio_stop_ap(wifi_radio_obj_t *self) {
    mp_raise_NotImplementedError(NULL);
}

wifi_radio_error_t common_hal_wifi_radio_connect(wifi_radio_obj_t *self, uint8_t *ssid, size_t ssid_len, uint8_t *password, size_t password_len, uint8_t channel, mp_float_t timeout, uint8_t *bssid, size_t bssid_len) {
    if (!common_hal_wifi_radio_get_enabled(self)) {
        mp_raise_RuntimeError(translate("wifi is not enabled"));
    }
    unsigned timeout_ms = timeout <= 0 ? 8000 : (unsigned)MAX(0, MICROPY_FLOAT_C_FUN(ceil)(timeout * 1000));
    // TODO use connect_async so we can service bg tasks & check for ctrl-c during
    // connect
    int result = cyw43_arch_wifi_connect_timeout_ms((const char *)ssid, (const char *)password, CYW43_AUTH_WPA2_AES_PSK, timeout_ms);
    switch (result) {
        case 0:
            return WIFI_RADIO_ERROR_NONE;
        // case CYW43_LINK_DOWN:
        // case CYW43_LINK_JOIN:
        // case CYW43_LINK_NOIP:
        // case CYW43_LINK_UP:
        case CYW43_LINK_FAIL:
            return WIFI_RADIO_ERROR_CONNECTION_FAIL;
        case CYW43_LINK_NONET:
            return WIFI_RADIO_ERROR_NO_AP_FOUND;
        case CYW43_LINK_BADAUTH:
            return WIFI_RADIO_ERROR_AUTH_FAIL;

        default:
            return WIFI_RADIO_ERROR_UNSPECIFIED;
    }
}

mp_obj_t common_hal_wifi_radio_get_ap_info(wifi_radio_obj_t *self) {
    mp_raise_NotImplementedError(NULL);
}

mp_obj_t common_hal_wifi_radio_get_ipv4_gateway(wifi_radio_obj_t *self) {
    if (!netif_is_up(NETIF_STA)) {
        return mp_const_none;
    }
    return common_hal_ipaddress_new_ipv4address(NETIF_STA->gw.addr);
}

mp_obj_t common_hal_wifi_radio_get_ipv4_gateway_ap(wifi_radio_obj_t *self) {
    if (!netif_is_up(NETIF_AP)) {
        return mp_const_none;
    }
    return common_hal_ipaddress_new_ipv4address(NETIF_AP->gw.addr);
}

mp_obj_t common_hal_wifi_radio_get_ipv4_subnet(wifi_radio_obj_t *self) {
    if (!netif_is_up(NETIF_STA)) {
        return mp_const_none;
    }
    return common_hal_ipaddress_new_ipv4address(NETIF_STA->netmask.addr);
}

mp_obj_t common_hal_wifi_radio_get_ipv4_subnet_ap(wifi_radio_obj_t *self) {
    if (!netif_is_up(NETIF_AP)) {
        return mp_const_none;
    }
    return common_hal_ipaddress_new_ipv4address(NETIF_AP->netmask.addr);
}

mp_obj_t common_hal_wifi_radio_get_ipv4_address(wifi_radio_obj_t *self) {
    if (!netif_is_up(NETIF_STA)) {
        return mp_const_none;
    }
    return common_hal_ipaddress_new_ipv4address(NETIF_STA->ip_addr.addr);
}

mp_obj_t common_hal_wifi_radio_get_ipv4_address_ap(wifi_radio_obj_t *self) {
    if (!netif_is_up(NETIF_AP)) {
        return mp_const_none;
    }
    return common_hal_ipaddress_new_ipv4address(NETIF_AP->ip_addr.addr);
}

mp_obj_t common_hal_wifi_radio_get_ipv4_dns(wifi_radio_obj_t *self) {
    uint32_t addr = dns_getserver(0)->addr;
    if (!netif_is_up(NETIF_STA) || addr == 0) {
        return mp_const_none;
    }
    return common_hal_ipaddress_new_ipv4address(addr);
}

void common_hal_wifi_radio_set_ipv4_dns(wifi_radio_obj_t *self, mp_obj_t ipv4_dns_addr) {
    mp_raise_NotImplementedError(NULL);
}

void common_hal_wifi_radio_start_dhcp_client(wifi_radio_obj_t *self) {
    mp_raise_NotImplementedError(NULL);
}

void common_hal_wifi_radio_stop_dhcp_client(wifi_radio_obj_t *self) {
    mp_raise_NotImplementedError(NULL);
}

void common_hal_wifi_radio_set_ipv4_address(wifi_radio_obj_t *self, mp_obj_t ipv4, mp_obj_t netmask, mp_obj_t gateway, mp_obj_t ipv4_dns) {
    mp_raise_NotImplementedError(NULL);
}

volatile bool ping_received;

static u8_t
ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr) {
    struct icmp_echo_hdr *iecho;
    LWIP_ASSERT("p != NULL", p != NULL);

    if ((p->tot_len >= (PBUF_IP_HLEN + sizeof(struct icmp_echo_hdr))) &&
        pbuf_remove_header(p, PBUF_IP_HLEN) == 0) {
        iecho = (struct icmp_echo_hdr *)p->payload;

        if ((iecho->id == PING_ID) && (iecho->seqno == lwip_htons(ping_seq_num))) {
            LWIP_DEBUGF(PING_DEBUG, ("ping: recv "));
            ip_addr_debug_print(PING_DEBUG, addr);
            LWIP_DEBUGF(PING_DEBUG, (" %"U32_F " ms\n", (sys_now() - ping_time)));

            /* do some ping result processing */
            ping_received = true;
            pbuf_free(p);
            return 1; /* eat the packet */
        }
        /* not eaten, restore original packet */
        pbuf_add_header(p, PBUF_IP_HLEN);
    }
    return 0; /* don't eat the packet */
}

mp_int_t common_hal_wifi_radio_ping(wifi_radio_obj_t *self, mp_obj_t ip_address, mp_float_t timeout) {
    ip_addr_t ping_addr;
    ipaddress_ipaddress_to_lwip(ip_address, &ping_addr);

    struct raw_pcb *ping_pcb;
    MICROPY_PY_LWIP_ENTER
        ping_pcb = raw_new(IP_PROTO_ICMP);
    if (!ping_pcb) {
        MICROPY_PY_LWIP_EXIT
        return -1;
    }
    raw_recv(ping_pcb, ping_recv, NULL);
    raw_bind(ping_pcb, IP_ADDR_ANY);
    MICROPY_PY_LWIP_EXIT

        ping_received = false;
    ping_send(ping_pcb, &ping_addr);
    size_t timeout_ms = (size_t)MICROPY_FLOAT_C_FUN(ceil)(timeout * 1000);
    uint64_t start = port_get_raw_ticks(NULL);
    uint64_t deadline = start + timeout_ms;
    while (port_get_raw_ticks(NULL) < deadline && !ping_received) {
        RUN_BACKGROUND_TASKS;
        if (mp_hal_is_interrupted()) {
            break;
        }
    }
    mp_int_t result = -1;
    if (ping_received) {
        uint64_t now = port_get_raw_ticks(NULL);
        result = now - start;
    }

    MICROPY_PY_LWIP_ENTER;
    raw_remove(ping_pcb);
    MICROPY_PY_LWIP_EXIT;

    return result;
}

void common_hal_wifi_radio_gc_collect(wifi_radio_obj_t *self) {
    // Only bother to scan the actual object references.
    gc_collect_ptr(self->current_scan);
}
