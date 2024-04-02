//
// (C) Copyright 2019-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package engine

import (
	"fmt"
	"os"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	maxHelperStreamCount         = 2
	numPrimaryProviders          = 1
	defaultNumSecondaryEndpoints = 1

	// MultiProviderSeparator delineates between providers in a multi-provider config.
	MultiProviderSeparator = ","

	envLogMasks      = "D_LOG_MASK"
	envLogDbgStreams = "DD_MASK"
	envLogSubsystems = "DD_SUBSYS"
)

// FabricConfig encapsulates networking fabric configuration.
type FabricConfig struct {
	Provider        string `yaml:"provider,omitempty" cmdEnv:"CRT_PHY_ADDR_STR"`
	Interface       string `yaml:"fabric_iface,omitempty" cmdEnv:"OFI_INTERFACE"`
	InterfacePort   int    `yaml:"fabric_iface_port,omitempty" cmdEnv:"OFI_PORT,nonzero"`
	NumaNodeIndex   uint   `yaml:"-"`
	BypassHealthChk *bool  `yaml:"bypass_health_chk,omitempty" cmdLongFlag:"--bypass_health_chk" cmdShortFlag:"-b"`
	CrtCtxShareAddr uint32 `yaml:"crt_ctx_share_addr,omitempty" cmdEnv:"CRT_CTX_SHARE_ADDR"`
	CrtTimeout      uint32 `yaml:"crt_timeout,omitempty" cmdEnv:"CRT_TIMEOUT"`
	// NumSecondaryEndpoints configures the number of cart endpoints per secondary provider.
	NumSecondaryEndpoints []int  `yaml:"secondary_provider_endpoints,omitempty" cmdLongFlag:"--nr_sec_ctx,nonzero" cmdShortFlag:"-S,nonzero"`
	DisableSRX            bool   `yaml:"disable_srx,omitempty" cmdEnv:"FI_OFI_RXM_USE_SRX,invertBool,intBool"`
	AuthKey               string `yaml:"fabric_auth_key,omitempty" cmdEnv:"D_PROVIDER_AUTH_KEY"`
}

// GetPrimaryProvider parses the primary provider from the Provider string.
func (fc *FabricConfig) GetPrimaryProvider() (string, error) {
	providers, err := fc.GetProviders()
	if err != nil {
		return "", err
	}

	return providers[0], nil
}

// GetProviders parses the Provider string to one or more providers.
func (fc *FabricConfig) GetProviders() ([]string, error) {
	if fc == nil {
		return nil, errors.New("FabricConfig is nil")
	}

	providers := splitMultiProviderStr(fc.Provider)
	if len(providers) == 0 {
		return nil, errors.New("provider not set")
	}

	return providers, nil
}

func splitMultiProviderStr(str string) []string {
	strs := strings.Split(str, MultiProviderSeparator)
	result := make([]string, 0)
	for _, s := range strs {
		trimmed := strings.TrimSpace(s)
		if trimmed != "" {
			result = append(result, trimmed)
		}
	}

	return result
}

// GetNumProviders gets the number of fabric providers configured.
func (fc *FabricConfig) GetNumProviders() int {
	providers, err := fc.GetProviders()
	if err != nil {
		return 0
	}
	return len(providers)
}

// GetPrimaryInterface parses the primary fabric interface from the Interface string.
func (fc *FabricConfig) GetPrimaryInterface() (string, error) {
	interfaces, err := fc.GetInterfaces()
	if err != nil {
		return "", err
	}

	return interfaces[0], nil
}

// GetInterfaces parses the Interface string into one or more interfaces.
func (fc *FabricConfig) GetInterfaces() ([]string, error) {
	if fc == nil {
		return nil, errors.New("FabricConfig is nil")
	}

	interfaces := splitMultiProviderStr(fc.Interface)
	if len(interfaces) == 0 {
		return nil, errors.New("fabric_iface not set")
	}

	return interfaces, nil
}

