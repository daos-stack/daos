//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package config

import (
	"github.com/daos-stack/daos/src/control/server/engine"
)

// ServerLegacy describes old configuration options that should be supported for backward
// compatibility and may be deprecated in future releases.
// See utils/config/daos_server.yml for parameter descriptions.
type ServerLegacy struct {
	// Detect outdated "enable_vmd" config parameter and direct users to update config file.
	EnableVMD *bool `yaml:"enable_vmd,omitempty"`
	// Detect outdated "servers" config, to direct users to change their config file.
	Servers []*engine.Config `yaml:"servers,omitempty"`
	// Detect outdated "recreate_superblocks" config, to direct users to change their config file.
	RecreateSuperblocks bool `yaml:"recreate_superblocks,omitempty"`
}

// WithEnableVMD can be used to set the state of VMD functionality,
// if not enabled then VMD devices will not be used if they exist.
func (sl *ServerLegacy) WithEnableVMD(enabled bool) *ServerLegacy {
	sl.EnableVMD = &enabled
	return sl
}

// WithRecreateSuperblocks indicates that a missing superblock should not be treated as
// an error. The server will create new superblocks as necessary.
func (sl *ServerLegacy) WithRecreateSuperblocks() *ServerLegacy {
	sl.RecreateSuperblocks = true
	return sl
}

func updateVMDSetting(legacyCfg ServerLegacy, srvCfg *Server) error {
	switch {
	case legacyCfg.EnableVMD == nil:
		return nil // Legacy VMD setting not used.
	case srvCfg.DisableVMD != nil:
		return FaultConfigVMDSettingDuplicate // Both legacy and current settings used.
	default:
		disable := !(*legacyCfg.EnableVMD)
		srvCfg.DisableVMD = &disable
		return nil
	}
}

func updateFromLegacyParams(srvCfg *Server) error {
	if err := updateVMDSetting(srvCfg.Legacy, srvCfg); err != nil {
		return err
	}

	return nil
}
