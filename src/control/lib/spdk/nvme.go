//
// (C) Copyright 2018-2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package spdk

// CGO_CFLAGS & CGO_LDFLAGS env vars can be used
// to specify additional dirs.

/*
#cgo CFLAGS: -I .
#cgo LDFLAGS: -L . -lnvme_control -lspdk

#include "stdlib.h"
#include "spdk/stdinc.h"
#include "spdk/nvme.h"
#include "spdk/env.h"
#include "include/nvme_control.h"
#include "include/nvme_control_common.h"
*/
import "C"

import (
	"os"
	"strings"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
)

const lockfilePathPrefix = "/tmp/spdk_pci_lock_"

// NVME is the interface that provides SPDK NVMe functionality.
type NVME interface {
	// Discover NVMe controllers and namespaces, and device health info
	Discover(logging.Logger) ([]Controller, error)
	// Discover NVMe SSD PCIe addresses behind VMD
	DiscoverVmd(log logging.Logger) ([]string, error)
	// Format NVMe controller namespaces
	Format(logging.Logger, string) error
	// Cleanup NVMe object references
	Cleanup()
	// CleanLockfiles removes SPDK lockfiles for specific PCI addresses
	CleanLockfiles(logging.Logger, ...string)
}

// Nvme is an NVME interface implementation.
type Nvme struct{}

// Controller struct mirrors C.struct_ctrlr_t and
// describes a NVMe controller.
//
// TODO: populate implicitly using inner member:
// +inner C.struct_ctrlr_t
type Controller struct {
	Model       string
	Serial      string
	PCIAddr     string
	FWRev       string
	SocketID    int32
	Namespaces  []*Namespace
	HealthStats *DeviceHealth
}

// Namespace struct mirrors C.struct_ns_t and
// describes a NVMe Namespace tied to a controller.
//
// TODO: populate implicitly using inner member:
// +inner C.struct_ns_t
type Namespace struct {
	ID   uint32
	Size uint64
}

// DeviceHealth struct mirrors C.struct_dev_health_t
// and describes the raw SPDK device health stats
// of a controller (NVMe SSD).
type DeviceHealth struct {
	Temperature     uint32
	TempWarnTime    uint32
	TempCritTime    uint32
	CtrlBusyTime    uint64
	PowerCycles     uint64
	PowerOnHours    uint64
	UnsafeShutdowns uint64
	MediaErrors     uint64
	ErrorLogEntries uint64
	TempWarn        bool
	AvailSpareWarn  bool
	ReliabilityWarn bool
	ReadOnlyWarn    bool
	VolatileWarn    bool
}

type remFunc func(name string) error

func realRemove(name string) error {
	return os.Remove(name)
}

func cleanLockfiles(log logging.Logger, remove remFunc, pciAddrs ...string) error {
	removed := make([]string, 0, len(pciAddrs))

	for _, pciAddr := range pciAddrs {
		fName := lockfilePathPrefix + pciAddr

		if err := remove(fName); err != nil {
			if os.IsNotExist(err) {
				continue
			}
			return errors.Wrapf(err, "remove %s", fName)
		}
		removed = append(removed, fName)
	}
	log.Debugf("removed lockfiles: %v", removed)

	return nil
}

// wrapCleanError encapsulates inErr inside any cleanErr.
func wrapCleanError(inErr error, cleanErr error) (outErr error) {
	outErr = inErr

	if cleanErr != nil {
		outErr = errors.Wrap(inErr, cleanErr.Error())
		if outErr == nil {
			outErr = cleanErr
		}
	}

	return
}

// CleanLockfiles removes SPDK lockfiles after binding operations.
func (n *Nvme) CleanLockfiles(log logging.Logger, pciAddrs ...string) error {
	return cleanLockfiles(log, realRemove, pciAddrs...)
}

func pciAddressList(ctrlrs []Controller) []string {
	pciAddrs := make([]string, 0, len(ctrlrs))
	for _, c := range ctrlrs {
		pciAddrs = append(pciAddrs, c.PCIAddr)
	}

	return pciAddrs
}

