//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
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
		return getFaultDomainFromCallback(cfg.FaultCb, build.ConfigDir)
	}

	return getDefaultFaultDomain(os.Hostname)
}

func newFaultDomainFromConfig(domainStr string) (*system.FaultDomain, error) {
	fd, err := system.NewFaultDomainFromString(domainStr)
	if err != nil || fd.NumLevels() == 0 {
		return nil, config.FaultConfigFaultDomainInvalid
	}
	// TODO DAOS-6353: remove when multiple layers supported
	if fd.NumLevels() > 2 {
		return nil, config.FaultConfigTooManyLayersInFaultDomain
	}
	return fd, nil
}

func getFaultDomainFromCallback(path, requiredDir string) (*system.FaultDomain, error) {
	if err := checkFaultDomainCallback(path, requiredDir); err != nil {
		return nil, err
	}

	output, err := exec.Command(path).Output()
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

func checkFaultDomainCallback(path, requiredDir string) error {
	if path == "" {
		return errors.New("no callback path supplied")
	}

	// Must be under the required directory
	absDir, err := filepath.Abs(requiredDir)
	if err != nil {
		return err
	}
	absScriptPath, err := filepath.Abs(path)
	if err != nil {
		return err
	}
	if !strings.HasPrefix(absScriptPath, absDir) {
		return config.FaultConfigFaultCallbackInsecure(absDir)
	}

	// Fault callback can't be an arbitrary command. Must point to a
	// specific executable file.
	fi, err := os.Lstat(path)
	if err != nil {
		if os.IsPermission(err) {
			return config.FaultConfigFaultCallbackBadPerms
		}
		return config.FaultConfigFaultCallbackNotFound
	}

	// Symlinks and setuid scripts are potentially dangerous and shouldn't
	// be automatically run.
	mode := fi.Mode()
	if mode&os.ModeSymlink != 0 || mode&os.ModeSetuid != 0 {
		return config.FaultConfigFaultCallbackInsecure(absDir)
	}

	// Script shouldn't be writable by non-owners.
	if mode.Perm()&0022 != 0 {
		return config.FaultConfigFaultCallbackInsecure(absDir)
	}

	return nil
}
