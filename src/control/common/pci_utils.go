//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"strconv"
	"strings"

	"github.com/pkg/errors"
)

// ParsePCIAddress returns separated components of BDF format PCI address.
func ParsePCIAddress(addr string) (dom, bus, dev, fun uint64, err error) {
	parts := strings.Split(addr, ":")
	devFunc := strings.Split(parts[len(parts)-1], ".")
	if len(parts) != 3 || len(devFunc) != 2 {
		err = errors.Errorf("unexpected pci address bdf format: %q", addr)
		return
	}

	if dom, err = strconv.ParseUint(parts[0], 16, 64); err != nil {
		return
	}
	if bus, err = strconv.ParseUint(parts[1], 16, 32); err != nil {
		return
	}
	if dev, err = strconv.ParseUint(devFunc[0], 16, 32); err != nil {
		return
	}
	if fun, err = strconv.ParseUint(devFunc[1], 16, 32); err != nil {
		return
	}

	return
}

// GetRangeLimits takes a string of format <Begin-End> and returns the begin and end values.
// Number base is detected from the string prefixes e.g. 0x for hexadecimal.
func GetRangeLimits(numRange string) (begin, end uint64, err error) {
	if numRange == "" {
		return
	}

	split := strings.Split(numRange, "-")
	if len(split) != 2 {
		return 0, 0, errors.Errorf("invalid busid range %q", numRange)
	}

	begin, err = strconv.ParseUint(split[0], 0, 64)
	if err != nil {
		return 0, 0, errors.Wrapf(err, "parse busid range %q", numRange)
	}

	end, err = strconv.ParseUint(split[1], 0, 64)
	if err != nil {
		return 0, 0, errors.Wrapf(err, "parse busid range %q", numRange)
	}

	if begin > end {
		return 0, 0, errors.Errorf("invalid busid range %q", numRange)
	}

	return
}
