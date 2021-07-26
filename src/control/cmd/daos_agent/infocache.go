//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"sync"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	invalidIndex         = -1
	verbsProvider        = "ofi+verbs"
	defaultNetworkDevice = "lo"
	defaultDomain        = "lo"
)

// NotCachedErr is the error returned when trying to fetch data that is not cached.
var NotCachedErr = errors.New("not cached")

func newAttachInfoCache(log logging.Logger, enabled bool) *attachInfoCache {
	return &attachInfoCache{
		log:     log,
		enabled: atm.NewBool(enabled),
	}
}

type attachInfoCache struct {
	mutex sync.RWMutex

	log         logging.Logger
	enabled     atm.Bool
	initialized atm.Bool

	// cached response from remote server
	attachInfo *mgmtpb.GetAttachInfoResp
}

// IsEnabled reports whether the cache is enabled.
func (c *attachInfoCache) IsEnabled() bool {
	if c == nil {
		return false
	}

	return c.enabled.IsTrue()
}

// IsCached reports whether there is data in the cache.
func (c *attachInfoCache) IsCached() bool {
	if c == nil {
		return false
	}
	return c.initialized.IsTrue()
}

// Cache preserves the results of a GetAttachInfo remote call.
func (c *attachInfoCache) Cache(ctx context.Context, resp *mgmtpb.GetAttachInfoResp) {
	if c == nil {
		return
	}

	c.mutex.Lock()
	defer c.mutex.Unlock()

	if !c.IsEnabled() {
		return
	}

	if resp == nil {
		return
	}

	c.attachInfo = resp
	c.initialized.SetTrue()
}

// GetAttachInfoResp fetches the cached GetAttachInfoResp.
func (c *attachInfoCache) GetAttachInfoResp() (*mgmtpb.GetAttachInfoResp, error) {
	if c == nil {
		return nil, NotCachedErr
	}

	c.mutex.RLock()
	defer c.mutex.RUnlock()

	if !c.IsCached() {
		return nil, NotCachedErr
	}

	aiCopy := proto.Clone(c.attachInfo)
	return aiCopy.(*mgmtpb.GetAttachInfoResp), nil
}

func newLocalFabricCache(log logging.Logger, enabled bool) *localFabricCache {
	return &localFabricCache{
		log:             log,
		localNUMAFabric: newNUMAFabric(log),
		enabled:         atm.NewBool(enabled),
	}
}

type localFabricCache struct {
	mutex sync.RWMutex

	log         logging.Logger
	enabled     atm.Bool
	initialized atm.Bool
	// cached fabric interfaces organized by NUMA affinity
	localNUMAFabric *NUMAFabric

	getDevAlias func(ctx context.Context, devName string) (string, error)
}

// IsEnabled reports whether the cache is enabled.
func (c *localFabricCache) IsEnabled() bool {
	if c == nil {
		return false
	}

	return c.enabled.IsTrue()
}

// IsCached reports whether there is data in the cache.
func (c *localFabricCache) IsCached() bool {
	if c == nil {
		return false
	}

	return c.initialized.IsTrue()
}

// Cache caches the results of a fabric scan locally.
func (c *localFabricCache) CacheScan(ctx context.Context, scan []*netdetect.FabricScan) {
	if c == nil {
		return
	}

	c.mutex.Lock()
	defer c.mutex.Unlock()

	if c.getDevAlias == nil {
		c.getDevAlias = netdetect.GetDeviceAlias
	}

	scanResult := NUMAFabricFromScan(ctx, c.log, scan, c.getDevAlias)
	c.setCache(scanResult)
}

// Cache initializes the cache with a specific NUMAFabric.
func (c *localFabricCache) Cache(ctx context.Context, nf *NUMAFabric) {
	if c == nil || nf == nil {
		return
	}

	c.mutex.Lock()
	defer c.mutex.Unlock()

	c.setCache(nf)
}

func (c *localFabricCache) setCache(nf *NUMAFabric) {
	if !c.IsEnabled() {
		return
	}

	c.localNUMAFabric = nf

	c.initialized.SetTrue()
}

// GetDevices fetches an appropriate fabric device from the cache.
func (c *localFabricCache) GetDevice(numaNode int, netDevClass uint32) (*FabricInterface, error) {
	if c == nil {
		return nil, NotCachedErr
	}

	c.mutex.RLock()
	defer c.mutex.RUnlock()

	if !c.IsCached() {
		return nil, NotCachedErr
	}
	return c.localNUMAFabric.GetDevice(numaNode, netDevClass)
}
