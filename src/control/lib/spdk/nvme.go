//
// (C) Copyright 2018-2019 Intel Corporation.
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
	"errors"
	"fmt"
	"unsafe"
)

// NVME is the interface that provides SPDK NVMe functionality.
type NVME interface {
	// Discover NVMe controllers and namespaces, and device health info
	Discover() ([]Controller, error)
	// Format NVMe controller namespaces
	Format(ctrlrPciAddr string) ([]Controller, error)
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
	Model      string
	Serial     string
	PCIAddr    string
	FWRev      string
	SocketID   int32
	Namespaces []*Namespace
	Health     *DeviceHealth
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

// Discover calls C.nvme_discover which returns
// pointers to single linked list of ctrlr_t, ns_t and
// dev_health_t structs.
// These are converted to slices of Controller, Namespace
// and DeviceHealth structs.
func (n *Nvme) Discover() ([]Controller, error) {
	failLocation := "NVMe Discover(): C.nvme_discover"

	if retPtr := C.nvme_discover(); retPtr != nil {
		return processReturn(retPtr, failLocation)
	}

	return nil, fmt.Errorf("%s unexpectedly returned NULL", failLocation)
}

// Format device at given pci address, destructive operation!
func (n *Nvme) Format(ctrlrPciAddr string) ([]Controller, error) {
	csPci := C.CString(ctrlrPciAddr)
	defer C.free(unsafe.Pointer(csPci))

	failLocation := "NVMe Format(): C.nvme_format"

	retPtr := C.nvme_format(csPci)
	if retPtr != nil {
		return processReturn(retPtr, failLocation)
	}

	return nil, fmt.Errorf("%s unexpectedly returned NULL", failLocation)
}

// Update calls C.nvme_fwupdate to update controller firmware image.
// Retrieves image from path and updates given firmware slot/register.
func (n *Nvme) Update(ctrlrPciAddr string, path string, slot int32) ([]Controller, error) {
	csPath := C.CString(path)
	defer C.free(unsafe.Pointer(csPath))

	csPci := C.CString(ctrlrPciAddr)
	defer C.free(unsafe.Pointer(csPci))

	failLocation := "NVMe Update(): C.nvme_fwupdate"

	retPtr := C.nvme_fwupdate(csPci, csPath, C.uint(slot))
	if retPtr != nil {
		return processReturn(retPtr, failLocation)
	}

	return nil, fmt.Errorf("%s unexpectedly returned NULL", failLocation)
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
	pciMap := make(map[string]*Controller)

	defer C.free(unsafe.Pointer(retPtr))

	if retPtr.rc == 0 {
		ctrlrPtr := retPtr.ctrlrs
		for ctrlrPtr != nil {
			defer C.free(unsafe.Pointer(ctrlrPtr))
			ctrlrs = append(ctrlrs, c2GoController(ctrlrPtr))
			ctrlrRef := &ctrlrs[len(ctrlrs)-1]

			if healthPtr := ctrlrPtr.dev_health; healthPtr != nil {
				defer C.free(unsafe.Pointer(healthPtr))
				ctrlrRef.Health = c2GoDeviceHealth(healthPtr)
			}

			// check doesn't already exist and store reference to add amespaces later
			if _, exists := pciMap[ctrlrRef.PCIAddr]; exists {
				return nil, errors.New("multiple entries for the same PCI address")
			}
			pciMap[ctrlrRef.PCIAddr] = ctrlrRef
			ctrlrPtr = ctrlrPtr.next
		}

		nsPtr := retPtr.nss
		for nsPtr != nil {
			defer C.free(unsafe.Pointer(nsPtr))
			pciAddr := C.GoString(&nsPtr.ctrlr_pci_addr[0])

			ctrlrRef, exists := pciMap[pciAddr]
			if !exists || ctrlrRef == nil {
				return nil, errors.New("failed to find parent controller for namespace")
			}
			ctrlrRef.Namespaces = append(ctrlrRef.Namespaces, c2GoNamespace(nsPtr))
			nsPtr = nsPtr.next
		}

		return ctrlrs, nil
	}

	return nil, fmt.Errorf("%s failed, rc: %d, %s", failLocation, retPtr.rc, C.GoString(&retPtr.err[0]))
}
