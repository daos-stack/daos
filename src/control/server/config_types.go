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
	"fmt"
	"math"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/pkg/errors"
	yaml "gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
)

const (
	maxRank rank = math.MaxUint32 - 1
	nilRank rank = math.MaxUint32

	scmDCPM ScmClass = "dcpm"
	scmRAM  ScmClass = "ram"

	// TODO: implement Provider discriminated union
	// TODO: implement LogMask discriminated union
)

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
type ControlLogLevel logging.LogLevel

// TODO(mjmac): Evaluate whether or not this layer of indirection
// adds any value.
const (
	ControlLogLevelDebug = ControlLogLevel(logging.LogLevelDebug)
	ControlLogLevelInfo  = ControlLogLevel(logging.LogLevelInfo)
	ControlLogLevelError = ControlLogLevel(logging.LogLevelError)
)

// UnmarshalYAML implements yaml.Unmarshaler on ControlLogMask struct
func (c *ControlLogLevel) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var strLevel string
	if err := unmarshal(&strLevel); err != nil {
		return err
	}

	var level logging.LogLevel
	if err := level.SetString(strLevel); err != nil {
		return err
	}
	*c = ControlLogLevel(level)
	return nil
}

func (c ControlLogLevel) MarshalYAML() (interface{}, error) {
	return c.String(), nil
}

func (c ControlLogLevel) String() string {
	return logging.LogLevel(c).String()
}

// ScmClass enum specifing device type for Storage Class Memory
type ScmClass string

// UnmarshalYAML implements yaml.Unmarshaler on ScmClass type
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

// BdevClass enum specifing block device type for storage
type BdevClass string

// UnmarshalYAML implements yaml.Unmarshaler on BdevClass type
func (b *BdevClass) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var class string
	if err := unmarshal(&class); err != nil {
		return err
	}
	bdevClass := BdevClass(class)
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

// TODO: implement UnMarshal for LogMask discriminated union

// IOServerConfig defines configuration options for DAOS IO Server instances.
// See utils/config/daos_server.yml for parameter descriptions.
type IOServerConfig struct {
	Rank            *rank     `yaml:"rank"`
	Targets         int       `yaml:"targets"`
	NrXsHelpers     int       `yaml:"nr_xs_helpers"`
	FirstCore       int       `yaml:"first_core"`
	FabricIface     string    `yaml:"fabric_iface"`
	FabricIfacePort int       `yaml:"fabric_iface_port"`
	LogMask         string    `yaml:"log_mask"`
	LogFile         string    `yaml:"log_file"`
	EnvVars         []string  `yaml:"env_vars"`
	ScmMount        string    `yaml:"scm_mount"`
	ScmClass        ScmClass  `yaml:"scm_class"`
	ScmList         []string  `yaml:"scm_list"`
	ScmSize         int       `yaml:"scm_size"`
	BdevClass       BdevClass `yaml:"bdev_class"`
	BdevList        []string  `yaml:"bdev_list"`
	BdevNumber      int       `yaml:"bdev_number"`
	BdevSize        int       `yaml:"bdev_size"`
	// ioParams represents commandline options and environment variables
	// to be passed on I/O server invocation.
	CliOpts   []string      // tuples (short option, value) e.g. ["-p", "10000"...]
	Hostname  string        // used when generating templates
	formatted chan struct{} // closed when server is formatted
}

// newDefaultServer creates a new instance of server struct with default values.
func newDefaultServer() *IOServerConfig {
	// TODO: fix by only ever creating server in one place
	host, _ := os.Hostname()

	return &IOServerConfig{
		ScmClass:    scmDCPM,
		BdevClass:   bdNVMe,
		Hostname:    host,
		NrXsHelpers: 2,
	}
}

// UnmarshalYAML implements yaml.Unmarshaler on server struct enabling defaults
// to be applied to each nested server.
//
// Type alias used to prevent recursive calls to UnmarshalYAML.
func (s *IOServerConfig) UnmarshalYAML(unmarshal func(interface{}) error) error {
	type serverAlias IOServerConfig
	srv := &serverAlias{
		ScmClass:    scmDCPM,
		BdevClass:   bdNVMe,
		NrXsHelpers: 2,
	}

	if err := unmarshal(&srv); err != nil {
		return err
	}

	*s = IOServerConfig(*srv)
	return nil
}

