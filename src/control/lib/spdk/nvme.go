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
	"path"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
)

const lockfilePrefix = "/tmp/spdk_pci_lock_"

// NVME is the interface that provides SPDK NVMe functionality.
type NVME interface {
	// Discover NVMe controllers and namespaces, and device health info
	Discover(logging.Logger) ([]Controller, error)
	// Format NVMe controller namespaces
	Format(logging.Logger, string) error
	// Cleanup NVMe object references
	Cleanup()
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
	ID   int32
	Size int32
}

// DeviceHealth struct mirrors C.struct_dev_health_t
// and describes the raw SPDK device health stats
// of a controller (NVMe SSD).
type DeviceHealth struct {
	Temp            uint32
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

func (n *Nvme) cleanLockFiles(log logging.Logger, pciAddrs ...string) {
	log.Debugf("removing lockfiles: %v", pciAddrs)

	for _, pciAddr := range pciAddrs {
		fName := path.Join(lockfilePrefix, pciAddr)

		err := os.Remove(fName)
		if err != nil && !os.IsNotExist(err) {
			log.Errorf("remove %s: %s", fName, err)
			continue
		}

		log.Debugf("%s removed", fName)
	}
}

// Discover calls C.nvme_discover which returns
// pointers to single linked list of ctrlr_t, ns_t and
// dev_health_t structs.
// These are converted to slices of Controller, Namespace
// and DeviceHealth structs.
func (n *Nvme) Discover(log logging.Logger) ([]Controller, error) {
	failLocation := "NVMe Discover(): C.nvme_discover"

	retPtr := C.nvme_discover()
	if retPtr == nil {
		return nil, errors.Errorf("%s unexpectedly returned NULL",
			failLocation)
	}

	ctrlrs, err := processReturn(retPtr, failLocation)
	if err != nil {
		return nil, err
	}

	ctrlrPciAddrs := make([]string, 0, len(ctrlrs))
	for _, c := range ctrlrs {
		ctrlrPciAddrs = append(ctrlrPciAddrs, c.PCIAddr)
	}
	log.Debugf("discovered nvme ssds: %v", ctrlrPciAddrs)

	// remove lock file for each discovered device
	n.cleanLockFiles(log, ctrlrPciAddrs...)

	return ctrlrs, err
}

// Format device at given pci address, destructive operation!
func (n *Nvme) Format(log logging.Logger, ctrlrPciAddr string) error {
	// remove lock file for each discovered device
	defer n.cleanLockFiles(log, ctrlrPciAddr)

	csPci := C.CString(ctrlrPciAddr)
	defer C.free(unsafe.Pointer(csPci))

	failLocation := "NVMe Format(): C.nvme_"

	// attempt wipe of namespace #1 LBA-0
	log.Debugf("attempting quick format on %s\n", ctrlrPciAddr)
	retPtr := C.nvme_wipe_first_ns(csPci)
	if retPtr == nil {
		return errors.Errorf("%swipe_first_ns() unexpectedly returned NULL",
			failLocation)
	}
	if retPtr.rc == 0 {
		return nil // quick format succeeded
	}

	log.Errorf("%swipe_first_ns() failed, rc: %d, %s",
		failLocation, retPtr.rc, C.GoString(&retPtr.err[0]))

	// fall back to full controller format if quick format failed
	log.Infof("falling back to full format on %s\n", ctrlrPciAddr)
	retPtr = C.nvme_format(csPci)
	if retPtr == nil {
		return errors.Errorf("%sformat() unexpectedly returned NULL",
			failLocation)
	}
	if retPtr.rc != 0 {
		return errors.Errorf("%sformat() failed, rc: %d, %s",
			failLocation, retPtr.rc, C.GoString(&retPtr.err[0]))
	}

	return nil
}

// Update calls C.nvme_fwupdate to update controller firmware image.
// Retrieves image from path and updates given firmware slot/register.
func (n *Nvme) Update(log logging.Logger, ctrlrPciAddr string, path string, slot int32) ([]Controller, error) {
	// remove lock file for each discovered device
	defer n.cleanLockFiles(log, ctrlrPciAddr)

	csPath := C.CString(path)
	defer C.free(unsafe.Pointer(csPath))

	csPci := C.CString(ctrlrPciAddr)
	defer C.free(unsafe.Pointer(csPci))

	failLocation := "NVMe Update(): C.nvme_fwupdate"

	retPtr := C.nvme_fwupdate(csPci, csPath, C.uint(slot))
	if retPtr == nil {
		return nil, errors.Errorf("%s unexpectedly returned NULL",
			failLocation)
	}

	return processReturn(retPtr, failLocation)
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
		Temp:            uint32(health.temperature),
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
		ID:   int32(ns.id),
		Size: int32(ns.size),
	}
}

// processReturn parses return structs
func processReturn(retPtr *C.struct_ret_t, failLocation string) (ctrlrs []Controller, err error) {
	if retPtr == nil {
		return nil, errors.New("empty return value")
	}

	if retPtr.rc != 0 {
		cleanReturn(retPtr)

		return nil, errors.Errorf("%s failed, rc: %d, %s",
			failLocation, retPtr.rc, C.GoString(&retPtr.err[0]))
	}

	ctrlrPtr := retPtr.ctrlrs
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
			return nil, errors.New("empty health stats")
		}
		ctrlr.HealthStats = c2GoDeviceHealth(healthPtr)

		ctrlrs = append(ctrlrs, ctrlr)

		ctrlrPtr = ctrlrPtr.next
	}

	cleanReturn(retPtr)

	return ctrlrs, nil
}

// cleanReturn frees memory that was allocated in C
func cleanReturn(retPtr *C.struct_ret_t) {
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