// GetInterfacePorts parses the InterfacePort string to one or more ports.
func (fc *FabricConfig) GetInterfacePorts() ([]int, error) {
	if fc == nil {
		return nil, errors.New("FabricConfig is nil")
	}

	if fc.InterfacePort == 0 {
		return nil, errors.New("fabric_iface_port not set")
	}

	return []int{fc.InterfacePort}, nil
}

// Update fills in any missing fields from the provided FabricConfig.
func (fc *FabricConfig) Update(other FabricConfig) {
	if fc == nil {
		return
	}

	if fc.Provider == "" {
		fc.Provider = other.Provider
	}
	if fc.Interface == "" {
		fc.Interface = other.Interface
	}
	if fc.InterfacePort == 0 {
		fc.InterfacePort = other.InterfacePort
	}
	if fc.CrtCtxShareAddr == 0 {
		fc.CrtCtxShareAddr = other.CrtCtxShareAddr
	}
	if fc.CrtTimeout == 0 {
		fc.CrtTimeout = other.CrtTimeout
	}
	if fc.DisableSRX == false {
		fc.DisableSRX = other.DisableSRX
	}
	if fc.AuthKey == "" {
		fc.AuthKey = other.AuthKey
	}
	if len(fc.NumSecondaryEndpoints) == 0 {
		fc.setNumSecondaryEndpoints(other.NumSecondaryEndpoints)
	}
}

func (fc *FabricConfig) setNumSecondaryEndpoints(other []int) {
	if len(other) == 0 {
		// Set defaults
		numSecProv := fc.GetNumProviders() - numPrimaryProviders
		for i := 0; i < numSecProv; i++ {
			other = append(other, defaultNumSecondaryEndpoints)
		}
	}
	fc.NumSecondaryEndpoints = other
}

// Validate ensures that the configuration meets minimum standards.
func (fc *FabricConfig) Validate() error {
	numProv := fc.GetNumProviders()
	if numProv == 0 {
		return errors.New("provider not set")
	}

	interfaces, err := fc.GetInterfaces()
	if err != nil {
		return err
	}

	ports, err := fc.GetInterfacePorts()
	if err != nil {
		return err
	}

	for _, p := range ports {
		if p < 0 {
			return errors.New("fabric_iface_port cannot be negative")
		}
	}

	if len(interfaces) != numProv { // TODO SRS-31: check num ports when multiprovider fully enabled: || len(ports) != numProv {
		return errors.Errorf("provider, fabric_iface and fabric_iface_port must include the same number of items delimited by %q", MultiProviderSeparator)
	}

	numSecProv := numProv - numPrimaryProviders
	if numSecProv > 0 {
		if len(fc.NumSecondaryEndpoints) != 0 && len(fc.NumSecondaryEndpoints) != numSecProv {
			return errors.New("secondary_provider_endpoints must have one value for each secondary provider")
		}

		for _, nrCtx := range fc.NumSecondaryEndpoints {
			if nrCtx < 1 {
				return errors.Errorf("all values in secondary_provider_endpoints must be > 0")
			}
		}
	}

	return nil
}

// GetAuthKeyEnv returns the environment variable string for the auth key.
func (fc *FabricConfig) GetAuthKeyEnv() string {
	return fmt.Sprintf("D_PROVIDER_AUTH_KEY=%s", fc.AuthKey)
}

// cleanEnvVars scrubs the supplied slice of environment
// variables by removing all variables not included in the
// allow list.
func cleanEnvVars(in, allowed []string) (out []string) {
	allowedMap := make(map[string]struct{})
	for _, key := range allowed {
		allowedMap[key] = struct{}{}
	}

	for _, pair := range in {
		kv := strings.SplitN(pair, "=", 2)
		if len(kv) != 2 || kv[0] == "" || kv[1] == "" {
			continue
		}
		if _, found := allowedMap[kv[0]]; !found {
			continue
		}
		out = append(out, pair)
	}

	return
}

