//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func Test_CalcRamdiskSize(t *testing.T) {
	for name, tc := range map[string]struct {
		memTotal uint64
		memHuge  uint64
		memSys   uint64
		tgtCount int
		engCount int
		expSize  uint64
		expErr   error
	}{
		"no mem": {
			expErr: errors.New("requires nonzero total mem"),
		},
		"no targets": {
			memTotal: humanize.GiByte,
			expErr:   errors.New("requires positive nonzero nr engine targets"),
		},
		"no engines": {
			memTotal: humanize.GiByte,
			tgtCount: 8,
			expErr:   errors.New("requires positive nonzero nr engines"),
		},
		"default values; low mem": {
			memTotal: humanize.GiByte * 30,
			memHuge:  humanize.GiByte * 14,
			memSys:   DefaultSysMemRsvd,
			tgtCount: 8,
			engCount: 1,
			expErr:   errors.New("insufficient ram"), // 30 - (14+16+1) = -1
		},
		"default values; high mem": {
			memTotal: humanize.GiByte * 60,
			memHuge:  humanize.GiByte * 30,
			memSys:   DefaultSysMemRsvd,
			tgtCount: 16,
			engCount: 2,
			expSize:  humanize.GiByte * 5, // (60 - (30+16+4)) / 2
		},
		"default values; low nr targets": {
			memTotal: humanize.GiByte * 60,
			memHuge:  humanize.GiByte * 30,
			memSys:   DefaultSysMemRsvd,
			tgtCount: 1,
			engCount: 2,
			expSize:  humanize.GiByte * 6, // (60 - (30+16+2)) / 2
		},
		"custom values; low sys reservation": {
			memTotal: humanize.GiByte * 60,
			memHuge:  humanize.GiByte * 30,
			memSys:   humanize.GiByte * 4,
			tgtCount: 16,
			engCount: 2,
			expSize:  humanize.GiByte * 11, // (60 - (30+4+4)) / 2
		},
		"custom values; high sys reservation": {
			memTotal: humanize.GiByte * 60,
			memHuge:  humanize.GiByte * 30,
			memSys:   humanize.GiByte * 27,
			tgtCount: 16,
			engCount: 2,
			expErr:   errors.New("insufficient ram"), // 60 - (30+27+4) = -1
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			gotSize, gotErr := CalcRamdiskSize(log, tc.memTotal, tc.memHuge, tc.memSys,
				tc.tgtCount, tc.engCount)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if gotSize != tc.expSize {
				t.Fatalf("expected %s, got %s",
					humanize.IBytes(tc.expSize), humanize.IBytes(gotSize))
			}
		})
	}
}
