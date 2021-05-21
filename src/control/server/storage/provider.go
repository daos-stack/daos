//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"fmt"
	"os"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
)

type SystemProvider interface {
	system.IsMountedProvider
	GetfsUsage(string) (uint64, uint64, error)
}

type Provider struct {
	log           logging.Logger
	engineIndex   int
	engineStorage *StorageConfig
	Sys           SystemProvider
	Scm           ScmProvider
	Bdev          BdevProvider
}

func DefaultProvider(log logging.Logger, idx int, engineStorage *StorageConfig) *Provider {
	return &Provider{
		log:           log,
		engineIndex:   idx,
		engineStorage: engineStorage,
		Sys:           system.DefaultProvider(),
		Scm:           NewScmForwarder(log),
		Bdev:          NewBdevForwarder(log),
	}
}

func (p *Provider) GetScmConfig() (*Config, error) {
	// NB: A bit wary of building in assumptions again about the number of
	// SCM tiers, but for the sake of expediency we'll assume that there is
	// only one. If that needs to change, hopefully it won't require quite so
	// many invasive code updates.
	scmConfigs := p.engineStorage.Tiers.ScmConfigs()
	if len(scmConfigs) != 1 {
		return nil, errors.New("expected exactly 1 SCM tier in storage configuration")
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

func (p *Provider) ScmIsMounted() (bool, error) {
	cfg, err := p.GetScmConfig()
	if err != nil {
		return false, err
	}
	return p.Sys.IsMounted(cfg.Scm.MountPoint)
}

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
	case ClassRAM:
		req.Size = cfg.Scm.RamdiskSize
	case ClassDCPM:
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
	case ClassRAM:
		req.Ramdisk = &RamdiskParams{
			Size: scmCfg.RamdiskSize,
		}
	case ClassDCPM:
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

func (p *Provider) HasBlockDevices() bool {
	for _, cfg := range p.engineStorage.Tiers.BdevConfigs() {
		if len(cfg.Bdev.DeviceList) > 0 {
			return true
		}
	}
	return false
}

type BdevTierFormatResult struct {
	Tier   int
	Error  error
	Result *BdevFormatResponse
}

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
		results[i].Result, results[i].Error = p.Bdev.Format(BdevFormatRequest{
			Class:      cfg.Class,
			DeviceList: cfg.Bdev.DeviceList,
			MemSize:    p.engineStorage.MemSize,
		})

		if err := results[i].Error; err != nil {
			p.log.Errorf("Instance %d: format failed (%s)", err)
			continue
		}

		p.log.Infof("Instance %d: finished format of %s block devices %v",
			p.engineIndex, cfg.Class, cfg.Bdev.DeviceList)
	}

	return
}

type BdevTierScanResult struct {
	Tier   int
	Result *BdevScanResponse
}

func (p *Provider) ScanBdevTiers(direct bool) (results []BdevTierScanResult, err error) {
	bdevCfgs := p.engineStorage.Tiers.BdevConfigs()
	results = make([]BdevTierScanResult, 0, len(bdevCfgs))

	// A config with SCM and no block devices is valid.
	if len(bdevCfgs) == 0 {
		return
	}

	for _, cfg := range bdevCfgs {
		if cfg.Class != ClassNvme {
			continue
		}
		if len(cfg.Bdev.DeviceList) == 0 {
			continue
		}
		tsr := BdevTierScanResult{Tier: cfg.Tier}

		req := BdevScanRequest{DeviceList: cfg.Bdev.DeviceList}
		p.log.Debugf("instance %d storage scan: only show bdev devices in config %v",
			p.engineIndex, req.DeviceList)

		// scan through control-plane to get up-to-date stats if io
		// server is not active (and therefore has not claimed the
		// assigned devices), bypass cache to get fresh health stats
		if direct {
			req.NoCache = true

			bsr, err := p.Bdev.Scan(req)
			if err != nil {
				return nil, errors.Wrap(err, "nvme scan")
			}
			tsr.Result = bsr
			results = append(results, tsr)

			continue
		}

		bsr, err := p.Bdev.Scan(req)
		if err != nil {
			return nil, errors.Wrap(err, "nvme scan")
		}
		tsr.Result = bsr
		results = append(results, tsr)
	}

	return
}

func (p *Provider) GenEngineConfig() error {
	p.engineStorage.TiersNum = len(p.engineStorage.Tiers)
	cfgScm, err := p.GetScmConfig()
	if err != nil {
		return err
	}

	bcp, err := NewClassProvider(p.log, cfgScm.Scm.MountPoint, p.engineStorage)
	if err != nil {
		return err
	}

	return bcp.GenConfigFile()
}
