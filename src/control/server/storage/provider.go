//
// (C) Copyright 2021-2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"context"
	"fmt"
	"os"
	"sync"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwloc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
)

const defaultMetadataPath = "/mnt/daos"

// SystemProvider provides operating system capabilities.
type SystemProvider interface {
	system.IsMountedProvider
	GetfsUsage(string) (uint64, uint64, error)
	ReadFile(string) ([]byte, error)
}

// Provider provides storage specific capabilities.
type Provider struct {
	sync.RWMutex
	log           logging.Logger
	engineIndex   int
	engineStorage *Config
	Sys           SystemProvider
	metadata      MetadataProvider
	scm           ScmProvider
	bdev          BdevProvider
	vmdEnabled    bool
}

// DefaultProvider returns a provider populated with default parameters.
func DefaultProvider(log logging.Logger, idx int, engineStorage *Config) *Provider {
	if engineStorage == nil {
		engineStorage = new(Config)
	}
	return NewProvider(log, idx, engineStorage, system.DefaultProvider(),
		NewScmForwarder(log), NewBdevForwarder(log), NewMetadataForwarder(log))
}

// FormatControlMetadata formats the storage used for control metadata.
func (p *Provider) FormatControlMetadata(engineIdxs []uint) error {
	if p == nil {
		return errors.New("nil provider")
	}

	if !p.engineStorage.ControlMetadata.HasPath() {
		// Nothing to do
		p.log.Debug("no control metadata path")
		return nil
	}

	req := MetadataFormatRequest{
		RootPath:   p.engineStorage.ControlMetadata.Path,
		Device:     p.engineStorage.ControlMetadata.DevicePath,
		DataPath:   p.engineStorage.ControlMetadata.Directory(),
		OwnerUID:   os.Geteuid(),
		OwnerGID:   os.Getegid(),
		EngineIdxs: engineIdxs,
	}
	p.log.Debugf("calling metadata storage provider format: %+v", req)
	return p.metadata.Format(req)
}

// ControlMetadataNeedsFormat checks whether we need to format the control metadata storage before
// using it.
func (p *Provider) ControlMetadataNeedsFormat() (bool, error) {
	if p == nil {
		return false, errors.New("nil provider")
	}

	if !p.engineStorage.ControlMetadata.HasPath() {
		// No metadata section defined, so we fall back to using SCM
		return false, nil
	}

	req := MetadataFormatRequest{
		RootPath: p.engineStorage.ControlMetadata.Path,
		Device:   p.engineStorage.ControlMetadata.DevicePath,
		DataPath: p.engineStorage.ControlMetadata.Directory(),
	}
	p.log.Debugf("checking metadata storage provider format: %+v", req)
	return p.metadata.NeedsFormat(req)
}

// ControlMetadataPathConfigured checks whether metadata section is defined
func (p *Provider) ControlMetadataPathConfigured() bool {
	if p == nil {
		return false
	}
	if p.engineStorage.ControlMetadata.HasPath() {
		return true
	}
	return false
}

// ControlMetadataPath returns the path where control plane metadata is stored.
func (p *Provider) ControlMetadataPath() string {
	if p == nil {
		return defaultMetadataPath
	}

	if p.engineStorage.ControlMetadata.HasPath() {
		return p.engineStorage.ControlMetadata.Directory()
	}

	return p.scmMetadataPath()
}

func (p *Provider) GetControlMetadata() *ControlMetadata {
	if p == nil {
		return nil
	}
	return &p.engineStorage.ControlMetadata
}

func (p *Provider) scmMetadataPath() string {
	cfg, err := p.GetScmConfig()
	if err != nil {
		p.log.Errorf("unable to get SCM config: %s", err)
		return defaultMetadataPath
	}

	storagePath := cfg.Scm.MountPoint
	if storagePath == "" {
		storagePath = defaultMetadataPath
	}

	return storagePath
}

