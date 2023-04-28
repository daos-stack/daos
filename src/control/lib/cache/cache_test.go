//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cache

import (
	"context"
	"errors"
	"testing"
	"time"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/google/go-cmp/cmp"
)

type testData string

func (c testData) Copy() Data {
	return c
}

func TestCache_Item_Data(t *testing.T) {
	for name, tc := range map[string]struct {
		item      *Item
		expResult Data
	}{
		"nil item": {},
		"empty item": {
			item: &Item{},
		},
		"cached": {
			item: &Item{
				data: testData("my test string"),
			},
			expResult: testData("my test string"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.item.Data()

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}

func TestCache_Item_SetData(t *testing.T) {
	for name, tc := range map[string]struct {
		item *Item
		data Data
	}{
		"nil item": { // all we care about is this doesn't crash
			data: testData("something"),
		},
		"no data": {
			item: &Item{},
			data: testData("something"),
		},
		"override data": {
			item: &Item{
				data: testData("something"),
			},
			data: testData("something else"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.item.SetData(tc.data)

			if tc.item == nil {
				return
			}

			data := tc.item.Data()
			if diff := cmp.Diff(tc.data, data); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}

func TestCache_Item_Fetch(t *testing.T) {
	for name, tc := range map[string]struct {
		item    *Item
		expErr  error
		expData Data
	}{
		"nil item": {
			expErr: errors.New("nil"),
		},
		"nil fetch function": {
			item: &Item{},
		},
		"fetch function failed": {
			item: &Item{
				getFreshData: func(_ context.Context) (Data, error) {
					return nil, errors.New("mock getFreshData")
				},
			},
			expErr: errors.New("mock getFreshData"),
		},
		"success": {
			item: &Item{
				getFreshData: func(_ context.Context) (Data, error) {
					return testData("fresh content!"), nil
				},
			},
			expData: testData("fresh content!"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			err := tc.item.FetchData(context.Background())

			test.CmpErr(t, tc.expErr, err)

			if tc.item == nil {
				return
			}

			data := tc.item.Data()
			if diff := cmp.Diff(tc.expData, data); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}

func TestCache_Item_NeedsFetch(t *testing.T) {
	testFetchFn := func(_ context.Context) (Data, error) {
		return testData("something"), nil
	}

	for name, tc := range map[string]struct {
		item      *Item
		expResult bool
	}{
		"nil item": {},
		"no fetch function": {
			item: &Item{},
		},
		"never cached": {
			item: &Item{
				getFreshData: testFetchFn,
			},
			expResult: true,
		},
		"no refresh interval": {
			item: &Item{
				getFreshData: testFetchFn,
				lastCache:    time.Date(1998, time.July, 6, 12, 0, 0, 0, time.Local),
			},
		},
		"refresh interval hasn't passed": {
			item: &Item{
				getFreshData:    testFetchFn,
				lastCache:       time.Now(),
				refreshInterval: time.Minute,
			},
		},
		"refresh interval has passed": {
			item: &Item{
				getFreshData:    testFetchFn,
				lastCache:       time.Now().Add(-time.Minute),
				refreshInterval: time.Minute,
			},
			expResult: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, tc.item.NeedsFetch(), "")
		})
	}
}

func TestCache_ItemCache_Set(t *testing.T) {
	for name, tc := range map[string]struct {
		ic        *ItemCache
		key       string
		val       *Item
		expCached bool
	}{
		"nil cache": { // all we care about in this case is that it doesn't crash
			key: "key",
			val: NewItem(testData("value")),
		},
		"empty key": {
			ic:  &ItemCache{},
			val: NewItem(testData("value")),
		},
		"cached": {
			ic:        &ItemCache{},
			key:       "key",
			val:       NewItem(testData("value")),
			expCached: true,
		},
		"overwrite": {
			ic: &ItemCache{
				items: map[string]*Item{
					"key": NewItem(testData("old value")),
				},
			},
			key:       "key",
			val:       NewItem(testData("value")),
			expCached: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.ic.Set(tc.key, tc.val)

			if tc.ic == nil {
				return
			}

			item, ok := tc.ic.items[tc.key]

			if tc.expCached {
				if !ok {
					t.Fatalf("expected %q to be cached", tc.key)
				}

				if diff := cmp.Diff(tc.val.data, item.data); diff != "" {
					t.Fatalf("-want, +got:\n%s", diff)
				}
			} else {
				if !ok {
					return
				}

				if diff := cmp.Diff(tc.val.data, item.data); diff == "" {
					t.Fatalf("value was not supposed to be cached")
				}
			}
		})
	}
}

func TestCache_ItemCache_Get(t *testing.T) {
	for name, tc := range map[string]struct {
		ic        *ItemCache
		key       string
		expResult Data
		expErr    error
	}{
		"nil cache": {
			key:    "something",
			expErr: errors.New("nil"),
		},
		"empty key": {
			ic:     &ItemCache{},
			expErr: errors.New("invalid key"),
		},
		"no match": {
			ic: &ItemCache{
				items: map[string]*Item{
					"a": NewItem(testData("alpha")),
					"b": NewItem(testData("beta")),
					"c": NewItem(testData("gamma")),
				},
			},
			key:    "d",
			expErr: errors.New("not found"),
		},
		"success": {
			ic: &ItemCache{
				items: map[string]*Item{
					"a": NewItem(testData("alpha")),
					"b": NewItem(testData("beta")),
					"c": NewItem(testData("gamma")),
				},
			},
			key:       "c",
			expResult: testData("gamma"),
		},
		"fetch": {
			ic: &ItemCache{
				items: map[string]*Item{
					"a": NewFetchableItem(0, func(ctx context.Context) (Data, error) {
						return testData("alpha"), nil
					}),
				},
			},
			key:       "a",
			expResult: testData("alpha"),
		},
		"fetch failed": {
			ic: &ItemCache{
				items: map[string]*Item{
					"a": NewFetchableItem(0, func(ctx context.Context) (Data, error) {
						return nil, errors.New("mock fetch")
					}),
				},
			},
			key:    "a",
			expErr: errors.New("mock fetch"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := tc.ic.Get(context.Background(), tc.key)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("-want, +got:\n%s", diff)
			}
		})
	}
}

func TestCache_ItemCache_Refresh(t *testing.T) {
	for name, tc := range map[string]struct {
		ic      *ItemCache
		expErr  error
		expData map[string]Data
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"empty": {
			ic:      &ItemCache{},
			expData: map[string]Data{},
		},
		"non-fetchable item": {
			ic: &ItemCache{
				items: map[string]*Item{
					"one": NewItem(testData("item 1")),
				},
			},
			expData: map[string]Data{
				"one": testData("item 1"),
			},
		},
		"fetch fails": {
			ic: &ItemCache{
				items: map[string]*Item{
					"one": NewFetchableItem(0, func(ctx context.Context) (Data, error) {
						return nil, errors.New("mock fetch")
					}),
				},
			},
			expErr: errors.New("mock fetch"),
			expData: map[string]Data{
				"one": nil,
			},
		},
		"success": {
			ic: &ItemCache{
				items: map[string]*Item{
					"one": NewFetchableItem(0, func(ctx context.Context) (Data, error) {
						return testData("new data1"), nil
					}),
					"two": NewFetchableItem(0, func(ctx context.Context) (Data, error) {
						return testData("new data2"), nil
					}),
					"three": NewItem(testData("static data3")),
					"four": NewFetchableItem(0, func(ctx context.Context) (Data, error) {
						return testData("new data4"), nil
					}),
				},
			},
			expData: map[string]Data{
				"one":   testData("new data1"),
				"two":   testData("new data2"),
				"three": testData("static data3"),
				"four":  testData("new data4"),
			},
		},
		"ignores refresh interval": {
			ic: &ItemCache{
				items: map[string]*Item{
					"one": {
						lastCache:       time.Now(),
						refreshInterval: time.Hour,
						getFreshData: func(ctx context.Context) (Data, error) {
							return testData("new data1"), nil
						},
					},
				},
			},
			expData: map[string]Data{
				"one": testData("new data1"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx, cleanup := context.WithCancel(context.Background())
			t.Cleanup(cleanup)

			err := tc.ic.Refresh(ctx)

			test.CmpErr(t, tc.expErr, err)

			if tc.ic != nil {
				test.AssertEqual(t, len(tc.expData), len(tc.ic.items), "")

				for key, expData := range tc.expData {
					item, exists := tc.ic.items[key]
					if !exists {
						t.Fatalf("expected item %q not found", key)
					}

					if diff := cmp.Diff(expData, item.data); diff != "" {
						t.Fatalf("-want, +got:\n%s", diff)
					}
				}
			}
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
			key: "mykey",
		},
		"success": {
			ic: &ItemCache{
				items: map[string]*Item{
					"mykey": NewItem("something"),
				},
			},
			key:       "mykey",
			expResult: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, tc.ic.Has(tc.key), "")
		})
	}
}

func TestCache_ItemCache_Delete(t *testing.T) {
	for name, tc := range map[string]struct {
		ic      *ItemCache
		key     string
		expData map[string]Data
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
				items: map[string]*Item{
					"one":   NewItem("1"),
					"two":   NewItem("2"),
					"three": NewItem("3"),
				},
			},
			key: "four",
			expData: map[string]Data{
				"one":   "1",
				"two":   "2",
				"three": "3",
			},
		},
		"success": {
			ic: &ItemCache{
				items: map[string]*Item{
					"one":   NewItem("1"),
					"two":   NewItem("2"),
					"three": NewItem("3"),
				},
			},
			key: "two",
			expData: map[string]Data{
				"one":   "1",
				"three": "3",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			tc.ic.Delete(tc.key)

			if tc.ic != nil {
				test.AssertEqual(t, len(tc.expData), len(tc.ic.items), "")

				for key, expData := range tc.expData {
					item, exists := tc.ic.items[key]
					if !exists {
						t.Fatalf("expected item %q not found", key)
					}

					if diff := cmp.Diff(expData, item.data); diff != "" {
						t.Fatalf("-want, +got:\n%s", diff)
					}
				}
			}
		})
	}
}
