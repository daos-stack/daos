//
// (C) Copyright 2021-2022 Intel Corporation.
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

	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwloc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
)

// SystemProvider provides operating system capabilities.
type SystemProvider interface {
	system.IsMountedProvider
	GetfsUsage(string) (uint64, uint64, error)
}

// Provider provides storage specific capabilities.
type Provider struct {
	sync.RWMutex
	log           logging.Logger
	engineIndex   int
	engineStorage *Config
	Sys           SystemProvider
	Scm           ScmProvider
	bdev          BdevProvider
	bdevCache     BdevScanResponse
	vmdEnabled    bool
}

// DefaultProvider returns a provider populated with default parameters.
func DefaultProvider(log logging.Logger, idx int, engineStorage *Config) *Provider {
	if engineStorage == nil {
		engineStorage = new(Config)
	}
	return NewProvider(log, idx, engineStorage, system.DefaultProvider(),
		NewScmForwarder(log), NewBdevForwarder(log))
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
		req.Size = cfg.Scm.RamdiskSize
	case ClassDcpm:
		if len(cfg.Scm.DeviceList) != 1 {
			return ErrInvalidDcpmCount
		}
		req.Device = cfg.Scm.DeviceList[0]
	default:
		return errors.New(ScmMsgClassNotSupported)
	}

	p.log.Debugf("attempting to mount existing SCM dir %s\n", cfg.Scm.MountPoint)

	res, err := p.Scm.Mount(req)
	if err != nil {
		return err
	}

	p.log.Debugf("%s mounted: %t", res.Target, res.Mounted)
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
			Size: scmCfg.RamdiskSize,
		}
	case ClassDcpm:
		if len(scmCfg.DeviceList) != 1 {
			return nil, ErrInvalidDcpmCount
		}
		req.Dcpm = &DcpmParams{
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

	res, err := p.Scm.CheckFormat(*req)
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
	res, err := p.Scm.Format(*req)
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

// GetBdevConfig returns the Bdev tier configs.
func (p *Provider) GetBdevConfigs() []*TierConfig {
	return p.engineStorage.Tiers.BdevConfigs()
}

// PrepareBdevs attempts to configure NVMe devices to be usable by DAOS.
func (p *Provider) PrepareBdevs(req BdevPrepareRequest) (*BdevPrepareResponse, error) {
	resp, err := p.bdev.Prepare(req)

	p.Lock()
	defer p.Unlock()

	if err == nil && resp != nil && !req.CleanHugePagesOnly {
		p.vmdEnabled = resp.VMDPrepared
	}
	return resp, err
}

// GetBlockDevices returns the addresses of all block devices in all bdev storage tiers.
func (p *Provider) GetBlockDevices() *BdevDeviceList {
	bdevs := []string{}
	for _, cfg := range p.engineStorage.Tiers.BdevConfigs() {
		bdevs = append(bdevs, cfg.Bdev.DeviceList.Devices()...)
	}

	p.log.Debugf("bdevs on instance %d: %v", p.engineIndex, bdevs)

	return MustNewBdevDeviceList(bdevs...)
}

// HasBlockDevices returns true if provider engine storage config has configured block devices.
func (p *Provider) HasBlockDevices() bool {
	return p.GetBlockDevices().Len() > 0
}

// BdevTierPropertiesFromConfig returns BdevTierProperties struct from given TierConfig.
func BdevTierPropertiesFromConfig(cfg *TierConfig) BdevTierProperties {
	return BdevTierProperties{
		Class:      cfg.Class,
		DeviceList: cfg.Bdev.DeviceList,
		// cfg size in nr GiBytes
		DeviceFileSize: uint64(humanize.GiByte * cfg.Bdev.FileSize),
		Tier:           cfg.Tier,
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
	Tier   int
	Error  error
	Result *BdevFormatResponse
}

// FormatBdevTiers formats all the Bdev tiers in the engine storage
// configuration.
func (p *Provider) FormatBdevTiers() (results []BdevTierFormatResult) {
	bdevCfgs := p.engineStorage.Tiers.BdevConfigs()
	results = make([]BdevTierFormatResult, len(bdevCfgs))

	// A config with SCM and no block devices is valid.
	if len(bdevCfgs) == 0 {
		return
	}

	for i, cfg := range bdevCfgs {
		p.log.Infof("Instance %d: starting format of %s block devices %v",
			p.engineIndex, cfg.Class, cfg.Bdev.DeviceList)

		results[i].Tier = cfg.Tier
		req, err := BdevFormatRequestFromConfig(p.log, cfg)
		if err != nil {
			results[i].Error = err
			p.log.Errorf("Instance %d: format failed (%s)", err)
			continue
		}

		p.RLock()
		req.BdevCache = &p.bdevCache
		req.VMDEnabled = p.vmdEnabled
		results[i].Result, results[i].Error = p.bdev.Format(req)
		p.RUnlock()

		if err := results[i].Error; err != nil {
			p.log.Errorf("Instance %d: format failed (%s)", err)
			continue
		}

		p.log.Infof("Instance %d: finished format of %s block devices %v",
			p.engineIndex, cfg.Class, cfg.Bdev.DeviceList)
	}

	return
}

type topologyGetter func(ctx context.Context) (*hardware.Topology, error)

// BdevWriteConfigRequestFromConfig returns a config write request from a storage config.
func BdevWriteConfigRequestFromConfig(ctx context.Context, log logging.Logger, cfg *Config, vmdEnabled bool, getTopo topologyGetter) (BdevWriteConfigRequest, error) {
	req := BdevWriteConfigRequest{
		OwnerUID: os.Geteuid(),
		OwnerGID: os.Getegid(),
	}
	if cfg == nil {
		return req, errors.New("received nil config")
	}
	req.ConfigOutputPath = cfg.ConfigOutputPath
	if getTopo == nil {
		return req, errors.New("received nil GetTopology function")
	}

	hn, err := os.Hostname()
	if err != nil {
		log.Errorf("get hostname: %s", err)
		return req, err
	}
	req.Hostname = hn
	req.HotplugEnabled = cfg.EnableHotplug

	bdevTiers := cfg.Tiers.BdevConfigs()
	req.TierProps = make([]BdevTierProperties, 0, len(bdevTiers))
	for idx, tier := range bdevTiers {
		req.TierProps = append(req.TierProps, BdevTierPropertiesFromConfig(tier))

		if !req.HotplugEnabled || idx != 0 {
			continue
		}

		// Populate hotplug bus-ID range limits when processing the first bdev tier.

		if vmdEnabled {
			req.HotplugBusidBegin = 0x00
			req.HotplugBusidEnd = 0xFF
			log.Debug("hotplug bus-id filter allows all as vmd is enabled")
			continue
		}

		// Applying the bus-id range limits hotplug activity of engine to a set of ssd
		// devices.

		var begin, end uint8
		if tier.Bdev.BusidRange != nil && !tier.Bdev.BusidRange.IsZero() {
			log.Debugf("received user-specified hotplug bus-id range %q",
				tier.Bdev.BusidRange)
			begin = tier.Bdev.BusidRange.LowAddress.Bus
			end = tier.Bdev.BusidRange.HighAddress.Bus
		} else {
			log.Debug("generating hotplug bus-id range based on hardware topology")
			begin, end, err = getNumaNodeBusidRange(ctx, getTopo, cfg.NumaNodeIndex)
		}

		if err != nil {
			return req, errors.Wrapf(err, "get busid range limits")
		}

		log.Debugf("NUMA %d: hotplug bus-ids %X-%X", cfg.NumaNodeIndex, uint8(begin),
			uint8(end))

		req.HotplugBusidBegin = begin
		req.HotplugBusidEnd = end
	}

	req.VMDEnabled = vmdEnabled

	return req, nil
}

// WriteNvmeConfig creates an NVMe config file which describes what devices
// should be used by a DAOS engine process.
func (p *Provider) WriteNvmeConfig(ctx context.Context, log logging.Logger) error {
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

	log.Infof("Writing NVMe config file for engine instance %d to %q", engineIndex,
		req.ConfigOutputPath)

	p.RLock()
	defer p.RUnlock()
	req.BdevCache = &p.bdevCache

	_, err = p.bdev.WriteConfig(req)

	return err
}

// BdevTierScanResult contains details of a scan operation result.
type BdevTierScanResult struct {
	Tier   int
	Result *BdevScanResponse
}

func (p *Provider) scanBdevTiers(direct bool, scan scanFn) (results []BdevTierScanResult, err error) {
	bdevCfgs := p.engineStorage.Tiers.BdevConfigs()
	results = make([]BdevTierScanResult, 0, len(bdevCfgs))

	// A config with SCM and no block devices is valid.
	if len(bdevCfgs) == 0 {
		return
	}

	for ti, cfg := range bdevCfgs {
		if cfg.Class != ClassNvme {
			continue
		}
		if cfg.Bdev.DeviceList.Len() == 0 {
			continue
		}

		p.RLock()
		req := BdevScanRequest{
			DeviceList:  cfg.Bdev.DeviceList,
			BypassCache: direct,
			VMDEnabled:  p.vmdEnabled,
		}

		bsr, err := scanBdevs(p.log, req, &p.bdevCache, scan)
		p.RUnlock()
		if err != nil {
			return nil, err
		}

		p.log.Debugf("storage provider for engine %d: scan tier-%d, bdevs %v, direct %v",
			p.engineIndex, ti, req.DeviceList, req.BypassCache)

		result := BdevTierScanResult{
			Tier:   cfg.Tier,
			Result: bsr,
		}
		results = append(results, result)
	}

	return
}

// ScanBdevTiers scans all Bdev tiers in the provider's engine storage configuration.
// If direct is set to true, bypass cache to retrieve up-to-date details.
func (p *Provider) ScanBdevTiers(direct bool) (results []BdevTierScanResult, err error) {
	return p.scanBdevTiers(direct, p.bdev.Scan)
}

type scanFn func(BdevScanRequest) (*BdevScanResponse, error)

func scanBdevs(log logging.Logger, req BdevScanRequest, cachedResp *BdevScanResponse, scan scanFn) (*BdevScanResponse, error) {
	if !req.BypassCache && cachedResp != nil && len(cachedResp.Controllers) != 0 {
		return cachedResp, nil
	}

	return scan(req)
}

// ScanBdevs either calls into backend bdev provider to scan SSDs or returns
// cached results if BypassCache is set to false in the request.
func (p *Provider) ScanBdevs(req BdevScanRequest) (*BdevScanResponse, error) {
	p.RLock()
	defer p.RUnlock()

	req.VMDEnabled = p.vmdEnabled
	return scanBdevs(p.log, req, &p.bdevCache, p.bdev.Scan)
}

// SetBdevCache stores given scan response in provider bdev cache.
func (p *Provider) SetBdevCache(resp BdevScanResponse) error {
	p.Lock()
	defer p.Unlock()

	// Filter out any controllers not configured in provider's engine storage config.
	if err := filterBdevScanResponse(p.GetBlockDevices(), &resp); err != nil {
		return errors.Wrap(err, "filtering scan response before caching")
	}

	p.log.Debugf("setting bdev cache in storage provider for engine %d: %v", p.engineIndex,
		resp.Controllers)
	p.bdevCache = resp
	p.vmdEnabled = resp.VMDEnabled

	return nil
}

// WithVMDEnabled enables VMD on storage provider.
func (p *Provider) WithVMDEnabled() *Provider {
	p.vmdEnabled = true
	return p
}

// QueryBdevFirmware queries NVMe SSD firmware.
func (p *Provider) QueryBdevFirmware(req NVMeFirmwareQueryRequest) (*NVMeFirmwareQueryResponse, error) {
	return p.bdev.QueryFirmware(req)
}

// UpdateBdevFirmware queries NVMe SSD firmware.
func (p *Provider) UpdateBdevFirmware(req NVMeFirmwareUpdateRequest) (*NVMeFirmwareUpdateResponse, error) {
	return p.bdev.UpdateFirmware(req)
}

// NewProvider returns an initialized storage provider.
func NewProvider(log logging.Logger, idx int, engineStorage *Config, sys SystemProvider, scm ScmProvider, bdev BdevProvider) *Provider {
	return &Provider{
		log:           log,
		engineIndex:   idx,
		engineStorage: engineStorage,
		Sys:           sys,
		Scm:           scm,
		bdev:          bdev,
	}
}
