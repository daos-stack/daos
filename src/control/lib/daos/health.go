//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"github.com/google/uuid"
)

type (
	// ComponentBuild contains string representations of a component's build information.
	ComponentBuild struct {
		Version   string `json:"version"`
		BuildInfo string `json:"build_info,omitempty"`
		LibPath   string `json:"lib_path,omitempty"`
	}

	// SystemHealthInfo provides a top-level structure to hold system health information.
	SystemHealthInfo struct {
		SystemInfo         *SystemInfo                    `json:"system_info"`
		ComponentBuildInfo map[string]ComponentBuild      `json:"component_build_info"`
		Pools              map[uuid.UUID]*PoolInfo        `json:"pools"`
		Containers         map[uuid.UUID][]*ContainerInfo `json:"pool_containers,omitempty"`
	}
)
