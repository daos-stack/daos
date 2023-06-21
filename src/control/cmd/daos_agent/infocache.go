//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"net"
	"strings"
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
		log:            log,
		ignoreIfaces:   cfg.ExcludeFabricIfaces,
		client:         client,
		cache:          cache.NewItemCache(log),
		getAttachInfo:  control.GetAttachInfo,
		fabricScan:     getFabricScanFn(log, cfg, hwprov.DefaultFabricScanner(log)),
		netIfaces:      net.Interfaces,
		devClassGetter: hwprov.DefaultNetDevClassProvider(log),
		devStateGetter: hwprov.DefaultNetDevStateProvider(log),
	}

	if cfg.DisableCache {
		ic.DisableAttachInfoCache()
		ic.DisableFabricCache()
		return ic
	}

	ic.EnableAttachInfoCache(time.Duration(cfg.CacheExpiration))
	if len(cfg.FabricInterfaces) > 0 {
		nf := NUMAFabricFromConfig(log, cfg.FabricInterfaces)
		ic.EnableStaticFabricCache(ctx, nf)
	} else {
		ic.EnableFabricCache()
	}

	return ic
}

func getFabricScanFn(log logging.Logger, cfg *Config, scanner *hardware.FabricScanner) fabricScanFn {
	return func(ctx context.Context, provs ...string) (*NUMAFabric, error) {
		fis, err := scanner.Scan(ctx, provs...)
		if err != nil {
			return nil, err
		}
		return NUMAFabricFromScan(ctx, log, fis).WithIgnoredDevices(cfg.ExcludeFabricIfaces), nil
	}
}

type cacheItem struct {
	sync.Mutex
	lastCached      time.Time
	refreshInterval time.Duration
}

func (ci *cacheItem) isStale() bool {
	if ci.refreshInterval == 0 {
		return false
	}
	return ci.lastCached.Add(ci.refreshInterval).Before(time.Now())
}

func (ci *cacheItem) isCached() bool {
	return !ci.lastCached.Equal(time.Time{})
}

type cachedAttachInfo struct {
	cacheItem
	fetch        getAttachInfoFn
	system       string
	rpcClient    control.UnaryInvoker
	lastResponse *control.GetAttachInfoResp
}

func newCachedAttachInfo(refreshInterval time.Duration, system string, rpcClient control.UnaryInvoker, fetchFn getAttachInfoFn) *cachedAttachInfo {
	return &cachedAttachInfo{
		cacheItem: cacheItem{
			refreshInterval: refreshInterval,
		},
		fetch:     fetchFn,
		system:    system,
		rpcClient: rpcClient,
	}
}

func sysAttachInfoKey(sys string) string {
	return attachInfoKey + "-" + sys
}

// Key returns the key for this system-specific instance of GetAttachInfo.
func (ci *cachedAttachInfo) Key() string {
	if ci == nil {
		return ""
	}
	if ci.system == "" {
		return attachInfoKey
	}
	return sysAttachInfoKey(ci.system)
}

// NeedsRefresh checks whether the cached data needs to be refreshed.
func (ci *cachedAttachInfo) NeedsRefresh() bool {
	if ci == nil {
		return false
	}
	return !ci.isCached() || ci.isStale()
}

// Refresh contacts the remote management server and refreshes the GetAttachInfo cache.
func (ci *cachedAttachInfo) Refresh(ctx context.Context) error {
	if ci == nil {
		return errors.New("cachedAttachInfo is nil")
	}

	req := &control.GetAttachInfoReq{System: ci.system, AllRanks: true}
	resp, err := ci.fetch(ctx, ci.rpcClient, req)
	if err != nil {
		return errors.Wrap(err, "refreshing cached attach info failed")
	}

	ci.lastResponse = resp
	ci.lastCached = time.Now()
	return nil
}

type cachedFabricInfo struct {
	cacheItem
	fetch       fabricScanFn
	lastResults *NUMAFabric
}

