/*
 * Copyright (C) 2024 Cornelis Networks.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef _OPX_HFI1_PRE_CN5000_H_
#define _OPX_HFI1_PRE_CN5000_H_

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "fi_opx_hfi1.h"

/* Implementation PRE-CN5000 */
#ifdef OPX_PRE_CN5000

/* Get the port out of user info or the default */
__OPX_FORCE_INLINE__
void opx_select_port_index(struct fi_opx_hfi1_context_internal *internal, int hfi_unit_number)
{
	/* The environment variable is the user-visible "port" number (PSM2 legacy),
	 * but the HFI1 wants a port index.
	 *
	 * A "port index" is the "port" number - 1
	 */
	int port = OPX_PORT_NUM_ANY;
	if (getenv("HFI_PORT")) {
		/* calculate port index from requested port */
		port = atoi(getenv("HFI_PORT"));
		assert((port == 0) || (port == 1) || (port == 2));
	}

	int port_index; /* calculate from port */
	if (port == OPX_PORT_NUM_ANY) {
		/* Rudimentary attempt at load balancing across ports */
		const pid_t pid = getpid();

		/* Spread port *index* from pid (even 0, odd 1) */
		port_index = (pid & (pid_t) 0x1);
		port = port_index + 1;
		/* check if port is usable and swap if it's down,
			   assuming here that at least one port is working */
		if (opx_hfi_get_port_lid(hfi_unit_number, port) <= 0) {
			FI_WARN(&fi_opx_provider, FI_LOG_FABRIC,
				"OPX_PRE_CN5000 port index %d failed, use %d,  pid %d\n",
				port_index, port_index ? 0 : 1,  getpid());
			port_index = port_index ? 0 : 1 ;
			port = port_index + 1;
		}
	} else {
		port_index = port - 1; /* User selected port */
	}
	/* Whatever we got from user or OPX_PORT_NUM_ANY better work now. */
	assert(opx_hfi_get_port_lid(hfi_unit_number, port) > 0);

	/* Use ioctl pad field to request a port index on the context. */
	internal->user_info.pad = port_index;
	FI_DBG_TRACE(&fi_opx_provider, FI_LOG_FABRIC,
		     "OPX_PRE_CN5000 userinfo pad/port index %d, hfi_port %u, pid %d\n",
		     internal->user_info.pad, internal->context.hfi_port, getpid());

	return;
}

/* Select and validate a port, use HFI_PORT if specified*/
__OPX_FORCE_INLINE__
int opx_get_port(struct hfi1_user_info_dep *uinfo)
{
	/* The port index is already in the "pad" */
	FI_DBG_TRACE(fi_opx_global.prov, FI_LOG_FABRIC,
		     "OPX_PRE_CN5000 pad/port index %d\n",
	       uinfo->pad);
	/* return port (port index + 1)*/
	return ((int)uinfo->pad + 1);
}

/* Early support - needs driver updates */
#define OPX_HFI1_MMAP_OFFSET_MASK   0xfffULL
#define OPX_HFI1_MMAP_OFFSET_SHIFT  0
#define OPX_HFI1_MMAP_SUBCTXT_MASK  0xfULL
#define OPX_HFI1_MMAP_SUBCTXT_SHIFT 12
#define OPX_HFI1_MMAP_CTXT_MASK     0xffULL
#define OPX_HFI1_MMAP_CTXT_SHIFT    16
#define OPX_HFI1_MMAP_TYPE_MASK     0xfULL
#define OPX_HFI1_MMAP_TYPE_SHIFT    24
#define OPX_HFI1_MMAP_MAGIC_MASK    0xffffffffULL
#define OPX_HFI1_MMAP_MAGIC_SHIFT   32

#define OPX_HFI1_MMAP_MAGIC         0xdabbad00

#define opx_offset_in_page(p)	((unsigned long)(p) & ~PAGE_MASK)

