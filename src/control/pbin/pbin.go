//
// (C) Copyright 2019-2022 Intel Corporation.
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
	// DaosPrivHelperName is the name of the privileged helper.
	DaosPrivHelperName = "daos_server_helper"

	// DaosPrivHelperLogFileEnvVar is the name of the environment variable which
	// can be set to enable non-ERROR logging in the privileged helper.
	DaosPrivHelperLogFileEnvVar = "DAOS_HELPER_LOG_FILE"

	// DisableReqFwdEnvVar is the name of the environment variable which
	// can be set to disable forwarding requests to the privileged binary.
	DisableReqFwdEnvVar = "DAOS_DISABLE_REQ_FWD"

	// DaosAdminLogFileEnvVar is the name of the environment variable which
	// can be set to enable non-ERROR logging in the privileged binary.
	DaosAdminLogFileEnvVar = "DAOS_ADMIN_LOG_FILE"

	// DaosFWName is the name of the firmware helper.
	DaosFWName = "daos_firmware_helper"

	// DaosFWLogFileEnvVar is the name of the environment variable that
	// can be set to enable non-ERROR logging in the firmware helper.
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

	if fwd.Disabled {
		return nil
	}

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
