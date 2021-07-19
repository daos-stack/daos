//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package config

import (
	"context"
	"fmt"
	"io/ioutil"
	"net"
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
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
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

// Server describes configuration options for DAOS control plane.
// See utils/config/daos_server.yml for parameter descriptions.
type Server struct {
	// control-specific
	ControlPort     int                       `yaml:"port"`
	TransportConfig *security.TransportConfig `yaml:"transport_config"`
	// Detect outdated "servers" config, to direct users to change their config file
	Servers             []*engine.Config `yaml:"servers,omitempty"`
	Engines             []*engine.Config `yaml:"engines"`
	BdevInclude         []string         `yaml:"bdev_include,omitempty"`
	BdevExclude         []string         `yaml:"bdev_exclude,omitempty"`
	DisableVFIO         bool             `yaml:"disable_vfio"`
	DisableVMD          bool             `yaml:"disable_vmd"`
	NrHugepages         int              `yaml:"nr_hugepages"`
	ControlLogMask      ControlLogLevel  `yaml:"control_log_mask"`
	ControlLogFile      string           `yaml:"control_log_file"`
	ControlLogJSON      bool             `yaml:"control_log_json,omitempty"`
	HelperLogFile       string           `yaml:"helper_log_file"`
	FWHelperLogFile     string           `yaml:"firmware_helper_log_file"`
	RecreateSuperblocks bool             `yaml:"recreate_superblocks"`
	FaultPath           string           `yaml:"fault_path"`
	TelemetryPort       int              `yaml:"telemetry_port"`

	// duplicated in engine.Config
	SystemName string              `yaml:"name"`
	SocketDir  string              `yaml:"socket_dir"`
	Fabric     engine.FabricConfig `yaml:",inline"`
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

	// pointer to a function that retrieves the I/O Engine network device class
	GetDeviceClassFn networkDeviceClass `yaml:"-"`
}

// WithRecreateSuperblocks indicates that a missing superblock should not be treated as
// an error. The server will create new superblocks as necessary.
func (cfg *Server) WithRecreateSuperblocks() *Server {
	cfg.RecreateSuperblocks = true
	return cfg
}

// WithProviderValidator sets the function that validates the provider
func (cfg *Server) WithProviderValidator(fn networkProviderValidation) *Server {
	cfg.validateProviderFn = fn
	return cfg
}

// WithNUMAValidator sets the function that validates the NUMA configuration
func (cfg *Server) WithNUMAValidator(fn networkNUMAValidation) *Server {
	cfg.validateNUMAFn = fn
	return cfg
}

// WithGetNetworkDeviceClass sets the function that determines the network device class
func (cfg *Server) WithGetNetworkDeviceClass(fn networkDeviceClass) *Server {
	cfg.GetDeviceClassFn = fn
	return cfg
}

// WithSystemName sets the system name.
func (cfg *Server) WithSystemName(name string) *Server {
	cfg.SystemName = name
	for _, engine := range cfg.Engines {
		engine.WithSystemName(name)
	}
	return cfg
}

// WithSocketDir sets the default socket directory.
func (cfg *Server) WithSocketDir(sockDir string) *Server {
	cfg.SocketDir = sockDir
	for _, engine := range cfg.Engines {
		engine.WithSocketDir(sockDir)
	}
	return cfg
}

// WithModules sets a list of server modules to load.
func (cfg *Server) WithModules(mList string) *Server {
	cfg.Modules = mList
	for _, engine := range cfg.Engines {
		engine.WithModules(mList)
	}
	return cfg
}

// WithFabricProvider sets the top-level fabric provider.
func (cfg *Server) WithFabricProvider(provider string) *Server {
	cfg.Fabric.Provider = provider
	for _, engine := range cfg.Engines {
		engine.Fabric.Update(cfg.Fabric)
	}
	return cfg
}

// WithCrtCtxShareAddr sets the top-level CrtCtxShareAddr.
func (cfg *Server) WithCrtCtxShareAddr(addr uint32) *Server {
	cfg.Fabric.CrtCtxShareAddr = addr
	for _, engine := range cfg.Engines {
		engine.Fabric.Update(cfg.Fabric)
	}
	return cfg
}

// WithCrtTimeout sets the top-level CrtTimeout.
func (cfg *Server) WithCrtTimeout(timeout uint32) *Server {
	cfg.Fabric.CrtTimeout = timeout
	for _, engine := range cfg.Engines {
		engine.Fabric.Update(cfg.Fabric)
	}
	return cfg
}

// NB: In order to ease maintenance, the set of chained config functions
// which modify nested engine configurations should be kept above this
// one as a reference for which things should be set/updated in the next
// function.
func (cfg *Server) updateServerConfig(cfgPtr **engine.Config) {
	// If we somehow get a nil config, we can't return an error, and
	// we don't want to cause a segfault. Instead, just create an
	// empty config and return early, so that it eventually fails
	// validation.
	if *cfgPtr == nil {
		*cfgPtr = &engine.Config{}
		return
	}

	engineCfg := *cfgPtr
	engineCfg.Fabric.Update(cfg.Fabric)
	engineCfg.SystemName = cfg.SystemName
	engineCfg.SocketDir = cfg.SocketDir
	engineCfg.Modules = cfg.Modules
}

// WithEngines sets the list of engine configurations.
func (cfg *Server) WithEngines(engineList ...*engine.Config) *Server {
	cfg.Engines = engineList
	for i := range cfg.Engines {
		cfg.updateServerConfig(&cfg.Engines[i])
	}
	return cfg
}

// WithAccessPoints sets the access point list.
func (cfg *Server) WithAccessPoints(aps ...string) *Server {
	cfg.AccessPoints = aps
	return cfg
}

// WithControlPort sets the gRPC listener port.
func (cfg *Server) WithControlPort(port int) *Server {
	cfg.ControlPort = port
	return cfg
}

// WithTransportConfig sets the gRPC transport configuration.
func (cfg *Server) WithTransportConfig(cfgTransport *security.TransportConfig) *Server {
	cfg.TransportConfig = cfgTransport
	return cfg
}

// WithFaultPath sets the fault path (identification string e.g. rack/shelf/node).
func (cfg *Server) WithFaultPath(fp string) *Server {
	cfg.FaultPath = fp
	return cfg
}

// WithFaultCb sets the path to the fault callback script.
func (cfg *Server) WithFaultCb(cb string) *Server {
	cfg.FaultCb = cb
	return cfg
}

// WithBdevExclude sets the block device exclude list.
func (cfg *Server) WithBdevExclude(bList ...string) *Server {
	cfg.BdevExclude = bList
	return cfg
}

// WithBdevInclude sets the block device include list.
func (cfg *Server) WithBdevInclude(bList ...string) *Server {
	cfg.BdevInclude = bList
	return cfg
}

// WithDisableVFIO indicates that the vfio-pci driver should not be
// used by SPDK even if an IOMMU is detected. Note that this option
// requires that DAOS be run as root.
func (cfg *Server) WithDisableVFIO(disabled bool) *Server {
	cfg.DisableVFIO = disabled
	return cfg
}

// WithDisableVMD indicates that vmd devices should not be used even if they
// exist.
func (cfg *Server) WithDisableVMD(disabled bool) *Server {
	cfg.DisableVMD = disabled
	return cfg
}

// WithHyperthreads enables or disables hyperthread support.
func (cfg *Server) WithHyperthreads(enabled bool) *Server {
	cfg.Hyperthreads = enabled
	return cfg
}

// WithNrHugePages sets the number of huge pages to be used.
func (cfg *Server) WithNrHugePages(nr int) *Server {
	cfg.NrHugepages = nr
	return cfg
}

// WithControlLogMask sets the daos_server log level.
func (cfg *Server) WithControlLogMask(lvl ControlLogLevel) *Server {
	cfg.ControlLogMask = lvl
	return cfg
}

// WithControlLogFile sets the path to the daos_server logfile.
func (cfg *Server) WithControlLogFile(filePath string) *Server {
	cfg.ControlLogFile = filePath
	return cfg
}

// WithControlLogJSON enables or disables JSON output.
func (cfg *Server) WithControlLogJSON(enabled bool) *Server {
	cfg.ControlLogJSON = enabled
	return cfg
}

// WithHelperLogFile sets the path to the daos_admin logfile.
func (cfg *Server) WithHelperLogFile(filePath string) *Server {
	cfg.HelperLogFile = filePath
	return cfg
}

// WithFirmwareHelperLogFile sets the path to the daos_firmware logfile.
func (cfg *Server) WithFirmwareHelperLogFile(filePath string) *Server {
	cfg.FWHelperLogFile = filePath
	return cfg
}

// WithTelemetryPort sets the port for the telemetry exporter.
func (cfg *Server) WithTelemetryPort(port int) *Server {
	cfg.TelemetryPort = port
	return cfg
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
func (cfg *Server) Load() error {
	if cfg.Path == "" {
		return FaultConfigNoPath
	}

	bytes, err := ioutil.ReadFile(cfg.Path)
	if err != nil {
		return errors.WithMessage(err, "reading file")
	}

	if err = yaml.UnmarshalStrict(bytes, cfg); err != nil {
		return errors.WithMessage(err, "parse failed; config contains invalid "+
			"parameters and may be out of date, see server config examples")
	}

	// propagate top-level settings to server configs
	for i := range cfg.Engines {
		cfg.updateServerConfig(&cfg.Engines[i])
	}

	return nil
}

// SaveToFile serializes the configuration and saves it to the specified filename.
func (cfg *Server) SaveToFile(filename string) error {
	bytes, err := yaml.Marshal(cfg)

	if err != nil {
		return err
	}

	return ioutil.WriteFile(filename, bytes, 0644)
}

// SetPath sets the default path to the configuration file.
func (cfg *Server) SetPath(inPath string) error {
	newPath, err := common.ResolvePath(inPath, cfg.Path)
	if err != nil {
		return err
	}
	cfg.Path = newPath

	if _, err = os.Stat(cfg.Path); err != nil {
		return err
	}

	return err
}

// SaveActiveConfig saves read-only active config, tries config dir then /tmp/.
func (cfg *Server) SaveActiveConfig(log logging.Logger) {
	activeConfig := filepath.Join(cfg.SocketDir, configOut)

	if err := cfg.SaveToFile(activeConfig); err != nil {
		log.Debugf("active config could not be saved: %s", err.Error())

		return
	}
	log.Debugf("active config saved to %s (read-only)", activeConfig)
}

func getAccessPointAddrWithPort(log logging.Logger, addr string, portDefault int) (string, error) {
	if !common.HasPort(addr) {
		return fmt.Sprintf("%s:%d", addr, portDefault), nil
	}

	host, port, err := net.SplitHostPort(addr)
	if err != nil {
		log.Errorf("invalid access point %q: %s", addr, err)
		return "", FaultConfigBadAccessPoints
	}

	portNum, err := strconv.Atoi(port)
	if err != nil {
		log.Errorf("invalid access point port: %s", err)
		return "", FaultConfigBadControlPort
	}
	if portNum <= 0 {
		m := "zero"
		if portNum < 0 {
			m = "negative"
		}
		log.Errorf("access point port cannot be %s", m)
		return "", FaultConfigBadControlPort
	}

	// warn if access point port differs from config control port
	if portDefault != portNum {
		log.Debugf("access point (%s) port (%s) differs from default port (%d)",
			host, port, portDefault)
	}

	return addr, nil
}

// Validate asserts that config meets minimum requirements.
func (cfg *Server) Validate(log logging.Logger) (err error) {
	msg := "validating config file"
	if cfg.Path != "" {
		msg += fmt.Sprintf(" read from %q", cfg.Path)
	}
	log.Debug(msg)

	// Append the user-friendly message to any error.
	defer func() {
		if err != nil && !fault.HasResolution(err) {
			examplesPath, _ := common.GetAdjacentPath(relConfExamplesPath)
			err = errors.WithMessage(FaultBadConfig, err.Error()+", examples: "+examplesPath)
		}
	}()

	// The config file format no longer supports "servers"
	if len(cfg.Servers) > 0 {
		return errors.New("\"servers\" server config file parameter is deprecated, use \"engines\" instead")
	}

	for idx, ec := range cfg.Engines {
		if ec.LegacyStorage.WasDefined() {
			log.Infof("engine %d: Legacy storage configuration detected. Please migrate to new-style storage configuration.", idx)
			var tierCfgs storage.TierConfigs
			if ec.LegacyStorage.ScmClass != storage.ClassNone {
				tierCfgs = append(tierCfgs,
					storage.NewTierConfig().
						WithScmClass(ec.LegacyStorage.ScmClass.String()).
						WithScmDeviceList(ec.LegacyStorage.ScmConfig.DeviceList...).
						WithScmMountPoint(ec.LegacyStorage.MountPoint).
						WithScmRamdiskSize(ec.LegacyStorage.RamdiskSize),
				)
			}

			// Do not add bdev tier if cls is none or nvme has no
			// devices to maintain backward compatible behavior.
			bc := ec.LegacyStorage.BdevClass
			switch {
			case bc == storage.ClassNvme && len(ec.LegacyStorage.BdevConfig.DeviceList) == 0:
			case bc == storage.ClassNone:
			default:
				tierCfgs = append(tierCfgs,
					storage.NewTierConfig().
						WithBdevClass(ec.LegacyStorage.BdevClass.String()).
						WithBdevDeviceCount(ec.LegacyStorage.DeviceCount).
						WithBdevDeviceList(ec.LegacyStorage.BdevConfig.DeviceList...).
						WithBdevFileSize(ec.LegacyStorage.FileSize),
				)
			}
			ec.WithStorage(tierCfgs...)
			ec.LegacyStorage = engine.LegacyStorage{}
		}
	}

	// A config without engines is valid when initially discovering hardware
	// prior to adding per-engine sections with device allocations.
	if len(cfg.Engines) == 0 {
		log.Infof("No %ss in configuration, %s starting in discovery mode", build.DataPlaneName,
			build.ControlPlaneName)
		cfg.Engines = nil
		return nil
	}

	switch {
	case cfg.Fabric.Provider == "":
		return FaultConfigNoProvider
	case cfg.ControlPort <= 0:
		return FaultConfigBadControlPort
	case cfg.TelemetryPort < 0:
		return FaultConfigBadTelemetryPort
	}

	// Update access point addresses with control port if port is not
	// supplied.
	newAPs := make([]string, 0, len(cfg.AccessPoints))
	for _, ap := range cfg.AccessPoints {
		newAP, err := getAccessPointAddrWithPort(log, ap, cfg.ControlPort)
		if err != nil {
			return err
		}
		newAPs = append(newAPs, newAP)
	}
	if common.StringSliceHasDuplicates(newAPs) {
		log.Error("duplicate access points addresses")
		return FaultConfigBadAccessPoints
	}
	cfg.AccessPoints = newAPs

	switch {
	case len(cfg.AccessPoints) < 1:
		return FaultConfigBadAccessPoints
	case len(cfg.AccessPoints)%2 == 0:
		return FaultConfigEvenAccessPoints
	case len(cfg.AccessPoints) > 1:
		// temporary notification while the feature is still being polished.
		log.Info("\n*******\nNOTICE: Support for multiple access points is an alpha feature and is not well-tested!\n*******\n\n")
	}

	for i, engine := range cfg.Engines {
		engine.Fabric.Update(cfg.Fabric)
		if err := engine.Validate(); err != nil {
			return errors.Wrapf(err, "I/O Engine %d failed config validation", i)
		}
	}

	if len(cfg.Engines) > 1 {
		if err := cfg.validateMultiServerConfig(log); err != nil {
			return err
		}
	}

	return nil
}

// validateMultiServerConfig performs an extra level of validation
// for multi-server configs. The goal is to ensure that each instance
// has unique values for resources which cannot be shared (e.g. log files,
// fabric configurations, PCI devices, etc.)
func (cfg *Server) validateMultiServerConfig(log logging.Logger) error {
	if len(cfg.Engines) < 2 {
		return nil
	}

	seenValues := make(map[string]int)
	seenScmSet := make(map[string]int)
	seenBdevSet := make(map[string]int)

	for idx, engine := range cfg.Engines {
		fabricConfig := fmt.Sprintf("fabric:%s-%s-%d",
			engine.Fabric.Provider,
			engine.Fabric.Interface,
			engine.Fabric.InterfacePort)

		if seenIn, exists := seenValues[fabricConfig]; exists {
			log.Debugf("%s in %d duplicates %d", fabricConfig, idx, seenIn)
			return FaultConfigDuplicateFabric(idx, seenIn)
		}
		seenValues[fabricConfig] = idx

		if engine.LogFile != "" {
			logConfig := fmt.Sprintf("log_file:%s", engine.LogFile)
			if seenIn, exists := seenValues[logConfig]; exists {
				log.Debugf("%s in %d duplicates %d", logConfig, idx, seenIn)
				return FaultConfigDuplicateLogFile(idx, seenIn)
			}
			seenValues[logConfig] = idx
		}

		for _, scmConf := range engine.Storage.Tiers.ScmConfigs() {
			mountConfig := fmt.Sprintf("scm_mount:%s", scmConf.Scm.MountPoint)
			if seenIn, exists := seenValues[mountConfig]; exists {
				log.Debugf("%s in %d duplicates %d", mountConfig, idx, seenIn)
				return FaultConfigDuplicateScmMount(idx, seenIn)
			}
			seenValues[mountConfig] = idx
		}

		for _, scmConf := range engine.Storage.Tiers.ScmConfigs() {
			for _, dev := range scmConf.Scm.DeviceList {
				if seenIn, exists := seenScmSet[dev]; exists {
					log.Debugf("scm_list entry %s in %d duplicates %d", dev, idx, seenIn)
					return FaultConfigDuplicateScmDeviceList(idx, seenIn)
				}
				seenScmSet[dev] = idx
			}
		}

		for _, bdevConf := range engine.Storage.Tiers.BdevConfigs() {
			for _, dev := range bdevConf.Bdev.DeviceList {
				if seenIn, exists := seenBdevSet[dev]; exists {
					log.Debugf("bdev_list entry %s in %d overlaps %d", dev, idx, seenIn)
					return FaultConfigOverlappingBdevDeviceList(idx, seenIn)
				}
				seenBdevSet[dev] = idx
			}
		}
	}

	return nil
}

// validateEngineFabric ensures engine configuration parameters are valid.
func (cfg *Server) validateEngineFabric(ctx context.Context, cfgEngine *engine.Config) error {
	if err := cfg.validateProviderFn(ctx, cfgEngine.Fabric.Interface, cfgEngine.Fabric.Provider); err != nil {
		return errors.Wrapf(err, "Network device %s does not support provider %s. "+
			"The configuration is invalid.", cfgEngine.Fabric.Interface,
			cfgEngine.Fabric.Provider)
	}

	// check to see if pinned numa node was provided in the engine config
	numaNode, err := cfgEngine.Fabric.GetNumaNode()
	if err != nil {
		// as pinned_numa_node is an optional config file parameter,
		// error is considered non-fatal
		if err == engine.ErrNoPinnedNumaNode {
			return nil
		}
		return err
	}
	// validate that numa node is correct for the given device
	if err := cfg.validateNUMAFn(ctx, cfgEngine.Fabric.Interface, numaNode); err != nil {
		return errors.Wrapf(err, "Network device %s on NUMA node %d is an "+
			"invalid configuration.", cfgEngine.Fabric.Interface, numaNode)
	}

	return nil
}

// CheckFabric ensures engines in configuration have compatible parameter
// values and returns fabric network device class for the configuration.
func (cfg *Server) CheckFabric(ctx context.Context) (uint32, error) {
	var netDevClass uint32
	for index, engine := range cfg.Engines {
		ndc, err := cfg.GetDeviceClassFn(engine.Fabric.Interface)
		if err != nil {
			return 0, err
		}
		if index == 0 {
			netDevClass = ndc
			if err := cfg.validateEngineFabric(ctx, engine); err != nil {
				return 0, err
			}
			continue
		}
		if ndc != netDevClass {
			return 0, FaultConfigInvalidNetDevClass(index, netDevClass,
				ndc, engine.Fabric.Interface)
		}
	}

	return netDevClass, nil
}
