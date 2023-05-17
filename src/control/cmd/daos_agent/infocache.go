//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"sync"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/atm"
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
type fabricScanFn func(ctx context.Context, providers ...string) (*NUMAFabric, error)

// NewInfoCache creates a new InfoCache with appropriate parameters set.
func NewInfoCache(ctx context.Context, log logging.Logger, client control.UnaryInvoker, cfg *Config) *InfoCache {
	ic := &InfoCache{
		log:          log,
		ignoreIfaces: cfg.ExcludeFabricIfaces,
		client:       client,
		cache:        cache.NewItemCache(ctx, log),
	}

	if cfg.DisableCache {
		return ic
	}

	ic.EnableAttachInfoCache(time.Duration(cfg.AttachInfoRefresh))
	if len(cfg.FabricInterfaces) > 0 {
		nf := NUMAFabricFromConfig(log, cfg.FabricInterfaces)
		ic.EnableStaticFabricCache(ctx, nf)
	} else {
		ic.EnableFabricCache()
	}

	return ic
}

type cacheItem struct {
	sync.Mutex
	lastCached      time.Time
	refreshInterval time.Duration
}

// Override this to return 0 for items that shouldn't be auto-refreshed,
// but could be refreshed if requested after a certain period of time,
// e.g. PoolFindByLabel.
func (ci *cacheItem) RefreshInterval() time.Duration {
	return ci.refreshInterval
}

func (ci *cacheItem) isStale() bool {
	if ci.refreshInterval == 0 {
		return false
	}
	return ci.lastCached.Add(ci.refreshInterval).Before(time.Now())
}

type cachedAttachInfo struct {
	cacheItem
	refreshFn    getAttachInfoFn
	system       string
	rpcClient    control.UnaryInvoker
	lastResponse *control.GetAttachInfoResp
}

func newCachedAttachInfo(refreshInterval time.Duration, system string, rpcClient control.UnaryInvoker) *cachedAttachInfo {
	return &cachedAttachInfo{
		cacheItem: cacheItem{
			refreshInterval: refreshInterval,
		},
		refreshFn: control.GetAttachInfo,
		system:    system,
		rpcClient: rpcClient,
	}
}

func sysAttachInfoKey(sys string) string {
	return attachInfoKey + "-" + sys
}

func (ci *cachedAttachInfo) Key() string {
	return sysAttachInfoKey(ci.system)
}

func (ci *cachedAttachInfo) Refresh(ctx context.Context, force bool) error {
	ci.Lock()
	defer ci.Unlock()

	if !force && ci.lastResponse != nil && !ci.isStale() {
		return nil
	}

	req := &control.GetAttachInfoReq{System: ci.system, AllRanks: true}
	resp, err := ci.refreshFn(ctx, ci.rpcClient, req)
	if err != nil {
		return errors.Wrap(err, "refreshing cached attach info failed")
	}

	ci.lastResponse = resp
	ci.lastCached = time.Now()
	return nil
}

type cachedFabricInfo struct {
	cacheItem
	refreshFn   fabricScanFn
	lastResults *NUMAFabric
}

func newCachedFabricInfo(log logging.Logger, ignoredIfaces common.StringSet, provs ...string) *cachedFabricInfo {
	return &cachedFabricInfo{
		refreshFn: func(ctx context.Context, _ ...string) (*NUMAFabric, error) {
			scanner := hwprov.DefaultFabricScanner(log)
			fis, err := scanner.Scan(ctx, provs...)
			if err != nil {
				return nil, err
			}
			return NUMAFabricFromScan(ctx, log, fis).WithIgnoredDevices(ignoredIfaces), nil
		},
	}
}

func (cfi *cachedFabricInfo) Key() string {
	return fabricKey
}

func (cfi *cachedFabricInfo) Refresh(ctx context.Context, force bool) error {
	cfi.Lock()
	defer cfi.Unlock()

	if !force && cfi.lastResults != nil && !cfi.isStale() {
		return nil
	}

	results, err := cfi.refreshFn(ctx)
	if err != nil {
		return errors.Wrap(err, "refreshing cached fabric info")
	}

	cfi.lastResults = results
	cfi.lastCached = time.Now()
	return nil
}

// InfoCache is a cache for the results of expensive operations needed by the agent.
type InfoCache struct {
	log                 logging.Logger
	cache               *cache.ItemCache
	fabricCacheDisabled atm.Bool
	getAttachInfo       getAttachInfoFn
	fabricScan          fabricScanFn

	client            control.UnaryInvoker
	attachInfoRefresh time.Duration
	providers         common.StringSet
	ignoreIfaces      common.StringSet
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
	return c.attachInfoRefresh > 0
}

