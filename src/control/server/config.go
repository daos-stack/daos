//
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

import (
	"fmt"
	"io/ioutil"

	"gopkg.in/yaml.v2"
)

func saveConfig(c Configuration, filename string) error {
	bytes, err := yaml.Marshal(c)
	if err != nil {
		return err
	}
	return ioutil.WriteFile(filename, bytes, 0644)
}

func loadConfig(filename string) (cPtr *Configuration, err error) {
	cPtr = NewDefaultConfiguration()
	bytes, err := ioutil.ReadFile(filename)
	if err != nil {
		return
	}
	err = yaml.Unmarshal(bytes, cPtr)
	if err != nil {
		return
	}
	// assert that config exists and is at least populated with SystemName
	if cPtr.SystemName == "" {
		err = fmt.Errorf(
			"config should exist and be populated with at least SystemName")
	}
	return
}

// NewDefaultConfiguration creates a new instance of Configuration struct
// populated with defaults.
func NewDefaultConfiguration() *Configuration {
	return &Configuration{
		Auto:         true,
		Format:       SAFE,
		AccessPoints: []string{"localhost"},
		Port:         10000,
		Cert:         "./.daos/daos_server.crt",
		Key:          "./.daos/daos_server.key",
		MountPath:    "/mnt/daos",
		Hyperhreads:  false,
	}
}
