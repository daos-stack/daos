/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "na_ip.h"

#include "na_error.h"

#include "mercury_inet.h"

#include <ctype.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/*---------------------------------------------------------------------------*/
na_return_t
na_ip_parse_subnet(const char *spec, uint32_t *net_p, uint32_t *netmask_p)
{
    int addr[4], depth, nb;
    const char *sp;
    na_return_t ret = NA_SUCCESS;

    memset(addr, 0, sizeof(addr));

    /* parse the numbers in the address spec string */
    for (sp = spec, depth = 0; *sp && *sp != '/'; sp++) {
        if (isdigit(*sp)) {
            addr[depth] = (addr[depth] * 10) + (*sp - '0');
            NA_CHECK_SUBSYS_ERROR(ip, addr[depth] > 255, done, ret,
                NA_INVALID_ARG, "Malformed address");
            continue;
        }
        NA_CHECK_SUBSYS_ERROR(ip, *sp != '.' || !isdigit(*(sp + 1)), done, ret,
            NA_INVALID_ARG, "Malformed address");
        depth++;
        NA_CHECK_SUBSYS_ERROR(
            ip, depth > 3, done, ret, NA_INVALID_ARG, "Malformed address");
    }
    if (*sp == '/') {
        nb = atoi(sp + 1);
        NA_CHECK_SUBSYS_ERROR(ip, nb < 1 || nb > 32, done, ret, NA_INVALID_ARG,
            "Malformed subnet mask");
    } else {
        nb = (depth + 1) * 8; /* no '/'... use depth to get network bits */
    }
    /* avoid right shifting by 32... it's undefined behavior */
    *netmask_p = (nb == 32) ? 0xffffffff : ~(0xffffffff >> nb);
    *net_p = (uint32_t) ((addr[0] << 24) | (addr[1] << 16) | (addr[2] << 8) |
                         addr[3]) &
             *netmask_p;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
na_ip_pref_addr(uint32_t net, uint32_t netmask, char *outstr)
{
    struct ifaddrs *ifaddr, *cur;
    struct sockaddr_in *sin;
    uint32_t cur_ipaddr;
    static uint32_t localhost = (127 << 24) | 1; /* 127.0.0.1 */
    na_return_t ret = NA_SUCCESS;
    int rc;

    rc = getifaddrs(&ifaddr);
    NA_CHECK_SUBSYS_ERROR(ip, rc == -1, done, ret, NA_FAULT,
        "getifaddrs() failed (%s)", strerror(errno));

    /* walk list looking for a match */
    for (cur = ifaddr; cur != NULL; cur = cur->ifa_next) {
        if ((cur->ifa_flags & IFF_UP) == 0)
            continue; /* skip interfaces that are down */
        if (cur->ifa_addr == NULL || cur->ifa_addr->sa_family != AF_INET)
            continue; /* skip interfaces w/o IP address */
        sin = (struct sockaddr_in *) cur->ifa_addr;
        cur_ipaddr = ntohl(sin->sin_addr.s_addr);
        if (netmask) {
            if ((cur_ipaddr & netmask) == net)
                break; /* got it! */
            continue;
        }
        if (cur_ipaddr != localhost)
            break; /* no net given, randomly select first !localhost addr */
    }

    NA_CHECK_SUBSYS_ERROR(ip, cur == NULL, cleanup, ret, NA_ADDRNOTAVAIL,
        "No match found for IP");

    rc = getnameinfo(cur->ifa_addr, sizeof(struct sockaddr_in), outstr, 16,
        NULL, 0, NI_NUMERICHOST);
    NA_CHECK_SUBSYS_ERROR(ip, rc != 0, cleanup, ret, NA_ADDRNOTAVAIL,
        "getnameinfo() failed (%s)", gai_strerror(rc));

cleanup:
    freeifaddrs(ifaddr);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
na_ip_check_interface(const char *name, uint16_t port, int family,
    char **ifa_name_p, struct sockaddr **sa_p, socklen_t *salen_p)
{
    struct ifaddrs *ifaddrs = NULL, *ifaddr;
    struct addrinfo hints, *addr_res = NULL;
    struct sockaddr_storage *ss_addr = NULL;
    socklen_t salen = 0;
    const char *node = NULL;
    na_return_t ret = NA_SUCCESS;
    int rc;

    /* Allocate new sin addr to store result */
    ss_addr = calloc(1, sizeof(*ss_addr));
    NA_CHECK_SUBSYS_ERROR(ip, ss_addr == NULL, done, ret, NA_NOMEM,
        "Could not allocate sin address");

    /* First check and compare interfaces */
    rc = getifaddrs(&ifaddrs);
    NA_CHECK_SUBSYS_ERROR(ip, rc == -1, done, ret, NA_ADDRNOTAVAIL,
        "getifaddrs() failed (%s)", strerror(errno));

    for (ifaddr = ifaddrs; ifaddr != NULL; ifaddr = ifaddr->ifa_next) {
        if ((ifaddr->ifa_flags & IFF_UP) == 0)
            continue; /* skip interfaces that are down */
        if (ifaddr->ifa_addr == NULL ||
            (ifaddr->ifa_addr->sa_family != AF_INET &&
                ifaddr->ifa_addr->sa_family != AF_INET6))
            continue; /* skip interfaces w/o IP address */

        if (family != AF_UNSPEC && family != ifaddr->ifa_addr->sa_family)
            continue; /* skip interfaces from different address family */

        if (strcmp(ifaddr->ifa_name, name) == 0)
            break; /* matches ifa_name */
    }

    if (ifaddr != NULL) { /* Matched against ifa_name */
        if (ifaddr->ifa_addr->sa_family == AF_INET) {
            *(struct sockaddr_in *) ss_addr =
                *(struct sockaddr_in *) ifaddr->ifa_addr;
            ((struct sockaddr_in *) ss_addr)->sin_port = htons(port);
            salen = sizeof(struct sockaddr_in);
        } else {
            *(struct sockaddr_in6 *) ss_addr =
                *(struct sockaddr_in6 *) ifaddr->ifa_addr;
            ((struct sockaddr_in6 *) ss_addr)->sin6_port = htons(port);
            salen = sizeof(struct sockaddr_in6);
        }
    } else { /* Try to match against passed name */
        char service[NI_MAXSERV];

        /* Add port */
        rc = snprintf(service, NI_MAXSERV, "%" PRIu16, port);
        NA_CHECK_SUBSYS_ERROR(ip, rc < 0 || rc > NI_MAXSERV, done, ret,
            NA_PROTOCOL_ERROR, "snprintf() failed or name truncated, rc: %d",
            rc);

        /* Try to resolve hostname first so that we can later compare the IP */
        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_family = family;
        hints.ai_socktype = SOCK_STREAM;
        if (strcmp(name, "0.0.0.0") == 0)
            hints.ai_flags = AI_PASSIVE;
        else
            node = name;

        rc = getaddrinfo(node, service, &hints, &addr_res);
        NA_CHECK_SUBSYS_ERROR(ip, rc != 0, done, ret, NA_ADDRNOTAVAIL,
            "getaddrinfo() failed (%s) for %s with port %s", gai_strerror(rc),
            node, service);

        memcpy(ss_addr, addr_res->ai_addr, addr_res->ai_addrlen);
        salen = addr_res->ai_addrlen;

        /* Try to find matching ifa_name if we asked for it */
        for (ifaddr = ifaddrs;
             ifaddr != NULL && ifa_name_p != NULL && node != NULL;
             ifaddr = ifaddr->ifa_next) {
            if ((ifaddr->ifa_flags & IFF_UP) == 0)
                continue; /* skip interfaces that are down */
            if (ifaddr->ifa_addr == NULL ||
                (ifaddr->ifa_addr->sa_family != AF_INET &&
                    ifaddr->ifa_addr->sa_family != AF_INET6))
                continue; /* skip interfaces w/o IP address */
            if (addr_res->ai_family != ifaddr->ifa_addr->sa_family)
                continue; /* skip if different address family */

            if (ifaddr->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in *sin_ifa_addr =
                    (struct sockaddr_in *) ifaddr->ifa_addr;
                struct sockaddr_in *sin_addr_res =
                    (struct sockaddr_in *) addr_res->ai_addr;

                if (sin_ifa_addr->sin_addr.s_addr ==
                    sin_addr_res->sin_addr.s_addr)
                    break;
            } else {
                struct sockaddr_in6 *sin6_ifa_addr =
                    (struct sockaddr_in6 *) ifaddr->ifa_addr;
                struct sockaddr_in6 *sin6_addr_res =
                    (struct sockaddr_in6 *) addr_res->ai_addr;

                if (memcmp(&sin6_ifa_addr->sin6_addr, &sin6_addr_res->sin6_addr,
                        sizeof(struct in6_addr)) == 0)
                    break;
            }
        }
        NA_CHECK_SUBSYS_ERROR(ip, ifaddr == NULL && ifa_name_p != NULL, done,
            ret, NA_ADDRNOTAVAIL, "No ifa_name match found for IP");
    }

    if (ifaddr && ifa_name_p && node) {
        *ifa_name_p = strdup(ifaddr->ifa_name);
        NA_CHECK_SUBSYS_ERROR(ip, *ifa_name_p == NULL, done, ret, NA_NOMEM,
            "Could not dup ifa_name");
    }

    if (salen_p)
        *salen_p = salen;

done:
    if (sa_p == NULL || ret != NA_SUCCESS)
        free(ss_addr);
    else
        *sa_p = (struct sockaddr *) ss_addr;

    freeifaddrs(ifaddrs);
    if (addr_res)
        freeaddrinfo(addr_res);

    return ret;
}
