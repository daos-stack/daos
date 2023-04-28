//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cache

import (
	"context"
	"sync"
	"time"

	"github.com/pkg/errors"
)

// Data is an interface for data that can be stored.
type Data interface{}

// NewItem creates a new Item without data-fetching capabilities.
func NewItem(data Data) *Item {
	newItem := &Item{}
	newItem.SetData(data)
	return newItem
}

// NewFetchableItem creates a new Item with with a data source.
func NewFetchableItem(refreshInterval time.Duration, fetchFn func(context.Context) (Data, error)) *Item {
	return &Item{
		refreshInterval: refreshInterval,
		getFreshData:    fetchFn,
	}
}

// Item is a wrapper for any type of data to be cached.
type Item struct {
	mutex           sync.RWMutex
	lastCache       time.Time
	refreshInterval time.Duration
	getFreshData    func(context.Context) (Data, error)
	data            Data
}

// Data returns a copy of the item's data.
func (item *Item) Data() Data {
	if item == nil {
		return nil
	}

	item.mutex.RLock()
	defer item.mutex.RUnlock()
	return item.data
}

// SetData sets the Item's data to a passed in type.
func (item *Item) SetData(data Data) {
	if item == nil {
		return
	}

	item.mutex.Lock()
	defer item.mutex.Unlock()

	item.cacheData(data)
}

func (item *Item) cacheData(data Data) {
	item.data = data
	item.lastCache = time.Now()
}

// FetchData fetches and updates the data.
func (item *Item) FetchData(ctx context.Context) error {
	if item == nil {
		return errors.New("Item is nil")
	}

	if !item.canFetch() {
		// Nothing to do
		return nil
	}

	item.mutex.Lock()
	defer item.mutex.Unlock()

	newData, err := item.getFreshData(ctx)
	if err != nil {
		return errors.Wrap(err, "fetching data for cache")
	}

	item.cacheData(newData)
	return nil
}

func (item *Item) canFetch() bool {
	return item.getFreshData != nil
}

// NeedsFetch checks whether the cached data needs to be fetched and updated.
func (item *Item) NeedsFetch() bool {
	if item == nil {
		return false
	}

	if !item.canFetch() {
		return false
	}

	item.mutex.RLock()
	defer item.mutex.RUnlock()

	if item.lastCache.IsZero() {
		return true
	}

	if item.refreshInterval == 0 {
		return false
	}

	return time.Since(item.lastCache) >= item.refreshInterval
}

// ItemCache is a mechanism for caching Items to keys.
type ItemCache struct {
	mutex sync.RWMutex
	items map[string]*Item
}

// Set caches an item under a given key.
func (ic *ItemCache) Set(key string, item *Item) {
	if ic == nil || key == "" {
		return
	}

	ic.mutex.Lock()
	defer ic.mutex.Unlock()

	ic.init()

	ic.items[key] = item
}

func (ic *ItemCache) init() {
	if ic.items == nil {
		ic.items = make(map[string]*Item)
	}
}

// Delete fully deletes an Item from the cache.
func (ic *ItemCache) Delete(key string) {
	if ic == nil {
		return
	}

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

// Get retrieves a copy of the data from the cache for a given key. If the data is unset or stale,
// it fetches the data first.
func (ic *ItemCache) Get(ctx context.Context, key string) (Data, error) {
	if ic == nil {
		return nil, errors.New("ItemCache is nil")
	}

	if key == "" {
		return nil, errors.Errorf("empty string is an invalid key")
	}

	ic.mutex.Lock()
	defer ic.mutex.Unlock()

	ic.init()

	item, err := ic.get(key)
	if err != nil {
		return nil, err
	}

	if item.NeedsFetch() {
		if err := item.FetchData(ctx); err != nil {
			return nil, errors.Wrapf(err, "fetch data for %q", key)
		}
	}
	return item.Data(), nil
}

func (ic *ItemCache) get(key string) (*Item, error) {
	val, ok := ic.items[key]
	if ok {
		return val, nil
	}
	return nil, errors.Errorf("key %q not found", key)
}

// Refresh forces a re-fetch of all items in the cache.
func (ic *ItemCache) Refresh(ctx context.Context) error {
	if ic == nil {
		return errors.New("nil ItemCache")
	}

	ic.mutex.Lock()
	defer ic.mutex.Unlock()

	ic.init()

	for key, item := range ic.items {
		if err := item.FetchData(ctx); err != nil {
			return errors.Wrapf(err, "refreshing item %q", key)
		}
	}
	return nil
}