// ControlMetadataEnginePath returns the path where control plane metadata for the engine is stored.
func (p *Provider) ControlMetadataEnginePath() string {
	if p == nil {
		return defaultMetadataPath
	}

	if p.engineStorage.ControlMetadata.HasPath() {
		return p.engineStorage.ControlMetadata.EngineDirectory(uint(p.engineIndex))
	}

	return p.scmMetadataPath()
}

// MountControlMetadata mounts the storage for control metadata, if it is on a separate device.
func (p *Provider) MountControlMetadata() error {
	if p == nil {
		return errors.New("nil provider")
	}

	if !p.engineStorage.ControlMetadata.HasPath() {
		// If there's no control metadata path, we use SCM for control metadata
		return p.MountScm()
	}

	req := MetadataMountRequest{
		RootPath: p.engineStorage.ControlMetadata.Path,
		Device:   p.engineStorage.ControlMetadata.DevicePath,
	}

	p.log.Debugf("calling metadata storage provider mount: %+v", req)
	_, err := p.metadata.Mount(req)

	return err
}

// ControlMetadataIsMounted determines whether the control metadata storage is already mounted.
func (p *Provider) ControlMetadataIsMounted() (bool, error) {
	if p == nil {
		return false, errors.New("nil provider")
	}

	if !p.engineStorage.ControlMetadata.HasPath() {
		// If there's no control metadata path, we use SCM for control metadata
		return p.ScmIsMounted()
	}
	p.log.Debugf("control metadata config: %+v", p.engineStorage.ControlMetadata)

	if p.engineStorage.ControlMetadata.DevicePath == "" {
		p.log.Debug("no metadata device defined")
		return false, nil
	}

	return p.Sys.IsMounted(p.engineStorage.ControlMetadata.Path)
}

// PrepareScm calls into storage SCM provider to attempt to configure PMem devices to be usable by
// DAOS.
func (p *Provider) PrepareScm(req ScmPrepareRequest) (*ScmPrepareResponse, error) {
	p.log.Debugf("calling scm storage provider prepare: %+v", req)
	return p.scm.Prepare(req)
}

// ScanScm calls into storage SCM provider to discover PMem modules, namespaces and state.
func (p *Provider) ScanScm(req ScmScanRequest) (*ScmScanResponse, error) {
	p.log.Debugf("calling scm storage provider scan: %+v", req)
	return p.scm.Scan(req)
}

// GetScmConfig returns the only SCM tier config.
func (p *Provider) GetScmConfig() (*TierConfig, error) {
	// NB: A bit wary of building in assumptions again about the number of
	// SCM tiers, but for the sake of expediency we'll assume that there is
	// only one. If that needs to change, hopefully it won't require quite so
	// many invasive code updates.
	scmConfigs := p.engineStorage.Tiers.ScmConfigs()
	if len(scmConfigs) != 1 {
		return nil, ErrNoScmTiers
	}
	return scmConfigs[0], nil
}

// GetScmUsage returns space utilization info for a mount point.
func (p *Provider) GetScmUsage() (*ScmMountPoint, error) {
	cfg, err := p.GetScmConfig()
	if err != nil {
		return nil, err
	}

	total, avail, err := p.Sys.GetfsUsage(cfg.Scm.MountPoint)
	if err != nil {
		return nil, err
	}

	return &ScmMountPoint{
		Class:      cfg.Class,
		DeviceList: cfg.Scm.DeviceList,
		Path:       cfg.Scm.MountPoint,
		TotalBytes: total,
		AvailBytes: avail,
	}, nil
}

// ScmIsMounted returns true if SCM is mounted.
func (p *Provider) ScmIsMounted() (bool, error) {
	cfg, err := p.GetScmConfig()
	if err != nil {
		return false, err
	}
	return p.Sys.IsMounted(cfg.Scm.MountPoint)
}

