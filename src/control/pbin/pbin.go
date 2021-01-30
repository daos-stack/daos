//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package pbin

import (
	"os"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	// DaosAdminName is the name of the daos_admin privileged helper.
	DaosAdminName = "daos_admin"

	// DaosAdminLogFileEnvVar is the name of the environment variable which
	// can be set to enable non-ERROR logging in the privileged binary.
	DaosAdminLogFileEnvVar = "DAOS_ADMIN_LOG_FILE"

	// DaosFWName is the name of the daos_firmware privileged helper.
	DaosFWName = "daos_firmware"

	// DaosFWLogFileEnvVar is the name of the environment variable that
	// can be set to enable non-ERROR logging in the daos_firmware binary.
	DaosFWLogFileEnvVar = "DAOS_FIRMWARE_LOG_FILE"
)

// PingResp is the response from a privileged helper application to a Ping
// command.
type PingResp struct {
	Version string
	AppName string
}

// CheckHelper attempts to invoke the helper to test for installation/setup
// problems. This function can be used to proactively identify problems and
// avoid console spam from multiple errors.
func CheckHelper(log logging.Logger, helperName string) error {
	fwd := NewForwarder(log, helperName)
	dummy := struct{}{}
	pingRes := PingResp{}

	if err := fwd.SendReq("Ping", dummy, &pingRes); err != nil {
		err = errors.Cause(err)
		switch {
		case fault.IsFault(err):
			return err
		case os.IsNotExist(err), os.IsPermission(err):
			return PrivilegedHelperNotAvailable(helperName)
		default:
			return PrivilegedHelperRequestFailed(err.Error())
		}
	}

	if pingRes.Version != build.DaosVersion {
		return errors.Errorf("version mismatch (server: %s; %s: %s)",
			build.DaosVersion, helperName, pingRes.Version)
	}

	return nil
}
