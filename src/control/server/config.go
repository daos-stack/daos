//
// (C) Copyright 2018-2020 Intel Corporation.
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
	"context"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strconv"

	"github.com/pkg/errors"
	yaml "gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/ioserver"
)

const (
	defaultRuntimeDir   = "/var/run/daos_server"
	defaultConfigPath   = "../etc/daos_server.yml"
	configOut           = ".daos_server.active.yml"
	relConfExamplesPath = "../utils/config/examples/"
)

type networkProviderValidation func(context.Context, string, string) error
type networkNUMAValidation func(context.Context, string, uint) error
type networkDeviceClass func(string) (uint32, error)

// ClientNetworkCfg elements are used by the libdaos clients to help initialize CaRT.
// These settings bring coherence between the client and server network configuration.
type ClientNetworkCfg struct {
	Provider        string
	CrtCtxShareAddr uint32
	CrtTimeout      uint32
	NetDevClass     uint32
}

// Configuration describes options for DAOS control plane.
// See utils/config/daos_server.yml for parameter descriptions.
type Configuration struct {
	// control-specific
	ControlPort         int                       `yaml:"port"`
	TransportConfig     *security.TransportConfig `yaml:"transport_config"`
	Servers             []*ioserver.Config        `yaml:"servers"`
	BdevInclude         []string                  `yaml:"bdev_include,omitempty"`
	BdevExclude         []string                  `yaml:"bdev_exclude,omitempty"`
	DisableVFIO         bool                      `yaml:"disable_vfio"`
	DisableVMD          bool                      `yaml:"disable_vmd"`
	NrHugepages         int                       `yaml:"nr_hugepages"`
	SetHugepages        bool                      `yaml:"set_hugepages"`
	ControlLogMask      ControlLogLevel           `yaml:"control_log_mask"`
	ControlLogFile      string                    `yaml:"control_log_file"`
	ControlLogJSON      bool                      `yaml:"control_log_json,omitempty"`
	HelperLogFile       string                    `yaml:"helper_log_file"`
	FWHelperLogFile     string                    `yaml:"firmware_helper_log_file"`
	RecreateSuperblocks bool                      `yaml:"recreate_superblocks"`
	FaultPath           string                    `yaml:"fault_path"`

	// duplicated in ioserver.Config
	SystemName string                `yaml:"name"`
	SocketDir  string                `yaml:"socket_dir"`
	Fabric     ioserver.FabricConfig `yaml:",inline"`
	Modules    string

	AccessPoints []string `yaml:"access_points"`

	// unused (?)
	FaultCb      string `yaml:"fault_cb"`
	Hyperthreads bool   `yaml:"hyperthreads"`

	Path string   // path to config file
	ext  External // interface to os utilities

	//a pointer to a function that validates the chosen provider
	validateProviderFn networkProviderValidation

	//a pointer to a function that validates the chosen numa node
	validateNUMAFn networkNUMAValidation

	//a pointer to a function that retrieves the IO server network device class
	getDeviceClassFn networkDeviceClass
}

// WithRecreateSuperblocks indicates that a missing superblock should not be treated as
// an error. The server will create new superblocks as necessary.
func (c *Configuration) WithRecreateSuperblocks() *Configuration {
	c.RecreateSuperblocks = true
	return c
}

// WithProviderValidator sets the function that validates the provider
func (c *Configuration) WithProviderValidator(fn networkProviderValidation) *Configuration {
	c.validateProviderFn = fn
	return c
}

// WithNUMAValidator sets the function that validates the NUMA configuration
func (c *Configuration) WithNUMAValidator(fn networkNUMAValidation) *Configuration {
	c.validateNUMAFn = fn
	return c
}