// MountScm mounts SCM based on provider config.
func (p *Provider) MountScm() error {
	cfg, err := p.GetScmConfig()
	if err != nil {
		return err
	}

	req := ScmMountRequest{
		Class:  cfg.Class,
		Target: cfg.Scm.MountPoint,
	}

	switch cfg.Class {
	case ClassRam:
		req.Ramdisk = &RamdiskParams{
			Size:             cfg.Scm.RamdiskSize,
			NUMANode:         cfg.Scm.NumaNodeIndex,
			DisableHugepages: cfg.Scm.DisableHugepages,
		}
	case ClassDcpm:
		if len(cfg.Scm.DeviceList) != 1 {
			return ErrInvalidDcpmCount
		}
		req.Device = cfg.Scm.DeviceList[0]
	default:
		return errors.New(ScmMsgClassNotSupported)
	}

	p.log.Debugf("attempting to mount SCM dir %s\n", cfg.Scm.MountPoint)

	res, err := p.scm.Mount(req)
	if err != nil {
		return err
	}

	p.log.Debugf("%s mounted: %t", res.Target, res.Mounted)
	return nil
}

// UnmountTmpfs unmounts SCM based on provider config.
func (p *Provider) UnmountTmpfs() error {
	cfg, err := p.GetScmConfig()
	if err != nil {
		return err
	}

	if cfg.Class != ClassRam {
		p.log.Debugf("skipping unmount tmpfs as scm class is not ram")
		return nil
	}

	req := ScmMountRequest{
		Target: cfg.Scm.MountPoint,
	}

	p.log.Debugf("attempting to unmount %s\n", cfg.Scm.MountPoint)

	res, err := p.scm.Unmount(req)
	if err != nil {
		return err
	}

	p.log.Debugf("%s unmounted: %t", res.Target, !res.Mounted)
	return nil
}

func createScmFormatRequest(class Class, scmCfg ScmConfig, force bool) (*ScmFormatRequest, error) {
	req := ScmFormatRequest{
		Mountpoint: scmCfg.MountPoint,
		Force:      force,
		OwnerUID:   os.Geteuid(),
		OwnerGID:   os.Getegid(),
	}

	switch class {
	case ClassRam:
		req.Ramdisk = &RamdiskParams{
			Size:             scmCfg.RamdiskSize,
			NUMANode:         scmCfg.NumaNodeIndex,
			DisableHugepages: scmCfg.DisableHugepages,
		}
	case ClassDcpm:
		if len(scmCfg.DeviceList) != 1 {
			return nil, ErrInvalidDcpmCount
		}
		req.Dcpm = &DeviceParams{
			Device: scmCfg.DeviceList[0],
		}
	default:
		return nil, errors.New(ScmMsgClassNotSupported)
	}

	return &req, nil
}

// ScmNeedsFormat returns true if SCM is found to require formatting.
func (p *Provider) ScmNeedsFormat() (bool, error) {
	cfg, err := p.GetScmConfig()
	if err != nil {
		return false, err
	}

	p.log.Debugf("%s: checking formatting", cfg.Scm.MountPoint)

	req, err := createScmFormatRequest(cfg.Class, cfg.Scm, false)
	if err != nil {
		return false, err
	}

	res, err := p.scm.CheckFormat(*req)
	if err != nil {
		return false, err
	}

	needsFormat := !res.Mounted && !res.Mountable
	p.log.Debugf("%s (%s) needs format: %t", cfg.Scm.MountPoint, cfg.Class, needsFormat)
	return needsFormat, nil
}

// FormatScm formats SCM based on provider config and force flag.
func (p *Provider) FormatScm(force bool) error {
	cfg, err := p.GetScmConfig()
	if err != nil {
		return err
	}

	req, err := createScmFormatRequest(cfg.Class, cfg.Scm, force)
	if err != nil {
		return errors.Wrap(err, "generate format request")
	}

	scmStr := fmt.Sprintf("SCM (%s:%s)", cfg.Class, cfg.Scm.MountPoint)
	p.log.Infof("Instance %d: starting format of %s", p.engineIndex, scmStr)
	res, err := p.scm.Format(*req)
	if err == nil && !res.Formatted {
		err = errors.Errorf("%s is still unformatted", cfg.Scm.MountPoint)
	}

	if err != nil {
		p.log.Errorf("  format of %s failed: %s", scmStr, err)
		return err
	}
	p.log.Infof("Instance %d: finished format of %s", p.engineIndex, scmStr)

	return nil
}

