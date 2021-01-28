//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
// Package build provides an importable repository of variables set at build time.
package build

var (
	// ConfigDir should be set via linker flag using the value of CONF_DIR.
	ConfigDir string = "./"
	// DaosVersion should be set via linker flag using the value of DAOS_VERSION.
	DaosVersion string = "unset"
	// ControlPlaneName defines a consistent name for the control plane server.
	ControlPlaneName = "DAOS Control Server"
	// DataPlaneName defines a consistent name for the ioserver.
	DataPlaneName = "DAOS I/O Server"
	// ManagementServiceName defines a consistent name for the Management Service.
	ManagementServiceName = "DAOS Management Service"
	// AgentName defines a consistent name for the compute node agent.
	AgentName = "DAOS Agent"

	// DefaultControlPort defines the default control plane listener port.
	DefaultControlPort = 10001
	// DefaultSystemName defines the default DAOS system name.
	DefaultSystemName = "daos_server"
)
