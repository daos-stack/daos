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

const giga = 1e+9

// DummyPreamble provides a prefix expected by library when reading a configuration dump. The actual
// address is irrelevant because only the config content is being used. Without an address and
// device values in the preamble, the library will refuse to parse the config dump file content.
var DummyPreamble = []byte("01:00.0 device #1\n")

// Error values for common invalid situations.
var (
	ErrNoDevice         = errors.New("no pci device scanned")
	ErrMultiDevices     = errors.New("want single device config got multiple")
	ErrCfgNotTerminated = errors.New("device config content not new-line terminated")
	ErrCfgMissing       = errors.New("incomplete device config")
)

type api struct{}

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

	return mant * giga
}

// PCIeCapsFromConfig takes a PCIe config space dump (of the format output by lspci -xxx) in the
// form of a byte slice. The second parameter is a reference to a PCIDevice struct to be populated.
// The library access method parameters are set to read the config dump from file and the input byte
// slice is written to a temporary file that is read on pci_scan_bus(). The device that has been
// populated on scan is used to populate the output PCIDevice field values.
func (api *api) PCIeCapsFromConfig(cfgBytes []byte, dev *hardware.PCIDevice) error {
	lenCfg := len(cfgBytes)
	if lenCfg == 0 {
		return errors.New("empty config")
	}
	if dev == nil {
		return errors.New("nil device reference")
	}
	if cfgBytes[lenCfg-1] != '\n' {
		return ErrCfgNotTerminated
	}
	if lenCfg <= len(DummyPreamble) {
		return ErrCfgMissing
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

	// Write config dump contents to file to be read by library scan call.
	if _, err := tmpFile.Write(cfgBytes); err != nil {
		return err
	}

	pName := "dump.name"
	cParam := C.CString(pName)
	defer C.free(unsafe.Pointer(cParam))

	cFileName := C.CString(fName)
	defer C.free(unsafe.Pointer(cFileName))

	// Set access parameters to read config dump from file.
	if rc := C.pci_set_param(access, cParam, cFileName); rc != 0 {
		return errors.Errorf("pci_set_param: rc=%d", rc)
	}

	// Init has to be called after access parameters have been set.
	C.pci_init(access)
	defer C.pci_cleanup(access)

	// Scan initiates read from config dump.
	C.pci_scan_bus(access)

	var pciDev *C.struct_pci_dev = access.devices

	if pciDev == nil {
		return ErrNoDevice
	}
	if pciDev.next != nil {
		return ErrMultiDevices
	}

	C.pci_fill_info(pciDev,
		C.PCI_FILL_IDENT|C.PCI_FILL_BASES|C.PCI_FILL_CLASS|C.PCI_FILL_EXT_CAPS)

	var cp *C.struct_pci_cap = C.pci_find_cap(pciDev, C.PCI_CAP_ID_EXP, C.PCI_CAP_NORMAL)

	if cp == nil {
		return errors.New("no pci-express capabilities found")
	}

	cpAddr := uint32(cp.addr)
	dev.LinkPortID = uint16(cpAddr >> 24)

	var tLnkCap C.u16 = C.pci_read_word(pciDev, C.int(cpAddr+C.PCI_EXP_LNKCAP))
	var tLnkSta C.u16 = C.pci_read_word(pciDev, C.int(cpAddr+C.PCI_EXP_LNKSTA))

	dev.LinkMaxSpeed = speedToFloat(uint16(tLnkCap & C.PCI_EXP_LNKCAP_SPEED))
	dev.LinkMaxWidth = uint16(tLnkCap & C.PCI_EXP_LNKCAP_WIDTH >> 4)
	dev.LinkNegSpeed = speedToFloat(uint16(tLnkSta & C.PCI_EXP_LNKSTA_SPEED))
	dev.LinkNegWidth = uint16(tLnkSta & C.PCI_EXP_LNKSTA_WIDTH >> 4)

	return nil
}
