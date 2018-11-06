// (C) Copyright 2018 Intel Corporation.
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

package main

import "fmt"

// Format represents enum specifying formatting behaviour
type Format string

const (
	// SAFE state defines cautionary behaviour where formatted devices will not be overwritten.
	SAFE Format = "safe"
	// CONTINUE state defines ehaviour which results in reuse of formatted devices.
	CONTINUE Format = "continue"
	// FORCE state defines aggressive/potentially resulting data loss formatting behaviour.
	FORCE Format = "force"
)

//Server defines configuration options for DAOS IO Server instances
type Server struct {
	Rank            int      `yaml:"rank"`
	Cpus            []string `yaml:"cpus"`
	FabricIface     string   `yaml:"fabric_iface"`
	FabricIfacePort int      `yaml:"fabric_iface_port"`
	MountForce      string   `yaml:"mount_force"`
	NvmeForce       []string `yaml:"nvme_force"`
	DebugMask       string   `yaml:"debug_mask"`
}

// Configuration defines configuration options for DAOS (mgmt) Server process
// including global and per-server options for DAOS IO Server instances.
type Configuration struct {
	SystemName   string   `yaml:"name"`
	Servers      []Server `yaml:"servers"`
	Auto         bool     `yaml:"auto"`
	Format       Format   `yaml:"format"`
	AccessPoints []string `yaml:"access_points"`
	Port         int      `yaml:"port"`
	CaCert       string   `yaml:"ca_cert"`
	Cert         string   `yaml:"cert"`
	Key          string   `yaml:"key"`
	FaultPath    string   `yaml:"fault_path"`
	FaultCb      string   `yaml:"fault_cb"`
	FabricIfaces []string `yaml:"fabric_ifaces"`
	Provider     string   `yaml:"provider"`
	MountPath    string   `yaml:"mount_path"`
	NvmeInclude  []string `yaml:"nvme_include"`
	NvmeExclude  []string `yaml:"nvme_exclude"`
	Hyperthreads bool     `yaml:"hyperthreads"`
}

// UnmarshalYAML implements yaml.Unmarshaler on Format struct
func (f *Format) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var fm string
	if err := unmarshal(&fm); err != nil {
		return err
	}
	format := Format(fm)
	switch format {
	case SAFE, CONTINUE, FORCE:
		*f = format
	default:
		return fmt.Errorf(
			"format value %v not supported in config", format)
	}
	return nil
}
