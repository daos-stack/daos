//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"fmt"
	"os"

	"github.com/dustin/go-humanize"
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
	engineStorage *Config
	Sys           SystemProvider
	Scm           ScmProvider
	Bdev          BdevProvider
}

func DefaultProvider(log logging.Logger, idx int, engineStorage *Config) *Provider {
	return &Provider{
		log:           log,
		engineIndex:   idx,
		engineStorage: engineStorage,
		Sys:           system.DefaultProvider(),
		Scm:           NewScmForwarder(log),
		Bdev:          NewBdevForwarder(log),
	}
}

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

func BdevTierPropertiesFromConfig(cfg *TierConfig) BdevTierProperties {
	return BdevTierProperties{
		Class:      cfg.Class,
		DeviceList: cfg.Bdev.DeviceList,
		// cfg size in nr GiBytes
		DeviceFileSize: uint64(humanize.GiByte * cfg.Bdev.FileSize),
		Tier:           cfg.Tier,
	}
}

// todo_tiering: merge ret error
// FormatRequestFromConfig returns a format request populated from a bdev
// storage configuration.
func BdevFormatRequestFromConfig(log logging.Logger, cfg *TierConfig) (BdevFormatRequest, error) {
	fr := BdevFormatRequest{
		Properties: BdevTierPropertiesFromConfig(cfg),
		OwnerUID:   os.Geteuid(),
		OwnerGID:   os.Getegid(),
	}

	hn, err := os.Hostname()
	if err != nil {
		log.Errorf("get hostname: %s", err)
		return fr, err
	}
	fr.Properties.Hostname = hn

	return fr, nil
}

// todo_tiering: merge ret error
func BdevWriteNvmeConfigRequestFromConfig(log logging.Logger, cfg *Config) (BdevWriteNvmeConfigRequest, error) {
	bdevTiers := cfg.Tiers.BdevConfigs()
	req := BdevWriteNvmeConfigRequest{
		ConfigOutputPath: cfg.ConfigOutputPath,
		OwnerUID:         os.Geteuid(),
		OwnerGID:         os.Getegid(),
	}

	hn, err := os.Hostname()
	if err != nil {
		log.Errorf("get hostname: %s", err)
		return req, err
	}

	req.TierProps = make([]BdevTierProperties, 0, len(bdevTiers))
	for _, tier := range bdevTiers {
		tierProps := BdevTierPropertiesFromConfig(tier)
		tierProps.Hostname = hn
		req.TierProps = append(req.TierProps, tierProps)
	}

	return req, nil
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
		req, err := BdevFormatRequestFromConfig(p.log, cfg)
		if err != nil {
			results[i].Error = err
			p.log.Errorf("Instance %d: format failed (%s)", err)
			continue
		}

		req.MemSize = p.engineStorage.MemSize
		results[i].Result, results[i].Error = p.Bdev.Format(req)

		if err := results[i].Error; err != nil {
			p.log.Errorf("Instance %d: format failed (%s)", err)
			continue
		}

		p.log.Infof("Instance %d: finished format of %s block devices %v",
			p.engineIndex, cfg.Class, cfg.Bdev.DeviceList)
	}

	return
}

func (p *Provider) WriteNvmeConfig() error {
	req, err := BdevWriteNvmeConfigRequestFromConfig(p.log, p.engineStorage)
	if err != nil {
		return err
	}

	_, err = p.Bdev.WriteNvmeConfig(req)
	return err
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

func NewProvider(log logging.Logger, idx int, engineStorage *Config, sys SystemProvider, scm ScmProvider, bdev BdevProvider) *Provider {
	return &Provider{
		log:           log,
		engineIndex:   idx,
		engineStorage: engineStorage,
		Sys:           sys,
		Scm:           scm,
		Bdev:          bdev,
	}
}
