//
// (C) Copyright 2023-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cache

import (
	"context"
	"fmt"
	"sort"
	"sync"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

type (
	// Item defines an interface for a cached item.
	Item interface {
		sync.Locker
		Key() string
	}

	// ExpirableItem is an Item that defines its own expiration criteria.
	ExpirableItem interface {
		Item
		IsExpired() bool
	}

	// RefreshableItem is an Item that defines its own refresh criteria and method.
	RefreshableItem interface {
		Item
		Refresh(ctx context.Context) (func(), error)
		RefreshIfNeeded(ctx context.Context) (func(), bool, error)
	}

	// ItemCache is a mechanism for caching Items to keys.
	ItemCache struct {
		log   logging.Logger
		mutex sync.RWMutex
		items map[string]Item
	}
)

// NewItemCache creates a new ItemCache.
func NewItemCache(log logging.Logger) *ItemCache {
	c := &ItemCache{
		log:   log,
		items: make(map[string]Item),
	}
	return c
}

// Set caches an item under a given key.
func (ic *ItemCache) Set(item Item) error {
	if ic == nil {
		return errors.New("ItemCache is nil")
	}

	if common.InterfaceIsNil(item) || item.Key() == "" {
		return errors.New("invalid item")
	}

	ic.mutex.Lock()
	defer ic.mutex.Unlock()

	ic.set(item)
	return nil
}

func (ic *ItemCache) set(item Item) {
	ic.items[item.Key()] = item
}

// Delete fully deletes an Item from the cache.
func (ic *ItemCache) Delete(key string) {
	if ic == nil {
		return
	}

	ic.mutex.Lock()
	defer ic.mutex.Unlock()

	delete(ic.items, key)
}

// Has checks whether any item is cached under the given key.
func (ic *ItemCache) Has(key string) bool {
	if ic == nil {
		return false
	}

	ic.mutex.RLock()
	defer ic.mutex.RUnlock()

	_, found := ic.items[key]
	return found
}

// Keys returns a sorted list of all keys in the cache.
func (ic *ItemCache) Keys() []string {
	if ic == nil {
		return nil
	}

	ic.mutex.RLock()
	defer ic.mutex.RUnlock()

	return ic.keys()
}

func (ic *ItemCache) keys() []string {
	keys := []string{}
	for k := range ic.items {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

type ItemCreateFunc func() (Item, error)

type errKeyNotFound struct {
	key string
}

func (e *errKeyNotFound) Error() string {
	return fmt.Sprintf("key %q not found", e.key)
}

// NoopRelease is a no-op function that does nothing, but can
// be safely returned in lieu of a real lock release function.
func NoopRelease() {}

// GetOrCreate returns an item from the cache if it exists, otherwise it creates
// the item using the given function and caches it. The item must be released
// by the caller when it is safe to be modified.
func (ic *ItemCache) GetOrCreate(ctx context.Context, key string, missFn ItemCreateFunc) (Item, func(), error) {
	if ic == nil {
		return nil, NoopRelease, errors.New("nil ItemCache")
	}

	if key == "" {
		return nil, NoopRelease, errors.Errorf("empty string is an invalid key")
	}

	if missFn == nil {
		return nil, NoopRelease, errors.Errorf("item create function is required")
	}

	ic.mutex.Lock()
	defer ic.mutex.Unlock()

	item, err := ic.get(key)
	if err != nil {
		ic.log.Debugf("failed to get item for key %q: %s", key, err.Error())
		item, err = missFn()
		if err != nil {
			return nil, NoopRelease, errors.Wrapf(err, "create item for %q", key)
		}
		ic.log.Debugf("created item for key %q", key)
		ic.set(item)
	}

	var release func()
	if ri, ok := item.(RefreshableItem); ok {
		var refreshed bool
		release, refreshed, err = ri.RefreshIfNeeded(ctx)
		if err != nil {
			return nil, NoopRelease, errors.Wrapf(err, "fetch data for %q", key)
		}
		if refreshed {
			ic.log.Debugf("refreshed item %q", key)
		}
	} else {
		item.Lock()
		release = item.Unlock
	}

	return item, release, nil
}

// Get returns an item from the cache if it exists, otherwise it returns an
// error. The item must be released by the caller when it is safe to be modified.
func (ic *ItemCache) Get(ctx context.Context, key string) (Item, func(), error) {
	if ic == nil {
		return nil, NoopRelease, errors.New("nil ItemCache")
	}

	if key == "" {
		return nil, NoopRelease, errors.Errorf("empty string is an invalid key")
	}

	ic.mutex.Lock()
	defer ic.mutex.Unlock()

	item, err := ic.get(key)
	if err != nil {
		return nil, NoopRelease, err
	}

	var release func()
	if ri, ok := item.(RefreshableItem); ok {
		var refreshed bool
		release, refreshed, err = ri.RefreshIfNeeded(ctx)
		if err != nil {
			return nil, NoopRelease, errors.Wrapf(err, "fetch data for %q", key)
		}
		if refreshed {
			ic.log.Debugf("refreshed item %q", key)
		}
	} else {
		item.Lock()
		release = item.Unlock
	}

	return item, release, nil
}

func (ic *ItemCache) get(key string) (Item, error) {
	item, ok := ic.items[key]
	if ok {
		if ei, ok := item.(ExpirableItem); ok && ei.IsExpired() {
			delete(ic.items, key)
		} else {
			return item, nil
		}
	}
	return nil, &errKeyNotFound{key: key}
}

// Refresh forces a re-fetch of all items in the cache.
func (ic *ItemCache) Refresh(ctx context.Context, keys ...string) error {
	if ic == nil {
		return errors.New("nil ItemCache")
	}

	ic.mutex.Lock()
	defer ic.mutex.Unlock()

	if len(keys) == 0 {
		keys = ic.keys()
	}

	for _, key := range keys {
		if err := ic.refreshItem(ctx, key); err != nil {
			return err
		}
	}
	return nil
}

func (ic *ItemCache) refreshItem(ctx context.Context, key string) error {
	item, err := ic.get(key)
	if err != nil {
		return err
	}

	if ri, ok := item.(RefreshableItem); ok {
		release, err := ri.Refresh(ctx)
		if err != nil {
			return errors.Wrapf(err, "failed to refresh cached item %q", item.Key())
		}
		release()
	}

	return nil
}
