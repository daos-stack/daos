//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package config

// ServerLegacy describes old configuration options that should be supported for backward
// compatibility and may be deprecated in future releases.
// See utils/config/daos_server.yml for parameter descriptions.
type ServerLegacy struct {
	// Detect outdated "enable_vmd" config parameter and direct users to update config file.
	EnableVMD *bool `yaml:"enable_vmd"`
}

// WithEnableVMD can be used to set the state of VMD functionality,
// if not enabled then VMD devices will not be used if they exist.
func (sl *ServerLegacy) WithEnableVMD(enabled bool) *ServerLegacy {
	sl.EnableVMD = &enabled
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

// parseLegacyConfig takes server config raw bytes and updates the provided server config struct.
func updateFromLegacyParams(srvCfg *Server) error {
	if err := updateVMDSetting(srvCfg.Legacy, srvCfg); err != nil {
		return err
	}

	return nil
}
