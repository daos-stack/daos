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
		memEng   uint64
		engCount int
		expSize  uint64
		expErr   error
	}{
		"no mem": {
			expErr: errors.New("requires nonzero total mem"),
		},
		"no engines": {
			memTotal: humanize.GiByte,
			expErr:   errors.New("requires nonzero nr engines"),
		},
		"default values; low mem": {
			memTotal: humanize.GiByte * 18,
			memHuge:  humanize.GiByte * 12,
			memSys:   DefaultSysMemRsvd,
			memEng:   DefaultEngineMemRsvd,
			engCount: 1,
			expErr:   errors.New("insufficient ram"),
		},
		"default values; high mem": {
			memTotal: humanize.GiByte * 23,
			memHuge:  humanize.GiByte * 12,
			memSys:   DefaultSysMemRsvd,
			memEng:   DefaultEngineMemRsvd,
			engCount: 1,
			expSize:  humanize.GiByte * 4,
		},
		"custom values; low sys reservation": {
			memSys:   humanize.GiByte * 4,
			memEng:   DefaultEngineMemRsvd,
			memTotal: humanize.GiByte * 18,
			memHuge:  humanize.GiByte * 12,
			engCount: 2,
		},
		"custom values; high eng reservation": {
			memSys:   DefaultSysMemRsvd,
			memEng:   humanize.GiByte * 3,
			memTotal: humanize.GiByte * 23,
			memHuge:  humanize.GiByte * 12,
			engCount: 2,
			expErr:   errors.New("insufficient ram"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			gotSize, gotErr := CalcRamdiskSize(log, tc.memTotal, tc.memHuge, tc.memSys,
				tc.memEng, tc.engCount)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if gotSize != tc.expSize {
				t.Fatalf("expected %d, got %d", tc.expSize, gotSize)
			}
		})
	}
}
