//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package checker

import (
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
)

func IsIncorrectMemberStates(err error) bool {
	return FaultIncorrectMemberStates("", "").Equals(err)
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

func FaultIncorrectMemberStates(members, expectedStates string) *fault.Fault {
	return checkerFault(
		code.SystemCheckerInvalidMemberStates,
		"members not in expected states ("+expectedStates+"): "+members,
		"restart system after enabling checker mode or administratively exclude members as appropriate",
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
