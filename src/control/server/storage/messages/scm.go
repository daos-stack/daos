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
package storage_messages

const (
	MsgScmRebootRequired   = "A reboot is required to process new memory allocation goals."
	MsgScmNoModules        = "no scm modules to prepare"
	MsgScmNotInited        = "scm storage could not be accessed"
	MsgScmAlreadyFormatted = "scm storage has already been formatted and " +
		"reformat not implemented"
	MsgScmMountEmpty = "scm mount must be specified in config"
	MsgScmBadDevList = "expecting one scm dcpm pmem device " +
		"per-server in config"
	MsgScmDevEmpty          = "scm dcpm device list must contain path"
	MsgScmClassNotSupported = "operation unsupported on scm class"
	MsgIpmctlDiscoverFail   = "ipmctl module discovery"
	MsgScmUpdateNotImpl     = "scm firmware update not supported"
)
