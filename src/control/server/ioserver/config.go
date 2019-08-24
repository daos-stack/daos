//
// (C) Copyright 2019 Intel Corporation.
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

package ioserver

import "github.com/pkg/errors"

const (
	maxHelperStreamCount = 2
)

type ScmConfig struct {
	MountPoint  string   `yaml:"scm_mount" cmdLongFlag:"--storage" cmdShortFlag:"-s"`
	Class       string   `yaml:"scm_class"`
	RamdiskSize int      `yaml:"scm_size"`
	DeviceList  []string `yaml:"scm_list"`
}

func (sc *ScmConfig) Validate() error {
	if sc.MountPoint == "" {
		return errors.New("no scm_mount set")
	}
	if sc.Class == "" {
		return errors.New("no scm_class set")
	}
	return nil
}

type BdevConfig struct {
	ConfigPath  string   `cmdLongFlag:"--nvme" cmdShortFlag:"-n"`
	Class       string   `yaml:"bdev_class"`
	DeviceList  []string `yaml:"bdev_list"`
	DeviceCount int      `yaml:"bdev_number"`
	FileSize    int      `yaml:"bdev_size"`
	ShmID       int      `cmdLongFlag:"--shm_id,nonzero" cmdShortFlag:"-i,nonzero"`
	VosEnv      string   `cmdEnv:"VOS_BDEV_CLASS"`
	Hostname    string   // used when generating templates
}

func (nc *BdevConfig) Validate() error {
	return nil
}

type StorageConfig struct {
	SCM  ScmConfig  `yaml:",inline"`
	Bdev BdevConfig `yaml:",inline"`
}

func (sc *StorageConfig) Validate() error {
	if err := sc.SCM.Validate(); err != nil {
		return errors.Wrap(err, "scm config validation failed")
	}
	if err := sc.Bdev.Validate(); err != nil {
		return errors.Wrap(err, "bdev config validation failed")
	}
	return nil
}

type FabricConfig struct {
	Provider  string `yaml:"provider" cmdEnv:"CRT_PHY_ADDR_STR"`
	Interface string `yaml:"fabric_iface" cmdEnv:"OFI_INTERFACE"`
	Port      int    `yaml:"fabric_iface_port" cmdEnv:"OFI_PORT,nonzero"`
}

// Update fills in any missing fields from the
// provided FabricConfig.
func (fc *FabricConfig) Update(other FabricConfig) {
	if fc.Provider == "" {
		fc.Provider = other.Provider
	}
	if fc.Interface == "" {
		fc.Interface = other.Interface
	}
	if fc.Port == 0 {
		fc.Port = other.Port
	}
}

func (fc *FabricConfig) Validate() error {
	if fc.Provider == "" {
		return errors.New("missing provider")
	}
	if fc.Interface == "" {
		return errors.New("missing interface")
	}
	return nil
}

type Config struct {
	Rank               *Rank         `yaml:"rank"`
	Modules            string        `yaml:"modules" cmdLongFlag:"--modules" cmdShortFlag:"-m"`
	TargetStreamCount  int           `yaml:"targets" cmdLongFlag:"--targets,nonzero" cmdShortFlag:"-t,nonzero"`
	HelperStreamCount  int           `yaml:"nr_xs_helpers" cmdLongFlag:"--xshelpernr" cmdShortFlag:"-x"`
	ServiceThreadIndex int           `yaml:"first_core" cmdLongFlag:"--firstcore,nonzero" cmdShortFlag:"-f,nonzero"`
	SystemName         string        `yaml:"name" cmdLongFlag:"--group" cmdShortFlag:"-g"`
	SocketDir          string        `yaml:"socket_dir" cmdLongFlag:"--socket_dir" cmdShortFlag:"-d"`
	AttachInfoPath     string        `cmdLongFlag:"--attach_info" cmdShortFlag:"-a"`
	LogMask            string        `yaml:"log_mask" cmdEnv:"D_LOG_MASK"`
	LogFile            string        `yaml:"log_file" cmdEnv:"D_LOG_FILE"`
	Storage            StorageConfig `yaml:",inline"`
	Fabric             FabricConfig  `yaml:",inline"`
	EnvVars            []string      `yaml:"env_vars"`

	Index int
}

func NewConfig() *Config {
	return &Config{
		HelperStreamCount: maxHelperStreamCount,
	}
}

func (c *Config) Validate() error {
	if err := c.Fabric.Validate(); err != nil {
		return errors.Wrap(err, "fabric config validation failed")
	}

	if err := c.Storage.Validate(); err != nil {
		return errors.Wrap(err, "storage config validation failed")
	}

	if c.HelperStreamCount > maxHelperStreamCount {
		c.HelperStreamCount = maxHelperStreamCount
	}

	return nil
}

func (c *Config) CmdLineArgs() ([]string, error) {
	return parseCmdTags(c, shortFlagTag, joinShortArgs, nil)
}

func (c *Config) CmdLineEnv() ([]string, error) {
	tagEnv, err := parseCmdTags(c, envTag, joinEnvVars, nil)
	if err != nil {
		return nil, err
	}
	return append(c.EnvVars, tagEnv...), nil
}

func (c *Config) WithRank(r *Rank) *Config {
	c.Rank = r
	return c
}

func (c *Config) WithSystemName(name string) *Config {
	c.SystemName = name
	return c
}

func (c *Config) WithHostname(name string) *Config {
	c.Storage.Bdev.Hostname = name
	return c
}

func (c *Config) WithSocketDir(dir string) *Config {
	c.SocketDir = dir
	return c
}

func (c *Config) WithScmClass(scmClass string) *Config {
	c.Storage.SCM.Class = scmClass
	return c
}

func (c *Config) WithScmMountPoint(scmPath string) *Config {
	c.Storage.SCM.MountPoint = scmPath
	return c
}

func (c *Config) WithAttachInfoPath(aip string) *Config {
	c.AttachInfoPath = aip
	return c
}

func (c *Config) WithModules(mList string) *Config {
	c.Modules = mList
	return c
}

func (c *Config) WithShmID(shmID int) *Config {
	c.Storage.Bdev.ShmID = shmID
	return c
}

func (c *Config) WithFabricProvider(provider string) *Config {
	c.Fabric.Provider = provider
	return c
}

func (c *Config) WithFabricInterface(iface string) *Config {
	c.Fabric.Interface = iface
	return c
}

func (c *Config) WithTargetStreamCount(count int) *Config {
	c.TargetStreamCount = count
	return c
}

func (c *Config) WithHelperStreamCount(count int) *Config {
	c.HelperStreamCount = count
	return c
}

func (c *Config) WithServiceThreadIndex(idx int) *Config {
	c.ServiceThreadIndex = idx
	return c
}