#define OPX_HFI1_MMAP_TOKEN_SET(field, val)                                     \
	(((val) & OPX_HFI1_MMAP_##field##_MASK) << OPX_HFI1_MMAP_##field##_SHIFT)

#define OPX_HFI1_MMAP_TOKEN_GET(field, token)                                   \
	(((token) >> OPX_HFI1_MMAP_##field##_SHIFT) & OPX_HFI1_MMAP_##field##_MASK)

#define OPX_HFI1_MMAP_TOKEN(type, ctxt, subctxt, addr)                          \
	(OPX_HFI1_MMAP_TOKEN_SET(MAGIC, OPX_HFI1_MMAP_MAGIC) |                  \
	OPX_HFI1_MMAP_TOKEN_SET(TYPE, type) |                                   \
	OPX_HFI1_MMAP_TOKEN_SET(CTXT, ctxt) |                                   \
	OPX_HFI1_MMAP_TOKEN_SET(SUBCTXT, subctxt) |                             \
	OPX_HFI1_MMAP_TOKEN_SET(OFFSET, (opx_offset_in_page(addr))))

#define OPX_RCV_RHEQ 14

/* Map the RHEQ if it's available */
__OPX_FORCE_INLINE__
void* opx_hfi_mmap_rheq(struct fi_opx_hfi1_context *context)
{
	const struct _hfi_ctrl *ctrl = context->ctrl;
	const struct hfi1_ctxt_info *ctxt_info = &ctrl->ctxt_info;
	__off64_t token = OPX_HFI1_MMAP_TOKEN(OPX_RCV_RHEQ,
					      ctxt_info->ctxt,
					      ctxt_info->subctxt, 0);

	/* 10. Map the RHEQ page */
	ssize_t sz = sizeof(uint64_t) * (size_t)ctxt_info->rcvhdrq_cnt;
	FI_DBG_TRACE(&fi_opx_provider, FI_LOG_FABRIC,
		      "OPX_PRE_CN5000 ctx %#x, subctxt %#x, token %#lX, fd %u, sz %zu\n",
		     ctxt_info->ctxt, ctxt_info->subctxt, token, context->fd, sz);
	void *maddr = HFI_MMAP_ALIGNOFF(context->fd, token, sz, PROT_READ);
	if (OFI_UNLIKELY(maddr == MAP_FAILED)) {
		FI_WARN(&fi_opx_provider, FI_LOG_FABRIC,
			"OPX_PRE_CN5000 mmap of RHEQ size %zu failed: %s\n",
			sz, strerror(errno));
		return NULL;
	}
	return maddr;
}

__OPX_FORCE_INLINE__
void opx_sw_trigger(void) {
#ifdef OPX_TRIGGER
	const char *resource0path = "/sys/class/infiniband/hfi1_0/device/resource0";
	const unsigned misc_gpio_out_csr_offset = 0x500218;
	int mmap_fd;
	fprintf(stderr, "======================================= do_jkr_trigger =======================================\n");

	mmap_fd = open(resource0path, O_RDWR|O_SYNC);
	if (mmap_fd < 0) {
		perror("open mmap_fd failed");
		return;
	}
	// This only maps through the MISC CSRs
	const uint64_t *mmap_addr = mmap(0, 0x1000000, PROT_READ|PROT_WRITE, MAP_SHARED, mmap_fd, 0);
	if (mmap_addr == MAP_FAILED)  {
		perror("mmap failed");
		return;
	}
	uint64_t *misc_gpio_out_addr = (uint64_t *)(((uint8_t *)mmap_addr) + misc_gpio_out_csr_offset);

	*misc_gpio_out_addr = 4;		// software trigger on a csr bit
#endif
	return;
}

#else // !OPX_PRE_CN5000 - WFR or older JKR

/* Get the port out of user info or the default */
__OPX_FORCE_INLINE__
int opx_get_port(struct hfi1_user_info_dep *uinfo)
{
	return OPX_PORT_NUM_ANY;
}

/* Select and validate a port, use HFI_PORT if specified */
__OPX_FORCE_INLINE__
void opx_select_port_index(struct fi_opx_hfi1_context_internal *internal, int hfi_unit_number)
{
	return;
}

/* Map the RHEQ if it's available */
__OPX_FORCE_INLINE__
void* opx_hfi_mmap_rheq(struct fi_opx_hfi1_context *context)
{
	return NULL;
}

__OPX_FORCE_INLINE__
void opx_sw_trigger(void)
{
	return;
}

#endif // OPX_PRE_CN5000

#endif // _OPX_HFI1_PRE_CN5000_H_
