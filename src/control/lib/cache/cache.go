//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cache

import (
	"context"
	"fmt"
	"sync"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

type Item interface {
	Lock()
	Unlock()
	Key() string
	Refresh(ctx context.Context, force bool) error
	RefreshInterval() time.Duration
}

// ItemCache is a mechanism for caching Items to keys.
type ItemCache struct {
	log             logging.Logger
	mutex           sync.RWMutex
	items           map[string]Item
	newItemChan     chan Item
	refreshItemChan chan Item
}

func NewItemCache(ctx context.Context, log logging.Logger) *ItemCache {
	c := &ItemCache{
		log:             log,
		items:           make(map[string]Item),
		newItemChan:     make(chan Item),
		refreshItemChan: make(chan Item),
	}

	go c.refreshLoop(ctx)
	return c
}

func itemRefreshLoop(ctx context.Context, refreshChan chan Item, item Item) {
	if item.RefreshInterval() == 0 {
		return
	}

	ticker := time.NewTicker(item.RefreshInterval())
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			refreshChan <- item
		}
	}
}

func (ic *ItemCache) refreshLoop(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return
		case item := <-ic.newItemChan:
			go itemRefreshLoop(ctx, ic.refreshItemChan, item)
		case item := <-ic.refreshItemChan:
			ic.log.Debugf("refreshing %q after %s", item.Key(), item.RefreshInterval())
			if err := item.Refresh(ctx, true); err != nil {
				ic.log.Errorf("refreshing %q: %v", item.Key(), err)
			}
		}
	}
}

// Set caches an item under a given key.
func (ic *ItemCache) Set(ctx context.Context, item Item) error {
	if ic == nil || common.InterfaceIsNil(item) || item.Key() == "" {
		return errors.New("invalid arguments")
	}

	ic.mutex.Lock()
	defer ic.mutex.Unlock()

	return ic.set(ctx, item)
}

func (ic *ItemCache) set(ctx context.Context, item Item) error {
	ic.items[item.Key()] = item

	select {
	case <-ctx.Done():
		return ctx.Err()
	case ic.newItemChan <- item:
		return nil
	}
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

type ItemCreateFunc func() (Item, error)

type errKeyNotFound struct {
	key string
}

func (e *errKeyNotFound) Error() string {
	return fmt.Sprintf("key %q not found", e.key)
}

func isKeyNotFound(err error) bool {
	_, ok := err.(*errKeyNotFound)
	return ok
}

func noopRelease() {}

// GetOrCreate returns an item from the cache if it exists, otherwise it creates
// the item using the given function and caches it. The item must be released
// by the caller when it is safe to be modified.
func (ic *ItemCache) GetOrCreate(ctx context.Context, key string, missFn ItemCreateFunc) (Item, func(), error) {
	if ic == nil {
		return nil, noopRelease, errors.New("ItemCache is nil")
	}

	if key == "" {
		return nil, noopRelease, errors.Errorf("empty string is an invalid key")
	}

	ic.mutex.Lock()
	defer ic.mutex.Unlock()

	item, err := ic.get(key)
	if err != nil {
		if !isKeyNotFound(err) {
			return nil, noopRelease, err
		}
		item, err = missFn()
		if err != nil {
			return nil, noopRelease, errors.Wrapf(err, "create item for %q", key)
		}
		if err := ic.set(ctx, item); err != nil {
			return nil, noopRelease, errors.Wrapf(err, "set item for %q", key)
		}
	}

	if err := item.Refresh(ctx, false); err != nil {
		return nil, noopRelease, errors.Wrapf(err, "fetch data for %q", key)
	}
	item.Lock()

	return item, item.Unlock, nil
}

// Get returns an item from the cache if it exists, otherwise it returns an
// error. The item must be released by the caller when it is safe to be modified.
func (ic *ItemCache) Get(ctx context.Context, key string) (Item, func(), error) {
	if ic == nil {
		return nil, noopRelease, errors.New("nil ItemCache")
	}

	ic.mutex.RLock()
	defer ic.mutex.RUnlock()

	item, err := ic.get(key)
	if err != nil {
		return nil, noopRelease, err
	}

	if err := item.Refresh(ctx, false); err != nil {
		return nil, noopRelease, errors.Wrapf(err, "fetch data for %q", key)
	}
	item.Lock()

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
func (ic *ItemCache) Refresh(ctx context.Context) error {
	if ic == nil {
		return errors.New("nil ItemCache")
	}

	ic.mutex.Lock()
	defer ic.mutex.Unlock()

	for _, item := range ic.items {
		item.Refresh(ctx, true)
	}
	return nil
}