// WithGetNetworkDeviceClass sets the function that determines the network device class
func (c *Configuration) WithGetNetworkDeviceClass(fn networkDeviceClass) *Configuration {
	c.getDeviceClassFn = fn
	return c
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

// WithModules sets a list of server modules to load.
func (c *Configuration) WithModules(mList string) *Configuration {
	c.Modules = mList
	for _, srv := range c.Servers {
		srv.WithModules(mList)
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

// WithCrtCtxShareAddr sets the top-level CrtCtxShareAddr.
func (c *Configuration) WithCrtCtxShareAddr(addr uint32) *Configuration {
	c.Fabric.CrtCtxShareAddr = addr
	for _, srv := range c.Servers {
		srv.Fabric.Update(c.Fabric)
	}
	return c
}

// WithCrtTimeout sets the top-level CrtTimeout.
func (c *Configuration) WithCrtTimeout(timeout uint32) *Configuration {
	c.Fabric.CrtTimeout = timeout
	for _, srv := range c.Servers {
		srv.Fabric.Update(c.Fabric)
	}
	return c
}

// NB: In order to ease maintenance, the set of chained config functions
// which modify nested ioserver configurations should be kept above this
// one as a reference for which things should be set/updated in the next
// function.
func (c *Configuration) updateServerConfig(cfgPtr **ioserver.Config) {
	// If we somehow get a nil config, we can't return an error, and
	// we don't want to cause a segfault. Instead, just create an
	// empty config and return early, so that it eventually fails
	// validation.
	if *cfgPtr == nil {
		*cfgPtr = &ioserver.Config{}
		return
	}

	srvCfg := *cfgPtr
	srvCfg.Fabric.Update(c.Fabric)
	srvCfg.SystemName = c.SystemName
	srvCfg.SocketDir = c.SocketDir
	srvCfg.Modules = c.Modules
}

// WithServers sets the list of IOServer configurations.
func (c *Configuration) WithServers(srvList ...*ioserver.Config) *Configuration {
	c.Servers = srvList
	for i := range c.Servers {
		c.updateServerConfig(&c.Servers[i])
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

// WithDisableVFIO indicates that the vfio-pci driver should not be
// used by SPDK even if an IOMMU is detected. Note that this option
// requires that DAOS be run as root.
func (c *Configuration) WithDisableVFIO(disabled bool) *Configuration {
	c.DisableVFIO = disabled
	return c
}

// WithDisableVMD indicates that vmd devices should not be used even if they
// exist.
func (c *Configuration) WithDisableVMD(disabled bool) *Configuration {
	c.DisableVMD = disabled
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

// WithHelperLogFile sets the path to the daos_admin logfile.
func (c *Configuration) WithHelperLogFile(filePath string) *Configuration {
	c.HelperLogFile = filePath
	return c
}

// WithFirmwareHelperLogFile sets the path to the daos_firmware logfile.
func (c *Configuration) WithFirmwareHelperLogFile(filePath string) *Configuration {
	c.FWHelperLogFile = filePath
	return c
}

// newDefaultConfiguration creates a new instance of configuration struct
// populated with defaults.
func newDefaultConfiguration(ext External) *Configuration {
	return &Configuration{
		SystemName:         build.DefaultSystemName,
		SocketDir:          defaultRuntimeDir,
		AccessPoints:       []string{fmt.Sprintf("localhost:%d", build.DefaultControlPort)},
		ControlPort:        build.DefaultControlPort,
		TransportConfig:    security.DefaultServerTransportConfig(),
		Hyperthreads:       false,
		Path:               defaultConfigPath,
		ControlLogMask:     ControlLogLevel(logging.LogLevelInfo),
		ext:                ext,
		validateProviderFn: netdetect.ValidateProviderStub,
		validateNUMAFn:     netdetect.ValidateNUMAStub,
		getDeviceClassFn:   netdetect.GetDeviceClass,
		DisableVMD:         true, // support currently unstable
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
		return FaultConfigNoPath
	}

	bytes, err := ioutil.ReadFile(c.Path)
	if err != nil {
		return errors.WithMessage(err, "reading file")
	}

	if err = yaml.UnmarshalStrict(bytes, c); err != nil {
		return errors.WithMessage(err, "parse failed; config contains invalid "+
			"parameters and may be out of date, see server config examples")
	}

	// propagate top-level settings to server configs
	for i := range c.Servers {
		c.updateServerConfig(&c.Servers[i])
	}

	return nil
}

// SaveToFile serializes the configuration and saves it to the specified filename.
func (c *Configuration) SaveToFile(filename string) error {
	bytes, err := yaml.Marshal(c)

	if err != nil {
		return err
	}

	return ioutil.WriteFile(filename, bytes, 0644)
}

// SetPath sets the default path to the configuration file.
func (c *Configuration) SetPath(inPath string) error {
	newPath, err := common.ResolvePath(inPath, c.Path)
	if err != nil {
		return err
	}
	c.Path = newPath

	if _, err = os.Stat(c.Path); err != nil {
		return err
	}

	return err
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
func (c *Configuration) Validate(log logging.Logger) (err error) {
	// config without servers is valid when initially discovering hardware
	// prior to adding per-server sections with device allocations
	if len(c.Servers) == 0 {
		log.Infof("No %ss in configuration, %s starting in discovery mode", build.DataPlaneName, build.ControlPlaneName)
		c.Servers = nil
		return nil
	}

	// append the user-friendly message to any error
	defer func() {
		if err != nil && !fault.HasResolution(err) {
			examplesPath, _ := c.ext.getAbsInstallPath(relConfExamplesPath)
			err = errors.WithMessage(FaultBadConfig, err.Error()+", examples: "+examplesPath)
		}
	}()

	if c.Fabric.Provider == "" {
		return FaultConfigNoProvider
	}

	// only single access point valid for now
	if len(c.AccessPoints) != 1 {
		return FaultConfigBadAccessPoints
	}
	for i := range c.AccessPoints {
		// apply configured control port if not supplied
		host, port, err := common.SplitPort(c.AccessPoints[i], c.ControlPort)
		if err != nil {
			return errors.Wrap(FaultConfigBadAccessPoints, err.Error())
		}

		// warn if access point port differs from config control port
		if strconv.Itoa(c.ControlPort) != port {
			log.Debugf("access point (%s) port (%s) differs from control port (%d)", host, port, c.ControlPort)
		}

		if port == "0" {
			return FaultConfigBadControlPort
		}

		c.AccessPoints[i] = fmt.Sprintf("%s:%s", host, port)
	}

	netCtx, err := netdetect.Init(context.Background())
	defer netdetect.CleanUp(netCtx)
	if err != nil {
		return err
	}

	for i, srv := range c.Servers {
		srv.Fabric.Update(c.Fabric)
		if err := srv.Validate(); err != nil {
			return errors.Wrapf(err, "I/O server %d failed config validation", i)
		}

		err := c.validateProviderFn(netCtx, srv.Fabric.Interface, srv.Fabric.Provider)
		if err != nil {
			return errors.Wrapf(err, "Network device %s does not support provider %s.  The configuration is invalid.",
				srv.Fabric.Interface, srv.Fabric.Provider)
		}

		// Check to see if the pinned NUMA node was provided in the configuration.
		// If it was provided, validate that the NUMA node is correct for the given device.
		// An error from srv.Fabric.GetNumaNode() means that no configuration was provided in the YML.
		// Because this is an optional parameter, this is considered non-fatal.
		numaNode, err := srv.Fabric.GetNumaNode()
		if err == nil {
			err = c.validateNUMAFn(netCtx, srv.Fabric.Interface, numaNode)
			if err != nil {
				return errors.Wrapf(err, "Network device %s on NUMA node %d is an invalid configuration.",
					srv.Fabric.Interface, numaNode)
			}
		}
	}

	if len(c.Servers) > 1 {
		if err := validateMultiServerConfig(log, c); err != nil {
			return err
		}
	}

	return nil
}

// validateMultiServerConfig performs an extra level of validation
// for multi-server configs. The goal is to ensure that each instance
// has unique values for resources which cannot be shared (e.g. log files,
// fabric configurations, PCI devices, etc.)
func validateMultiServerConfig(log logging.Logger, c *Configuration) error {
	if len(c.Servers) < 2 {
		return nil
	}

	seenValues := make(map[string]int)
	seenScmSet := make(map[string]int)
	seenBdevSet := make(map[string]int)

	var netDevClass uint32
	for idx, srv := range c.Servers {
		fabricConfig := fmt.Sprintf("fabric:%s-%s-%d",
			srv.Fabric.Provider,
			srv.Fabric.Interface,
			srv.Fabric.InterfacePort)

		if seenIn, exists := seenValues[fabricConfig]; exists {
			log.Debugf("%s in %d duplicates %d", fabricConfig, idx, seenIn)
			return FaultConfigDuplicateFabric(idx, seenIn)
		}
		seenValues[fabricConfig] = idx

		if srv.LogFile != "" {
			logConfig := fmt.Sprintf("log_file:%s", srv.LogFile)
			if seenIn, exists := seenValues[logConfig]; exists {
				log.Debugf("%s in %d duplicates %d", logConfig, idx, seenIn)
				return FaultConfigDuplicateLogFile(idx, seenIn)
			}
			seenValues[logConfig] = idx
		}

		scmConf := srv.Storage.SCM
		mountConfig := fmt.Sprintf("scm_mount:%s", scmConf.MountPoint)
		if seenIn, exists := seenValues[mountConfig]; exists {
			log.Debugf("%s in %d duplicates %d", mountConfig, idx, seenIn)
			return FaultConfigDuplicateScmMount(idx, seenIn)
		}
		seenValues[mountConfig] = idx

		for _, dev := range scmConf.DeviceList {
			if seenIn, exists := seenScmSet[dev]; exists {
				log.Debugf("scm_list entry %s in %d duplicates %d", dev, idx, seenIn)
				return FaultConfigDuplicateScmDeviceList(idx, seenIn)
			}
			seenScmSet[dev] = idx
		}

		bdevConf := srv.Storage.Bdev
		for _, dev := range bdevConf.DeviceList {
			if seenIn, exists := seenBdevSet[dev]; exists {
				log.Debugf("bdev_list entry %s in %d overlaps %d", dev, idx, seenIn)
				return FaultConfigOverlappingBdevDeviceList(idx, seenIn)
			}
			seenBdevSet[dev] = idx
		}

		ndc, err := c.getDeviceClassFn(srv.Fabric.Interface)
		if err != nil {
			return err
		}

		switch idx {
		case 0:
			netDevClass = ndc
		default:
			if ndc != netDevClass {
				return FaultConfigInvalidNetDevClass(idx, netDevClass, ndc, srv.Fabric.Interface)
			}
		}
	}

	return nil
}
