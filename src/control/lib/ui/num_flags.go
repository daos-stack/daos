//
// (C) Copyright 2022-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ui

import (
	"fmt"
	"math"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/atm"
)

// FmtHumanSize formats the supplied size in a human-readable format.
func FmtHumanSize(size float64, suffix string, binary bool) string {
	if size == 0 {
		return "0 " + suffix
	}
	val := size

	base := float64(1000)
	if binary {
		base = 1024
		if suffix != "" {
			suffix = "i" + suffix
		}
	}

	for _, unit := range []string{"", " K", " M", " G", " T", " P", " E", " Z", " Y"} {
		if math.Abs(val) < base {
			if unit == "" && suffix != "" {
				unit = " "
			}
			return fmt.Sprintf("%.02f%s%s", val, unit, suffix)
		}
		val /= base
	}

	// Fallback to scientific notation for unexpectedly huge numbers.
	return fmt.Sprintf("%E %s", size, suffix)
}

// ByteSizeFlag is a go-flags compatible flag type for converting
// string input into a byte size.
type ByteSizeFlag struct {
	set   atm.Bool
	Bytes uint64
}

func (sf ByteSizeFlag) IsSet() bool {
	return sf.set.IsTrue()
}

func (sf ByteSizeFlag) String() string {
	return humanize.Bytes(sf.Bytes)
}

func (sf *ByteSizeFlag) UnmarshalFlag(fv string) (err error) {
	if fv == "" {
		return errors.New("no size specified")
	}

	sf.Bytes, err = humanize.ParseBytes(fv)
	if err != nil {
		return errors.Errorf("invalid size %q", fv)
	}
	sf.set.SetTrue()

	return nil
}