// Config encapsulates an I/O Engine's configuration.
type Config struct {
	Modules           string         `yaml:"modules,omitempty" cmdLongFlag:"--modules" cmdShortFlag:"-m"`
	TargetCount       int            `yaml:"targets,omitempty" cmdLongFlag:"--targets,nonzero" cmdShortFlag:"-t,nonzero"`
	HelperStreamCount int            `yaml:"nr_xs_helpers" cmdLongFlag:"--xshelpernr" cmdShortFlag:"-x"`
	ServiceThreadCore int            `yaml:"first_core" cmdLongFlag:"--firstcore,nonzero" cmdShortFlag:"-f,nonzero"`
	SystemName        string         `yaml:"-" cmdLongFlag:"--group" cmdShortFlag:"-g"`
	SocketDir         string         `yaml:"-" cmdLongFlag:"--socket_dir" cmdShortFlag:"-d"`
	LogMask           string         `yaml:"log_mask,omitempty" cmdEnv:"D_LOG_MASK"`
	LogFile           string         `yaml:"log_file,omitempty" cmdEnv:"D_LOG_FILE"`
	LegacyStorage     LegacyStorage  `yaml:",inline,omitempty"`
	Storage           storage.Config `yaml:",inline,omitempty"`
	Fabric            FabricConfig   `yaml:",inline"`
	EnvVars           []string       `yaml:"env_vars,omitempty"`
	EnvPassThrough    []string       `yaml:"env_pass_through,omitempty"`
	PinnedNumaNode    *uint          `yaml:"pinned_numa_node,omitempty" cmdLongFlag:"--pinned_numa_node" cmdShortFlag:"-p"`
	Index             uint32         `yaml:"-" cmdLongFlag:"--instance_idx" cmdShortFlag:"-I"`
	MemSize           int            `yaml:"-" cmdLongFlag:"--mem_size" cmdShortFlag:"-r"`
	HugepageSz        int            `yaml:"-" cmdLongFlag:"--hugepage_size" cmdShortFlag:"-H"`
}

// NewConfig returns an I/O Engine config.
func NewConfig() *Config {
	return &Config{
		HelperStreamCount: maxHelperStreamCount,
	}
}

// ReadLogDbgStreams extracts the value of DD_MASK from engine config env_vars.
func (c *Config) ReadLogDbgStreams() (string, error) {
	val, err := c.GetEnvVar(envLogDbgStreams)
	if err != nil && !errors.Is(err, os.ErrNotExist) {
		return "", err
	}

	return val, nil
}

// ReadLogSubsystems extracts the value of DD_SUBSYS from engine config env_vars.
func (c *Config) ReadLogSubsystems() (string, error) {
	val, err := c.GetEnvVar(envLogSubsystems)
	if err != nil && !errors.Is(err, os.ErrNotExist) {
		return "", err
	}

	return val, nil
}

// Validate ensures that the configuration meets minimum standards.
func (c *Config) Validate() error {
	if c.PinnedNumaNode != nil && c.ServiceThreadCore != 0 {
		return errors.New("cannot specify both pinned_numa_node and first_core")
	}

	errNegative := func(s string) error {
		return errors.Errorf("%s must not be negative", s)
	}
	if c.TargetCount < 0 {
		return errNegative("target count")
	}
	if c.HelperStreamCount < 0 {
		return errNegative("helper stream count")
	}
	if c.ServiceThreadCore < 0 {
		return errNegative("service thread core index")
	}
	if c.MemSize < 0 {
		return errNegative("mem size")
	}
	if c.HugepageSz < 0 {
		return errNegative("hugepage size")
	}

	if c.TargetCount == 0 {
		return errors.New("target count must be nonzero")
	}

	if err := c.Fabric.Validate(); err != nil {
		return errors.Wrap(err, "fabric config validation failed")
	}

	if err := c.Storage.Validate(); err != nil {
		return err
	}

	if err := ValidateLogMasks(c.LogMask); err != nil {
		return errors.Wrap(err, "validate engine log masks")
	}

	streams, err := c.ReadLogDbgStreams()
	if err != nil {
		return errors.Wrap(err, "reading environment variable")
	}
	if err := ValidateLogStreams(streams); err != nil {
		return errors.Wrap(err, "validate engine log debug streams")
	}

	subsystems, err := c.ReadLogSubsystems()
	if err != nil {
		return errors.Wrap(err, "reading environment variable")
	}
	if err := ValidateLogSubsystems(subsystems); err != nil {
		return errors.Wrap(err, "validate engine log subsystems")
	}

	return nil
}

