//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestControl_FaultConnectionNetUnreachable(t *testing.T) {
	err := FaultConnectionNetUnreachable("10.0.0.1")

	test.AssertTrue(t, IsConnErr(err), "expected a connection error")
	test.AssertFalse(t, IsRetryableConnErr(err), "expected the default retry policy to exclude it")
}
