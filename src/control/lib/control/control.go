//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"io/ioutil"

	"github.com/daos-stack/daos/src/control/logging"
)

// defaultLogger is used to provide a valid logger when none has
// been supplied.
var defaultLogger debugLogger = logging.NewCombinedLogger("", ioutil.Discard)

type (
	// debugLogger defines a debug-only logging interface.
	debugLogger interface {
		Debug(string)
		Debugf(string, ...interface{})
	}
)
