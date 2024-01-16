//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build spdk
// +build spdk

// Package spdk provides Go bindings for SPDK
package spdk

// CGO_CFLAGS & CGO_LDFLAGS env vars can be used
// to specify additional dirs.

/*
#cgo CFLAGS: -I .
#cgo LDFLAGS: -L . -lnvme_control
#cgo LDFLAGS: -lspdk_util -lspdk_log -lspdk_env_dpdk -lspdk_nvme -lspdk_vmd
#cgo LDFLAGS: -lrte_mempool -lrte_mempool_ring -lrte_bus_pci

#include "stdlib.h"
#include "daos_srv/control.h"
#include "spdk/stdinc.h"
#include "spdk/string.h"
#include "spdk/log.h"
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

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

// rc2err returns error from label and rc.
func rc2err(label string, rc C.int) error {
	msgErrno := C.GoString(C.spdk_strerror(-rc))

	if msgErrno != "" {
		return fmt.Errorf("%s: %s (rc=%d)", label, msgErrno, rc)
	}
	return fmt.Errorf("%s: rc=%d", label, rc)
}

// InitSPDKEnv initializes the SPDK environment.
//
// SPDK relies on an abstraction around the local environment
// named env that handles memory allocation and PCI device operations.
// The library must be initialized first.
func (ei *EnvImpl) InitSPDKEnv(log logging.Logger, opts *EnvOptions) error {
	if ei == nil {
		return errors.New("nil EnvImpl")
	}
	if opts == nil {
		return errors.New("nil EnvOptions")
	}
	if opts.PCIAllowList == nil {
		opts.PCIAllowList = hardware.MustNewPCIAddressSet()
	}

	log.Debugf("spdk init go opts: %+v", opts)

	// Only print error and more severe to stderr.
	C.spdk_log_set_print_level(C.SPDK_LOG_ERROR)

	if err := opts.sanitizeAllowList(); err != nil {
		return errors.Wrap(err, "sanitizing PCI include list")
	}

	// Build C array in Go from opts.PCIAllowList []string
	cAllowList := C.makeCStringArray(C.int(opts.PCIAllowList.Len()))
	defer C.freeCStringArray(cAllowList, C.int(opts.PCIAllowList.Len()))

	for i, s := range opts.PCIAllowList.Strings() {
		C.setArrayString(cAllowList, C.CString(s), C.int(i))
	}

	envCtx := C.dpdk_cli_override_opts

	retPtr := C.daos_spdk_init(0, envCtx, C.ulong(opts.PCIAllowList.Len()),
		cAllowList)
	defer clean(retPtr)

	if err := checkRet(retPtr, "daos_spdk_init()"); err != nil {
		return err
	}

	if opts.EnableVMD {
		if rc := C.spdk_vmd_init(); rc != 0 {
			return rc2err("spdk_vmd_init()", rc)
		}
	}

	return nil
}

// FiniSPDKEnv initializes the SPDK environment.
func (ei *EnvImpl) FiniSPDKEnv(log logging.Logger, opts *EnvOptions) {
	if ei == nil {
		return
	}
	if opts == nil {
		return
	}

	log.Debugf("spdk fini go opts: %+v", opts)

	if opts.EnableVMD {
		C.spdk_vmd_fini()
	}

	C.spdk_env_fini()
}