// Configuration describes options for DAOS control plane.
// See utils/config/daos_server.yml for parameter descriptions.
type Configuration struct {
	SystemName      string                    `yaml:"name"`
	Servers         []*IOServerConfig         `yaml:"servers"`
	Provider        string                    `yaml:"provider"`
	SocketDir       string                    `yaml:"socket_dir"`
	AccessPoints    []string                  `yaml:"access_points"`
	Port            int                       `yaml:"port"`
	TransportConfig *security.TransportConfig `yaml:"transport_config"`
	FaultPath       string                    `yaml:"fault_path"`
	FaultCb         string                    `yaml:"fault_cb"`
	FabricIfaces    []string                  `yaml:"fabric_ifaces"`
	ScmMountPath    string                    `yaml:"scm_mount_path"`
	BdevInclude     []string                  `yaml:"bdev_include"`
	BdevExclude     []string                  `yaml:"bdev_exclude"`
	Hyperthreads    bool                      `yaml:"hyperthreads"`
	NrHugepages     int                       `yaml:"nr_hugepages"`
	ControlLogMask  ControlLogLevel           `yaml:"control_log_mask"`
	ControlLogFile  string                    `yaml:"control_log_file"`
	ControlLogJSON  bool                      `yaml:"control_log_json,omitempty"`
	UserName        string                    `yaml:"user_name"`
	GroupName       string                    `yaml:"group_name"`
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

// WithSystemName sets the system name
func (c *Configuration) WithSystemName(name string) *Configuration {
	c.SystemName = name
	return c
}

// WithServers sets the list of IOServer configurations
func (c *Configuration) WithServers(srvList ...*IOServerConfig) *Configuration {
	c.Servers = srvList
	return c
}

// WithProvider sets the default fabric provider string
func (c *Configuration) WithProvider(provider string) *Configuration {
	c.Provider = provider
	return c
}

// WithSocketDir sets the default socket directory
func (c *Configuration) WithSocketDir(sockDir string) *Configuration {
	c.SocketDir = sockDir
	return c
}

// WithAccessPoints sets the access point list
func (c *Configuration) WithAccessPoints(aps ...string) *Configuration {
	c.AccessPoints = aps
	return c
}

// WithPort sets the RPC listener port
func (c *Configuration) WithPort(port int) *Configuration {
	c.Port = port
	return c
}

// WithTransportConfig sets the gRPC transport configuration
func (c *Configuration) WithTransportConfig(cfg *security.TransportConfig) *Configuration {
	c.TransportConfig = cfg
	return c
}

// WithFaultPath sets the fault path
func (c *Configuration) WithFaultPath(fp string) *Configuration {
	c.FaultPath = fp
	return c
}

// WithFaultCb sets the fault callback
func (c *Configuration) WithFaultCb(cb string) *Configuration {
	c.FaultCb = cb
	return c
}

// WithFabricIfaces sets the list of fabric interfaces
func (c *Configuration) WithFabricIfaces(ifaceList ...string) *Configuration {
	c.FabricIfaces = ifaceList
	return c
}

// WithScmMountPath sets the default SCM mount path
func (c *Configuration) WithScmMountPath(mp string) *Configuration {
	c.ScmMountPath = mp
	return c
}

// WithBdevExclude sets the block device exclude list
func (c *Configuration) WithBdevExclude(bList ...string) *Configuration {
	c.BdevExclude = bList
	return c
}

// WithBdevInclude sets the block device include list
func (c *Configuration) WithBdevInclude(bList ...string) *Configuration {
	c.BdevInclude = bList
	return c
}

// WithHyperthreads enables or disables hyperthread support
func (c *Configuration) WithHyperthreads(enabled bool) *Configuration {
	c.Hyperthreads = enabled
	return c
}

// WithNrHugePages sets the number of huge pages to be used
func (c *Configuration) WithNrHugePages(nr int) *Configuration {
	c.NrHugepages = nr
	return c
}

// WithControlLogMask sets the log level
func (c *Configuration) WithControlLogMask(lvl ControlLogLevel) *Configuration {
	c.ControlLogMask = lvl
	return c
}

// WithControlLogFile sets the path to the logfile
func (c *Configuration) WithControlLogFile(filePath string) *Configuration {
	c.ControlLogFile = filePath
	return c
}

// WithControlLogJSON enables or disables JSON output
func (c *Configuration) WithControlLogJSON(enabled bool) *Configuration {
	c.ControlLogJSON = enabled
	return c
}

// WithUserName sets the user to run as
func (c *Configuration) WithUserName(name string) *Configuration {
	c.UserName = name
	return c
}

// WithGroupName sets the group to run as
func (c *Configuration) WithGroupName(name string) *Configuration {
	c.GroupName = name
	return c
}

// WithModules sets a list of server modules to load
func (c *Configuration) WithModules(mList string) *Configuration {
	c.Modules = mList
	return c
}

// WithAttach sets attachment info path
func (c *Configuration) WithAttach(aip string) *Configuration {
	c.Attach = aip
	return c
}

// todo: implement UnMarshal for Provider discriminated union

// parse decodes YAML representation of configuration
func (c *Configuration) parse(data []byte) error {
	return yaml.Unmarshal(data, c)
}

// checkMount verifies that the provided path or parent directory is listed as
// a distinct mountpoint in output of os mount command.
func (c *Configuration) checkMount(path string) error {
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
func newDefaultConfiguration(ext External) *Configuration {
	return &Configuration{
		SystemName:      "daos_server",
		SocketDir:       "/var/run/daos_server",
		AccessPoints:    []string{"localhost"},
		Port:            10000,
		TransportConfig: security.DefaultServerTransportConfig(),
		ScmMountPath:    "/mnt/daos",
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
