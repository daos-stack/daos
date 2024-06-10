//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pciutils

import (
	"errors"
	"fmt"
	"os"
	"unsafe"

	"github.com/daos-stack/daos/src/control/lib/hardware"
)

/*
#cgo LDFLAGS: -lpci

#include <stdlib.h>
#include <pci/pci.h>

char *lookup_pci_name(struct pci_access *a, char *buf, int size, int flags, ushort vendor_id, ushort device_id)
{
	return pci_lookup_name(a, buf, size, flags, vendor_id, device_id);
}
*/
import "C"

type api struct {
	access *C.struct_pci_access
}

func initAPI() (*api, error) {
	api := &api{
		access: C.pci_alloc(),
	}

	if api.access == nil {
		return nil, errors.New("pci_alloc() failed")
	}

	C.pci_init(api.access)

	return api, nil
}

func (api *api) Cleanup() {
	C.pci_cleanup(api.access)
}

func (api *api) PCIDeviceFromConfig(cfgBytes []byte, dev *hardware.PCIDevice) error {
	if len(cfgBytes) == 0 {
		return errors.New("empty config")
	}
	if dev == nil {
		return errors.New("nil dev")
	}

	// NB: We have to do the pci_init() after we've set the access method,
	// so the fancy initAPI() flow doesn't work.
	C.pci_cleanup(api.access)
	api.access = C.pci_alloc()
	if api.access == nil {
		return errors.New("pci_alloc() failed")
	}

	tmpFile, err := os.CreateTemp("", "pciutils")
	if err != nil {
		return err
	}
	defer os.Remove(tmpFile.Name())

	cfgBytes = append([]byte("01:00.0 device #1\n"), cfgBytes...)
	if _, err := tmpFile.Write(cfgBytes); err != nil {
		return err
	}

	cParam := C.CString("dump.name")
	defer C.free(unsafe.Pointer(cParam))
	cFileName := C.CString(tmpFile.Name())
	defer C.free(unsafe.Pointer(cFileName))
	C.pci_set_param(api.access, cParam, cFileName)
	api.access.method = C.PCI_ACCESS_DUMP
	C.pci_init(api.access)

	C.pci_scan_bus(api.access)
	var pciDev *C.struct_pci_dev
	var nameBuf [1024]C.char
	var name *C.char

	for pciDev = api.access.devices; pciDev != nil; pciDev = pciDev.next {
		C.pci_fill_info(pciDev, C.PCI_FILL_IDENT|C.PCI_FILL_BASES|C.PCI_FILL_CLASS|C.PCI_FILL_CAPS)
		fmt.Fprintf(os.Stderr, "Vendor: %04x, Device: %04x, Class: %08x, Cap: %+v\n", pciDev.vendor_id, pciDev.device_id, pciDev.device_class, pciDev.first_cap)

		dev.PCIAddr.Bus = uint8(pciDev.bus)
		dev.PCIAddr.Device = uint8(pciDev.dev)
		dev.PCIAddr.Function = uint8(pciDev._func)

		name = C.lookup_pci_name(api.access, &nameBuf[0], C.int(len(nameBuf)), C.PCI_LOOKUP_DEVICE, pciDev.vendor_id, pciDev.device_id)
		dev.Name = C.GoString(name)

	}

	return nil
}
