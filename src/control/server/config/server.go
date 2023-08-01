//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package config

import (
	"fmt"
	"io/ioutil"
	"math"
	"net"
	"os"
	"path/filepath"
	"strconv"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"
	yaml "gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	defaultRuntimeDir   = "/var/run/daos_server"
	defaultConfigPath   = "../etc/daos_server.yml"
	ConfigOut           = ".daos_server.active.yml"
	relConfExamplesPath = "../utils/config/examples/"
)

// Server describes configuration options for DAOS control plane.
// See utils/config/daos_server.yml for parameter descriptions.
type Server struct {
	// control-specific
	ControlPort         int                       `yaml:"port"`
	TransportConfig     *security.TransportConfig `yaml:"transport_config"`
	Engines             []*engine.Config          `yaml:"engines"`
	BdevExclude         []string                  `yaml:"bdev_exclude,omitempty"`
	DisableVFIO         bool                      `yaml:"disable_vfio"`
	DisableVMD          *bool                     `yaml:"disable_vmd"`
	EnableHotplug       bool                      `yaml:"enable_hotplug"`
	NrHugepages         int                       `yaml:"nr_hugepages"`        // total for all engines
	SystemRamReserved   int                       `yaml:"system_ram_reserved"` // total for all engines
	DisableHugepages    bool                      `yaml:"disable_hugepages"`
	ControlLogMask      common.ControlLogLevel    `yaml:"control_log_mask"`
	ControlLogFile      string                    `yaml:"control_log_file,omitempty"`
	ControlLogJSON      bool                      `yaml:"control_log_json,omitempty"`
	HelperLogFile       string                    `yaml:"helper_log_file,omitempty"`
	FWHelperLogFile     string                    `yaml:"firmware_helper_log_file,omitempty"`
	RecreateSuperblocks bool                      `yaml:"recreate_superblocks,omitempty"`
	FaultPath           string                    `yaml:"fault_path,omitempty"`
	TelemetryPort       int                       `yaml:"telemetry_port,omitempty"`
	CoreDumpFilter      uint8                     `yaml:"core_dump_filter,omitempty"`
	ClientEnvVars       []string                  `yaml:"client_env_vars,omitempty"`

	// duplicated in engine.Config
	SystemName string              `yaml:"name"`
	SocketDir  string              `yaml:"socket_dir"`
	Fabric     engine.FabricConfig `yaml:",inline"`
	Modules    string              `yaml:"-"`

	AccessPoints []string `yaml:"access_points"`

	Metadata storage.ControlMetadata `yaml:"control_metadata,omitempty"`

	// unused (?)
	FaultCb      string `yaml:"fault_cb"`
	Hyperthreads bool   `yaml:"hyperthreads"`

	Path string `yaml:"-"` // path to config file

	// Legacy config file parameters stored in a separate struct.
	Legacy ServerLegacy `yaml:",inline"`
}

// WithCoreDumpFilter sets the core dump filter written to /proc/self/coredump_filter.
func (cfg *Server) WithCoreDumpFilter(filter uint8) *Server {
	cfg.CoreDumpFilter = filter
	return cfg
}

// WithRecreateSuperblocks indicates that a missing superblock should not be treated as
// an error. The server will create new superblocks as necessary.
func (cfg *Server) WithRecreateSuperblocks() *Server {
	cfg.RecreateSuperblocks = true
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
		engine.Fabric.Provider = cfg.Fabric.Provider
	}
	return cfg
}

// WithFabricAuthKey sets the top-level fabric authorization key.
func (cfg *Server) WithFabricAuthKey(key string) *Server {
	cfg.Fabric.AuthKey = key
	cfg.ClientEnvVars = common.MergeKeyValues(cfg.ClientEnvVars, []string{cfg.Fabric.GetAuthKeyEnv()})
	for _, engine := range cfg.Engines {
		engine.Fabric.AuthKey = cfg.Fabric.AuthKey
	}
	return cfg
}

