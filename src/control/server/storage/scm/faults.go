//
// (C) Copyright 2019 Intel Corporation.
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
package scm

import (
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
)

var (
	FaultUnknown = scmFault(
		code.ScmUnknown, "unknown scm error", "",
	)
	FaultDiscoveryFailed = scmFault(
		code.ScmUnknown, "module discovery failed", "",
	)
	FaultFormatInvalidSize = scmFault(
		code.ScmFormatBadParam, "format request must specify a size greater than 0", "",
	)
	FaultFormatInvalidDeviceCount = scmFault(
		code.ScmFormatBadParam, "format request must have exactly 1 dcpm device", "",
	)
	FaultFormatMissingMountpoint = scmFault(
		code.ScmFormatBadParam, "format request must specify mountpoint", "",
	)
	FaultFormatMissingParam = scmFault(
		code.ScmFormatBadParam, "format request must have ramdisk or dcpm parameter", "",
	)
	FaultFormatConflictingParam = scmFault(
		code.ScmFormatBadParam,
		"format request must not have both ramdisk and dcpm parameters", "",
	)
	FaultFormatNoReformat = scmFault(
		code.StorageAlreadyFormatted,
		"format request for already-formatted storage and reformat not specified",
		"retry the operation with reformat option to overwrite existing format",
	)
	FaultDeviceAlreadyMounted = scmFault(
		code.StorageDeviceAlreadyMounted,
		"request included already-mounted device",
		"unmount the device and retry the operation",
	)
	FaultTargetAlreadyMounted = scmFault(
		code.StorageDeviceAlreadyMounted,
		"request included already-mounted mount target (cannot double-mount)",
		"unmount the target and retry the operation",
	)
	FaultFormatMissingDevice = scmFault(
		code.ScmFormatBadParam,
		"configured SCM device does not exist",
		"check the configured value and/or perform the SCM preparation procedure",
	)
	FaultMissingNdctl = scmFault(
		code.MissingSoftwareDependency,
		"ndctl utility not found", "install the ndctl software for your OS",
	)
)

func scmFault(code code.Code, desc, res string) *fault.Fault {
	return &fault.Fault{
		Domain:      "scm",
		Code:        code,
		Description: desc,
		Resolution:  res,
	}
}