// GetBdevConfigs returns the Bdev tier configs.
func (p *Provider) GetBdevConfigs() []*TierConfig {
	return p.engineStorage.Tiers.BdevConfigs()
}

// QueryScmFirmware queries PMem SSD firmware.
func (p *Provider) QueryScmFirmware(req ScmFirmwareQueryRequest) (*ScmFirmwareQueryResponse, error) {
	return p.scm.QueryFirmware(req)
}

// UpdateScmFirmware queries PMem SSD firmware.
func (p *Provider) UpdateScmFirmware(req ScmFirmwareUpdateRequest) (*ScmFirmwareUpdateResponse, error) {
	return p.scm.UpdateFirmware(req)
}

// PrepareBdevs attempts to configure NVMe devices to be usable by DAOS.
func (p *Provider) PrepareBdevs(req BdevPrepareRequest) (*BdevPrepareResponse, error) {
	resp, err := p.bdev.Prepare(req)

	p.Lock()
	defer p.Unlock()

	if err == nil && resp != nil && !req.CleanHugepagesOnly {
		p.vmdEnabled = resp.VMDPrepared
		p.log.Debugf("setting vmd=%v on storage provider", p.vmdEnabled)
	}
	return resp, err
}

// HasBlockDevices returns true if provider engine storage config has configured block devices.
func (p *Provider) HasBlockDevices() bool {
	p.RLock()
	defer p.RUnlock()

	return p.engineStorage.GetBdevs().Len() > 0
}

// BdevTierPropertiesFromConfig returns BdevTierProperties struct from given TierConfig.
func BdevTierPropertiesFromConfig(cfg *TierConfig) BdevTierProperties {
	return BdevTierProperties{
		Class:          cfg.Class,
		DeviceList:     cfg.Bdev.DeviceList,
		DeviceFileSize: uint64(humanize.GiByte * cfg.Bdev.FileSize),
		Tier:           cfg.Tier,
		DeviceRoles:    cfg.Bdev.DeviceRoles,
	}
}

// BdevFormatRequestFromConfig returns a bdev format request populated from a
// TierConfig.
func BdevFormatRequestFromConfig(log logging.Logger, cfg *TierConfig) (BdevFormatRequest, error) {
	req := BdevFormatRequest{
		Properties: BdevTierPropertiesFromConfig(cfg),
		OwnerUID:   os.Geteuid(),
		OwnerGID:   os.Getegid(),
	}

	hn, err := os.Hostname()
	if err != nil {
		log.Errorf("get hostname: %s", err)
		return req, err
	}
	req.Hostname = hn

	return req, nil
}

// BdevTierFormatResult contains details of a format operation result.
type BdevTierFormatResult struct {
	Tier        int
	DeviceRoles BdevRoles
	Error       error
	Result      *BdevFormatResponse
}

// FormatBdevTiers formats all the Bdev tiers in the engine storage
// configuration.
func (p *Provider) FormatBdevTiers(ctrlrs NvmeControllers) (results []BdevTierFormatResult) {
	bdevCfgs := p.engineStorage.Tiers.BdevConfigs()
	results = make([]BdevTierFormatResult, len(bdevCfgs))

	// A config with SCM and no block devices is valid.
	if len(bdevCfgs) == 0 {
		return
	}

	for i, cfg := range bdevCfgs {
		p.log.Infof("Instance %d: starting format of %s block devices %v",
			p.engineIndex, cfg.Class, cfg.Bdev.DeviceList)

		req, err := BdevFormatRequestFromConfig(p.log, cfg)
		if err != nil {
			results[i].Error = err
			p.log.Errorf("Instance %d: format failed (%s)", err)
			continue
		}
		req.ScannedBdevs = ctrlrs

		p.RLock()
		req.VMDEnabled = p.vmdEnabled
		results[i].Result, results[i].Error = p.bdev.Format(req)
		p.RUnlock()

		results[i].Tier = cfg.Tier
		results[i].DeviceRoles = cfg.Bdev.DeviceRoles
		if err := results[i].Error; err != nil {
			p.log.Errorf("Instance %d: format failed (%s)", err)
			continue
		}

		p.log.Infof("Instance %d: finished format of %s block devices %v",
			p.engineIndex, cfg.Class, cfg.Bdev.DeviceList)
	}

	return
}