func newCachedFabricInfo(log logging.Logger, fetchFn fabricScanFn) *cachedFabricInfo {
	return &cachedFabricInfo{
		fetch: fetchFn,
	}
}

// Key returns the cache key for the fabric information.
func (cfi *cachedFabricInfo) Key() string {
	return fabricKey
}

// NeedsRefresh indicates that the fabric information does not need to be refreshed unless it has
// never been populated.
func (cfi *cachedFabricInfo) NeedsRefresh() bool {
	if cfi == nil {
		return false
	}
	return !cfi.isCached()
}

// Refresh scans the hardware for information about the fabric devices and caches the result.
func (cfi *cachedFabricInfo) Refresh(ctx context.Context) error {
	if cfi == nil {
		return errors.New("cachedFabricInfo is nil")
	}

	results, err := cfi.fetch(ctx)
	if err != nil {
		return errors.Wrap(err, "refreshing cached fabric info")
	}

	cfi.lastResults = results
	cfi.lastCached = time.Now()
	return nil
}

// InfoCache is a cache for the results of expensive operations needed by the agent.
type InfoCache struct {
	log                     logging.Logger
	cache                   *cache.ItemCache
	fabricCacheDisabled     atm.Bool
	attachInfoCacheDisabled atm.Bool

	getAttachInfo  getAttachInfoFn
	fabricScan     fabricScanFn
	netIfaces      func() ([]net.Interface, error)
	devClassGetter hardware.NetDevClassProvider
	devStateGetter hardware.NetDevStateProvider

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
	return !c.attachInfoCacheDisabled.Load()
}

// DisableAttachInfoCache fully disables the attach info cache.
func (c *InfoCache) DisableAttachInfoCache() {
	if c == nil {
		return
	}
	c.attachInfoCacheDisabled.Store(true)
}

// EnableAttachInfoCache enables a refreshable GetAttachInfo cache.
func (c *InfoCache) EnableAttachInfoCache(interval time.Duration) {
	if c == nil {
		return
	}
	c.attachInfoRefresh = interval
	c.attachInfoCacheDisabled.Store(false)
}

