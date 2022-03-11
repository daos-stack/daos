//
// (C) Copyright 2020-2022 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/lib/hardware"
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
	mutex sync.Mutex

	log         logging.Logger
	enabled     atm.Bool
	initialized atm.Bool

	// cached response from remote server
	attachInfo *mgmtpb.GetAttachInfoResp
}

func (c *attachInfoCache) isCached() bool {
	return c.initialized.IsTrue()
}

func (c *attachInfoCache) isEnabled() bool {
	return c.enabled.IsTrue()
}

func (c *attachInfoCache) getAttachInfoResp() (*mgmtpb.GetAttachInfoResp, error) {
	if !c.isCached() {
		return nil, NotCachedErr
	}

	aiCopy := proto.Clone(c.attachInfo)
	return aiCopy.(*mgmtpb.GetAttachInfoResp), nil
}

type getAttachInfoFn func(ctx context.Context, numaNode int, sys string) (*mgmtpb.GetAttachInfoResp, error)

// Get is responsible for returning a GetAttachInfo response, either from the cache or from
// the remote server if the cache is disabled.
func (c *attachInfoCache) Get(ctx context.Context, numaNode int, sys string, getRemote getAttachInfoFn) (*mgmtpb.GetAttachInfoResp, error) {
	c.mutex.Lock()
	defer c.mutex.Unlock()

	if c.isEnabled() && c.isCached() {
		return c.getAttachInfoResp()
	}

	attachInfo, err := getRemote(ctx, numaNode, sys)
	if err != nil {
		return nil, err
	}

	if !c.isEnabled() {
		return attachInfo, nil
	}

	c.attachInfo = attachInfo
	c.initialized.SetTrue()

	return c.getAttachInfoResp()
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
func (c *localFabricCache) CacheScan(ctx context.Context, scan *hardware.FabricInterfaceSet) {
	if c == nil {
		return
	}

	c.mutex.Lock()
	defer c.mutex.Unlock()

	scanResult := NUMAFabricFromScan(ctx, c.log, scan)
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
	c.log.Debugf("cached:\n%+v", c.localNUMAFabric.numaMap)
}

// GetDevices fetches an appropriate fabric device from the cache.
func (c *localFabricCache) GetDevice(numaNode int, netDevClass hardware.NetDevClass, provider string) (*FabricInterface, error) {
	if c == nil {
		return nil, NotCachedErr
	}

	c.mutex.RLock()
	defer c.mutex.RUnlock()

	if !c.IsCached() {
		return nil, NotCachedErr
	}
	return c.localNUMAFabric.GetDevice(numaNode, netDevClass, provider)
}