// WithClientEnvVars sets the environment variables to be sent to the client.
func (cfg *Server) WithClientEnvVars(envVars []string) *Server {
	cfg.ClientEnvVars = envVars
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

// WithControlMetadata sets the control plane metadata.
func (cfg *Server) WithControlMetadata(md storage.ControlMetadata) *Server {
	cfg.Metadata = md
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
	engineCfg.Storage.EnableHotplug = cfg.EnableHotplug
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

// WithDisableVFIO indicates that the vfio-pci driver should not be
// used by SPDK even if an IOMMU is detected. Note that this option
// requires that DAOS be run as root.
func (cfg *Server) WithDisableVFIO(disabled bool) *Server {
	cfg.DisableVFIO = disabled
	return cfg
}

// WithDisableVMD can be used to set the state of VMD functionality,
// if disabled then VMD devices will not be used if they exist.
func (cfg *Server) WithDisableVMD(disabled bool) *Server {
	cfg.DisableVMD = &disabled
	return cfg
}

// WithEnableHotplug can be used to enable hotplug
func (cfg *Server) WithEnableHotplug(enabled bool) *Server {
	cfg.EnableHotplug = enabled
	return cfg
}

// WithHyperthreads enables or disables hyperthread support.
func (cfg *Server) WithHyperthreads(enabled bool) *Server {
	cfg.Hyperthreads = enabled
	return cfg
}

// WithNrHugepages sets the number of huge pages to be used (total for all engines).
func (cfg *Server) WithNrHugepages(nr int) *Server {
	cfg.NrHugepages = nr
	return cfg
}

// WithDisableHugepages disables the use of huge pages.
func (cfg *Server) WithDisableHugepages(disabled bool) *Server {
	cfg.DisableHugepages = disabled
	return cfg
}

// WithSystemRamReserved sets the amount of system memory to reserve for system (non-DAOS)
// use. In units of GiB.
func (cfg *Server) WithSystemRamReserved(nr int) *Server {
	cfg.SystemRamReserved = nr
	return cfg
}

// WithControlLogMask sets the daos_server log level.
func (cfg *Server) WithControlLogMask(lvl common.ControlLogLevel) *Server {
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

// WithHelperLogFile sets the path to the daos_server_helper logfile.
func (cfg *Server) WithHelperLogFile(filePath string) *Server {
	cfg.HelperLogFile = filePath
	return cfg
}

// WithFirmwareHelperLogFile sets the path to the daos_firmware_helper logfile.
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
		SystemName:        build.DefaultSystemName,
		SocketDir:         defaultRuntimeDir,
		AccessPoints:      []string{fmt.Sprintf("localhost:%d", build.DefaultControlPort)},
		ControlPort:       build.DefaultControlPort,
		TransportConfig:   security.DefaultServerTransportConfig(),
		Hyperthreads:      false,
		SystemRamReserved: storage.DefaultSysMemRsvd / humanize.GiByte,
		Path:              defaultConfigPath,
		ControlLogMask:    common.ControlLogLevel(logging.LogLevelInfo),
		EnableHotplug:     false, // disabled by default
		// https://man7.org/linux/man-pages/man5/core.5.html
		CoreDumpFilter: 0b00010011, // private, shared, ELF
	}
}

// Load reads the serialized configuration from disk and validates file syntax.
func (cfg *Server) Load() error {
	if cfg.Path == "" {
		return FaultConfigNoPath
	}

	bytes, err := ioutil.ReadFile(cfg.Path)
	if err != nil {
		return errors.WithMessage(err, "reading file")
	}

	if err = yaml.UnmarshalStrict(bytes, cfg); err != nil {
		return errors.WithMessagef(err, "parse of %q failed; config contains invalid "+
			"parameters and may be out of date, see server config examples",
			cfg.Path)
	}

	if !daos.SystemNameIsValid(cfg.SystemName) {
		return errors.Errorf("invalid system name: %q", cfg.SystemName)
	}

	// Update server config based on legacy parameters.
	if err := updateFromLegacyParams(cfg); err != nil {
		return errors.Wrap(err, "updating config from legacy parameters")
	}

	// propagate top-level settings to engine configs
	for i := range cfg.Engines {
		cfg.updateServerConfig(&cfg.Engines[i])
	}

	if cfg.Fabric.AuthKey != "" {
		cfg.ClientEnvVars = common.MergeKeyValues(cfg.ClientEnvVars, []string{cfg.Fabric.GetAuthKeyEnv()})
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
	activeConfig := filepath.Join(cfg.SocketDir, ConfigOut)

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

func hugePageBytes(hpNr, hpSz int) uint64 {
	return uint64(hpNr*hpSz) * humanize.KiByte
}

// SetNrHugepages calculates minimum based on total target count if using nvme.
func (cfg *Server) SetNrHugepages(log logging.Logger, mi *common.MemInfo) error {
	var cfgTargetCount int
	var sysXSCount int
	for idx, ec := range cfg.Engines {
		msg := fmt.Sprintf("engine %d fabric numa %d, storage numa %d", idx,
			ec.Fabric.NumaNodeIndex, ec.Storage.NumaNodeIndex)

		// Calculate overall target count if NVMe is enabled.
		if ec.Storage.Tiers.HaveBdevs() {
			cfgTargetCount += ec.TargetCount
			if ec.Storage.Tiers.HasBdevRoleMeta() {
				msg = fmt.Sprintf("%s (MD-on-SSD)", msg)
				// MD-on-SSD has extra sys-xstream for rdb.
				sysXSCount++
			}
		}
		log.Debug(msg)
	}

	if cfgTargetCount <= 0 {
		return nil // no nvme, no hugepages required
	}

	if cfg.DisableHugepages {
		return FaultConfigHugepagesDisabled
	}

	// Calculate minimum number of hugepages for all configured engines.
	minHugepages, err := storage.CalcMinHugepages(mi.HugepageSizeKiB, cfgTargetCount+sysXSCount)
	if err != nil {
		return err
	}

	// If the config doesn't specify hugepages, use the minimum. Otherwise, validate
	// that the configured amount is sufficient.
	if cfg.NrHugepages == 0 {
		var msgSysXS string
		if sysXSCount > 0 {
			msgSysXS = fmt.Sprintf(" and %d sys-xstreams", sysXSCount)
		}
		log.Debugf("calculated nr_hugepages: %d for %d targets%s", minHugepages,
			cfgTargetCount, msgSysXS)
		cfg.NrHugepages = minHugepages
		log.Infof("hugepage count automatically set to %d (%s)", minHugepages,
			humanize.IBytes(hugePageBytes(minHugepages, mi.HugepageSizeKiB)))
	}

	if cfg.NrHugepages < minHugepages {
		log.Noticef("configured nr_hugepages %d is less than recommended %d, "+
			"if this is not intentional update the 'nr_hugepages' config "+
			"parameter or remove and it will be automatically calculated",
			cfg.NrHugepages, minHugepages)
	}

	return nil
}

// CalcRamdiskSize calculates possible RAM-disk size using nr hugepages from config and total memory.
func (cfg *Server) CalcRamdiskSize(log logging.Logger, hpSizeKiB, memKiB int) (uint64, error) {
	// Convert memory from kib to bytes.
	memTotal := uint64(memKiB * humanize.KiByte)

	// Calculate assigned hugepage memory in bytes.
	memHuge := hugePageBytes(cfg.NrHugepages, hpSizeKiB)

	// Calculate reserved system memory in bytes.
	memSys := uint64(cfg.SystemRamReserved * humanize.GiByte)

	if len(cfg.Engines) == 0 {
		return 0, errors.New("no engines in config")
	}

	return storage.CalcRamdiskSize(log, memTotal, memHuge, memSys, cfg.Engines[0].TargetCount,
		len(cfg.Engines))
}

// CalcMemForRamdiskSize calculates minimum memory needed for a given RAM-disk size.
func (cfg *Server) CalcMemForRamdiskSize(log logging.Logger, hpSizeKiB int, ramdiskSize uint64) (uint64, error) {
	// Calculate assigned hugepage memory in bytes.
	memHuge := uint64(cfg.NrHugepages * hpSizeKiB * humanize.KiByte)

	// Calculate reserved system memory in bytes.
	memSys := uint64(cfg.SystemRamReserved * humanize.GiByte)

	if len(cfg.Engines) == 0 {
		return 0, errors.New("no engines in config")
	}

	return storage.CalcMemForRamdiskSize(log, ramdiskSize, memHuge, memSys,
		cfg.Engines[0].TargetCount, len(cfg.Engines))
}

// SetRamdiskSize calculates maximum RAM-disk size using total memory as reported by /proc/meminfo.
// Then either validate configured engine storage values or assign if not already set.
func (cfg *Server) SetRamdiskSize(log logging.Logger, mi *common.MemInfo) error {
	if len(cfg.Engines) == 0 {
		return nil // no engines
	}

	// Create the same size scm for each engine.
	scmCfgs := cfg.Engines[0].Storage.Tiers.ScmConfigs()

	if len(scmCfgs) == 0 || scmCfgs[0].Class != storage.ClassRam {
		return nil // no ramdisk to size
	}

	maxRamdiskSize, err := cfg.CalcRamdiskSize(log, mi.HugepageSizeKiB, mi.MemTotalKiB)
	if err != nil {
		return errors.Wrapf(err, "calculate ramdisk size")
	}

	memTotBytes := uint64(mi.MemTotalKiB) * humanize.KiByte

	msg := fmt.Sprintf("calculated max ram-disk size (%s) using MemTotal (%s)",
		humanize.IBytes(maxRamdiskSize), humanize.IBytes(memTotBytes))

	if maxRamdiskSize < storage.MinRamdiskMem {
		// Total RAM is insufficient to meet minimum size.
		log.Errorf("%s: insufficient total memory", msg)

		minMem, err := cfg.CalcMemForRamdiskSize(log, mi.HugepageSizeKiB,
			storage.MinRamdiskMem)
		if err != nil {
			log.Error(err.Error())
		}

		return storage.FaultRamdiskLowMem("Total", storage.MinRamdiskMem, minMem,
			memTotBytes)
	}

	for idx, ec := range cfg.Engines {
		scs := ec.Storage.Tiers.ScmConfigs()
		if len(scs) != 1 {
			return errors.Errorf("unexpected number of scm tiers, want 1 got %d",
				len(scs))
		}

		// Validate or set configured scm sizes based on calculated value.
		confSize := uint64(scs[0].Scm.RamdiskSize) * humanize.GiByte
		if confSize == 0 {
			// Apply calculated size in config as not already set.
			log.Debugf("%s: auto-sized ram-disk in engine-%d config", msg, idx)
			scs[0].WithScmRamdiskSize(uint(maxRamdiskSize / humanize.GiByte))
			log.Infof("engine-%d: ramdisk size automatically set to %s", idx,
				humanize.IBytes(maxRamdiskSize))
		} else if confSize > maxRamdiskSize {
			// Total RAM is not enough to meet tmpfs size requested in config.
			log.Errorf("%s: engine-%d config size too large for total memory", msg,
				idx)

			return FaultConfigRamdiskOverMaxMem(confSize, maxRamdiskSize,
				storage.MinRamdiskMem)
		}
	}

	return nil
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
			err = errors.WithMessage(FaultBadConfig, err.Error()+", examples: "+
				examplesPath)
		}
	}()

	// The config file format no longer supports "servers"
	if len(cfg.Legacy.Servers) > 0 {
		return errors.New("\"servers\" server config file parameter is deprecated, use " +
			"\"engines\" instead")
	}

	// Set DisableVMD reference if unset in config file.
	if cfg.DisableVMD == nil {
		cfg.WithDisableVMD(false)
	}

	log.Debugf("vfio=%v hotplug=%v vmd=%v requested in config", !cfg.DisableVFIO,
		cfg.EnableHotplug, !(*cfg.DisableVMD))

	// Update access point addresses with control port if port is not supplied.
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

	if cfg.Metadata.DevicePath != "" && cfg.Metadata.Path == "" {
		return FaultConfigControlMetadataNoPath
	}

	if cfg.SystemRamReserved <= 0 {
		return FaultConfigSysRsvdZero
	}

	// A config without engines is valid when initially discovering hardware prior to adding
	// per-engine sections with device allocations.
	if len(cfg.Engines) == 0 {
		log.Infof("No %ss in configuration, %s starting in discovery mode",
			build.DataPlaneName, build.ControlPlaneName)
		cfg.Engines = nil
		return nil
	}

	switch {
	case len(cfg.AccessPoints) < 1:
		return FaultConfigBadAccessPoints
	case len(cfg.AccessPoints)%2 == 0:
		return FaultConfigEvenAccessPoints
	case len(cfg.AccessPoints) == 1:
		log.Noticef("Configuration includes only one access point. This provides no redundancy " +
			"in the event of an access point failure.")
	}

	switch {
	case cfg.Fabric.Provider == "":
		return FaultConfigNoProvider
	case cfg.ControlPort <= 0:
		return FaultConfigBadControlPort
	case cfg.TelemetryPort < 0:
		return FaultConfigBadTelemetryPort
	}

	for idx, ec := range cfg.Engines {
		ec.Storage.ControlMetadata = cfg.Metadata
		ec.Storage.EngineIdx = uint(idx)
		ec.ConvertLegacyStorage(log, idx)
		ec.Fabric.Update(cfg.Fabric)

		if err := ec.Validate(); err != nil {
			return errors.Wrapf(err, "I/O Engine %d failed config validation", idx)
		}
	}

	if len(cfg.Engines) > 1 {
		if err := cfg.validateMultiEngineConfig(log); err != nil {
			return err
		}
	}

	if cfg.NrHugepages < 0 || cfg.NrHugepages > math.MaxInt32 {
		return FaultConfigNrHugepagesOutOfRange(cfg.NrHugepages, math.MaxInt32)
	}

	return nil
}

// validateMultiEngineConfig performs an extra level of validation for multi-server configs. The
// goal is to ensure that each instance has unique values for resources which cannot be shared
// (e.g. log files, fabric configurations, PCI devices, etc.)
func (cfg *Server) validateMultiEngineConfig(log logging.Logger) error {
	if len(cfg.Engines) < 2 {
		return nil
	}

	seenValues := make(map[string]int)
	seenScmSet := make(map[string]int)
	seenBdevSet := make(map[string]int)
	seenIdx := 0
	seenBdevCount := -1
	seenTargetCount := -1
	seenHelperStreamCount := -1
	seenScmCls := storage.ClassNone
	seenScmClsIdx := -1

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

			for _, dev := range scmConf.Scm.DeviceList {
				if seenIn, exists := seenScmSet[dev]; exists {
					log.Debugf("scm_list entry %s in %d duplicates %d", dev, idx, seenIn)
					return FaultConfigDuplicateScmDeviceList(idx, seenIn)
				}
				seenScmSet[dev] = idx
			}

			if seenScmClsIdx != -1 && scmConf.Class != seenScmCls {
				log.Debugf("scm_class entry %s in %d doesn't match %d",
					scmConf.Class, idx, seenScmClsIdx)
				return FaultConfigScmDiffClass(idx, seenScmClsIdx)
			}
			seenScmCls = scmConf.Class
			seenScmClsIdx = idx
		}

		bdevs := engine.Storage.GetBdevs()
		bdevCount := bdevs.Len()
		for _, dev := range bdevs.Devices() {
			if seenIn, exists := seenBdevSet[dev]; exists {
				log.Debugf("bdev_list entry %s in %d overlaps %d", dev, idx, seenIn)
				return FaultConfigOverlappingBdevDeviceList(idx, seenIn)
			}
			seenBdevSet[dev] = idx
		}
		if seenBdevCount != -1 && bdevCount != seenBdevCount {
			// Log error but don't fail in order to be lenient with unbalanced device
			// counts in particular cases e.g. using different capacity SSDs or VMDs
			// with different number of backing devices.
			err := FaultConfigBdevCountMismatch(idx, bdevCount, seenIdx, seenBdevCount)
			log.Noticef(err.Error())
		}
		if seenTargetCount != -1 && engine.TargetCount != seenTargetCount {
			return FaultConfigTargetCountMismatch(idx, engine.TargetCount, seenIdx,
				seenTargetCount)
		}
		if seenHelperStreamCount != -1 && engine.HelperStreamCount != seenHelperStreamCount {
			return FaultConfigHelperStreamCountMismatch(idx, engine.HelperStreamCount,
				seenIdx, seenHelperStreamCount)
		}
		seenIdx = idx
		seenBdevCount = bdevCount
		seenTargetCount = engine.TargetCount
		seenHelperStreamCount = engine.HelperStreamCount
	}

	return nil
}

