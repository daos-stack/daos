//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

import (
	"sync"

	"github.com/daos-stack/daos/src/control/lib/daos"
)

/*
#include <daos.h>

#cgo LDFLAGS: -lcart -lgurt -ldaos -ldaos_common
*/
import "C"

type (
	api struct {
		sync.RWMutex
		initialized bool
	}
)

func daosError(rc C.int) error {
	return daos.ErrorFromRC(int(rc))
}

func (api *api) isInitialized() bool {
	api.RLock()
	defer api.RUnlock()
	return api.initialized
}

// Init performs DAOS API initialization steps and returns a closure
// to be called before application exit.
func (api *api) Init(initLogging bool) (func(), error) {
	api.Lock()
	defer api.Unlock()

	stubFini := func() {}
	if api.initialized {
		return stubFini, daos.Already
	}

	logFini := stubFini
	if initLogging {
		fini, err := daos.InitLogging(daos.DefaultErrorMask)
		if err != nil {
			return stubFini, err
		}
		logFini = fini
	}

	if err := daosError(daos_init()); err != nil {
		return stubFini, err
	}
	api.initialized = true

	return func() { api.Fini(); logFini() }, nil
}

// Fini releases resources obtained during DAOS API initialization.
func (api *api) Fini() {
	api.Lock()
	defer api.Unlock()

	if !api.initialized {
		return
	}

	daos_fini()
	api.initialized = false
}
