//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package libfabric

/*
#cgo LDFLAGS: -lfabric
#include <stdlib.h>
#include <stdio.h>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_errno.h>

#define getHFIUnitError -2
typedef struct {
	uint64_t reserved_1;
	uint8_t  reserved_2;
	int8_t   unit;
	uint8_t  port;
	uint8_t  reserved_3;
	uint32_t service;
} psmx2_ep_name;

int get_hfi_unit(void *src_addr) {
	psmx2_ep_name *psmx2;
	psmx2 = (psmx2_ep_name *)src_addr;
	if (!psmx2)
		return getHFIUnitError;
	return psmx2->unit;
}

// Major and minor versions are hard-coded per libfabric recommendations
uint lib_fabric_version(void)
{
	return FI_VERSION(1, 7);
}
*/
import "C"

import (
	"github.com/pkg/errors"
)

func libFabricVersion() C.uint {
	return C.lib_fabric_version()
}

var errHFIUnitsInUse = errors.New("all HFI units in use")

type fiInfo struct {
	cFI *C.struct_fi_info
}

func (f *fiInfo) domainName() string {
	if f.cFI == nil || f.cFI.domain_attr == nil || f.cFI.domain_attr.name == nil {
		return ""
	}
	return C.GoString(f.cFI.domain_attr.name)
}

func (f *fiInfo) fabricProvider() string {
	if f.cFI == nil || f.cFI.fabric_attr == nil || f.cFI.fabric_attr.prov_name == nil {
		return ""
	}
	return C.GoString(f.cFI.fabric_attr.prov_name)
}

func (f *fiInfo) osName() string {
	if f.cFI == nil || f.cFI.nic == nil || f.cFI.nic.device_attr == nil || f.cFI.nic.device_attr.name == nil {
		return ""
	}
	return C.GoString(f.cFI.nic.device_attr.name)
}

func (f *fiInfo) hfiUnit() (uint, error) {
	hfiUnit := C.get_hfi_unit(f.cFI.src_addr)
	switch hfiUnit {
	case C.getHFIUnitError:
		return 0, errors.New("failed to get HFI unit")
	case -1:
		return 0, errHFIUnitsInUse
	}
	return uint(hfiUnit), nil
}

// fiGetInfo fetches the list of fi_info structs with the desired provider (if non-empty), or all of
// them otherwise. It also returns the cleanup function to free the fi_info.
func fiGetInfo(provider string) ([]*fiInfo, func(), error) {
	hint := C.fi_allocinfo()
	if hint == nil {
		return nil, nil, errors.New("fi_allocinfo() failed to allocate hint")
	}
	defer C.fi_freeinfo(hint)

	if provider != "" {
		hint.fabric_attr.prov_name = C.CString(provider)
	}

	var fi *C.struct_fi_info
	result := C.fi_getinfo(libFabricVersion(), nil, nil, 0, hint, &fi)
	if result < 0 {
		return nil, nil, errors.Errorf("fi_getinfo() failed: %s", C.GoString(C.fi_strerror(-result)))
	}
	if fi == nil {
		return nil, nil, errors.Errorf("fi_getinfo() returned no results for provider %q", provider)
	}

	fiList := make([]*fiInfo, 0)
	for ; fi != nil; fi = fi.next {
		fiList = append(fiList, &fiInfo{
			cFI: fi,
		})
	}

	return fiList, func() {
		C.fi_freeinfo(fi)
	}, nil
}
