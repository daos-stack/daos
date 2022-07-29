//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package libfabric

/*
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
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

// call into C functions that are looked up at runtime

int call_fi_getinfo(void *fn, struct fi_info *hint, struct fi_info **info)
{
	int (*getinfo)(uint, char*, char*, ulong, struct fi_info*, struct fi_info**);

	assert(fn != NULL);
	getinfo = (int (*)(uint, char*, char*, ulong, struct fi_info*, struct fi_info**))fn;
	return getinfo(lib_fabric_version(), NULL, NULL, 0, hint, info);
}

struct fi_info *call_fi_dupinfo(void *fn, struct fi_info *info)
{
	struct fi_info *(*dupinfo)(struct fi_info*);

	assert(fn != NULL);
	dupinfo = (struct fi_info *(*)(struct fi_info*))fn;
	return dupinfo(info);
}

void call_fi_freeinfo(void *fn, struct fi_info *info)
{
	void (*freeinfo)(struct fi_info*);

	assert(fn != NULL);
	freeinfo = (void (*)(struct fi_info*))fn;
	freeinfo(info);
}

char *call_fi_strerror(void *fn, int code)
{
	char *(*strerr)(int);

	assert(fn != NULL);
	strerr = (char *(*)(int))fn;
	return strerr(code);
}
*/
import "C"

import (
	"fmt"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/dlopen"
)

// Load dynamically loads the libfabric library and provides a method to unload it.
func Load() (func(), error) {
	hdl, err := openLib()
	if err != nil {
		return nil, err
	}
	return func() {
		hdl.Close()
	}, nil
}

func openLib() (*dlopen.LibHandle, error) {
	return dlopen.GetHandle([]string{"libfabric.so.1", "libfabric.so"})
}

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
func fiGetInfo(hdl *dlopen.LibHandle) ([]*fiInfo, func() error, error) {
	getInfoPtr, err := getLibFuncPtr(hdl, "fi_getinfo")
	if err != nil {
		return nil, nil, err
	}

	var fi *C.struct_fi_info
	result := C.call_fi_getinfo(getInfoPtr, nil, &fi)
	if result < 0 {
		return nil, nil, errors.Errorf("fi_getinfo() failed: %s", fiStrError(hdl, -result))
	}
	if fi == nil {
		return nil, nil, errors.Errorf("fi_getinfo() returned no results")
	}

	fiList := make([]*fiInfo, 0)
	for ; fi != nil; fi = fi.next {
		fiList = append(fiList, &fiInfo{
			cFI: fi,
		})
	}

	return fiList, func() error {
		return fiFreeInfo(hdl, fi)
	}, nil
}

func fiAllocInfo(hdl *dlopen.LibHandle) (*C.struct_fi_info, func() error, error) {
	dupPtr, err := getLibFuncPtr(hdl, "fi_dupinfo")
	if err != nil {
		return nil, nil, err
	}

	result := C.call_fi_dupinfo(dupPtr, nil)
	if result == nil {
		return nil, nil, errors.New("fi_dupinfo() failed")
	}

	return result, func() error {
		return fiFreeInfo(hdl, result)
	}, nil
}

func fiStrError(hdl *dlopen.LibHandle, result C.int) string {
	ptr, err := getLibFuncPtr(hdl, "fi_strerror")
	if err != nil {
		return fmt.Sprintf("%d (%s)", result, err.Error())
	}

	cStr := C.call_fi_strerror(ptr, -result)
	return C.GoString(cStr)
}

func fiFreeInfo(hdl *dlopen.LibHandle, info *C.struct_fi_info) error {
	ptr, err := getLibFuncPtr(hdl, "fi_freeinfo")
	if err != nil {
		return err
	}

	C.call_fi_freeinfo(ptr, info)
	return nil
}

func getLibFuncPtr(hdl *dlopen.LibHandle, fnName string) (unsafe.Pointer, error) {
	fnPtr, err := hdl.GetSymbolPointer(fnName)
	if err != nil {
		return nil, err
	}

	if fnPtr == nil {
		return nil, errors.Errorf("%q is nil", fnName)
	}

	return fnPtr, nil
}
