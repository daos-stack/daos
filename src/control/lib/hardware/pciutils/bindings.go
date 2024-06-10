//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pciutils

import (
	"errors"
	"os"
	"unsafe"

	"github.com/daos-stack/daos/src/control/lib/hardware"
)

/*
#cgo LDFLAGS: -lpci

#include <stdlib.h>
#include <pci/pci.h>
*/
import "C"

type api struct {
	access *C.struct_pci_access
}

func getAPI() (*api, error) {
	api := &api{
		access: C.pci_alloc(),
	}

	if api.access == nil {
		return nil, errors.New("pci_alloc() failed")
	}

	return api, nil
}

func (api *api) Cleanup() {}

func speedToFloat(speed uint16) float32 {
	giga := 1000000000
	tera := 1000 * giga

	switch speed {
	case 1:
		return float32(giga) * 2.5
	case 2:
		return float32(tera) * 5
	case 3:
		return float32(tera) * 8
	case 4:
		return float32(tera) * 16
	case 5:
		return float32(tera) * 32
	case 6:
		return float32(tera) * 64
	default:
		return 0
	}
}

func (api *api) PCIDeviceFromConfig(cfgBytes []byte, dev *hardware.PCIDevice) error {
	if len(cfgBytes) == 0 {
		return errors.New("empty config")
	}
	if dev == nil {
		return errors.New("nil dev")
	}

	tmpFile, err := os.CreateTemp("", "pciutils")
	if err != nil {
		return err
	}
	defer os.Remove(tmpFile.Name())

	cfgBytes = append([]byte("01:00.0 device #1\n"), cfgBytes...)
	cfgBytes = append(cfgBytes, []byte("\n")...)
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
	defer C.pci_cleanup(api.access)

	C.pci_scan_bus(api.access)

	var pciDev *C.struct_pci_dev
	var cp *C.struct_pci_cap
	var tLnkCap, tLnkSta C.u16

	for pciDev = api.access.devices; pciDev != nil; pciDev = pciDev.next {
		C.pci_fill_info(pciDev,
			C.PCI_FILL_IDENT|C.PCI_FILL_BASES|C.PCI_FILL_CLASS|C.PCI_FILL_EXT_CAPS)

		//		fmt.Printf("%04x:%02x:%02x.%d vendor=%04x device=%04x class=%04x irq=%d base0=%x\n",
		//			pciDev.domain, pciDev.bus, pciDev.dev, pciDev._func, pciDev.vendor_id,
		//			pciDev.device_id, pciDev.device_class, pciDev.irq, pciDev.base_addr[0])

		cp = C.pci_find_cap(pciDev, C.PCI_CAP_ID_EXP, C.PCI_CAP_NORMAL)
		if cp == nil {
			return errors.New("no pci-express capabilities found")
		}

		cpAddr := uint32(cp.addr)
		dev.LinkPortID = uint16(cpAddr >> 24)

		tLnkCap = C.pci_read_word(pciDev, C.int(cpAddr+C.PCI_EXP_LNKCAP))
		tLnkSta = C.pci_read_word(pciDev, C.int(cpAddr+C.PCI_EXP_LNKSTA))

		dev.LinkMaxSpeed = speedToFloat(uint16(tLnkCap & C.PCI_EXP_LNKCAP_SPEED))
		dev.LinkMaxWidth = uint16(tLnkCap & C.PCI_EXP_LNKCAP_WIDTH >> 4)
		dev.LinkNegSpeed = speedToFloat(uint16(tLnkCap & C.PCI_EXP_LNKSTA_SPEED))
		dev.LinkNegWidth = uint16(tLnkSta & C.PCI_EXP_LNKSTA_WIDTH >> 4)

		//		fmt.Printf("link port/max_speed/max_width/neg_speed/neg_width: %d/%d/%d/%d/%d\n",
		//			cpAddr>>24, tLnkCap&C.PCI_EXP_LNKCAP_SPEED,
		//			tLnkCap&C.PCI_EXP_LNKCAP_WIDTH>>4, tLnkSta&C.PCI_EXP_LNKSTA_SPEED,
		//			tLnkSta&C.PCI_EXP_LNKSTA_WIDTH>>4)
	}

	return nil
}
