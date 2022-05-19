//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"testing"

	"github.com/google/go-cmp/cmp"
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
