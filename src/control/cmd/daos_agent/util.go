//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

/*
// NB: There should only be one set of CFLAGS/LDFLAGS definitions
// for the whole package!
#cgo CFLAGS: -I${SRCDIR}/../../../utils
#cgo LDFLAGS: -lgurt -lcart -ldaos -ldaos_common -lrt

#include "util.h"
*/
import "C"

func apiVersion() string {
	return fmt.Sprintf("%d.%d.%d",
		C.DAOS_API_VERSION_MAJOR,
		C.DAOS_API_VERSION_MINOR,
		C.DAOS_API_VERSION_FIX,
	)
}

func daosError(rc C.int) error {
	if rc == 0 {
		return nil
	}
	return daos.Status(rc)
}
