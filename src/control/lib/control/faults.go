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

package control

import (
	"fmt"

	"github.com/pkg/errors"

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
		code.ClientConfigBadControlPort,
		"invalid control port in configuration",
		"specify a nonzero control port in configuration ('port' parameter) and retry the client application",
	)
	FaultConfigEmptyHostList = clientFault(
		code.ClientConfigEmptyHostList,
		"empty hostlist parameter in configuration",
		"specify a non-empty list of DAOS server addresses in configuration ('hostlist' parameter) and retry the client application",
	)
)

func IsConnectionError(err error) bool {
	f, ok := errors.Cause(err).(*fault.Fault)
	if !ok {
		return false
	}

	for _, connCode := range []code.Code{
		code.ClientConnectionBadHost, code.ClientConnectionClosed,
		code.ClientConnectionNoRoute, code.ClientConnectionRefused,
	} {
		if f.Code == connCode {
			return true
		}
	}

	return false
}

func FaultConnectionBadHost(srvAddr string) *fault.Fault {
	return clientFault(
		code.ClientConnectionBadHost,
		fmt.Sprintf("the server address (%s) could not be resolved", srvAddr),
		"verify that the configured address is correct",
	)
}

func FaultConnectionNoRoute(srvAddr string) *fault.Fault {
	return clientFault(
		code.ClientConnectionNoRoute,
		fmt.Sprintf("the server could not be reached at the configured address (%s)", srvAddr),
		"verify that the configured address is correct and that the server is available on the network",
	)
}

func FaultConnectionRefused(srvAddr string) *fault.Fault {
	return clientFault(
		code.ClientConnectionRefused,
		fmt.Sprintf("the server at %s refused the connection", srvAddr),
		"verify that the configured address is correct and that the server is running",
	)
}

func FaultConnectionClosed(srvAddr string) *fault.Fault {
	return clientFault(
		code.ClientConnectionClosed,
		fmt.Sprintf("the server at %s closed the connection without accepting a request", srvAddr),
		"verify that the configured address and your TLS configuration are correct (or disable transport security)",
	)
}

func clientFault(code code.Code, desc, res string) *fault.Fault {
	return &fault.Fault{
		Domain:      "client",
		Code:        code,
		Description: desc,
		Resolution:  res,
	}
}
