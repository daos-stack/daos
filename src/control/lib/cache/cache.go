//
// (C) Copyright 2023 Intel Corporation.
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

type Item interface {
	Lock()
	Unlock()
	Key() string
	Refresh(ctx context.Context) error
	NeedsRefresh() bool
}

// ItemCache is a mechanism for caching Items to keys.
type ItemCache struct {
	log   logging.Logger
	mutex sync.RWMutex
	items map[string]Item
}

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

func noopRelease() {}

// GetOrCreate returns an item from the cache if it exists, otherwise it creates
// the item using the given function and caches it. The item must be released
// by the caller when it is safe to be modified.
func (ic *ItemCache) GetOrCreate(ctx context.Context, key string, missFn ItemCreateFunc) (Item, func(), error) {
	if ic == nil {
		return nil, noopRelease, errors.New("nil ItemCache")
	}

	if key == "" {
		return nil, noopRelease, errors.Errorf("empty string is an invalid key")
	}

	if missFn == nil {
		return nil, noopRelease, errors.Errorf("item create function is required")
	}

	ic.mutex.Lock()
	defer ic.mutex.Unlock()

	item, err := ic.get(key)
	if err != nil {
		ic.log.Debugf("failed to get item for key %q: %s", key, err.Error())
		item, err = missFn()
		if err != nil {
			return nil, noopRelease, errors.Wrapf(err, "create item for %q", key)
		}
		ic.log.Debugf("created item for key %q", key)
		ic.set(item)
	}

	item.Lock()
	if item.NeedsRefresh() {
		if err := item.Refresh(ctx); err != nil {
			item.Unlock()
			return nil, noopRelease, errors.Wrapf(err, "fetch data for %q", key)
		}
		ic.log.Debugf("refreshed item %q", key)
	}

	return item, item.Unlock, nil
}

// Get returns an item from the cache if it exists, otherwise it returns an
// error. The item must be released by the caller when it is safe to be modified.
func (ic *ItemCache) Get(ctx context.Context, key string) (Item, func(), error) {
	if ic == nil {
		return nil, noopRelease, errors.New("nil ItemCache")
	}

	if key == "" {
		return nil, noopRelease, errors.Errorf("empty string is an invalid key")
	}

	ic.mutex.Lock()
	defer ic.mutex.Unlock()

	item, err := ic.get(key)
	if err != nil {
		return nil, noopRelease, err
	}

	item.Lock()
	if item.NeedsRefresh() {
		if err := item.Refresh(ctx); err != nil {
			item.Unlock()
			return nil, noopRelease, errors.Wrapf(err, "fetch data for %q", key)
		}
		ic.log.Debugf("refreshed item %q", key)
	}

	return item, item.Unlock, nil
}

func (ic *ItemCache) get(key string) (Item, error) {
	val, ok := ic.items[key]
	if ok {
		return val, nil
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

	item.Lock()
	defer item.Unlock()
	if err := item.Refresh(ctx); err != nil {
		return errors.Wrapf(err, "failed to refresh cached item %q", item.Key())
	}

	return nil
}
