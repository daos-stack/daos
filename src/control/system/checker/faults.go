//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package checker

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
)

func IsIncorrectMemberStates(err error) bool {
	return fault.IsFaultCode(err, code.SystemCheckerInvalidMemberStates)
}

var (
	FaultCheckerNotEnabled = checkerFault(
		code.SystemCheckerNotEnabled,
		"system checker is not enabled",
		"enable the system checker and try again",
	)
	FaultCheckerEnabled = checkerFault(
		code.SystemCheckerEnabled,
		"system checker is enabled; normal operations are disabled",
		"disable the system checker to enable normal operations",
	)
)

func FaultIncorrectMemberStates(stopRequired bool, members, expectedStates string) *fault.Fault {
	remedy := "enable checker mode"
	if stopRequired {
		remedy = "stop system before enabling checker mode"
	}
	return checkerFault(
		code.SystemCheckerInvalidMemberStates,
		"members not in expected states ("+expectedStates+"): "+members,
		fmt.Sprintf("%s and/or administratively exclude members as appropriate", remedy),
	)
}

func checkerFault(code code.Code, desc, res string) *fault.Fault {
	return &fault.Fault{
		Domain:      "checker",
		Code:        code,
		Description: desc,
		Resolution:  res,
	}
}
