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

// Package spdk provides Go bindings for SPDK
package spdk

// CGO_CFLAGS & CGO_LDFLAGS env vars can be used
// to specify additional dirs.

/*
#cgo LDFLAGS: -lspdk

#include "stdlib.h"
#include "spdk/stdinc.h"
#include "spdk/env.h"
*/
import "C"

import (
	"fmt"
)

// ENV is the interface that provides SPDK environment management.
type ENV interface {
	InitSPDKEnv(int) error
}

// Env is a simple ENV implementation.
type Env struct{}

// rc2err returns an failure if rc != 0.
//
// TODO: If err is already set then it is wrapped,
// otherwise it is ignored. e.g.
// func rc2err(label string, rc C.int, err error) error {
func rc2err(label string, rc C.int) error {
	if rc != 0 {
		if rc < 0 {
			rc = -rc
		}
		// e := errors.Error(rc)
		return fmt.Errorf("%s: %d", label, rc) // e
	}
	return nil
}

// InitSPDKEnv initializes the SPDK environment.
//
// SPDK relies on an abstraction around the local environment
// named env that handles memory allocation and PCI device operations.
// The library must be initialized first.
func (e *Env) InitSPDKEnv(shmID int) (err error) {
	opts := &C.struct_spdk_env_opts{}

	C.spdk_env_opts_init(opts)

	// shmID 0 will be ignored
	if shmID > 0 {
		opts.shm_id = C.int(shmID)
	}

	rc := C.spdk_env_init(opts)
	if err = rc2err("spdk_env_opts_init", rc); err != nil {
		return
	}

	return
}
