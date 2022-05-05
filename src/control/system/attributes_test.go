//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
)

type testAttrDb struct {
	attrs map[string]string
}

func (db *testAttrDb) SetSystemAttrs(attrs map[string]string) error {
	for k := range attrs {
		if attrs[k] == "" {
			delete(db.attrs, k)
			continue
		}
		db.attrs[k] = attrs[k]
	}
	return nil
}

func (db *testAttrDb) GetSystemAttrs(keys []string) (map[string]string, error) {
	out := make(map[string]string)
	if len(keys) == 0 {
		for k, v := range db.attrs {
			out[k] = v
		}
		return out, nil
	}

	for _, k := range keys {
		if v, ok := db.attrs[k]; ok {
			out[k] = v
			continue
		}
		return nil, ErrSystemAttrNotFound(k)
	}
	return out, nil
}

func newAttrDb(attrs map[string]string) *testAttrDb {
	if attrs == nil {
		attrs = make(map[string]string)
	}

	return &testAttrDb{
		attrs: attrs,
	}
}

func TestSystem_SetAttributes(t *testing.T) {
	for name, tc := range map[string]struct {
		userAttrs map[string]string
		expErr    error
	}{
		"reserved key": {
			userAttrs: map[string]string{
				mgmtPropPrefix + "foo": "bar",
			},
			expErr: errors.New("reserved key"),
		},
		"success": {
			userAttrs: map[string]string{
				"foo": "bar",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotErr := SetAttributes(newAttrDb(nil), tc.userAttrs)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestSystem_GetAttributes(t *testing.T) {
	reservedKey := mgmtPropPrefix + "system-stuff"
	attrDb := newAttrDb(map[string]string{
		"foo":       "bar",
		"baz":       "qux",
		reservedKey: "buzz off",
	})
	for name, tc := range map[string]struct {
		attrKeys []string
		expAttrs map[string]string
		expErr   error
	}{
		"query for reserved attr is filtered": {
			attrKeys: []string{reservedKey},
			expAttrs: map[string]string{},
		},
		"query for all filters reserved": {
			expAttrs: map[string]string{
				"foo": "bar",
				"baz": "qux",
			},
		},
		"query for single attr": {
			attrKeys: []string{"foo"},
			expAttrs: map[string]string{
				"foo": "bar",
			},
		},
		"unknown attr": {
			attrKeys: []string{"bananas"},
			expErr:   ErrSystemAttrNotFound("bananas"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotAttrs, gotErr := GetAttributes(attrDb, tc.attrKeys)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expAttrs, gotAttrs); diff != "" {
				t.Fatalf("unexpected attrerties (-want +got):\n%s", diff)
			}
		})
	}
}
