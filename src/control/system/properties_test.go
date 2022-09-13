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

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

func TestSystem_MgmtProperties(t *testing.T) {
	attrDb := newAttrDb(nil)

	if err := SetMgmtProperty(attrDb, "foo", "bar"); err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(map[string]string{mgmtPropPrefix + "foo": "bar"}, attrDb.attrs); diff != "" {
		t.Fatalf("unexpected attributes (-want +got):\n%s", diff)
	}

	gotVal, err := GetMgmtProperty(attrDb, "foo")
	if err != nil {
		t.Fatal(err)
	}
	if gotVal != "bar" {
		t.Fatalf("unexpected value: want %q, got %q", "bar", gotVal)
	}

	if err := DelMgmtProperty(attrDb, "foo"); err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(map[string]string{}, attrDb.attrs); diff != "" {
		t.Fatalf("unexpected attributes (-want +got):\n%s", diff)
	}
}

func genTestProps(t *testing.T, min int) (daos.SystemPropertyMap, []*daos.SystemProperty) {
	t.Helper()

	sysProps := daos.SystemProperties()
	if len(sysProps) < min {
		t.Skip("not enough user-visible properties")
	}
	propList := make([]*daos.SystemProperty, 0, len(sysProps))
	for _, prop := range sysProps {
		prop := prop
		propList = append(propList, &prop)
	}

	for i := min; i < len(propList); i++ {
		delete(sysProps, propList[i].Key)
	}

	return sysProps, propList
}

func TestSystem_SetUserProperties(t *testing.T) {
	attrDb := newAttrDb(nil)
	sysProps, propList := genTestProps(t, 2)

	// This is a little weird, but the aim here is to test the logic
	// in SetUserProperties, not to verify the specifics of the defined
	// system properties. We have to work with existing property keys
	// due to the way the key resolution works.
	settableProp := propList[0]
	settableProp.Value = daos.NewStringPropVal("test", "test-set")
	sysProps[settableProp.Key] = *settableProp
	unsettableProp := propList[1]
	unsettableProp.Value = daos.NewCompPropVal(func() string {
		return "test"
	})
	sysProps[unsettableProp.Key] = *unsettableProp
	unknownProp := &daos.SystemProperty{Key: 0}

	for name, tc := range map[string]struct {
		toSet  map[string]string
		expErr error
	}{
		"settable": {
			toSet: map[string]string{
				settableProp.Key.String(): "test-set",
			},
		},
		"unsettable": {
			toSet: map[string]string{
				unsettableProp.Key.String(): "test-set",
			},
			expErr: errors.New("computed"),
		},
		"unknown prop key": {
			toSet: map[string]string{
				unknownProp.Key.String(): "test-set",
			},
			expErr: errors.New("unknown property"),
		},
		"invalid prop value": {
			toSet: map[string]string{
				settableProp.Key.String(): "invalid",
			},
			expErr: errors.New("invalid value"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotErr := SetUserProperties(attrDb, sysProps, tc.toSet)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestSystem_GetUserProperties(t *testing.T) {
	attrDb := newAttrDb(map[string]string{
		mgmtPropPrefix + "foo": "bar", // mgmt prop (not user-visible)
		"foo":                  "bar", // user attr (not included in props)
	})
	sysProps, propList := genTestProps(t, 2)

	// This is a little weird, but the aim here is to test the logic
	// in GetUserProperties, not to verify the specifics of the defined
	// system properties. We have to work with existing property keys
	// due to the way the key resolution works.
	userProp := propList[0]
	userProp.Value = daos.NewStringPropVal("default")
	sysProps[userProp.Key] = *userProp
	compProp := propList[1]
	compProp.Value = daos.NewCompPropVal(func() string {
		return "computed"
	})
	sysProps[compProp.Key] = *compProp

	for name, tc := range map[string]struct {
		db       *testAttrDb
		toGet    []string
		expProps map[string]string
		expErr   error
	}{
		"unknown prop": {
			toGet:  []string{"unknown"},
			expErr: errors.New("unknown property"),
		},
		"all (defaults)": {
			expProps: map[string]string{
				userProp.Key.String(): "default",
				compProp.Key.String(): "computed",
			},
		},
		"all (user value from db)": {
			db: newAttrDb(map[string]string{
				userPropPrefix + userProp.Key.String(): "db-value",
			}),
			expProps: map[string]string{
				userProp.Key.String(): "db-value",
				compProp.Key.String(): "computed",
			},
		},
		"user value default": {
			toGet: []string{
				userProp.Key.String(),
			},
			expProps: map[string]string{
				userProp.Key.String(): "default",
			},
		},
		"user value db": {
			toGet: []string{
				userProp.Key.String(),
			},
			db: newAttrDb(map[string]string{
				userPropPrefix + userProp.Key.String(): "db-value",
			}),
			expProps: map[string]string{
				userProp.Key.String(): "db-value",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			db := tc.db
			if db == nil {
				db = attrDb
			}

			gotProps, gotErr := GetUserProperties(db, sysProps, tc.toGet)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expProps, gotProps); diff != "" {
				t.Fatalf("unexpected properties (-want +got):\n%s", diff)
			}
		})
	}
}

func TestSystem_GetUserProperty(t *testing.T) {
	sysProps, propList := genTestProps(t, 1)

	userProp := propList[0]
	userProp.Value = daos.NewStringPropVal("default")
	sysProps[userProp.Key] = *userProp

	for name, tc := range map[string]struct {
		db       *testAttrDb
		toGet    string
		expValue string
		expErr   error
	}{
		"unknown prop": {
			toGet:  "unknown",
			expErr: errors.New("unknown property"),
		},
		"default value": {
			toGet:    userProp.Key.String(),
			expValue: "default",
		},
		"value set in db": {
			toGet: userProp.Key.String(),
			db: newAttrDb(map[string]string{
				userPropPrefix + userProp.Key.String(): "db-value",
			}),
			expValue: "db-value",
		},
	} {
		t.Run(name, func(t *testing.T) {
			db := tc.db
			if db == nil {
				db = newAttrDb(nil)
			}

			gotValue, gotErr := GetUserProperty(db, sysProps, tc.toGet)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expValue, gotValue); diff != "" {
				t.Fatalf("unexpected value (-want +got):\n%s", diff)
			}
		})
	}
}