// setHotplugRange sets request parameters related to bus-id range limits to restrict hotplug
// actions of engine to a set of ssd devices.
func setHotplugRange(ctx context.Context, log logging.Logger, getTopo topologyGetter, numaNode uint, tier *TierConfig, req *BdevWriteConfigRequest) error {
	var begin, end uint8

	switch {
	case req.VMDEnabled:
		log.Debug("hotplug bus-id filter allows all as vmd is enabled")
		begin = 0x00
		end = 0xFF
	case tier.Bdev.BusidRange != nil && !tier.Bdev.BusidRange.IsZero():
		log.Debugf("received user-specified hotplug bus-id range %q", tier.Bdev.BusidRange)
		begin = tier.Bdev.BusidRange.LowAddress.Bus
		end = tier.Bdev.BusidRange.HighAddress.Bus
	default:
		var err error
		log.Debug("generating hotplug bus-id range based on hardware topology")
		begin, end, err = getNumaNodeBusidRange(ctx, getTopo, numaNode)
		if err != nil {
			return errors.Wrapf(err, "get busid range limits")
		}
	}

	log.Infof("NUMA %d: hotplug bus-ids %X-%X", numaNode, begin, end)
	req.HotplugBusidBegin = begin
	req.HotplugBusidEnd = end

	return nil
}

type topologyGetter func(ctx context.Context) (*hardware.Topology, error)

// BdevWriteConfigRequestFromConfig returns a config write request derived from a storage config.
func BdevWriteConfigRequestFromConfig(ctx context.Context, log logging.Logger, cfg *Config, vmdEnabled bool, getTopo topologyGetter) (*BdevWriteConfigRequest, error) {
	if cfg == nil {
		return nil, errors.New("received nil config")
	}
	if getTopo == nil {
		return nil, errors.New("received nil GetTopology function")
	}

	hn, err := os.Hostname()
	if err != nil {
		return nil, errors.Wrap(err, "get hostname")
	}

	req := &BdevWriteConfigRequest{
		OwnerUID:         os.Geteuid(),
		OwnerGID:         os.Getegid(),
		Hostname:         hn,
		ConfigOutputPath: cfg.ConfigOutputPath,
		HotplugEnabled:   cfg.EnableHotplug,
		VMDEnabled:       vmdEnabled,
		TierProps:        []BdevTierProperties{},
		AccelProps:       cfg.AccelProps,
		SpdkRpcSrvProps:  cfg.SpdkRpcSrvProps,
		AutoFaultyProps:  cfg.AutoFaultyProps,
	}

	for idx, tier := range cfg.Tiers.BdevConfigs() {
		req.TierProps = append(req.TierProps, BdevTierPropertiesFromConfig(tier))

		if !req.HotplugEnabled || idx != 0 {
			continue
		}

		// Populate hotplug bus-ID range limits when processing the first bdev tier.
		if err := setHotplugRange(ctx, log, getTopo, cfg.NumaNodeIndex, tier, req); err != nil {
			return nil, errors.Wrapf(err, "set busid range limits")
		}
	}

	log.Debugf("BdevWriteConfigRequest: %+v", req)
	return req, nil
}

// WriteNvmeConfig creates an NVMe config file which describes what devices
// should be used by a DAOS engine process.
func (p *Provider) WriteNvmeConfig(ctx context.Context, log logging.Logger, ctrlrs NvmeControllers) error {
	p.RLock()
	vmdEnabled := p.vmdEnabled
	engineIndex := p.engineIndex
	engineStorage := p.engineStorage
	p.RUnlock()

	req, err := BdevWriteConfigRequestFromConfig(ctx, log, engineStorage,
		vmdEnabled, hwloc.NewProvider(log).GetTopology)
	if err != nil {
		return errors.Wrap(err, "creating write config request")
	}
	req.ScannedBdevs = ctrlrs

	log.Infof("Writing NVMe config file for engine instance %d to %q", engineIndex,
		req.ConfigOutputPath)

	_, err = p.bdev.WriteConfig(*req)

	return err
}

