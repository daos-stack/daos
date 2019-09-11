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

package server

import (
	"hash/fnv"
	"io/ioutil"
	"os"
	"path/filepath"
	"strconv"

	"github.com/pkg/errors"
	yaml "gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/ioserver"
)

const (
	configOut           = ".daos_server.active.yml"
	relConfExamplesPath = "utils/config/examples/"
	msgBadConfig        = "insufficient config file, see examples in "
	msgConfigNoProvider = "provider not specified in config"
	msgConfigNoPath     = "no config path set"
	msgConfigNoServers  = "no servers specified in config"
)

// Configuration describes options for DAOS control plane.
// See utils/config/daos_server.yml for parameter descriptions.
type Configuration struct {
	// control-specific
	ControlPort     int                       `yaml:"port"`
	TransportConfig *security.TransportConfig `yaml:"transport_config"`
	Servers         []*ioserver.Config        `yaml:"servers"`
	BdevInclude     []string                  `yaml:"bdev_include,omitempty"`
	BdevExclude     []string                  `yaml:"bdev_exclude,omitempty"`
	NrHugepages     int                       `yaml:"nr_hugepages"`
	ControlLogMask  ControlLogLevel           `yaml:"control_log_mask"`
	ControlLogFile  string                    `yaml:"control_log_file"`
	ControlLogJSON  bool                      `yaml:"control_log_json,omitempty"`
	UserName        string                    `yaml:"user_name"`
	GroupName       string                    `yaml:"group_name"`

	// duplicated in ioserver.Config
	SystemName string                `yaml:"name"`
	SocketDir  string                `yaml:"socket_dir"`
	Fabric     ioserver.FabricConfig `yaml:",inline"`
	Modules    string
	Attach     string

	// deprecated
	AccessPoints []string `yaml:"access_points"`

	// unused (?)
	FaultPath    string `yaml:"fault_path"`
	FaultCb      string `yaml:"fault_cb"`
	Hyperthreads bool   `yaml:"hyperthreads"`

	Path string   // path to config file
	ext  External // interface to os utilities
	// Shared memory segment ID to enable SPDK multiprocess mode,
	// SPDK application processes can then access the same shared
	// memory and therefore NVMe controllers.
	// TODO: Is it also necessary to provide distinct coremask args?
	NvmeShmID int
}

// WithSystemName sets the system name.
func (c *Configuration) WithSystemName(name string) *Configuration {
	c.SystemName = name
	for _, srv := range c.Servers {
		srv.WithSystemName(name)
	}
	return c
}

// WithSocketDir sets the default socket directory.
func (c *Configuration) WithSocketDir(sockDir string) *Configuration {
	c.SocketDir = sockDir
	for _, srv := range c.Servers {
		srv.WithSocketDir(sockDir)
	}
	return c
}

// WithNvmeShmID sets the common shmID used for SPDK multiprocess mode.
func (c *Configuration) WithNvmeShmID(id int) *Configuration {
	c.NvmeShmID = id
	for _, srv := range c.Servers {
		srv.WithShmID(id)
	}
	return c
}

// WithModules sets a list of server modules to load.
func (c *Configuration) WithModules(mList string) *Configuration {
	c.Modules = mList
	for _, srv := range c.Servers {
		srv.WithModules(mList)
	}
	return c
}

// WithAttach sets attachment info path.
func (c *Configuration) WithAttachInfo(aip string) *Configuration {
	c.Attach = aip
	// TODO: Should all instances share this? Thinking probably not...
	for _, srv := range c.Servers {
		srv.WithAttachInfoPath(aip)
	}
	return c
}

// WithFabricProvider sets the top-level fabric provider.
func (c *Configuration) WithFabricProvider(provider string) *Configuration {
	c.Fabric.Provider = provider
	for _, srv := range c.Servers {
		srv.Fabric.Update(c.Fabric)
	}
	return c
}

