//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package systemd

import (
	"errors"
)

// ErrSdNotifyNoSocket is the error returned when the NOTIFY_SOCKET does not exist.
var ErrSdNotifyNoSocket = errors.New("No socket")

// Ready sends READY=1 to the systemd notify socket.
func Ready() error {
	return SdNotify("READY=1")
}

// Stopping sends STOPPING=1 to the systemd notify socket.
func Stopping() error {
	return SdNotify("STOPPING=1")
}