type cfgNumaMismatch struct {
	cfgNode uint
	detNode uint
}

func (cnm *cfgNumaMismatch) Error() string {
	return fmt.Sprintf("configured NUMA node %d does not match detected NUMA node %d", cnm.cfgNode, cnm.detNode)
}

func errNumaMismatch(cfg, det uint) error {
	return &cfgNumaMismatch{cfgNode: cfg, detNode: det}
}

// IsNUMAMismatch returns true if the supplied error is the result
// of a NUMA node configuration error.
func IsNUMAMismatch(err error) bool {
	_, ok := errors.Cause(err).(*cfgNumaMismatch)
	return ok
}

// SetNUMAAffinity sets the NUMA affinity for the engine,
// if not already set in the configuration.
func (c *Config) SetNUMAAffinity(node uint) error {
	if c.PinnedNumaNode != nil && c.ServiceThreadCore != 0 {
		return errors.New("cannot set both NUMA node and service core")
	}

	var hasMismatch error
	if c.PinnedNumaNode != nil {
		if *c.PinnedNumaNode != node {
			// advisory for now; may become fatal in future
			hasMismatch = errNumaMismatch(*c.PinnedNumaNode, node)
		}
	} else {
		// If not set via config, use the detected NUMA node affinity.
		c.PinnedNumaNode = &node
	}

	// Propagate the NUMA node affinity to the nested config structs.
	c.Storage.SetNUMAAffinity(*c.PinnedNumaNode)
	c.Fabric.NumaNodeIndex = *c.PinnedNumaNode

	return hasMismatch
}

// CmdLineArgs returns a slice of command line arguments to be
// supplied when starting an I/O Engine instance.
func (c *Config) CmdLineArgs() ([]string, error) {
	args, err := parseCmdTags(c, shortFlagTag, joinShortArgs, nil)
	if err != nil {
		return nil, err
	}
	for _, sc := range c.Storage.Tiers {
		sArgs, err := parseCmdTags(sc, shortFlagTag, joinShortArgs, nil)
		if err != nil {
			return nil, err
		}
		args = append(args, sArgs...)
	}

	return args, nil
}

// CmdLineEnv returns a slice of environment variables to be
// supplied when starting an I/O Engine instance.
func (c *Config) CmdLineEnv() ([]string, error) {
	env, err := parseCmdTags(c, envTag, joinEnvVars, nil)
	if err != nil {
		return nil, err
	}
	for _, sc := range c.Storage.Tiers {
		sEnv, err := parseCmdTags(sc, envTag, joinEnvVars, nil)
		if err != nil {
			return nil, err
		}
		env = common.MergeKeyValues(env, sEnv)
	}

	return common.MergeKeyValues(c.EnvVars, env), nil
}

// HasEnvVar returns true if the configuration contains
// an environment variable with the given name.
func (c *Config) HasEnvVar(name string) bool {
	for _, keyPair := range c.EnvVars {
		if strings.HasPrefix(keyPair, name+"=") {
			return true
		}
	}
	return false
}

// GetEnvVar returns the value of the given environment variable to be supplied when starting an I/O
// engine instance.
func (c *Config) GetEnvVar(name string) (string, error) {
	env, err := c.CmdLineEnv()
	if err != nil {
		return "", err
	}

	env = common.MergeKeyValues(cleanEnvVars(os.Environ(), c.EnvPassThrough), env)

	return common.FindKeyValue(env, name)
}

// WithEnvVars applies the supplied list of environment
// variables to any existing variables, with new values
// overwriting existing values.
func (c *Config) WithEnvVars(newVars ...string) *Config {
	c.EnvVars = common.MergeKeyValues(c.EnvVars, newVars)

	return c
}

// WithEnvPassThrough sets a list of environment variable
// names that will be allowed to pass through into the
// engine subprocess environment.
func (c *Config) WithEnvPassThrough(allowList ...string) *Config {
	c.EnvPassThrough = allowList
	return c
}

