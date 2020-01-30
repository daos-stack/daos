//
// (C) Copyright 2020 Intel Corporation.
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
package server

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
)

var (
	FaultUnknown = serverFault(
		code.ServerUnknown,
		"unknown control server error", "")
	FaultBadConfig = serverFault(
		code.ServerBadConfig,
		"insufficient information in configuration",
		"supply path to valid configuration file, use examples for reference",
	)
	FaultConfigNoPath = serverFault(
		code.ServerNoConfigPath,
		"configuration file path not set",
		"supply the path to a server configuration file when restarting the control server with commandline option '-o'",
	)
	FaultConfigBadControlPort = serverFault(
		code.ServerConfigBadControlPort,
		"invalid control port in configuration",
		"specify a nonzero control port in configuration ('port' parameter) and restart the control server",
	)
	FaultConfigBadAccessPoints = serverFault(
		code.ServerConfigBadAccessPoints,
		"invalid list of access points in configuration",
		"only a single access point is currently supported, specify only one and restart the control server",
	)
	FaultConfigNoProvider = serverFault(
		code.ServerConfigBadProvider,
		"provider not specified in configuration",
		"specify a valid network provider in configuration ('provider' parameter) and restart the control server",
	)
	FaultConfigNoServers = serverFault(
		code.ServerConfigNoServers,
		"no DAOS IO Servers specified in configuration",
		"specify at least one IO Server configuration ('servers' list parameter) and restart the control server",
	)
)

func FaultScmUnmanaged(mntPoint string) *fault.Fault {
	return serverFault(
		code.ServerScmUnmanaged,
		fmt.Sprintf("the SCM mountpoint at %s is unavailable and can't be created/mounted", mntPoint),
		fmt.Sprintf("manually create %s or remove --recreate-superblocks from the server arguments", mntPoint),
	)
}

func serverFault(code code.Code, desc, res string) *fault.Fault {
	return &fault.Fault{
		Domain:      "server",
		Code:        code,
		Description: desc,
		Resolution:  res,
	}
}
