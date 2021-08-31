//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
)

var (
	FaultUnknown = securityFault(
		code.SecurityUnknown,
		"unknown security error",
		"",
	)
)

func FaultMissingCertFile(filePath string) *fault.Fault {
	return securityFault(
		code.SecurityMissingCertFile,
		fmt.Sprintf("certificates are enabled, but %s was not found", filePath),
		"refer to the System Deployment section of the DAOS Admin Guide for information about certificates",
	)
}

func FaultUnreadableCertFile(filePath string) *fault.Fault {
	return securityFault(
		code.SecurityUnreadableCertFile,
		fmt.Sprintf("certificates are enabled, %s was found, but its permissions are incorrect", filePath),
		"refer to the System Deployment section of the DAOS Admin Guide for information about certificates",
	)
}

func securityFault(code code.Code, desc, res string) *fault.Fault {
	return &fault.Fault{
		Domain:      "security",
		Code:        code,
		Description: desc,
		Resolution:  res,
	}
}
