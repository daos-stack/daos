//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
	FaultFormatRunningSystem = clientFault(
		code.ClientFormatRunningSystem,
		"storage format invoked on a running system",
		"stop and erase the system, then retry the format operation",
	)
	FaultConfigVMDImbalance = clientFault(
		code.ClientConfigVMDImbalance,
		"different number of backing devices behind each engine's VMD addresses",
		"assign an equal number of NVMe SSDs to each VMD when configuring in BIOS",
	)
)

// IsRetryableConnErr indicates whether the error is a connection error that
// can be retried.
func IsRetryableConnErr(err error) bool {
	if !IsConnErr(err) {
		return false
	}

	f, ok := errors.Cause(err).(*fault.Fault)
	if !ok {
		return false
	}

	return f.Code == code.ClientConnectionRefused ||
		f.Code == code.ClientConnectionClosed
}

// IsConnErr indicates whether the error is a connection error.
func IsConnErr(err error) bool {
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

func FaultRpcTimeout(req deadliner) *fault.Fault {
	timeout := req.getTimeout()
	if timeout == 0 {
		timeout = defaultRequestTimeout
	}
	return clientFault(
		code.ClientRpcTimeout,
		fmt.Sprintf("the %T request timed out after %s", req, timeout),
		"retry the request or check server logs for more information",
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
