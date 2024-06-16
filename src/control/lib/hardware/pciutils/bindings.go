//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pciutils

import (
	"os"
	"unsafe"

	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/pkg/errors"
)

/*
#cgo LDFLAGS: -lpci

#include <stdlib.h>
#include <pci/pci.h>
*/
import "C"

type api struct{}

func (api *api) Cleanup() {}

func speedToFloat(speed uint16) float32 {
	var mant float32

	switch speed {
	case 1:
		mant = 2.5
	case 2:
		mant = 5
	case 3:
		mant = 8
	case 4:
		mant = 16
	case 5:
		mant = 32
	case 6:
		mant = 64
	default:
		return 0
	}

	return mant * 1e+9
}

func (api *api) PCIeCapsFromConfig(cfgBytes []byte, dev *hardware.PCIDevice) error {
	if len(cfgBytes) == 0 {
		return errors.New("empty config")
	}
	if dev == nil {
		return errors.New("nil dev")
	}

	access := C.pci_alloc()
	if access == nil {
		return errors.New("pci_alloc() failed")
	}
	access.method = C.PCI_ACCESS_DUMP

	tmpFile, err := os.CreateTemp("", "pciutils")
	if err != nil {
		return err
	}
	fName := tmpFile.Name()
	defer os.Remove(fName)

	cfgBytes = append([]byte("01:00.0 device #1\n"), cfgBytes...)
	cfgBytes = append(cfgBytes, []byte("\n")...)
	if _, err := tmpFile.Write(cfgBytes); err != nil {
		return err
	}

	pName := "dump.name"
	cParam := C.CString(pName)
	defer C.free(unsafe.Pointer(cParam))
	cFileName := C.CString(fName)
	defer C.free(unsafe.Pointer(cFileName))

	if rc := C.pci_set_param(access, cParam, cFileName); rc != 0 {
		return errors.Errorf("pci_set_param: rc=%d", rc)
	}

	C.pci_init(access)
	defer C.pci_cleanup(access)

	C.pci_scan_bus(access)

	var pciDev *C.struct_pci_dev
	var cp *C.struct_pci_cap
	var tLnkCap, tLnkSta C.u16

	for pciDev = access.devices; pciDev != nil; pciDev = pciDev.next {
		C.pci_fill_info(pciDev,
			C.PCI_FILL_IDENT|C.PCI_FILL_BASES|C.PCI_FILL_CLASS|C.PCI_FILL_EXT_CAPS)

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
		dev.LinkNegSpeed = speedToFloat(uint16(tLnkSta & C.PCI_EXP_LNKSTA_SPEED))
		dev.LinkNegWidth = uint16(tLnkSta & C.PCI_EXP_LNKSTA_WIDTH >> 4)
	}

	return nil
}
