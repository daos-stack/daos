//
// (C) Copyright 2018-2019 Intel Corporation.
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
	"math"
	"path/filepath"
	"strconv"
	"strings"
	"sync"

	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"
)

const (
	maxRank rank = math.MaxUint32 - 1
	nilRank rank = math.MaxUint32

	cLogDebug ControlLogLevel = "DEBUG"
	cLogError ControlLogLevel = "ERROR"

	scmDCPM ScmClass = "dcpm"
	scmRAM  ScmClass = "ram"

	bdNVMe   BdClass = "nvme"
	bdMalloc BdClass = "malloc"
	bdKdev   BdClass = "kdev"
	bdFile   BdClass = "file"

	// todo: implement Provider discriminated union
	// todo: implement LogMask discriminated union
)

// rank represents a rank of an I/O server or a nil rank.
type rank uint32

func (r rank) String() string {
	return strconv.FormatUint(uint64(r), 10)
}

func (r *rank) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var i uint32
	if err := unmarshal(&i); err != nil {
		return err
	}
	if err := checkRank(rank(i)); err != nil {
		return err
	}
	*r = rank(i)
	return nil
}

func (r *rank) UnmarshalFlag(value string) error {
	i, err := strconv.ParseUint(value, 0, 32)
	if err != nil {
		return err
	}
	if err = checkRank(rank(i)); err != nil {
		return err
	}
	*r = rank(i)
	return nil
}

func checkRank(r rank) error {
	if r == nilRank {
		return errors.Errorf("rank %d out of range [0, %d]", r, maxRank)
	}
	return nil
}

// ControlLogLevel is a type that specifies log levels
type ControlLogLevel string

// UnmarshalYAML implements yaml.Unmarshaler on ControlLogMask struct
func (c *ControlLogLevel) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var level string
	if err := unmarshal(&level); err != nil {
		return err
	}
	logLevel := ControlLogLevel(level)
	switch logLevel {
	case cLogDebug, cLogError:
		*c = logLevel
	default:
		return errors.Errorf(
			"control_log_mask value %v not supported in config (DEBUG/ERROR)",
			logLevel)
	}
	return nil
}

// ScmClass enum specifing device type for Storage Class Memoryo
type ScmClass string

// UnmarshalYAML implements yaml.Unmarshaler on ScmClass struct
func (s *ScmClass) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var class string
	if err := unmarshal(&class); err != nil {
		return err
	}

	scmClass := ScmClass(class)
	switch scmClass {
	case scmDCPM, scmRAM:
		*s = scmClass
	default:
		return errors.Errorf(
			"scm_class value %v not supported in config (dcpm/ram)",
			scmClass)
	}
	return nil
}

// BdClass enum specifing block device type for storage
type BdClass string

// UnmarshalYAML implements yaml.Unmarshaler on BdClass struct
func (b *BdClass) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var class string
	if err := unmarshal(&class); err != nil {
		return err
	}
	bdevClass := BdClass(class)
	switch bdevClass {
	case bdNVMe, bdMalloc, bdKdev, bdFile:
		*b = bdevClass
	default:
		return errors.Errorf(
			"bdev_class value %v not supported in config (nvme/malloc/kdev/file)",
			bdevClass)
	}
	return nil
}

// todo: implement UnMarshal for LogMask discriminated union

// server defines configuration options for DAOS IO Server instances
type server struct {
	Rank            *rank    `yaml:"rank"`
	Cpus            []string `yaml:"cpus"`
	FabricIface     string   `yaml:"fabric_iface"`
	FabricIfacePort int      `yaml:"fabric_iface_port"`
	LogMask         string   `yaml:"log_mask"`
	LogFile         string   `yaml:"log_file"`
	EnvVars         []string `yaml:"env_vars"`
	ScmMount        string   `yaml:"scm_mount"`
	ScmClass        ScmClass `yaml:"scm_class"`
	ScmList         []string `yaml:"scm_list"`
	ScmSize         int      `yaml:"scm_size"`
	BdevClass       BdClass  `yaml:"bdev_class"`
	BdevList        []string `yaml:"bdev_list"`
	BdevNumber      int      `yaml:"bdev_number"`
	BdevSize        int      `yaml:"bdev_size"`
	// ioParams represents commandline options and environment variables
	// to be passed on I/O server invocation.
	CliOpts []string // tuples (short option, value) e.g. ["-p", "10000"...]
	// Condition variable to announce formatting of storage
	FormatCond *sync.Cond
}

// newDefaultServer creates a new instance of server struct
// populated with defaults.
func newDefaultServer() server {
	return server{
		ScmClass:  scmDCPM,
		BdevClass: bdNVMe,
	}
}

type configuration struct {
	SystemName     string          `yaml:"name"`
	Servers        []server        `yaml:"servers"`
	Provider       string          `yaml:"provider"`
	SocketDir      string          `yaml:"socket_dir"`
	AccessPoints   []string        `yaml:"access_points"`
	Port           int             `yaml:"port"`
	CaCert         string          `yaml:"ca_cert"`
	Cert           string          `yaml:"cert"`
	Key            string          `yaml:"key"`
	FaultPath      string          `yaml:"fault_path"`
	FaultCb        string          `yaml:"fault_cb"`
	FabricIfaces   []string        `yaml:"fabric_ifaces"`
	FormatOverride bool            `yaml:"format_override"`
	ScmMountPath   string          `yaml:"scm_mount_path"`
	BdevInclude    []string        `yaml:"bdev_include"`
	BdevExclude    []string        `yaml:"bdev_exclude"`
	Hyperthreads   bool            `yaml:"hyperthreads"`
	NrHugepages    int             `yaml:"nr_hugepages"`
	ControlLogMask ControlLogLevel `yaml:"control_log_mask"`
	ControlLogFile string          `yaml:"control_log_file"`
	// development (subject to change) config fields
	Modules   string
	Attach    string
	SystemMap string
	Path      string
	ext       External // interface to os utilities
	// Shared memory segment ID to enable SPDK multiprocess mode,
	// SPDK application processes can then access the same shared
	// memory and therefore NVMe controllers.
	// TODO: Is it also necessary to provide distinct coremask args?
	NvmeShmID int
}

// todo: implement UnMarshal for Provider discriminated union

// parse decodes YAML representation of configure struct and checks for Group
func (c *configuration) parse(data []byte) error {
	return yaml.Unmarshal(data, c)
}

// checkMount verifies that the provided path or parent directory is listed as
// a distinct mountpoint in output of os mount command.
func (c *configuration) checkMount(path string) error {
	path = strings.TrimSpace(path)
	f := func(p string) error {
		return c.ext.runCommand(fmt.Sprintf("mount | grep ' %s '", p))
	}
	if err := f(path); err != nil {
		return f(filepath.Dir(path))
	}
	return nil
}

// newDefaultConfiguration creates a new instance of configuration struct
// populated with defaults.
func newDefaultConfiguration(ext External) configuration {
	return configuration{
		SystemName:     "daos_server",
		SocketDir:      "/var/run/daos_server",
		AccessPoints:   []string{"localhost"},
		Port:           10000,
		Cert:           "./.daos/daos_server.crt",
		Key:            "./.daos/daos_server.key",
		ScmMountPath:   "/mnt/daos",
		Hyperthreads:   false,
		NrHugepages:    1024,
		Path:           "etc/daos_server.yml",
		NvmeShmID:      0,
		ControlLogMask: cLogDebug,
		ext:            ext,
	}
}

// newConfiguration creates a new instance of configuration struct
// populated with defaults and default external interface.
func newConfiguration() configuration {
	return newDefaultConfiguration(&ext{})
}
