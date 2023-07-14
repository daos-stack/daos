//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cache

import (
	"context"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
)

func TestCache_NewItemCache(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ic := NewItemCache(log)

	if ic == nil {
		t.Fatal("failed to create ItemCache")
	}

	if ic.items == nil {
		t.Fatal("didn't set up item map")
	}

	if ic.log != log {
		t.Fatal("didn't preserve logger")
	}
}

type mockItem struct {
	ItemKey            string
	ID                 string
	RefreshErr         error
	NeedsRefreshResult bool
}

func (m *mockItem) Lock() {}

func (m *mockItem) Unlock() {}

func (m *mockItem) Key() string {
	return m.ItemKey
}

func (m *mockItem) Refresh(ctx context.Context) error {
	return m.RefreshErr
}

func (m *mockItem) NeedsRefresh() bool {
	return m.NeedsRefreshResult
}

func testMockItem(id ...string) *mockItem {
	mock := &mockItem{ItemKey: "mock"}
	if len(id) > 0 {
		mock.ID = id[0]
	}
	return mock
}

func TestCache_ItemCache_Set(t *testing.T) {
	for name, tc := range map[string]struct {
		nilCache      bool
		alreadyCached map[string]Item
		val           *mockItem
		expErr        error
		expCached     bool
	}{
		"nil cache": {
			nilCache: true,
			val:      testMockItem(),
			expErr:   errors.New("nil"),
		},
		"nil item": {
			expErr: errors.New("invalid item"),
		},
		"empty key": {
			val:    &mockItem{},
			expErr: errors.New("invalid item"),
		},
		"cached": {
			val:       testMockItem(),
			expCached: true,
		},
		"overwrite": {
			alreadyCached: map[string]Item{
				"mock": testMockItem("old"),
			},
			val:       testMockItem("new"),
			expCached: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var ic *ItemCache
			if !tc.nilCache {
				ic = NewItemCache(log)
			}
			err := ic.Set(tc.val)

			test.CmpErr(t, tc.expErr, err)

			if ic == nil || tc.val == nil {
				return
			}

			item, ok := ic.items[tc.val.ItemKey]
			if tc.expCached {
				if !ok {
					t.Fatalf("expected %q to be cached", tc.val.ItemKey)
				}

				if diff := cmp.Diff(tc.val, item); diff != "" {
					t.Fatalf("-want, +got:\n%s", diff)
				}
			} else {
				if !ok {
					return
				}

				if diff := cmp.Diff(tc.val, item); diff == "" {
					t.Fatalf("value was not supposed to be cached")
				}
			}
		})
	}
}

