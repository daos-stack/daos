//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

/*
#cgo LDFLAGS: -lgurt

#include <gurt/common.h>
*/
import "C"

import (
	"time"

	"github.com/daos-stack/daos/src/control/common"
)

const (
	// ZeroHLCDate is the date of the zero HLC, as
	// defined by D_HLC_START_SEC in src/gurt/hlc.c
	ZeroHLCDate = "2021-01-01 00:00:00 +0000 UTC"
)

type (
	// HLC is a high-resolution clock.
	HLC uint64
)

// Nanoseconds returns the HLC represented as the number of nanoseconds since the Unix epoch.
func (hlc HLC) Nanoseconds() int64 {
	return int64(C.d_hlc2unixnsec(C.uint64_t(hlc)))
}

func (hlc HLC) String() string {
	return hlc.ToTime().String()
}

func (hlc HLC) ToTime() time.Time {
	return time.Unix(0, hlc.Nanoseconds())
}

func (hlc HLC) MarshalJSON() ([]byte, error) {
	return []byte(`"` + common.FormatTime(hlc.ToTime()) + `"`), nil
}

func (hlc *HLC) UnmarshalJSON(b []byte) error {
	t, err := common.ParseTime(string(b))
	if err != nil {
		return err
	}
	*hlc = NewHLC(t.UnixNano())
	return nil
}

// NewHLC creates a new HLC from the given number of nanoseconds since the Unix epoch.
func NewHLC(nsec int64) HLC {
	return HLC(C.d_unixnsec2hlc(C.uint64_t(nsec)))
}
