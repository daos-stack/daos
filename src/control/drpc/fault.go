//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package drpc

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
)

// FaultSocketFileInUse indicates that the dRPC socket file was already in use when we tried
// to start the dRPC server.
func FaultSocketFileInUse(path string) *fault.Fault {
	return &fault.Fault{
		Domain:      "drpc",
		Code:        code.SocketFileInUse,
		Description: fmt.Sprintf("Configured dRPC socket file '%s' is already in use.", path),
		Reason:      "dRPC socket file already in use",
		Resolution: "If another process is using the socket file, configure a different socket directory. " +
			"Otherwise, delete the existing socket file and try again.",
	}
}