// WithSystemName sets the system name to which the instance belongs.
func (c *Config) WithSystemName(name string) *Config {
	c.SystemName = name
	return c
}

// WithStorage creates the set of storage tier configurations.
// Note that this method replaces any existing configs. To append,
// use AppendStorage().
func (c *Config) WithStorage(cfgs ...*storage.TierConfig) *Config {
	c.Storage.Tiers = storage.TierConfigs{}
	c.AppendStorage(cfgs...)
	return c
}

// AppendStorage appends the given storage tier configurations to
// the existing set of storage configs.
func (c *Config) AppendStorage(cfgs ...*storage.TierConfig) *Config {
	for _, cfg := range cfgs {
		if cfg.Tier == 0 {
			cfg.Tier = len(c.Storage.Tiers)
		}
		c.Storage.Tiers = append(c.Storage.Tiers, cfg)
	}
	return c
}

// WithStorageConfigOutputPath sets the path to the generated NVMe config file used by SPDK.
func (c *Config) WithStorageConfigOutputPath(cfgPath string) *Config {
	c.Storage.ConfigOutputPath = cfgPath
	return c
}

// WithStorageVosEnv sets the VOS_BDEV_CLASS env variable.
func (c *Config) WithStorageVosEnv(ve string) *Config {
	c.Storage.VosEnv = ve
	return c
}

// WithStorageEnableHotplug sets EnableHotplug in engine storage.
func (c *Config) WithStorageEnableHotplug(enable bool) *Config {
	c.Storage.EnableHotplug = enable
	return c
}

// WithStorageNumaNodeIndex sets the NUMA node index to be used by this instance.
func (c *Config) WithStorageNumaNodeIndex(nodeIndex uint) *Config {
	c.Storage.NumaNodeIndex = nodeIndex
	return c
}

// WithStorageControlMetadataPath sets the metadata path to be used by this instance.
func (c *Config) WithStorageControlMetadataPath(path string) *Config {
	c.Storage.ControlMetadata.Path = path
	return c
}

// WithStorageControlMetadataDevice sets the metadata device to be used by this instance.
func (c *Config) WithStorageControlMetadataDevice(device string) *Config {
	c.Storage.ControlMetadata.DevicePath = device
	return c
}

// WithSocketDir sets the path to the instance's dRPC socket directory.
func (c *Config) WithSocketDir(dir string) *Config {
	c.SocketDir = dir
	return c
}

// WithModules sets the list of I/O Engine modules to be loaded.
func (c *Config) WithModules(mList string) *Config {
	c.Modules = mList
	return c
}

// WithFabricProvider sets the name of the CArT fabric provider.
func (c *Config) WithFabricProvider(provider string) *Config {
	c.Fabric.Provider = provider
	return c
}

// WithFabricInterface sets the interface name to be used by this instance.
func (c *Config) WithFabricInterface(iface string) *Config {
	c.Fabric.Interface = iface
	return c
}

// WithFabricInterfacePort sets the numeric interface port to be used by this instance.
func (c *Config) WithFabricInterfacePort(ifacePort int) *Config {
	c.Fabric.InterfacePort = ifacePort
	return c
}

// WithFabricAuthKey sets the fabric authorization key.
func (c *Config) WithFabricAuthKey(key string) *Config {
	c.Fabric.AuthKey = key
	return c
}

// WithSrxDisabled disables or enables SRX.
func (c *Config) WithSrxDisabled(disable bool) *Config {
	c.Fabric.DisableSRX = disable
	return c
}

// WithFabricNumaNodeIndex sets the NUMA node index to be used by this instance.
func (c *Config) WithFabricNumaNodeIndex(nodeIndex uint) *Config {
	c.Fabric.NumaNodeIndex = nodeIndex
	return c
}

// WithBypassHealthChk sets the NVME health check bypass for this instance
func (c *Config) WithBypassHealthChk(bypass *bool) *Config {
	c.Fabric.BypassHealthChk = bypass
	return c
}

// WithCrtCtxShareAddr defines the CRT_CTX_SHARE_ADDR for this instance
func (c *Config) WithCrtCtxShareAddr(addr uint32) *Config {
	c.Fabric.CrtCtxShareAddr = addr
	return c
}