// NB: In order to ease maintenance, the set of chained config functions
// which modify nested ioserver configurations should be kept above this
// one as a reference for which things should be set/updated in the next
// function.
func (c *Configuration) updateServerConfig(srvCfg *ioserver.Config) {
	srvCfg.Fabric.Update(c.Fabric)
	srvCfg.SystemName = c.SystemName
	srvCfg.WithShmID(c.NvmeShmID)
	srvCfg.SocketDir = c.SocketDir
	srvCfg.Modules = c.Modules
	srvCfg.AttachInfoPath = c.Attach // TODO: Is this correct?
}

// WithServers sets the list of IOServer configurations.
func (c *Configuration) WithServers(srvList ...*ioserver.Config) *Configuration {
	c.Servers = srvList
	for _, srvCfg := range c.Servers {
		c.updateServerConfig(srvCfg)
	}
	return c
}

// WithScmMountPoint sets the SCM mountpoint for the first I/O Server.
//
// Deprecated: This function exists to ease transition away from
// specifying the SCM mountpoint via daos_server CLI flag. Future
// versions will require the mountpoint to be set via configuration.
func (c *Configuration) WithScmMountPoint(mp string) *Configuration {
	if len(c.Servers) > 0 {
		c.Servers[0].WithScmMountPoint(mp)
	}
	return c
}

// WithAccessPoints sets the access point list.
func (c *Configuration) WithAccessPoints(aps ...string) *Configuration {
	c.AccessPoints = aps
	return c
}

// WithControlPort sets the gRPC listener port.
func (c *Configuration) WithControlPort(port int) *Configuration {
	c.ControlPort = port
	return c
}

// WithTransportConfig sets the gRPC transport configuration.
func (c *Configuration) WithTransportConfig(cfg *security.TransportConfig) *Configuration {
	c.TransportConfig = cfg
	return c
}

// WithFaultPath sets the fault path (identification string e.g. rack/shelf/node).
func (c *Configuration) WithFaultPath(fp string) *Configuration {
	c.FaultPath = fp
	return c
}

// WithFaultCb sets the path to the fault callback script.
func (c *Configuration) WithFaultCb(cb string) *Configuration {
	c.FaultCb = cb
	return c
}

// WithBdevExclude sets the block device exclude list.
func (c *Configuration) WithBdevExclude(bList ...string) *Configuration {
	c.BdevExclude = bList
	return c
}

// WithBdevInclude sets the block device include list.
func (c *Configuration) WithBdevInclude(bList ...string) *Configuration {
	c.BdevInclude = bList
	return c
}

// WithHyperthreads enables or disables hyperthread support.
func (c *Configuration) WithHyperthreads(enabled bool) *Configuration {
	c.Hyperthreads = enabled
	return c
}

// WithNrHugePages sets the number of huge pages to be used.
func (c *Configuration) WithNrHugePages(nr int) *Configuration {
	c.NrHugepages = nr
	return c
}

// WithControlLogMask sets the daos_server log level.
func (c *Configuration) WithControlLogMask(lvl ControlLogLevel) *Configuration {
	c.ControlLogMask = lvl
	return c
}

// WithControlLogFile sets the path to the daos_server logfile.
func (c *Configuration) WithControlLogFile(filePath string) *Configuration {
	c.ControlLogFile = filePath
	return c
}

// WithControlLogJSON enables or disables JSON output.
func (c *Configuration) WithControlLogJSON(enabled bool) *Configuration {
	c.ControlLogJSON = enabled
	return c
}

// WithUserName sets the user to run as.
func (c *Configuration) WithUserName(name string) *Configuration {
	c.UserName = name
	return c
}

// WithGroupName sets the group to run as.
func (c *Configuration) WithGroupName(name string) *Configuration {
	c.GroupName = name
	return c
}

// parse decodes YAML representation of configuration
func (c *Configuration) parse(data []byte) error {
	return yaml.Unmarshal(data, c)
}