func TestCache_ItemCache_GetOrCreate(t *testing.T) {
	defaultCreate := func() (Item, error) {
		return testMockItem("default"), nil
	}

	for name, tc := range map[string]struct {
		nilCache      bool
		key           string
		createFunc    ItemCreateFunc
		alreadyCached map[string]Item
		expResult     Item
		expErr        error
	}{
		"nil": {
			nilCache:   true,
			key:        "mock",
			createFunc: defaultCreate,
			expErr:     errors.New("nil"),
		},
		"empty key": {
			key:        "",
			createFunc: defaultCreate,
			expErr:     errors.New("invalid key"),
		},
		"nil create func": {
			key:    "mock",
			expErr: errors.New("create function is required"),
		},
		"cached": {
			key: "mock",
			createFunc: func() (Item, error) {
				return nil, errors.New("shouldn't call create")
			},
			alreadyCached: map[string]Item{
				"mock": testMockItem("cached"),
			},
			expResult: testMockItem("cached"),
		},
		"create func failed": {
			key: "mock",
			createFunc: func() (Item, error) {
				return nil, errors.New("mock create")
			},
			expErr: errors.New("mock create"),
		},
		"created": {
			key:        "mock",
			createFunc: defaultCreate,
			expResult:  testMockItem("default"),
		},
		"refresh failed": {
			key: "mock",
			createFunc: func() (Item, error) {
				mi := testMockItem("default")
				mi.NeedsRefreshResult = true
				mi.RefreshErr = errors.New("mock item refresh")
				return mi, nil
			},
			expErr: errors.New("mock item refresh"),
		},
		"refresh success": {
			key: "mock",
			createFunc: func() (Item, error) {
				mi := testMockItem("default")
				mi.NeedsRefreshResult = true
				return mi, nil
			},
			expResult: &mockItem{
				ItemKey:            "mock",
				ID:                 "default",
				NeedsRefreshResult: true,
			},
		},
		"no refresh needed": {
			key: "mock",
			createFunc: func() (Item, error) {
				return &mockItem{
					ItemKey:    "mock",
					ID:         "default",
					RefreshErr: errors.New("should not call refresh"),
				}, nil
			},
			expResult: testMockItem("default"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var ic *ItemCache
			if !tc.nilCache {
				ic = NewItemCache(log)
				if tc.alreadyCached != nil {
					ic.items = tc.alreadyCached
				}
			}

			result, cleanup, err := ic.GetOrCreate(test.Context(t), tc.key, tc.createFunc)

			if cleanup == nil {
				t.Fatal("expected non-nil cleanup function")
			}
			defer cleanup()

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result, cmpopts.IgnoreFields(mockItem{}, "RefreshErr")); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}

func TestCache_ItemCache_Get(t *testing.T) {
	for name, tc := range map[string]struct {
		nilCache      bool
		key           string
		alreadyCached map[string]Item
		expResult     Item
		expErr        error
	}{
		"nil": {
			nilCache: true,
			key:      "mock",
			expErr:   errors.New("nil"),
		},
		"empty key": {
			key:    "",
			expErr: errors.New("invalid key"),
		},
		"missing": {
			key:    "mock",
			expErr: &errKeyNotFound{key: "mock"},
		},
		"refresh failed": {
			key: "mock",
			alreadyCached: map[string]Item{
				"mock": &mockItem{
					ItemKey:            "mock",
					NeedsRefreshResult: true,
					RefreshErr:         errors.New("mock item refresh"),
				},
			},
			expErr: errors.New("mock item refresh"),
		},
		"refresh success": {
			key: "mock",
			alreadyCached: map[string]Item{
				"mock": &mockItem{
					ItemKey:            "mock",
					NeedsRefreshResult: true,
				},
			},
			expResult: &mockItem{
				ItemKey:            "mock",
				NeedsRefreshResult: true,
			},
		},
		"no refresh needed": {
			key: "mock",
			alreadyCached: map[string]Item{
				"mock": &mockItem{
					ItemKey:    "mock",
					RefreshErr: errors.New("should not call refresh"),
				},
			},
			expResult: testMockItem(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var ic *ItemCache
			if !tc.nilCache {
				ic = NewItemCache(log)
				if tc.alreadyCached != nil {
					ic.items = tc.alreadyCached
				}
			}

			result, cleanup, err := ic.Get(test.Context(t), tc.key)

			if cleanup == nil {
				t.Fatal("expected non-nil cleanup function")
			}
			defer cleanup()

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result, cmpopts.IgnoreFields(mockItem{}, "RefreshErr")); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}

func TestCache_ItemCache_Refresh(t *testing.T) {
	for name, tc := range map[string]struct {
		nilCache bool
		keys     []string
		cache    map[string]Item
		expErr   error
	}{
		"nil": {
			nilCache: true,
			expErr:   errors.New("nil"),
		},
		"no items": {},
		"refresh fails": {
			cache: map[string]Item{
				"mock": &mockItem{
					ItemKey:    "mock",
					RefreshErr: errors.New("mock refresh"),
				},
			},
			expErr: errors.New("mock refresh"),
		},
		"success": {
			cache: map[string]Item{
				"mock": testMockItem(),
			},
		},
		"specific key": {
			keys: []string{"one"},
			cache: map[string]Item{
				"one": &mockItem{
					ItemKey: "one",
				},
				"two": &mockItem{
					ItemKey:    "two",
					RefreshErr: errors.New("shouldn't call two"),
				},
				"three": &mockItem{
					ItemKey:    "three",
					RefreshErr: errors.New("shouldn't call three"),
				},
			},
		},
		"multiple keys": {
			keys: []string{"one"},
			cache: map[string]Item{
				"one": &mockItem{
					ItemKey: "one",
				},
				"two": &mockItem{
					ItemKey:    "two",
					RefreshErr: errors.New("shouldn't call two"),
				},
				"three": &mockItem{
					ItemKey: "three",
				},
			},
		},
		"invalid key": {
			keys: []string{"fake"},
			cache: map[string]Item{
				"one": &mockItem{
					ItemKey:    "one",
					RefreshErr: errors.New("shouldn't call one"),
				},
				"two": &mockItem{
					ItemKey:    "two",
					RefreshErr: errors.New("shouldn't call two"),
				},
				"three": &mockItem{
					ItemKey:    "three",
					RefreshErr: errors.New("shouldn't call three"),
				},
			},
			expErr: &errKeyNotFound{"fake"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var ic *ItemCache
			if !tc.nilCache {
				ic = NewItemCache(log)
				if tc.cache != nil {
					ic.items = tc.cache
				}
			}

			err := ic.Refresh(test.Context(t), tc.keys...)

			test.CmpErr(t, tc.expErr, err)
		})
	}
}

func TestCache_ItemCache_Has(t *testing.T) {
	for name, tc := range map[string]struct {
		ic        *ItemCache
		key       string
		expResult bool
	}{
		"nil": {
			key: "something",
		},
		"empty": {
			ic:  &ItemCache{},
			key: "mock",
		},
		"success": {
			ic: &ItemCache{
				items: map[string]Item{
					"mock": testMockItem(),
				},
			},
			key:       "mock",
			expResult: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, tc.ic.Has(tc.key), "")
		})
	}
}

func TestCache_ItemCache_Keys(t *testing.T) {
	for name, tc := range map[string]struct {
		nilCache bool
		cached   map[string]Item
		expKeys  []string
	}{
		"nil": {
			nilCache: true,
		},
		"empty": {
			expKeys: []string{},
		},
		"one": {
			cached: map[string]Item{
				"something": &mockItem{},
			},
			expKeys: []string{"something"},
		},
		"multi": {
			cached: map[string]Item{
				"one":  &mockItem{},
				"ring": &mockItem{},
				"to":   &mockItem{},
				"rule": &mockItem{},
				"them": &mockItem{},
				"all":  &mockItem{},
			},
			expKeys: []string{"all", "one", "ring", "rule", "them", "to"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var ic *ItemCache
			if !tc.nilCache {
				ic = NewItemCache(log)
				if tc.cached != nil {
					ic.items = tc.cached
				}
			}

			keys := ic.Keys()

			if diff := cmp.Diff(tc.expKeys, keys); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}

func TestCache_ItemCache_Delete(t *testing.T) {
	for name, tc := range map[string]struct {
		ic       *ItemCache
		key      string
		expCache map[string]Item
	}{
		"nil": {
			key: "dontcare",
		},
		"empty": {
			ic:  &ItemCache{},
			key: "dontcare",
		},
		"key not found": {
			ic: &ItemCache{
				items: map[string]Item{
					"one":   testMockItem("1"),
					"two":   testMockItem("2"),
					"three": testMockItem("3"),
				},
			},
			key: "four",
			expCache: map[string]Item{
				"one":   testMockItem("1"),
				"two":   testMockItem("2"),
				"three": testMockItem("3"),
			},
		},
		"success": {
			ic: &ItemCache{
				items: map[string]Item{
					"one":   testMockItem("1"),
					"two":   testMockItem("2"),
					"three": testMockItem("3"),
				},
			},
			key: "two",
			expCache: map[string]Item{
				"one":   testMockItem("1"),
				"three": testMockItem("3"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.ic.Delete(tc.key)

			if tc.ic != nil {
				if diff := cmp.Diff(tc.expCache, tc.ic.items); diff != "" {
					t.Fatalf("-want, +got:\n%s", diff)
				}
			}
		})
	}
}
