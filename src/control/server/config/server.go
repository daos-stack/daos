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

package config

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

// Server describes configuration options for DAOS control plane.
// See utils/config/daos_server.yml for parameter descriptions.
type Server struct {
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

	Path string // path to config file

	// pointer to a function that validates the chosen provider
	validateProviderFn networkProviderValidation

	// pointer to a function that validates the chosen numa node
	validateNUMAFn networkNUMAValidation

	// pointer to a function that retrieves the IO server network device class
	GetDeviceClassFn networkDeviceClass `yaml:"-"`
}

// WithRecreateSuperblocks indicates that a missing superblock should not be treated as
// an error. The server will create new superblocks as necessary.
func (c *Server) WithRecreateSuperblocks() *Server {
	c.RecreateSuperblocks = true
	return c
}

// WithProviderValidator sets the function that validates the provider
func (c *Server) WithProviderValidator(fn networkProviderValidation) *Server {
	c.validateProviderFn = fn
	return c
}

// WithNUMAValidator sets the function that validates the NUMA configuration
func (c *Server) WithNUMAValidator(fn networkNUMAValidation) *Server {
	c.validateNUMAFn = fn
	return c
}

// WithGetNetworkDeviceClass sets the function that determines the network device class
func (c *Server) WithGetNetworkDeviceClass(fn networkDeviceClass) *Server {
	c.GetDeviceClassFn = fn
	return c
}

// WithSystemName sets the system name.
func (c *Server) WithSystemName(name string) *Server {
	c.SystemName = name
	for _, srv := range c.Servers {
		srv.WithSystemName(name)
	}
	return c
}

// WithSocketDir sets the default socket directory.
func (c *Server) WithSocketDir(sockDir string) *Server {
	c.SocketDir = sockDir
	for _, srv := range c.Servers {
		srv.WithSocketDir(sockDir)
	}
	return c
}

// WithModules sets a list of server modules to load.
func (c *Server) WithModules(mList string) *Server {
	c.Modules = mList
	for _, srv := range c.Servers {
		srv.WithModules(mList)
	}
	return c
}

// WithFabricProvider sets the top-level fabric provider.
func (c *Server) WithFabricProvider(provider string) *Server {
	c.Fabric.Provider = provider
	for _, srv := range c.Servers {
		srv.Fabric.Update(c.Fabric)
	}
	return c
}

// WithCrtCtxShareAddr sets the top-level CrtCtxShareAddr.
func (c *Server) WithCrtCtxShareAddr(addr uint32) *Server {
	c.Fabric.CrtCtxShareAddr = addr
	for _, srv := range c.Servers {
		srv.Fabric.Update(c.Fabric)
	}
	return c
}

