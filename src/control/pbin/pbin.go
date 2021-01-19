//
// (C) Copyright 2019-2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
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

	// DisableReqFwdEnvVar is the name of the environment variable which
	// can be set to disable forwarding requests to the privileged binary.
	DisableReqFwdEnvVar = "DAOS_DISABLE_REQ_FWD"

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