// ReadNvmeConfig calls into the bdev storage provider to read an NVMe config file.
func (p *Provider) ReadNvmeConfig(ctx context.Context) (*BdevReadConfigResponse, error) {
	req := BdevReadConfigRequest{
		ConfigPath: p.engineStorage.ConfigOutputPath,
	}

	return p.bdev.ReadConfig(req)
}

// BdevTierScanResult contains details of a scan operation result.
type BdevTierScanResult struct {
	Tier   int
	Result *BdevScanResponse
}

// ScanBdevs calls into bdev storage provider to scan SSDs, always bypassing cache.
// Function should not be called when engines have been started and SSDs have been claimed by SPDK.
func (p *Provider) ScanBdevs(req BdevScanRequest) (*BdevScanResponse, error) {
	p.RLock()
	defer p.RUnlock()

	req.VMDEnabled = p.vmdEnabled
	return p.bdev.Scan(req)
}

// WithVMDEnabled enables VMD on storage provider.
func (p *Provider) WithVMDEnabled(b bool) *Provider {
	p.vmdEnabled = b
	return p
}

// IsVMDEnabled queries whether VMD is enabled on storage provider.
func (p *Provider) IsVMDEnabled() bool {
	return p.vmdEnabled
}

func (p *Provider) BdevRoleMetaConfigured() bool {
	bdevConfigs := p.GetBdevConfigs()
	for _, bc := range bdevConfigs {
		bits := bc.Bdev.DeviceRoles.OptionBits
		if (bits & BdevRoleMeta) != 0 {
			return true
		}
	}
	return false
}

// QueryBdevFirmware queries NVMe SSD firmware.
func (p *Provider) QueryBdevFirmware(req NVMeFirmwareQueryRequest) (*NVMeFirmwareQueryResponse, error) {
	return p.bdev.QueryFirmware(req)
}

// UpdateBdevFirmware queries NVMe SSD firmware.
func (p *Provider) UpdateBdevFirmware(req NVMeFirmwareUpdateRequest) (*NVMeFirmwareUpdateResponse, error) {
	return p.bdev.UpdateFirmware(req)
}

// UpgradeBdevConfig updates an existing SPDK bdev config, if necessary.
func (p *Provider) UpgradeBdevConfig(ctx context.Context, ctrlrs NvmeControllers) error {
	if !p.HasBlockDevices() {
		return nil
	}

	_, err := p.ReadNvmeConfig(ctx)
	if err == nil {
		// For now, we'll just assume that if we can read the config file, then
		// we don't need to do anything else.
		return nil
	}

	// Take the conservative approach that if we expect there to have been a config
	// file and it's not there, then we need the admin to investigate.
	if !fault.IsFaultCode(err, code.SpdkInvalidConfiguration) {
		p.log.Errorf("Failed to read bdev config file: %s", p.engineStorage.ConfigOutputPath)
		return err
	}

	// Otherwise, if the config file is there but we can't understand it, then
	// it's probably from a different version of DAOS and we should just regenerate
	// it based on our version of the configuration.
	p.log.Notice("The bdev config file was unparsable; regenerating it.")
	return p.WriteNvmeConfig(ctx, p.log, ctrlrs)
}

// NewProvider returns an initialized storage provider.
func NewProvider(log logging.Logger, idx int, engineStorage *Config, sys SystemProvider, scm ScmProvider, bdev BdevProvider, meta MetadataProvider) *Provider {
	return &Provider{
		log:           log,
		engineIndex:   idx,
		engineStorage: engineStorage,
		Sys:           sys,
		scm:           scm,
		bdev:          bdev,
		metadata:      meta,
	}
}
