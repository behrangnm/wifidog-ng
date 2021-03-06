/*
 * Copyright (C) 2017 Jianhui Zhao <jianhuizhao329@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <net/if.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>
#include <sys/time.h>
#include <net/if_arp.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <libubox/ulog.h>

#include "utils.h"
#include "resolv.h"

int get_iface_ip(const char *ifname, char *dst, int len)
{
    struct ifreq ifr;
    int sock = -1;

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        ULOG_ERR("socket:%s\n", strerror(errno));
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

    if (ioctl(sock, SIOCGIFADDR, &ifr) < 0) {
        ULOG_ERR("ioctl:%s\n", strerror(errno));
        close(sock);
        return -1;
    }

    close(sock);
    snprintf(dst, len, "%s", inet_ntoa(((struct sockaddr_in*)&(ifr.ifr_addr))->sin_addr));
    
    return 0;
}

int get_iface_mac(const char *ifname, char *dst, int len)
{
    struct ifreq ifr;
    int sock;
    uint8_t *hw;

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        ULOG_ERR("socket:%s\n", strerror(errno));
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

    if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
        ULOG_ERR("ioctl:%s\n", strerror(errno));
        close(sock);
        return -1;
    }
    
    close(sock);

    hw = (uint8_t *)ifr.ifr_hwaddr.sa_data;
    snprintf(dst, len, "%02X%02X%02X%02X%02X%02X", hw[0], hw[1], hw[2], hw[3], hw[4], hw[5]);

    return 0;
}

int arp_get(const char *ifname, const char *ip, char *dst, int len)
{
    struct arpreq req;  
    struct sockaddr_in *sin;  
    int sock = 0;
    uint8_t *hw;
  
    memset(&req, 0, sizeof(struct arpreq));  
  
    sin = (struct sockaddr_in *)&req.arp_pa;  
    sin->sin_family = AF_INET;  
    sin->sin_addr.s_addr = inet_addr(ip);  
  
    strncpy(req.arp_dev, ifname, sizeof(req.arp_dev));  
  
    sock = socket(AF_INET, SOCK_DGRAM, 0);  
    if(sock < 0) {
        ULOG_ERR("socket:%s\n", strerror(errno));  
        return -1;
    }  
  
    if (ioctl(sock, SIOCGARP, &req) < 0) {
        ULOG_ERR("ioctl:%s\n", strerror(errno));  
        close(sock);  
        return -1;  
    }

    close(sock);

    hw = (uint8_t *)req.arp_ha.sa_data; 
    snprintf(dst, len, "%02X:%02X:%02X:%02X:%02X:%02X", hw[0], hw[1], hw[2], hw[3], hw[4], hw[5]);

    return 0;
}

/* blen is the size of buf; slen is the length of src.  The input-string need
** not be, and the output string will not be, null-terminated.  Returns the
** length of the encoded string, or -1 on error (buffer overflow) */
int urlencode(char *buf, int blen, const char *src, int slen)
{
    static const char hex[] = "0123456789abcdef";
    int i, len = 0;

    blen -= 1;

    for (i = 0; (i < slen) && (len < blen); i++) {
        if(isalnum(src[i]) || (src[i] == '-') || (src[i] == '_') ||
            (src[i] == '.') || (src[i] == '~')) {
            buf[len++] = src[i];
        } else if ((len + 3) <= blen) {
            buf[len++] = '%';
            buf[len++] = hex[(src[i] >> 4) & 15];
            buf[len++] = hex[ src[i]       & 15];
        } else {
            len = -1;
            break;
        }
    }

    if (i == slen)
        buf[slen] = 0;
    return (i == slen) ? len : -1;
}

static int kmod_ctl(const char *interface, bool enable)
{
    FILE *fp = fopen("/proc/wifidog-ng/config", "w");
    if (!fp) {
        ULOG_ERR("Kernel module is not loaded\n");
        return -1;
    }

    if (enable)
        fprintf(fp, "interface=%s\n", interface);

    fprintf(fp, "enabled=%d\n", enable);
    fclose(fp);

    ULOG_INFO("%s kmod\n", enable ? "Enable" : "Disable");
    return 0;
}

int enable_kmod(const char *interface)
{
    return kmod_ctl(interface, true);
}

int disable_kmod()
{
    return kmod_ctl(NULL, false);
}

static int destip_ctl(const char *ip, bool allow)
{
    FILE *fp = fopen("/proc/wifidog-ng/ip", "w");
    if (!fp) {
        ULOG_ERR("Kernel module is not loaded\n");
        return -1;
    }

    fprintf(fp, "%c%s\n", allow ? '+' : '-', ip);
    fclose(fp);

    ULOG_INFO("%s destip: %s\n", ip, allow ? "allow" : "deny");

    return 0;
}

int allow_destip(const char *ip)
{
    return destip_ctl(ip, true);
}

int deny_destip(const char *ip)
{
    return destip_ctl(ip, false);
}

static void my_resolv_cb(struct hostent *he, void *data)
{
    char **p;
    char addr_buf[INET_ADDRSTRLEN];
    bool allow = data;

    if (!he)
        return;

    for (p = he->h_addr_list; *p; p++) {
        inet_ntop(he->h_addrtype, *p, addr_buf, sizeof(addr_buf));
        destip_ctl(addr_buf, allow);
    }
}

static int domain_ctl(const char *domain, bool allow)
{
    int ip[4];

    if (sscanf(domain, "%d.%d.%d.%d", ip + 0, ip + 1, ip + 2, ip + 3) == 4) {
        destip_ctl(domain, allow);
        return 0;
    }

    resolv_start(domain, my_resolv_cb, (void *)allow);
    return 0;
}

int allow_domain(const char *domain)
{
    return domain_ctl(domain, true);
}

int deny_domain(const char *domain)
{
    return domain_ctl(domain, false);
}

static int termianl_ctl(const char *mac, const char *token, char action)
{
    FILE *fp;

    fp = fopen("/proc/wifidog-ng/term", "w");
    if (!fp) {
        ULOG_ERR("fopen:%s\n", strerror(errno));
        return -1;
    }


    fprintf(fp, "%c%s %s\n", action, mac, token ? token : "");
    fclose(fp);

    ULOG_INFO("termianl ctl: %c %s\n", action, mac);

    return 0;
}

int allow_termianl(const char *mac, const char *token, bool temporary)
{
    return termianl_ctl(mac, token, temporary ? '?' : '+');
}

int deny_termianl(const char *mac)
{
    return termianl_ctl(mac, "", '-');
}

int whitelist_termianl(const char *mac)
{
    return termianl_ctl(mac, "", '!');
}