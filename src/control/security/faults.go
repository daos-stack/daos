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

const invalidCertResolution = "verify the certificates in the DAOS server, agent, and control configurations are valid, are from the same CA, and are not expired"

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

// FaultInvalidCertFile indicates that a certificate loaded from a file was invalid.
func FaultInvalidCertFile(filePath string, err error) *fault.Fault {
	f := securityFault(
		code.SecurityInvalidCert,
		fmt.Sprintf("certificate at path %q is invalid", filePath),
		invalidCertResolution,
	)
	if err != nil {
		f.Reason = err.Error()
	}
	return f
}

// FaultInvalidCert indicates that a certificate was invalid.
func FaultInvalidCert(err error) *fault.Fault {
	f := securityFault(
		code.SecurityInvalidCert,
		"an invalid x509 certificate was detected",
		invalidCertResolution,
	)
	if err != nil {
		f.Reason = err.Error()
	}
	return f
}

func securityFault(code code.Code, desc, res string) *fault.Fault {
	return &fault.Fault{
		Domain:      "security",
		Code:        code,
		Description: desc,
		Resolution:  res,
	}
}