// DisableAttachInfoCache fully disables the attach info cache.
func (c *InfoCache) DisableAttachInfoCache() {
	if c == nil {
		return
	}
	c.attachInfoRefresh = 0
}

// EnableAttachInfoCache enables a refreshable GetAttachInfo cache.
func (c *InfoCache) EnableAttachInfoCache(interval time.Duration) {
	if c == nil {
		return
	}
	c.attachInfoRefresh = interval
}

func (c *InfoCache) getAttachInfoRemote(ctx context.Context, sys string) (*control.GetAttachInfoResp, error) {
	if c.getAttachInfo == nil {
		c.getAttachInfo = control.GetAttachInfo
	}

	c.log.Debug("GetAttachInfo not cached, fetching directly from MS")
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
	return c.fabricCacheDisabled.IsFalse()
}

// DisableFabricCache fully disables the fabric device cache.
func (c *InfoCache) DisableFabricCache() {
	if c == nil {
		return
	}
	c.fabricCacheDisabled.Store(true)
}

// EnableFabricCache enables a refreshable local fabric cache.
func (c *InfoCache) EnableFabricCache() {
	if c == nil {
		return
	}
	c.fabricCacheDisabled.Store(false)
}

func (c *InfoCache) scanFabric(ctx context.Context, providers ...string) (*NUMAFabric, error) {
	c.log.Debug("NUMAFabric not cached, rescanning")
	if c.fabricScan == nil {
		c.fabricScan = func(ctx context.Context, provs ...string) (*NUMAFabric, error) {
			scanner := hwprov.DefaultFabricScanner(c.log)
			fis, err := scanner.Scan(ctx, provs...)
			if err != nil {
				return nil, err
			}
			return NUMAFabricFromScan(ctx, c.log, fis).WithIgnoredDevices(c.ignoreIfaces), nil
		}
	}
	return c.fabricScan(ctx, providers...)
}

// EnableStaticFabricCache sets up a fabric cache based on a static value that cannot be refreshed.
func (c *InfoCache) EnableStaticFabricCache(ctx context.Context, nf *NUMAFabric) {
	item := &cachedFabricInfo{
		cacheItem: cacheItem{
			lastCached: time.Now(),
		},
		refreshFn: func(context.Context, ...string) (*NUMAFabric, error) {
			return nf, nil
		},
		lastResults: nf,
	}
	if err := c.cache.Set(ctx, item); err != nil {
		c.log.Errorf("error setting static fabric cache: %v", err)
	}
}

// GetAttachInfo fetches the attach info from the cache, and refreshes if necessary.
func (c *InfoCache) GetAttachInfo(ctx context.Context, sys string) (*control.GetAttachInfoResp, error) {
	if c == nil {
		return nil, errors.New("InfoCache is nil")
	}
	if !c.IsAttachInfoCacheEnabled() {
		return c.getAttachInfoRemote(ctx, sys)
	}

	// Use the default system if none is specified.
	if sys == "" {
		sys = build.DefaultSystemName
	}
	createItem := func() (cache.Item, error) {
		c.log.Debugf("cache miss for %s", sysAttachInfoKey(sys))
		return newCachedAttachInfo(c.attachInfoRefresh, sys, c.client), nil
	}

	item, release, err := c.cache.GetOrCreate(ctx, sysAttachInfoKey(sys), createItem)
	defer release()
	if err != nil {
		return nil, errors.Wrap(err, "getting attach info from cache")
	}

	cai, ok := item.(*cachedAttachInfo)
	if !ok {
		return nil, errors.Errorf("unexpected attach info data type %T", item)
	}

	return cai.lastResponse, nil
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

	createItem := func() (cache.Item, error) {
		c.log.Debug("NUMAFabric cache miss")
		return newCachedFabricInfo(c.log, c.ignoreIfaces, c.providers.ToSlice()...), nil
	}

	item, release, err := c.cache.GetOrCreate(ctx, fabricKey, createItem)
	defer release()
	if err != nil {
		return nil, errors.Wrap(err, "getting fabric scan from cache")
	}

	cfi, ok := item.(*cachedFabricInfo)
	if !ok {
		return nil, errors.Errorf("unexpected fabric data type %T", item)
	}

	return cfi.lastResults, nil
}

// Refresh forces any enabled, refreshable caches to re-fetch their content immediately.
func (c *InfoCache) Refresh(ctx context.Context) error {
	return c.cache.Refresh(ctx)
}
