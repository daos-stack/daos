//
// (C) Copyright 2022-2023 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build spdk
// +build spdk

package spdk

// CGO_CFLAGS & CGO_LDFLAGS env vars can be used
// to specify additional dirs.

/*
#cgo CFLAGS: -I .
#cgo LDFLAGS: -L . -lnvme_control
#cgo LDFLAGS: -lspdk_env_dpdk -lspdk_nvme -lspdk_vmd -lspdk_util
#cgo LDFLAGS: -lrte_mempool -lrte_mempool_ring -lrte_bus_pci

#include "stdlib.h"
#include "daos_srv/control.h"
#include "spdk/stdinc.h"
#include "spdk/string.h"
#include "spdk/env.h"
#include "spdk/nvme.h"
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
	"github.com/daos-stack/daos/src/control/server/storage"
)

func realRemove(name string) error {
	return os.Remove(name)
}

// Clean implements the method of the same name for the Nvme interface on the NvmeImpl struct.
func (n *NvmeImpl) Clean(log logging.Logger, pciAddrChecker LockfileAddrCheckFn) ([]string, error) {
	if n == nil {
		return nil, errors.New("nil NvmeImpl")
	}

	return cleanLockfiles(log, n.LocksDir, pciAddrChecker, realRemove)
}

// Discover NVMe devices, including NVMe devices behind VMDs if enabled,
// accessible by SPDK on a given host.
//
// Calls C.nvme_discover which returns pointers to single linked list of
// nvme_ctrlr_t structs. These are converted and returned as Controller slices
// containing any Namespace and DeviceHealth structs.
// Afterwards remove lockfile for each discovered device.
func (n *NvmeImpl) Discover(log logging.Logger) (storage.NvmeControllers, error) {
	if n == nil {
		return nil, errors.New("nil NvmeImpl")
	}

	ctrlrs, errCollect := collectCtrlrs(C.nvme_discover(), "NVMe Discover(): C.nvme_discover")

	pciAddrs := make([]string, 0, len(ctrlrs))
	for _, c := range ctrlrs {
		log.Debugf("nvme ssd scanned: %+v", c)
		pciAddrs = append(pciAddrs, c.PciAddr)
	}

	errRemLocks := cleanKnownLockfiles(log, n, pciAddrs...)

	return ctrlrs, wrapCleanError(errCollect, errRemLocks)
}

// Format devices available through SPDK, destructive operation!
//
// Attempt wipe of each controller namespace's LBA-0.
// Afterwards remove lockfile for each formatted device.
func (n *NvmeImpl) Format(log logging.Logger) ([]*FormatResult, error) {
	if n == nil {
		return nil, errors.New("nil NvmeImpl")
	}

	results, errCollect := collectFormatResults(C.nvme_wipe_namespaces(),
		"NVMe Format(): C.nvme_wipe_namespaces()")

	pciAddrs := resultPCIAddresses(results)
	log.Debugf("formatted nvme ssds: %v", pciAddrs)

	errRemLocks := cleanKnownLockfiles(log, n, pciAddrs...)

	return results, wrapCleanError(errCollect, errRemLocks)
}

// Update updates the firmware image via SPDK in a given slot on the device.
//
// Afterwards remove lockfile for the updated device.
func (n *NvmeImpl) Update(log logging.Logger, ctrlrPciAddr string, path string, slot int32) error {
	if n == nil {
		return errors.New("nil NvmeImpl")
	}

	csPath := C.CString(path)
	defer C.free(unsafe.Pointer(csPath))

	csPci := C.CString(ctrlrPciAddr)
	defer C.free(unsafe.Pointer(csPci))

	_, errCollect := collectCtrlrs(C.nvme_fwupdate(csPci, csPath, C.uint(slot)),
		"NVMe Update(): C.nvme_fwupdate")

	errRemLocks := cleanKnownLockfiles(log, n, ctrlrPciAddr)

	return wrapCleanError(errCollect, errRemLocks)
}

// c2GoController is a private translation function.
func c2GoController(ctrlr *C.struct_nvme_ctrlr_t) *storage.NvmeController {
	return &storage.NvmeController{
		Model:    C.GoString(ctrlr.model),
		Serial:   C.GoString(ctrlr.serial),
		PciAddr:  C.GoString(ctrlr.pci_addr),
		FwRev:    C.GoString(ctrlr.fw_rev),
		SocketID: int32(ctrlr.socket_id),
	}
}

// c2GoDeviceHealth is a private translation function.
func c2GoDeviceHealth(hs *C.struct_nvme_stats) *storage.NvmeHealth {
	return &storage.NvmeHealth{
		TempWarnTime:            uint32(hs.warn_temp_time),
		TempCritTime:            uint32(hs.crit_temp_time),
		CtrlBusyTime:            uint64(hs.ctrl_busy_time),
		PowerCycles:             uint64(hs.power_cycles),
		PowerOnHours:            uint64(hs.power_on_hours),
		UnsafeShutdowns:         uint64(hs.unsafe_shutdowns),
		MediaErrors:             uint64(hs.media_errs),
		ErrorLogEntries:         uint64(hs.err_log_entries),
		Temperature:             uint32(hs.temperature),
		TempWarn:                bool(hs.temp_warn),
		AvailSpareWarn:          bool(hs.avail_spare_warn),
		ReliabilityWarn:         bool(hs.dev_reliability_warn),
		ReadOnlyWarn:            bool(hs.read_only_warn),
		VolatileWarn:            bool(hs.volatile_mem_warn),
		ProgFailCntNorm:         uint8(hs.program_fail_cnt_norm),
		ProgFailCntRaw:          uint64(hs.program_fail_cnt_raw),
		EraseFailCntNorm:        uint8(hs.erase_fail_cnt_norm),
		EraseFailCntRaw:         uint64(hs.erase_fail_cnt_raw),
		WearLevelingCntNorm:     uint8(hs.wear_leveling_cnt_norm),
		WearLevelingCntMin:      uint16(hs.wear_leveling_cnt_min),
		WearLevelingCntMax:      uint16(hs.wear_leveling_cnt_max),
		WearLevelingCntAvg:      uint16(hs.wear_leveling_cnt_avg),
		EndtoendErrCntRaw:       uint64(hs.endtoend_err_cnt_raw),
		CrcErrCntRaw:            uint64(hs.crc_err_cnt_raw),
		MediaWearRaw:            uint64(hs.media_wear_raw),
		HostReadsRaw:            uint64(hs.host_reads_raw),
		WorkloadTimerRaw:        uint64(hs.workload_timer_raw),
		ThermalThrottleStatus:   uint8(hs.thermal_throttle_status),
		ThermalThrottleEventCnt: uint64(hs.thermal_throttle_event_cnt),
		RetryBufferOverflowCnt:  uint64(hs.retry_buffer_overflow_cnt),
		PllLockLossCnt:          uint64(hs.pll_lock_loss_cnt),
		NandBytesWritten:        uint64(hs.nand_bytes_written),
		HostBytesWritten:        uint64(hs.host_bytes_written),
	}
}

// c2GoNamespace is a private translation function.
func c2GoNamespace(ns *C.struct_nvme_ns_t) *storage.NvmeNamespace {
	return &storage.NvmeNamespace{
		ID:   uint32(ns.id),
		Size: uint64(ns.size),
	}
}

// c2GoFormatResult is a private translation function.
func c2GoFormatResult(fmtResult *C.struct_wipe_res_t) *FormatResult {
	var err error
	if fmtResult.rc != 0 {
		err = rc2err(C.GoString(&fmtResult.info[0]), fmtResult.rc)
	}

	return &FormatResult{
		CtrlrPCIAddr: C.GoString(&fmtResult.ctrlr_pci_addr[0]),
		NsID:         uint32(fmtResult.ns_id),
		Err:          err,
	}
}

// clean deallocates memory in return structure and frees the pointer.
func clean(retPtr *C.struct_ret_t) {
	C.clean_ret(retPtr)
	C.free(unsafe.Pointer(retPtr))
}

// checkRet returns fault if ret_t struct input is nil or rc is non-zero.
func checkRet(retPtr *C.struct_ret_t, msgFail string) error {
	if retPtr == nil {
		return FaultBindingRetNull(msgFail)
	}

	if retPtr.rc != 0 {
		var msgs []string

		if msgFail != "" {
			msgs = append(msgs, msgFail)
		}

		msgInfo := C.GoString(&retPtr.info[0])
		if msgInfo != "" {
			msgs = append(msgs, msgInfo)
		}

		msgErrno := C.GoString(C.spdk_strerror(-retPtr.rc))
		if msgErrno != "" {
			msgs = append(msgs, msgErrno)
		}

		return FaultBindingFailed(int(retPtr.rc), strings.Join(msgs, ": "))
	}

	return nil
}

// collectCtrlrs parses return struct to collect slice of nvme.Controller.
func collectCtrlrs(retPtr *C.struct_ret_t, msgFail string) (storage.NvmeControllers, error) {
	defer clean(retPtr)

	if err := checkRet(retPtr, msgFail); err != nil {
		return nil, err
	}

	var ctrlrs storage.NvmeControllers
	ctrlrPtr := retPtr.ctrlrs
	for ctrlrPtr != nil {
		ctrlr := c2GoController(ctrlrPtr)

		if nsPtr := ctrlrPtr.nss; nsPtr != nil {
			for nsPtr != nil {
				ctrlr.Namespaces = append(ctrlr.Namespaces,
					c2GoNamespace(nsPtr))
				nsPtr = nsPtr.next
			}
		}

		healthPtr := ctrlrPtr.stats
		if healthPtr == nil {
			return ctrlrs, errors.Wrapf(FaultCtrlrNoHealth, "ctrlr %s", ctrlr.PciAddr)
		}
		ctrlr.HealthStats = c2GoDeviceHealth(healthPtr)

		ctrlrs = append(ctrlrs, ctrlr)

		ctrlrPtr = ctrlrPtr.next
	}

	return ctrlrs, nil
}

// collectFormatResults parses return struct to collect slice of
// nvme.FormatResult.
func collectFormatResults(retPtr *C.struct_ret_t, msgFail string) ([]*FormatResult, error) {
	defer clean(retPtr)

	if err := checkRet(retPtr, msgFail); err != nil {
		return nil, err
	}

	var fmtResults []*FormatResult
	fmtResult := retPtr.wipe_results
	for fmtResult != nil {
		fmtResults = append(fmtResults, c2GoFormatResult(fmtResult))
		fmtResult = fmtResult.next
	}

	return fmtResults, nil
}