// WithCrtTimeout defines the CRT_TIMEOUT for this instance
func (c *Config) WithCrtTimeout(timeout uint32) *Config {
	c.Fabric.CrtTimeout = timeout
	return c
}

// WithNumSecondaryEndpoints sets the number of network endpoints for each secondary provider.
func (c *Config) WithNumSecondaryEndpoints(nr []int) *Config {
	c.Fabric.NumSecondaryEndpoints = nr
	return c
}

// WithTargetCount sets the number of VOS targets to run on this instance.
func (c *Config) WithTargetCount(count int) *Config {
	c.TargetCount = count
	return c
}

// WithHelperStreamCount sets the number of XS Helper streams to run on this instance.
func (c *Config) WithHelperStreamCount(count int) *Config {
	c.HelperStreamCount = count
	return c
}

// WithServiceThreadCore sets the core index to be used for running DAOS service threads.
func (c *Config) WithServiceThreadCore(idx int) *Config {
	c.ServiceThreadCore = idx
	return c
}

// WithLogFile sets the path to the log file to be used by this instance.
func (c *Config) WithLogFile(logPath string) *Config {
	c.LogFile = logPath
	return c
}

// WithLogMask sets the DAOS logging mask to be used by this instance.
func (c *Config) WithLogMask(logMask string) *Config {
	c.LogMask = logMask
	return c
}

// WithLogStreams sets the DAOS logging debug streams to be used by this instance.
func (c *Config) WithLogStreams(streams string) *Config {
	c.EnvVars = append(c.EnvVars, fmt.Sprintf("%s=%s", envLogDbgStreams, streams))
	return c
}

// WithLogSubsystems sets the DAOS logging subsystems to be used by this instance.
func (c *Config) WithLogSubsystems(subsystems string) *Config {
	c.EnvVars = append(c.EnvVars, fmt.Sprintf("%s=%s", envLogSubsystems, subsystems))
	return c
}

// WithMemSize sets the NVMe memory size for SPDK memory allocation on this instance.
func (c *Config) WithMemSize(memsize int) *Config {
	c.MemSize = memsize
	return c
}

// WithHugepageSize sets the configured hugepage size on this instance.
func (c *Config) WithHugepageSize(hugepagesz int) *Config {
	c.HugepageSz = hugepagesz
	return c
}

// WithPinnedNumaNode sets the NUMA node affinity for the I/O Engine instance.
func (c *Config) WithPinnedNumaNode(numa uint) *Config {
	c.PinnedNumaNode = &numa
	return c
}

// WithStorageAccelProps sets the acceleration properties for the I/O Engine instance.
func (c *Config) WithStorageAccelProps(name string, mask storage.AccelOptionBits) *Config {
	c.Storage.AccelProps.Engine = name
	c.Storage.AccelProps.Options = mask
	return c
}

// WithStorageSpdkRpcSrvProps specifies whether a SPDK JSON-RPC server will run in the I/O Engine.
func (c *Config) WithStorageSpdkRpcSrvProps(enable bool, sockAddr string) *Config {
	c.Storage.SpdkRpcSrvProps.Enable = enable
	c.Storage.SpdkRpcSrvProps.SockAddr = sockAddr
	return c
}

// WithStorageAutoFaultyCriteria specifies NVMe auto-faulty settings in the I/O Engine.
func (c *Config) WithStorageAutoFaultyCriteria(enable bool, maxIoErrs, maxCsumErrs uint32) *Config {
	c.Storage.AutoFaultyProps.Enable = enable
	c.Storage.AutoFaultyProps.MaxIoErrs = maxIoErrs
	c.Storage.AutoFaultyProps.MaxCsumErrs = maxCsumErrs
	return c
}

// WithIndex sets the I/O Engine instance index.
func (c *Config) WithIndex(i uint32) *Config {
	c.Index = i
	return c.WithStorageIndex(i)
}

// WithStorageIndex sets the I/O Engine instance index in the storage struct.
func (c *Config) WithStorageIndex(i uint32) *Config {
	c.Storage.EngineIdx = uint(i)
	return c
}