// WithCrtTimeout sets the top-level CrtTimeout.
func (c *Server) WithCrtTimeout(timeout uint32) *Server {
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
func (c *Server) updateServerConfig(cfgPtr **ioserver.Config) {
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
func (c *Server) WithServers(srvList ...*ioserver.Config) *Server {
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
func (c *Server) WithScmMountPoint(mp string) *Server {
	if len(c.Servers) > 0 {
		c.Servers[0].WithScmMountPoint(mp)
	}
	return c
}

// WithAccessPoints sets the access point list.
func (c *Server) WithAccessPoints(aps ...string) *Server {
	c.AccessPoints = aps
	return c
}

// WithControlPort sets the gRPC listener port.
func (c *Server) WithControlPort(port int) *Server {
	c.ControlPort = port
	return c
}

// WithTransportConfig sets the gRPC transport configuration.
func (c *Server) WithTransportConfig(cfg *security.TransportConfig) *Server {
	c.TransportConfig = cfg
	return c
}

// WithFaultPath sets the fault path (identification string e.g. rack/shelf/node).
func (c *Server) WithFaultPath(fp string) *Server {
	c.FaultPath = fp
	return c
}

// WithFaultCb sets the path to the fault callback script.
func (c *Server) WithFaultCb(cb string) *Server {
	c.FaultCb = cb
	return c
}

// WithBdevExclude sets the block device exclude list.
func (c *Server) WithBdevExclude(bList ...string) *Server {
	c.BdevExclude = bList
	return c
}

// WithBdevInclude sets the block device include list.
func (c *Server) WithBdevInclude(bList ...string) *Server {
	c.BdevInclude = bList
	return c
}

// WithDisableVFIO indicates that the vfio-pci driver should not be
// used by SPDK even if an IOMMU is detected. Note that this option
// requires that DAOS be run as root.
func (c *Server) WithDisableVFIO(disabled bool) *Server {
	c.DisableVFIO = disabled
	return c
}

// WithDisableVMD indicates that vmd devices should not be used even if they
// exist.
func (c *Server) WithDisableVMD(disabled bool) *Server {
	c.DisableVMD = disabled
	return c
}

// WithHyperthreads enables or disables hyperthread support.
func (c *Server) WithHyperthreads(enabled bool) *Server {
	c.Hyperthreads = enabled
	return c
}

// WithNrHugePages sets the number of huge pages to be used.
func (c *Server) WithNrHugePages(nr int) *Server {
	c.NrHugepages = nr
	return c
}

// WithControlLogMask sets the daos_server log level.
func (c *Server) WithControlLogMask(lvl ControlLogLevel) *Server {
	c.ControlLogMask = lvl
	return c
}

// WithControlLogFile sets the path to the daos_server logfile.
func (c *Server) WithControlLogFile(filePath string) *Server {
	c.ControlLogFile = filePath
	return c
}

// WithControlLogJSON enables or disables JSON output.
func (c *Server) WithControlLogJSON(enabled bool) *Server {
	c.ControlLogJSON = enabled
	return c
}

// WithHelperLogFile sets the path to the daos_admin logfile.
func (c *Server) WithHelperLogFile(filePath string) *Server {
	c.HelperLogFile = filePath
	return c
}

// WithFirmwareHelperLogFile sets the path to the daos_firmware logfile.
func (c *Server) WithFirmwareHelperLogFile(filePath string) *Server {
	c.FWHelperLogFile = filePath
	return c
}

// DefaultServer creates a new instance of configuration struct
// populated with defaults.
func DefaultServer() *Server {
	return &Server{
		SystemName:         build.DefaultSystemName,
		SocketDir:          defaultRuntimeDir,
		AccessPoints:       []string{fmt.Sprintf("localhost:%d", build.DefaultControlPort)},
		ControlPort:        build.DefaultControlPort,
		TransportConfig:    security.DefaultServerTransportConfig(),
		Hyperthreads:       false,
		Path:               defaultConfigPath,
		ControlLogMask:     ControlLogLevel(logging.LogLevelInfo),
		validateProviderFn: netdetect.ValidateProviderStub,
		validateNUMAFn:     netdetect.ValidateNUMAStub,
		GetDeviceClassFn:   netdetect.GetDeviceClass,
		DisableVMD:         true, // support currently unstable
	}
}

// Load reads the serialized configuration from disk and validates it.
func (c *Server) Load() error {
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
func (c *Server) SaveToFile(filename string) error {
	bytes, err := yaml.Marshal(c)

	if err != nil {
		return err
	}

	return ioutil.WriteFile(filename, bytes, 0644)
}

// SetPath sets the default path to the configuration file.
func (c *Server) SetPath(inPath string) error {
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

// SaveActiveConfig saves read-only active config, tries config dir then /tmp/
func SaveActiveConfig(log logging.Logger, config *Server) {
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
func (c *Server) Validate(log logging.Logger) (err error) {
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
			examplesPath, _ := common.GetAdjacentPath(relConfExamplesPath)
			err = errors.WithMessage(FaultBadConfig, err.Error()+", examples: "+examplesPath)
		}
	}()

	if c.Fabric.Provider == "" {
		return FaultConfigNoProvider
	}

	switch {
	case len(c.AccessPoints) < 1:
		return FaultConfigBadAccessPoints
	case len(c.AccessPoints)%2 == 0:
		return FaultConfigEvenAccessPoints
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
func validateMultiServerConfig(log logging.Logger, c *Server) error {
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

		ndc, err := c.GetDeviceClassFn(srv.Fabric.Interface)
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
