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
	AttachInfo *mgmtpb.GetAttachInfoResp
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
	return c.enabled.IsTrue() && c.initialized.IsTrue()
}

// Cache preserves the results of a GetAttachInfo remote call.
func (c *attachInfoCache) Cache(ctx context.Context, resp *mgmtpb.GetAttachInfoResp) error {
	if c == nil {
		return errors.New("nil attachInfoCache")
	}

	c.mutex.Lock()
	defer c.mutex.Unlock()

	if !c.IsEnabled() {
		return errors.New("cache is not enabled")
	}

	if resp == nil {
		return errors.New("nil input")
	}

	c.AttachInfo = resp

	c.initialized.SetTrue()
	return nil
}

func newLocalFabricCache(log logging.Logger) *localFabricCache {
	return &localFabricCache{
		log:             log,
		localNUMAFabric: newNUMAFabric(log),
	}
}

type localFabricCache struct {
	sync.Mutex // Caller should lock and unlock around cache operations

	log         logging.Logger
	initialized atm.Bool
	// cached fabric interfaces organized by NUMA affinity
	localNUMAFabric *NUMAFabric

	getDevAlias func(ctx context.Context, devName string) (string, error)
}

// IsCached reports whether there is data in the cache.
func (c *localFabricCache) IsCached() bool {
	if c == nil {
		return false
	}

	return c.initialized.IsTrue()
}

// Cache caches the results of a fabric scan locally.
func (c *localFabricCache) Cache(ctx context.Context, scan []*netdetect.FabricScan) error {
	if c == nil {
		return errors.New("nil localFabricCache")
	}

	if c.getDevAlias == nil {
		c.getDevAlias = netdetect.GetDeviceAlias
	}

	c.localNUMAFabric = NUMAFabricFromScan(ctx, c.log, scan, c.getDevAlias)

	c.initialized.SetTrue()
	return nil
}

// GetDevices fetches an appropriate fabric device from the cache.
func (c *localFabricCache) GetDevice(numaNode int, netDevClass uint32) (*FabricInterface, error) {
	if c == nil {
		return nil, errors.New("nil localFabricCache")
	}
	if !c.IsCached() {
		return nil, errors.New("not cached")
	}
	return c.localNUMAFabric.GetDevice(numaNode, netDevClass)
}
