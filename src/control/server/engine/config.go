//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package engine

import (
	"os"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

const maxHelperStreamCount = 2

// FabricConfig encapsulates networking fabric configuration.
type FabricConfig struct {
	Provider        string `yaml:"provider,omitempty" cmdEnv:"CRT_PHY_ADDR_STR"`
	Interface       string `yaml:"fabric_iface,omitempty" cmdEnv:"OFI_INTERFACE"`
	InterfacePort   int    `yaml:"fabric_iface_port,omitempty" cmdEnv:"OFI_PORT,nonzero"`
	NumaNodeIndex   uint   `yaml:"-"`
	BypassHealthChk *bool  `yaml:"bypass_health_chk,omitempty" cmdLongFlag:"--bypass_health_chk" cmdShortFlag:"-b"`
	CrtCtxShareAddr uint32 `yaml:"crt_ctx_share_addr,omitempty" cmdEnv:"CRT_CTX_SHARE_ADDR"`
	CrtTimeout      uint32 `yaml:"crt_timeout,omitempty" cmdEnv:"CRT_TIMEOUT"`
	DisableSRX      bool   `yaml:"disable_srx,omitempty" cmdEnv:"FI_OFI_RXM_USE_SRX,invertBool,intBool"`
}

// Update fills in any missing fields from the provided FabricConfig.
func (fc *FabricConfig) Update(other FabricConfig) {
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
}

// Validate ensures that the configuration meets minimum standards.
func (fc *FabricConfig) Validate() error {
	switch {
	case fc.Provider == "":
		return errors.New("provider not set")
	case fc.Interface == "":
		return errors.New("fabric_iface not set")
	case fc.InterfacePort == 0:
		return errors.New("fabric_iface_port not set")
	case fc.InterfacePort < 0:
		return errors.New("fabric_iface_port cannot be negative")
	default:
		return nil
	}
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

// mergeEnvVars merges and deduplicates two slices of environment
// variables. Conflicts are resolved by taking the value from the
// second list.
func mergeEnvVars(curVars []string, newVars []string) (merged []string) {
	mergeMap := make(map[string]string)
	for _, pair := range curVars {
		kv := strings.SplitN(pair, "=", 2)
		if len(kv) != 2 || kv[0] == "" || kv[1] == "" {
			continue
		}
		// strip duplicates in curVars; shouldn't be any
		// but this will ensure it.
		if _, found := mergeMap[kv[0]]; found {
			continue
		}
		mergeMap[kv[0]] = kv[1]
	}

	mergedKeys := make(map[string]struct{})
	for _, pair := range newVars {
		kv := strings.SplitN(pair, "=", 2)
		if len(kv) != 2 || kv[0] == "" || kv[1] == "" {
			continue
		}
		// strip duplicates in newVars
		if _, found := mergedKeys[kv[0]]; found {
			continue
		}
		mergedKeys[kv[0]] = struct{}{}
		mergeMap[kv[0]] = kv[1]
	}

	merged = make([]string, 0, len(mergeMap))
	for key, val := range mergeMap {
		merged = append(merged, strings.Join([]string{key, val}, "="))
	}

	return
}

type LegacyStorage struct {
	storage.ScmConfig  `yaml:",inline,omitempty"`
	ScmClass           storage.Class `yaml:"scm_class,omitempty"`
	storage.BdevConfig `yaml:",inline,omitempty"`
	BdevClass          storage.Class `yaml:"bdev_class,omitempty"`
}

func (ls *LegacyStorage) WasDefined() bool {
	return ls.ScmClass != storage.ClassNone || ls.BdevClass != storage.ClassNone
}

// Config encapsulates an I/O Engine's configuration.
type Config struct {
	Rank              *system.Rank   `yaml:"rank,omitempty"`
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
	HugePageSz        int            `yaml:"-" cmdLongFlag:"--hugepage_size" cmdShortFlag:"-H"`
}

// NewConfig returns an I/O Engine config.
func NewConfig() *Config {
	return &Config{
		HelperStreamCount: maxHelperStreamCount,
	}
}

// Validate ensures that the configuration meets minimum standards.
func (c *Config) Validate() error {
	if c.PinnedNumaNode != nil && c.ServiceThreadCore != 0 {
		return errors.New("cannot specify both pinned_numa_node and first_core")
	}

	if err := c.Fabric.Validate(); err != nil {
		return errors.Wrap(err, "fabric config validation failed")
	}

	if err := c.Storage.Validate(); err != nil {
		return errors.Wrap(err, "storage config validation failed")
	}

	if err := ValidateLogMasks(c.LogMask); err != nil {
		return errors.Wrap(err, "validate engine log masks")
	}

	return nil
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
		env = mergeEnvVars(env, sEnv)
	}

	return mergeEnvVars(c.EnvVars, env), nil
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

	env = mergeEnvVars(cleanEnvVars(os.Environ(), c.EnvPassThrough), env)

	for _, keyPair := range mergeEnvVars(c.EnvVars, env) {
		keyValue := strings.SplitN(keyPair, "=", 2)
		if keyValue[0] == name {
			return keyValue[1], nil
		}
	}

	return "", errors.Errorf("Undefined environment variable %q", name)
}

// WithEnvVars applies the supplied list of environment
// variables to any existing variables, with new values
// overwriting existing values.
func (c *Config) WithEnvVars(newVars ...string) *Config {
	c.EnvVars = mergeEnvVars(c.EnvVars, newVars)

	return c
}

// WithEnvPassThrough sets a list of environment variable
// names that will be allowed to pass through into the
// engine subprocess environment.
func (c *Config) WithEnvPassThrough(allowList ...string) *Config {
	c.EnvPassThrough = allowList
	return c
}

// WithRank sets the instance rank.
func (c *Config) WithRank(r uint32) *Config {
	c.Rank = system.NewRankPtr(r)
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
	c.Storage.Tiers = c.Storage.Tiers[:]
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

// WithMemSize sets the NVMe memory size for SPDK memory allocation on this instance.
func (c *Config) WithMemSize(memsize int) *Config {
	c.MemSize = memsize
	return c
}

// WithHugePageSize sets the configured hugepage size on this instance.
func (c *Config) WithHugePageSize(hugepagesz int) *Config {
	c.HugePageSz = hugepagesz
	return c
}

// WithPinnedNumaNode sets the NUMA node affinity for the I/O Engine instance.
func (c *Config) WithPinnedNumaNode(numa uint) *Config {
	c.PinnedNumaNode = &numa
	return c
}

// WithStorageAccelEngine sets the acceleration engine setting for the I/O Engine instance.
func (c *Config) WithStorageAccelEngine(name string) *Config {
	c.Storage.AccelProps.AccelEngine = name
	return c
}

// WithStorageAccelOpts sets the acceleration option string slice for the I/O Engine instance.
func (c *Config) WithStorageAccelOpts(opts ...string) *Config {
	c.Storage.AccelProps.AccelOpts = opts
	return c
}

// WithStorageAccelOptMask sets the acceleration option bit mask for the I/O Engine instance.
func (c *Config) WithStorageAccelOptMask(mask uint16) *Config {
	c.Storage.AccelProps.AccelOptMask = mask
	return c
}
