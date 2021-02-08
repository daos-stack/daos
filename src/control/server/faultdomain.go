//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"os"
	"os/exec"
	"strings"

	"github.com/pkg/errors"
	"golang.org/x/sys/unix"

	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/system"
)

type hostnameGetterFn func() (string, error)

// getDefaultFaultDomain determines the fault domain that should be used for this
// server if none is externally defined.
func getDefaultFaultDomain(getHostname hostnameGetterFn) (*system.FaultDomain, error) {
	hostname, err := getHostname()
	if err != nil {
		return nil, err
	}
	return newFaultDomainFromConfig(system.FaultDomainSeparator + hostname)
}

// getFaultDomain determines the fault domain for the system.
func getFaultDomain(cfg *config.Server) (*system.FaultDomain, error) {
	if cfg == nil {
		return nil, config.FaultBadConfig
	}

	if cfg.FaultPath != "" && cfg.FaultCb != "" {
		return nil, config.FaultConfigBothFaultPathAndCb
	}

	if cfg.FaultPath != "" {
		return newFaultDomainFromConfig(cfg.FaultPath)
	}

	if cfg.FaultCb != "" {
		return getFaultDomainFromCallback(cfg.FaultCb)
	}

	return getDefaultFaultDomain(os.Hostname)
}

func newFaultDomainFromConfig(domainStr string) (*system.FaultDomain, error) {
	fd, err := system.NewFaultDomainFromString(domainStr)
	if err != nil {
		return nil, config.FaultConfigFaultDomainInvalid
	}
	return fd, nil
}

func getFaultDomainFromCallback(callbackPath string) (*system.FaultDomain, error) {
	if callbackPath == "" {
		return nil, errors.New("no callback path supplied")
	}

	// Fault callback can't be an arbitrary command. Must point to a
	// specific executable file.
	if err := unix.Stat(callbackPath, nil); os.IsNotExist(err) {
		return nil, config.FaultConfigFaultCallbackNotFound
	}

	output, err := exec.Command(callbackPath).Output()
	if os.IsPermission(err) {
		return nil, config.FaultConfigFaultCallbackBadPerms
	} else if err != nil {
		return nil, config.FaultConfigFaultCallbackFailed(err)
	}

	trimmedOutput := strings.TrimSpace(string(output))
	if trimmedOutput == "" {
		return nil, config.FaultConfigFaultCallbackEmpty
	}

	return newFaultDomainFromConfig(trimmedOutput)
}
