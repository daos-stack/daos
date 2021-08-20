//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Package spdk provides Go bindings for SPDK
package spdk

// CGO_CFLAGS & CGO_LDFLAGS env vars can be used
// to specify additional dirs.

/*
#cgo CFLAGS: -I .
#cgo LDFLAGS: -L . -lnvme_control
#cgo LDFLAGS: -lspdk_env_dpdk -lspdk_nvme -lspdk_vmd -lrte_mempool
#cgo LDFLAGS: -lrte_mempool_ring -lrte_bus_pci

#include "stdlib.h"
#include "daos_srv/control.h"
#include "spdk/stdinc.h"
#include "spdk/string.h"
#include "spdk/env.h"
#include "spdk/nvme.h"
#include "spdk/vmd.h"
#include "include/nvme_control.h"
#include "include/nvme_control_common.h"

static char **makeCStringArray(int size) {
        return calloc(sizeof(char*), size);
}

static void setArrayString(char **a, char *s, int n) {
        a[n] = s;
}

static void freeCStringArray(char **a, int size) {
        int i;
        for (i = 0; i < size; i++)
                free(a[i]);
        free(a);
}
*/
import "C"

import (
	"fmt"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

// Env is the interface that provides SPDK environment management.
type Env interface {
	InitSPDKEnv(logging.Logger, *EnvOptions) error
	FiniSPDKEnv(logging.Logger, *EnvOptions)
}

// EnvImpl is a an implementation of the Env interface.
type EnvImpl struct{}

// Rc2err returns error from label and rc.
func Rc2err(label string, rc C.int) error {
	return fmt.Errorf("%s: %d", label, rc)
}

// EnvOptions describe parameters to be used when initializing a processes
// SPDK environment.
type EnvOptions struct {
	PCIAllowList []string // restrict SPDK device access
	EnableVMD    bool     // flag if VMD functionality should be enabled
}

func (o *EnvOptions) sanitizeAllowList(log logging.Logger) error {
	if !o.EnableVMD {
		// DPDK will not accept VMD backing device addresses
		// so convert to VMD address
		newAllowList, err := revertBackingToVmd(log, o.PCIAllowList)
		if err != nil {
			return err
		}
		o.PCIAllowList = newAllowList
	}

	return nil
}

// revertBackingToVmd converts VMD backing device PCI addresses (with the VMD
// address encoded in the domain component of the PCI address) back to the PCI
// address of the VMD e.g. [5d0505:01:00.0, 5d0505:03:00.0] -> [0000:5d:05.5].
//
// Many assumptions are made as to the input and output PCI address structure in
// the conversion.
func revertBackingToVmd(log logging.Logger, pciAddrs []string) ([]string, error) {
	var outAddrs []string

	for _, inAddr := range pciAddrs {
		domain, _, _, _, err := common.ParsePCIAddress(inAddr)
		if err != nil {
			return nil, err
		}
		if domain == 0 {
			outAddrs = append(outAddrs, inAddr)
			continue
		}

		domainStr := fmt.Sprintf("%x", domain)
		if len(domainStr) != 6 {
			return nil, errors.New("unexpected length of domain")
		}

		outAddr := fmt.Sprintf("0000:%s:%s.%s",
			domainStr[:2], domainStr[2:4], domainStr[5:])
		if !common.Includes(outAddrs, outAddr) {
			log.Debugf("replacing backing device %s with vmd %s", inAddr, outAddr)
			outAddrs = append(outAddrs, outAddr)
		}
	}

	return outAddrs, nil
}

// InitSPDKEnv initializes the SPDK environment.
//
// SPDK relies on an abstraction around the local environment
// named env that handles memory allocation and PCI device operations.
// The library must be initialized first.
func (e *EnvImpl) InitSPDKEnv(log logging.Logger, opts *EnvOptions) error {
	log.Debugf("spdk init go opts: %+v", opts)

	if err := opts.sanitizeAllowList(log); err != nil {
		return errors.Wrap(err, "sanitizing PCI include list")
	}

	// Build C array in Go from opts.PCIAllowList []string
	cAllowList := C.makeCStringArray(C.int(len(opts.PCIAllowList)))
	defer C.freeCStringArray(cAllowList, C.int(len(opts.PCIAllowList)))

	for i, s := range opts.PCIAllowList {
		C.setArrayString(cAllowList, C.CString(s), C.int(i))
	}

	// Disable DPDK telemetry to avoid socket file clashes and quiet DPDK
	// logging by setting level to ERROR.
	envCtx := C.CString("--log-level=lib.eal:4 --log-level=lib.user1:4 --no-telemetry")
	defer C.free(unsafe.Pointer(envCtx))

	retPtr := C.daos_spdk_init(0, envCtx, C.ulong(len(opts.PCIAllowList)),
		cAllowList)
	if err := checkRet(retPtr, "daos_spdk_init()"); err != nil {
		return err
	}
	clean(retPtr)

	// TODO DAOS-8040: re-enable VMD
	//	if !opts.EnableVMD {
	//		return nil
	//	}
	//
	//	if rc := C.spdk_vmd_init(); rc != 0 {
	//		return Rc2err("spdk_vmd_init()", rc)
	//	}

	return nil
}

// FiniSPDKEnv initializes the SPDK environment.
func (e *EnvImpl) FiniSPDKEnv(log logging.Logger, opts *EnvOptions) {
	log.Debugf("spdk fini go opts: %+v", opts)

	C.spdk_env_fini()

	// TODO: enable when vmd_fini supported in daos spdk version
	//	if !opts.EnableVMD {
	//		return nil
	//	}
	//
	//	if rc := C.spdk_vmd_fini(); rc != 0 {
	//		return Rc2err("spdk_vmd_fini()", rc)
	//	}
}
