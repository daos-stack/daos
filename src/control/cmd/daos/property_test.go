//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"strconv"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

func TestProperty_EcCellSize(t *testing.T) {
	for name, tc := range map[string]struct {
		SizeStr    string
		EntryBytes uint64
	}{
		"Human Minimal Entry": {
			SizeStr:    humanize.IBytes(daos.ECCellMin),
			EntryBytes: daos.ECCellMin,
		},
		"Human Default Entry": {
			SizeStr:    humanize.IBytes(daos.ECCellDefault),
			EntryBytes: daos.ECCellDefault,
		},
		"Human Maximal Entry": {
			SizeStr:    humanize.IBytes(daos.ECCellMax),
			EntryBytes: daos.ECCellMax,
		},
		"Bytes Minimal Entry": {
			SizeStr:    strconv.FormatUint(daos.ECCellMin, 10),
			EntryBytes: daos.ECCellMin,
		},
		"Bytes Default Entry": {
			SizeStr:    strconv.FormatUint(daos.ECCellDefault, 10),
			EntryBytes: daos.ECCellDefault,
		},
		"Bytes Maximal Entry": {
			SizeStr:    strconv.FormatUint(daos.ECCellMax, 10),
			EntryBytes: daos.ECCellMax,
		},
	} {
		t.Run(name, func(t *testing.T) {
			propEntry := newTestPropEntry()
			err := propHdlrs[daos.PropEntryECCellSize].nameHdlr(nil,
				propEntry,
				tc.SizeStr)
			if err != nil {
				t.Fatalf("Expected no error: %s", err.Error())
			}
			val, err := getDpeVal(propEntry)
			if err != nil {
				t.Fatal(err)
			}
			test.AssertEqual(t,
				val,
				tc.EntryBytes,
				"Invalid EC Cell size")

			sizeStr := propHdlrs[daos.PropEntryECCellSize].toString(propEntry,
				daos.PropEntryECCellSize)
			test.AssertEqual(t,
				humanize.IBytes(tc.EntryBytes),
				sizeStr,
				"Invalid EC Cell size representation")
		})
	}
}

func TestProperty_EcCellSize_Errors(t *testing.T) {
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
			propEntry := newTestPropEntry()
			err := propHdlrs[daos.PropEntryECCellSize].nameHdlr(nil,
				propEntry,
				tc.SizeStr)
			test.CmpErr(t, tc.ExpectError, err)
		})
	}

	t.Run("Invalid Entry error message: nil entry", func(t *testing.T) {
		sizeStr := propHdlrs[daos.PropEntryECCellSize].toString(nil,
			daos.PropEntryECCellSize)
		test.AssertEqual(t,
			"property \""+daos.PropEntryECCellSize+"\" not found",
			sizeStr,
			"Invalid error message")
	})

	t.Run("Invalid Entry error message: invalid size", func(t *testing.T) {
		propEntry := newTestPropEntry()
		sizeStr := propHdlrs[daos.PropEntryECCellSize].toString(propEntry,
			daos.PropEntryECCellSize)
		test.AssertEqual(t,
			"invalid size 0",
			sizeStr,
			"Invalid error message")
	})

}