// Discover NVMe devices accessible by SPDK on a given host.
//
// Calls C.nvme_discover which returns pointers to single linked list of
// ctrlr_t structs. These are converted and returned as Controller slices
// containing any Namespace and DeviceHealth structs. Afterwards remove
// lockfile for each discovered device.
func (n *Nvme) Discover(log logging.Logger) ([]Controller, error) {
	ctrlrs, err := processReturn(C.nvme_discover(), "NVMe Discover(): C.nvme_discover")

	pciAddrs := pciAddressList(ctrlrs)
	log.Debugf("discovered nvme ssds: %v", pciAddrs)

	return ctrlrs, wrapCleanError(err, n.CleanLockfiles(log, pciAddrs...))
}

// DiscoverVmd discovers NVMe SSD PCIe addresses behind VMD
func (n *Nvme) DiscoverVmd(log logging.Logger) (addrs []string, err error) {
	var rc C.int

	addr_buf := C.malloc(C.sizeof_char * 128)
	defer C.free(unsafe.Pointer(addr_buf))

	count := 0
	pci_device := C.spdk_pci_get_first_device()
	for pci_device != nil {
		devType := C.spdk_pci_device_get_type(pci_device)
		log.Debugf("spdk device type %+v", devType)
		if devType != nil && strings.Compare(C.GoString(devType), "vmd") == 0 {
			count += 1
		}

		pci_device = C.spdk_pci_get_next_device(pci_device)
	}

	addrs = make([]string, 0, count)

	i := 0
	pci_device = C.spdk_pci_get_first_device()
	for pci_device != nil {
		devType := C.spdk_pci_device_get_type(pci_device)

		if strings.Compare(C.GoString(devType), "vmd") == 0 {
			rc = C.spdk_pci_addr_fmt((*C.char)(addr_buf), C.sizeof_char*128,
				&pci_device.addr)
			if rc != 0 {
				if err = Rc2err("spdk_pci_addr_fmt", rc); err != nil {
					continue
				}
			}

			if i == count {
				break
			}
			addrs = append(addrs, C.GoString((*C.char)(addr_buf)))
		}

		pci_device = C.spdk_pci_get_next_device(pci_device)
		i += 1
	}

	log.Debugf("discovered nvme ssds behind VMD: %v", addrs)

	return addrs, nil
}

// Format device at given pci address, destructive operation!
//
// Attempt wipe of namespace #1 LBA-0 and falls back to full controller
// format if quick format failed. Afterwards remove lockfile for formatted
// device.
func (n *Nvme) Format(log logging.Logger, ctrlrPciAddr string) (err error) {
	defer func() {
		err = wrapCleanError(err, n.CleanLockfiles(log, ctrlrPciAddr))
	}()

	csPci := C.CString(ctrlrPciAddr)
	defer C.free(unsafe.Pointer(csPci))

	failMsg := "NVMe Format(): C.nvme_"
	wipeMsg := failMsg + "wipe_first_ns()"

	if _, err = processReturn(C.nvme_wipe_first_ns(csPci), wipeMsg); err == nil {
		return // quick format succeeded
	}

	log.Debugf("%s: %s", wipeMsg, err.Error())

	log.Infof("falling back to full format on %s\n", ctrlrPciAddr)
	_, err = processReturn(C.nvme_format(csPci), failMsg+"format()")

	return
}

// Update calls C.nvme_fwupdate to update controller firmware image.
//
// Retrieves image from path and updates given firmware slot/register
// then remove lockfile for updated device.
func (n *Nvme) Update(log logging.Logger, ctrlrPciAddr string, path string, slot int32) (ctrlrs []Controller, err error) {
	csPath := C.CString(path)
	defer C.free(unsafe.Pointer(csPath))

	csPci := C.CString(ctrlrPciAddr)
	defer C.free(unsafe.Pointer(csPci))

	ctrlrs, err = processReturn(C.nvme_fwupdate(csPci, csPath, C.uint(slot)),
		"NVMe Update(): C.nvme_fwupdate")

	err = wrapCleanError(err, n.CleanLockfiles(log, ctrlrPciAddr))

	return
}