// IsFabricCacheEnabled checks whether the NUMAFabric cache is enabled.
func (c *InfoCache) IsFabricCacheEnabled() bool {
	if c == nil {
		return false
	}
	return !c.fabricCacheDisabled.Load()
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

// EnableStaticFabricCache sets up a fabric cache based on a static value that cannot be refreshed.
func (c *InfoCache) EnableStaticFabricCache(ctx context.Context, nf *NUMAFabric) {
	if c == nil {
		return
	}

	item := &cachedFabricInfo{
		cacheItem: cacheItem{
			lastCached: time.Now(),
		},
		fetch: func(context.Context, ...string) (*NUMAFabric, error) {
			return nf, nil
		},
		lastResults: nf,
	}
	if err := c.cache.Set(item); err != nil {
		c.log.Errorf("error setting static fabric cache: %v", err)
	}
	c.EnableFabricCache()
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
		return newCachedAttachInfo(c.attachInfoRefresh, sys, c.client, c.getAttachInfo), nil
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

	return copyGetAttachInfoResp(cai.lastResponse), nil
}

func copyGetAttachInfoResp(orig *control.GetAttachInfoResp) *control.GetAttachInfoResp {
	if orig == nil {
		return nil
	}

	cp := new(control.GetAttachInfoResp)
	*cp = *orig

	// Copy slices instead of using original pointers
	cp.MSRanks = make([]uint32, len(orig.MSRanks))
	_ = copy(cp.MSRanks, orig.MSRanks)
	cp.ServiceRanks = make([]*control.PrimaryServiceRank, len(orig.ServiceRanks))
	_ = copy(cp.ServiceRanks, orig.ServiceRanks)

	if orig.ClientNetHint.EnvVars != nil {
		cp.ClientNetHint.EnvVars = make([]string, len(orig.ClientNetHint.EnvVars))
		_ = copy(cp.ClientNetHint.EnvVars, orig.ClientNetHint.EnvVars)
	}
	return cp
}

func (c *InfoCache) getAttachInfoRemote(ctx context.Context, sys string) (*control.GetAttachInfoResp, error) {
	c.log.Debug("GetAttachInfo not cached, fetching directly from MS")
	// Ask the MS for _all_ info, regardless of pbReq.AllRanks, so that the
	// cache can serve future "pbReq.AllRanks == true" requests.
	req := new(control.GetAttachInfoReq)
	req.SetSystem(sys)
	req.AllRanks = true
	resp, err := c.getAttachInfo(ctx, c.client, req)
	if err != nil {
		return nil, errors.Wrapf(err, "GetAttachInfo %+v", req)
	}

	if resp.ClientNetHint.Provider == "" {
		return nil, errors.New("GetAttachInfo response contained no provider")
	}
	return resp, nil
}

// GetFabricDevice returns an appropriate fabric device from the cache based on the requested parameters,
// and refreshes the cache if necessary.
func (c *InfoCache) GetFabricDevice(ctx context.Context, numaNode int, netDevClass hardware.NetDevClass, provider string) (*FabricInterface, error) {
	if c == nil {
		return nil, errors.New("InfoCache is nil")
	}
	nf, err := c.getNUMAFabric(ctx, netDevClass, provider)
	if err != nil {
		return nil, err
	}

	return nf.GetDevice(numaNode, netDevClass, provider)
}

func (c *InfoCache) getNUMAFabric(ctx context.Context, netDevClass hardware.NetDevClass, providers ...string) (*NUMAFabric, error) {
	if !c.IsFabricCacheEnabled() {
		c.log.Debug("NUMAFabric not cached, rescanning")
		if err := c.waitFabricReady(ctx, netDevClass); err != nil {
			return nil, err
		}
		return c.fabricScan(ctx, providers...)
	}

	createItem := func() (cache.Item, error) {
		c.log.Debug("NUMAFabric cache miss")
		if err := c.waitFabricReady(ctx, netDevClass); err != nil {
			return nil, err
		}
		return newCachedFabricInfo(c.log, c.fabricScan), nil
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

func (c *InfoCache) waitFabricReady(ctx context.Context, netDevClass hardware.NetDevClass) error {
	ifaces, err := c.netIfaces()
	if err != nil {
		return errors.Wrap(err, "getting net interfaces")
	}

	var needIfaces []string
	for _, iface := range ifaces {
		devClass, err := c.devClassGetter.GetNetDevClass(iface.Name)
		if err != nil {
			return errors.Wrapf(err, "getting device class for %s", iface.Name)
		}
		if devClass == netDevClass {
			needIfaces = append(needIfaces, iface.Name)
		}
	}

	return hardware.WaitFabricReady(ctx, c.log, hardware.WaitFabricReadyParams{
		StateProvider:  c.devStateGetter,
		FabricIfaces:   needIfaces,
		IgnoreUnusable: true,
		IterationSleep: time.Second,
	})
}

// Refresh forces any enabled, refreshable caches to re-fetch their content immediately.
func (c *InfoCache) Refresh(ctx context.Context) error {
	if c == nil {
		return errors.New("InfoCache is nil")
	}

	if !c.IsAttachInfoCacheEnabled() && !c.IsFabricCacheEnabled() {
		return errors.New("all caches are disabled")
	}

	keys := []string{}
	if c.IsFabricCacheEnabled() && c.cache.Has(fabricKey) {
		keys = append(keys, fabricKey)
	}
	if c.IsAttachInfoCacheEnabled() {
		for _, k := range c.cache.Keys() {
			if strings.HasPrefix(k, attachInfoKey) {
				keys = append(keys, k)
			}
		}
	}
	c.log.Debugf("refreshing cache keys: %+v", keys)
	return c.cache.Refresh(ctx, keys...)
}
