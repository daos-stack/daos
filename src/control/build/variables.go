//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Package build provides an importable repository of variables set at build time.
package build

import "time"

var (
	// ConfigDir should be set via linker flag using the value of CONF_DIR.
	ConfigDir string = "./"
	// DaosVersion should be set via linker flag using the value of DAOS_VERSION.
	DaosVersion string = "unset"
	// ControlPlaneName defines a consistent name for the control plane server.
	ControlPlaneName = "DAOS Control Server"
	// DataPlaneName defines a consistent name for the engine.
	DataPlaneName = "DAOS I/O Engine"
	// ManagementServiceName defines a consistent name for the Management Service.
	ManagementServiceName = "DAOS Management Service"
	// AgentName defines a consistent name for the compute node agent.
	AgentName = "DAOS Agent"
	// CLIUtilName defines a consistent name for the daos CLI utility.
	CLIUtilName = "DAOS CLI"
	// AdminUtilName defines a consistent name for the dmg utility.
	AdminUtilName = "DAOS Admin Tool"

	// DefaultControlPort defines the default control plane listener port.
	DefaultControlPort = 10001

	// DefaultSystemName defines the default DAOS system name.
	DefaultSystemName = "daos_server"

	// VCS is the version control system used to build the binary.
	VCS = ""
	// Revision is the VCS revision of the binary.
	Revision = ""
	// LastCommit is the time of the last commit.
	LastCommit time.Time
	// ReleaseBuild is true if the binary was built with the release tag.
	ReleaseBuild bool
	// DirtyBuild is true if the binary was built with uncommitted changes.
	DirtyBuild bool
)
