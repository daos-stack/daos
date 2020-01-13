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
package client

import (
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
)

var (
	FaultUnknown = clientFault(
		code.ClientUnknown,
		"unknown client error",
		"",
	)
	FaultConfigBadControlPort = clientFault(
		code.ClientConfigBadParam,
		"invalid control port in configuration",
		"specify a nonzero control port in configuration ('port' parameter) and retry the client application",
	)
	FaultConfigBadAccessPoints = clientFault(
		code.ClientConfigBadParam,
		"invalid list of access points in configuration",
		"only a single access point is currently supported, specify only one and retry the client application",
	)
)

func clientFault(code code.Code, desc, res string) *fault.Fault {
	return &fault.Fault{
		Domain:      "client",
		Code:        code,
		Description: desc,
		Resolution:  res,
	}
}
