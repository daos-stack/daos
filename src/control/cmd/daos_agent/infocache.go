//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/cache"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwprov"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	attachInfoKey = "GetAttachInfo"
	fabricKey     = "NUMAFabric"
)

type getAttachInfoFn func(ctx context.Context, rpcClient control.UnaryInvoker, req *control.GetAttachInfoReq) (*control.GetAttachInfoResp, error)
type fabricScanFn func(ctx context.Context, providers ...string) (*hardware.FabricInterfaceSet, error)

// NewInfoCache creates a new InfoCache with appropriate parameters set.
func NewInfoCache(log logging.Logger, cfg *Config) *InfoCache {
	ic := &InfoCache{
		log:          log,
		ignoreIfaces: cfg.ExcludeFabricIfaces,
	}

	if cfg.DisableCache {
		return ic
	}

	ic.refreshInterval = cfg.CacheRefreshInterval()
	ic.EnableAttachInfoCache(ic.refreshInterval)

	if len(cfg.FabricInterfaces) > 0 {
		nf := NUMAFabricFromConfig(log, cfg.FabricInterfaces)
		ic.EnableStaticFabricCache(nf)
	} else {
		ic.EnableFabricCache(ic.refreshInterval)
	}

	return ic
}

// InfoCache is a cache for the results of expensive operations needed by the agent.
type InfoCache struct {
	log           logging.Logger
	cache         cache.ItemCache
	getAttachInfo getAttachInfoFn
	fabricScan    fabricScanFn

	sys             string
	refreshInterval time.Duration
	providers       common.StringSet
	ignoreIfaces    common.StringSet
}

// AddProvider adds a fabric provider to the scan list.
func (c *InfoCache) AddProvider(prov string) {
	if c == nil || prov == "" {
		return
	}
	if c.providers == nil {
		c.providers = common.NewStringSet()
	}
	c.providers.Add(prov)
}

// IsAttachInfoEnabled checks whether the GetAttachInfo cache is enabled.
func (c *InfoCache) IsAttachInfoCacheEnabled() bool {
	if c == nil {
		return false
	}
	return c.cache.Has(attachInfoKey)
}

// DisableAttachInfoCache fully disables the attach info cache.
func (c *InfoCache) DisableAttachInfoCache() {
	if c == nil {
		return
	}
	c.cache.Delete(attachInfoKey)
}

// EnableAttachInfoCache enables a refreshable GetAttachInfo cache.
func (c *InfoCache) EnableAttachInfoCache(refreshInterval time.Duration) {
	if c == nil {
		return
	}
	c.cache.Set(attachInfoKey, cache.NewFetchableItem(refreshInterval, c.fetchAttachInfoData))
}

func (c *InfoCache) fetchAttachInfoData(ctx context.Context) (cache.Data, error) {
	return c.getAttachInfoRemote(ctx, c.sys)
}

func (c *InfoCache) getAttachInfoRemote(ctx context.Context, sys string) (*control.GetAttachInfoResp, error) {
	if c.getAttachInfo == nil {
		c.getAttachInfo = control.GetAttachInfo
	}

	// Ask the MS for _all_ info, regardless of pbReq.AllRanks, so that the
	// cache can serve future "pbReq.AllRanks == true" requests.
	req := new(control.GetAttachInfoReq)
	req.SetSystem(sys)
	req.AllRanks = true
	resp, err := c.getAttachInfo(ctx, control.DefaultClient(), req)
	if err != nil {
		return nil, errors.Wrapf(err, "GetAttachInfo %+v", req)
	}

	if resp.ClientNetHint.Provider == "" {
		return nil, errors.New("GetAttachInfo response contained no provider")
	}
	return resp, nil
}

// IsFabricCacheEnabled checks whether the NUMAFabric cache is enabled.
func (c *InfoCache) IsFabricCacheEnabled() bool {
	if c == nil {
		return false
	}
	return c.cache.Has(fabricKey)
}

// DisableFabricCache fully disables the fabric device cache.
func (c *InfoCache) DisableFabricCache() {
	if c == nil {
		return
	}
	c.cache.Delete(fabricKey)
}

// EnableFabricCache enables a refreshable local fabric cache.
func (c *InfoCache) EnableFabricCache(refreshInterval time.Duration) {
	if c == nil {
		return
	}
	c.cache.Set(fabricKey, cache.NewFetchableItem(refreshInterval, c.fetchFabricData))
}

func (c *InfoCache) fetchFabricData(ctx context.Context) (cache.Data, error) {
	return c.scanFabric(ctx, c.providers.ToSlice()...)
}

func (c *InfoCache) scanFabric(ctx context.Context, providers ...string) (*NUMAFabric, error) {
	if c.fabricScan == nil {
		c.fabricScan = func(ctx context.Context, provs ...string) (*hardware.FabricInterfaceSet, error) {
			scanner := hwprov.DefaultFabricScanner(c.log)
			return scanner.Scan(ctx, provs...)
		}
	}
	fis, err := c.fabricScan(ctx, providers...)
	if err != nil {
		return nil, errors.Wrap(err, "scanning fabric")
	}

	return NUMAFabricFromScan(ctx, c.log, fis).WithIgnoredDevices(c.ignoreIfaces), nil
}

// EnableStaticFabricCache sets up a fabric cache based on a static value that cannot be refreshed.
func (c *InfoCache) EnableStaticFabricCache(nf *NUMAFabric) {
	c.cache.Set(fabricKey, cache.NewItem(nf))
}

// GetAttachInfo fetches the attach info from the cache, and refreshes if necessary.
func (c *InfoCache) GetAttachInfo(ctx context.Context, sys string) (*control.GetAttachInfoResp, error) {
	if c == nil {
		return nil, errors.New("InfoCache is nil")
	}
	if !c.IsAttachInfoCacheEnabled() {
		return c.getAttachInfoRemote(ctx, sys)
	}

	c.sys = sys
	data, err := c.cache.Get(ctx, attachInfoKey)
	if err != nil {
		return nil, errors.Wrap(err, "getting attach info from cache")
	}

	resp, ok := data.(*control.GetAttachInfoResp)
	if !ok {
		return nil, errors.Errorf("unexpected attach info data type %T", data)
	}

	return resp, nil
}

// GetFabricDevice returns an appropriate fabric device from the cache based on the requested parameters,
// and refreshes the cache if necessary.
func (c *InfoCache) GetFabricDevice(ctx context.Context, numaNode int, netDevClass hardware.NetDevClass, provider string) (*FabricInterface, error) {
	nf, err := c.getNUMAFabric(ctx, provider)
	if err != nil {
		return nil, err
	}

	return nf.GetDevice(numaNode, netDevClass, provider)
}

func (c *InfoCache) getNUMAFabric(ctx context.Context, provider string) (*NUMAFabric, error) {
	if !c.IsFabricCacheEnabled() {
		return c.scanFabric(ctx, provider)
	}

	data, err := c.cache.Get(ctx, fabricKey)
	if err != nil {
		return nil, errors.Wrap(err, "getting fabric devices from cache")
	}

	nf, ok := data.(*NUMAFabric)
	if !ok {
		return nil, errors.Errorf("unexpected fabric cache data type %T", data)
	}

	return nf, nil
}

// Refresh forces any enabled, refreshable caches to re-fetch their content immediately.
func (c *InfoCache) Refresh(ctx context.Context) error {
	return c.cache.Refresh(ctx)
}
