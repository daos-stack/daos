/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef NA_IP_H
#define NA_IP_H

#include "na_types.h"

#include <sys/socket.h>

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

/*****************/
/* Public Macros */
/*****************/

/*********************/
/* Public Prototypes */
/*********************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse a subnet specification string.
 *
 * \param spec  [IN]    the specification string to parse
 * \param net_p  [OUT]  pointer to where to put network info
 * \param mask_p [OUT]  pointer to where to put the netmask info
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
NA_PLUGIN_VISIBILITY na_return_t
na_ip_parse_subnet(const char *spec, uint32_t *net_p, uint32_t *netmask_p);

/**
 * Get preferred ip address (based on provided subnet).
 *
 * \param net     [IN]  desired network
 * \param netmask [IN]  netmask for desired network, 0 if no preference given
 * \param outstr  [OUT] result returned here (size should at least be 16 to fit)
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
NA_PLUGIN_VISIBILITY na_return_t
na_ip_pref_addr(uint32_t net, uint32_t netmask, char *outstr);

/**
 * Return interface name and sockaddr from a given hostname / port. If non-null,
 * \sa_p must be freed after its use. If non-null \ifa_name must be freed after
 * its use.
 *
 * \param name [IN]             name to resolve (host or ifa name)
 * \param port [IN]             port to use
 * \param family [IN]           address family to use (AF_UNSPEC if any)
 * \param ifa_name_p [OUT]      returned iface name
 * \param sa_p [OUT]            returned pointer to usable sockaddr
 * \param salen_p [OUT]         returned length of address
 *
 * \return NA_SUCCESS or corresponding NA error code
 */
NA_PLUGIN_VISIBILITY na_return_t
na_ip_check_interface(const char *name, uint16_t port, int family,
    char **ifa_name_p, struct sockaddr **sa_p, socklen_t *salen_p);

#ifdef __cplusplus
}
#endif

#endif /* NA_IP_H */
