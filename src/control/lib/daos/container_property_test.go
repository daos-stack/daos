//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"strconv"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
)

func newTestContainerProperty(hdlr *propHdlr) *ContainerProperty {
	return &ContainerProperty{
		property: property{
			entry: &_Ctype_struct_daos_prop_entry{}, // managed by Go; no need to free()
		},
		hdlr: hdlr,
	}
}

func TestDaos_ContainerProperty_EcCellSize(t *testing.T) {
	for name, tc := range map[string]struct {
		SizeStr    string
		EntryBytes uint64
	}{
		"Human Minimal Entry": {
			SizeStr:    humanize.IBytes(ECCellMin),
			EntryBytes: ECCellMin,
		},
		"Human Default Entry": {
			SizeStr:    humanize.IBytes(ECCellDefault),
			EntryBytes: ECCellDefault,
		},
		"Human Maximal Entry": {
			SizeStr:    humanize.IBytes(ECCellMax),
			EntryBytes: ECCellMax,
		},
		"Bytes Minimal Entry": {
			SizeStr:    strconv.FormatUint(ECCellMin, 10),
			EntryBytes: ECCellMin,
		},
		"Bytes Default Entry": {
			SizeStr:    strconv.FormatUint(ECCellDefault, 10),
			EntryBytes: ECCellDefault,
		},
		"Bytes Maximal Entry": {
			SizeStr:    strconv.FormatUint(ECCellMax, 10),
			EntryBytes: ECCellMax,
		},
	} {
		t.Run(name, func(t *testing.T) {
			testProp := newTestContainerProperty(propHdlrs[ContainerPropEcCellSize.String()])
			err := testProp.Set(tc.SizeStr)
			if err != nil {
				t.Fatalf("Expected no error: %s", err.Error())
			}
			test.AssertEqual(t,
				testProp.GetValue(),
				tc.EntryBytes,
				"Invalid EC Cell size")

			test.AssertEqual(t,
				humanize.IBytes(tc.EntryBytes),
				testProp.StringValue(),
				"Invalid EC Cell size representation")
		})
	}
}

func TestDaos_ContainerProperty_EcCellSize_Errors(t *testing.T) {
	propName := ContainerPropEcCellSize.String()
	for name, tc := range map[string]struct {
		SizeStr     string
		ExpectError error
	}{
		"Invalid integer": {
			SizeStr:     "100 Giga Bytes",
			ExpectError: errors.New("invalid EC cell size \"100 Giga Bytes\" (try N<unit>)"),
		},
		"Invalid cell size": {
			SizeStr:     "10",
			ExpectError: errors.New("invalid EC cell size 10"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			testProp := newTestContainerProperty(propHdlrs[ContainerPropEcCellSize.String()])
			err := testProp.Set(tc.SizeStr)
			test.CmpErr(t, tc.ExpectError, err)
		})
	}

	t.Run("Invalid Entry error message: nil entry", func(t *testing.T) {
		sizeStr := propHdlrs[propName].toString(nil, propName)
		test.AssertEqual(t,
			"property \""+propName+"\" not found",
			sizeStr,
			"Invalid error message")
	})

	t.Run("Invalid Entry error message: invalid size", func(t *testing.T) {
		testProp := newTestContainerProperty(propHdlrs[ContainerPropEcCellSize.String()])
		test.AssertEqual(t,
			"invalid size 0",
			testProp.StringValue(),
			"Invalid error message")
	})
}
