//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestDaos_ObjectClass(t *testing.T) {
	testClasses := map[ObjectClass]string{
		1: "one",
	}
	objClass2StringStash := objClass2String
	objClass2String = func(class ObjectClass) string {
		return testClasses[class]
	}
	objClassName2ClassStash := objClassName2Class
	objClassName2Class = func(name string) (ObjectClass, error) {
		for class, className := range testClasses {
			if className == name {
				return class, nil
			}
		}
		return 0, InvalidInput
	}
	defer func() {
		objClass2String = objClass2StringStash
		objClassName2Class = objClassName2ClassStash
	}()

	for name, tc := range map[string]struct {
		classStr string
		expClass ObjectClass
		expErr   error
	}{
		"nil string": {
			expErr: InvalidInput,
		},
		"empty string": {
			classStr: "",
			expErr:   InvalidInput,
		},
		"invalid string": {
			classStr: "foo",
			expErr:   InvalidInput,
		},
		"valid string": {
			classStr: "one",
			expClass: ObjectClass(1),
		},
	} {
		t.Run(name, func(t *testing.T) {
			class, err := ObjectClassFromString(tc.classStr)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}
			if diff := cmp.Diff(tc.expClass, class); diff != "" {
				t.Fatalf("unexpected ObjectClass (-want +got):\n%s", diff)
			}
			if diff := cmp.Diff(tc.expClass.String(), tc.classStr); diff != "" {
				t.Fatalf("unexpected string (-want +got):\n%s", diff)
			}
		})
	}
}

func TestDaos_ObjectID(t *testing.T) {
	for name, tc := range map[string]struct {
		oidStr  string
		expOid  *ObjectID
		expZero bool
		expErr  error
	}{
		"nil string": {
			expErr: InvalidInput,
		},
		"empty string": {
			oidStr: "",
			expErr: InvalidInput,
		},
		"invalid string": {
			oidStr: "foo",
			expErr: InvalidInput,
		},
		"zero": {
			oidStr:  "0.0",
			expZero: true,
			expOid:  &ObjectID{0, 0},
		},
		"valid string": {
			oidStr: "281479271677952.42",
			expOid: &ObjectID{
				hi: 281479271677952,
				lo: 42,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			oid, err := ObjectIDFromString(tc.oidStr)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}
			if diff := cmp.Diff(tc.expOid, &oid, cmp.AllowUnexported(ObjectID{})); diff != "" {
				t.Fatalf("unexpected ObjectID (-want +got):\n%s", diff)
			}
			if diff := cmp.Diff(tc.expOid.String(), tc.oidStr); diff != "" {
				t.Fatalf("unexpected string (-want +got):\n%s", diff)
			}
			if diff := cmp.Diff(tc.expZero, oid.IsZero()); diff != "" {
				t.Fatalf("unexpected IsZero (-want +got):\n%s", diff)
			}
		})
	}
}