// newDefaultConfiguration creates a new instance of configuration struct
// populated with defaults.
func newDefaultConfiguration(ext External) *Configuration {
	return &Configuration{
		SystemName:      "daos_server",
		SocketDir:       "/var/run/daos_server",
		AccessPoints:    []string{"localhost"},
		ControlPort:     10000,
		TransportConfig: security.DefaultServerTransportConfig(),
		Hyperthreads:    false,
		NrHugepages:     1024,
		Path:            "etc/daos_server.yml",
		NvmeShmID:       0,
		ControlLogMask:  ControlLogLevel(logging.LogLevelInfo),
		ext:             ext,
	}
}

// NewConfiguration creates a new instance of configuration struct
// populated with defaults and default external interface.
func NewConfiguration() *Configuration {
	return newDefaultConfiguration(&ext{})
}

// Load reads the serialized configuration from disk and validates it.
func (c *Configuration) Load() error {
	if c.Path == "" {
		return errors.New(msgConfigNoPath)
	}

	bytes, err := ioutil.ReadFile(c.Path)
	if err != nil {
		return errors.WithMessage(err, "reading file")
	}

	if err = c.parse(bytes); err != nil {
		return errors.WithMessage(err, "parse failed; config contains invalid "+
			"parameters and may be out of date, see server config examples")
	}

	// propagate top-level settings to server configs
	for _, srvCfg := range c.Servers {
		c.updateServerConfig(srvCfg)
	}

	return c.Validate()
}

// SaveToFile serializes the configuration and saves it to the specified filename.
func (c *Configuration) SaveToFile(filename string) error {
	bytes, err := yaml.Marshal(c)

	if err != nil {
		return err
	}

	return ioutil.WriteFile(filename, bytes, 0644)
}

// hash produces unique int from string, mask MSB on conversion to signed int
func hash(s string) int {
	h := fnv.New32a()
	if _, err := h.Write([]byte(s)); err != nil {
		panic(err) // should never happen
	}

	return int(h.Sum32() & 0x7FFFFFFF) // mask MSB of uint32 as this will be sign bit
}

func (c *Configuration) SetNvmeShmID(base string) {
	c.WithNvmeShmID(hash(base + strconv.Itoa(os.Getpid())))
}

// SetPath sets the default path to the configuration file.
func (c *Configuration) SetPath(path string) error {
	if path != "" {
		c.Path = path
	}

	if !filepath.IsAbs(c.Path) {
		newPath, err := c.ext.getAbsInstallPath(c.Path)
		if err != nil {
			return err
		}
		c.Path = newPath
	}

	return nil
}

// saveActiveConfig saves read-only active config, tries config dir then /tmp/
func saveActiveConfig(log logging.Logger, config *Configuration) {
	activeConfig := filepath.Join(filepath.Dir(config.Path), configOut)
	eMsg := "Warning: active config could not be saved (%s)"
	err := config.SaveToFile(activeConfig)
	if err != nil {
		log.Debugf(eMsg, err)

		activeConfig = filepath.Join("/tmp", configOut)
		err = config.SaveToFile(activeConfig)
		if err != nil {
			log.Debugf(eMsg, err)
		}
	}
	if err == nil {
		log.Debugf("Active config saved to %s (read-only)", activeConfig)
	}
}

// Validate asserts that config meets minimum requirements.
func (c *Configuration) Validate() (err error) {
	// append the user-friendly message to any error
	// TODO: use a fault/resolution
	defer func() {
		if err != nil {
			examplesPath, _ := c.ext.getAbsInstallPath(relConfExamplesPath)
			err = errors.WithMessage(err, msgBadConfig+examplesPath)
		}
	}()

	if c.Fabric.Provider == "" {
		return errors.New(msgConfigNoProvider)
	}

	if len(c.Servers) == 0 {
		return errors.New(msgConfigNoServers)
	}

	for i, srv := range c.Servers {
		srv.Fabric.Update(c.Fabric)
		if err := srv.Validate(); err != nil {
			return errors.Wrapf(err, "I/O server %d failed config validation", i)
		}
	}

	return nil
}