var (
	// ErrNoAffinityDetected is a sentinel error used to indicate that no affinity was detected.
	ErrNoAffinityDetected = errors.New("no NUMA affinity detected")
)

// EngineAffinityFn defines a function which returns the NUMA node affinity of a given engine.
type EngineAffinityFn func(logging.Logger, *engine.Config) (uint, error)

func detectEngineAffinity(log logging.Logger, engineCfg *engine.Config, affSources ...EngineAffinityFn) (node uint, err error) {
	for _, affSource := range affSources {
		if affSource == nil {
			return 0, errors.New("nil affinity source")
		}

		node, err = affSource(log, engineCfg)
		if err == nil {
			return
		}

		if err != nil && err != ErrNoAffinityDetected {
			return
		}
	}

	return 0, ErrNoAffinityDetected
}

// SetEngineAffinities sets the NUMA node affinity for all engines in the configuration.
func (cfg *Server) SetEngineAffinities(log logging.Logger, affSources ...EngineAffinityFn) error {
	if len(affSources) == 0 {
		return errors.New("requires at least one affinity source")
	}
	defaultAffinity := uint(0)

	// Detect legacy mode by checking if first_core is being used.
	legacyMode := false
	for _, engineCfg := range cfg.Engines {
		if engineCfg.ServiceThreadCore != 0 {
			legacyMode = true
			break
		}
	}

	// Fail if any engine has an explicit pin and non-zero first_core.
	for idx, engineCfg := range cfg.Engines {
		if legacyMode {
			log.Debugf("setting legacy core allocation algorithm on engine %d", idx)
			engineCfg.PinnedNumaNode = nil
			continue
		}

		numaAffinity, err := detectEngineAffinity(log, engineCfg, affSources...)
		if err != nil {
			if err != ErrNoAffinityDetected {
				return errors.Wrap(err, "failure while detecting engine affinity")
			}
			log.Debugf("no NUMA affinity detected for engine %d; defaulting to %d", idx,
				defaultAffinity)
			numaAffinity = defaultAffinity
		} else {
			log.Debugf("detected NUMA affinity %d for engine %d", numaAffinity, idx)
		}

		// Special case: If only one engine is defined, NUMA is not pinned and engine's
		// detected NUMA node is zero, don't pin the engine to any NUMA node in order to
		// enable the engine's legacy core allocation algorithm.
		if len(cfg.Engines) == 1 && engineCfg.PinnedNumaNode == nil && numaAffinity == 0 {
			log.Debugf("setting legacy core allocation algorithm on engine %d", idx)
			continue
		}

		if err := engineCfg.SetNUMAAffinity(numaAffinity); err != nil {
			// For now, just log the error and continue.
			if engine.IsNUMAMismatch(err) {
				log.Noticef("engine %d: %s", idx, err)
				continue
			}
			return errors.Wrapf(err, "unable to set engine affinity to %d", numaAffinity)
		}
	}

	return nil
}
