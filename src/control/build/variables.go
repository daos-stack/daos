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