// Cleanup unlinks and detaches any controllers or namespaces,
// as well as cleans up optional device health information.
func (n *Nvme) Cleanup() {
	C.nvme_cleanup()
}

// c2GoController is a private translation function
func c2GoController(ctrlr *C.struct_ctrlr_t) Controller {
	return Controller{
		Model:    C.GoString(&ctrlr.model[0]),
		Serial:   C.GoString(&ctrlr.serial[0]),
		PCIAddr:  C.GoString(&ctrlr.pci_addr[0]),
		FWRev:    C.GoString(&ctrlr.fw_rev[0]),
		SocketID: int32(ctrlr.socket_id),
	}
}

// c2GoDeviceHealth is a private translation function
func c2GoDeviceHealth(health *C.struct_dev_health_t) *DeviceHealth {
	return &DeviceHealth{
		Temperature:     uint32(health.temperature),
		TempWarnTime:    uint32(health.warn_temp_time),
		TempCritTime:    uint32(health.crit_temp_time),
		CtrlBusyTime:    uint64(health.ctrl_busy_time),
		PowerCycles:     uint64(health.power_cycles),
		PowerOnHours:    uint64(health.power_on_hours),
		UnsafeShutdowns: uint64(health.unsafe_shutdowns),
		MediaErrors:     uint64(health.media_errors),
		ErrorLogEntries: uint64(health.error_log_entries),
		TempWarn:        bool(health.temp_warning),
		AvailSpareWarn:  bool(health.avail_spare_warning),
		ReliabilityWarn: bool(health.dev_reliabilty_warning),
		ReadOnlyWarn:    bool(health.read_only_warning),
		VolatileWarn:    bool(health.volatile_mem_warning),
	}
}

// c2GoNamespace is a private translation function
func c2GoNamespace(ns *C.struct_ns_t) *Namespace {
	return &Namespace{
		ID:   uint32(ns.id),
		Size: uint64(ns.size),
	}
}

// processReturn parses return structs
func processReturn(retPtr *C.struct_ret_t, failMsg string) (ctrlrs []Controller, err error) {
	if retPtr == nil {
		return nil, errors.Wrap(FaultBindingRetNull, failMsg)
	}

	defer freeReturn(retPtr)

	ctrlrPtr := retPtr.ctrlrs

	if retPtr.rc != 0 {
		err = errors.Wrap(FaultBindingFailed(int(retPtr.rc), C.GoString(&retPtr.err[0])),
			failMsg)

		return
	}

	for ctrlrPtr != nil {
		ctrlr := c2GoController(ctrlrPtr)

		if nsPtr := ctrlrPtr.nss; nsPtr != nil {
			for nsPtr != nil {
				ctrlr.Namespaces = append(ctrlr.Namespaces, c2GoNamespace(nsPtr))
				nsPtr = nsPtr.next
			}
		}

		healthPtr := ctrlrPtr.dev_health
		if healthPtr == nil {
			err = FaultCtrlrNoHealth

			return
		}
		ctrlr.HealthStats = c2GoDeviceHealth(healthPtr)

		ctrlrs = append(ctrlrs, ctrlr)

		ctrlrPtr = ctrlrPtr.next
	}

	return
}

// freeReturn frees memory that was allocated in C
func freeReturn(retPtr *C.struct_ret_t) {
	ctrlr := retPtr.ctrlrs

	for ctrlr != nil {
		ctrlrNext := ctrlr.next

		ns := ctrlr.nss
		for ns != nil {
			nsNext := ns.next
			C.free(unsafe.Pointer(ns))
			ns = nsNext
		}

		if ctrlr.dev_health != nil {
			C.free(unsafe.Pointer(ctrlr.dev_health))
		}

		C.free(unsafe.Pointer(ctrlr))
		ctrlr = ctrlrNext
	}

	C.free(unsafe.Pointer(retPtr))
	retPtr = nil
}
