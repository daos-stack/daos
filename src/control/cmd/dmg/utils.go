//
// (C) Copyright 2018-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bytes"
	"fmt"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

type singleHostFlag ui.HostSetFlag

// UnmarshalFlag implements the go-flags.Unmarshaler interface.
func (shf *singleHostFlag) UnmarshalFlag(value string) error {
	if err := (*ui.HostSetFlag)(shf).UnmarshalFlag(value); err != nil {
		return err
	}

	if shf.Count() != 1 {
		return errors.New("must specify a single host")
	}

	return nil
}

// formatHostGroups adds group title header per group results.
func formatHostGroups(buf *bytes.Buffer, groups hostlist.HostGroups) string {
	for _, res := range groups.Keys() {
		hostset := groups[res].RangedString()
		lineBreak := strings.Repeat("-", len(hostset))
		fmt.Fprintf(buf, "%s\n%s\n%s\n%s", lineBreak, hostset, lineBreak, res)
	}

	return buf.String()
}

// errIncompatFlags accepts a base flag and a set of incompatible
// flags in order to generate a user-comprehensible error when an
// incompatible set of parameters was supplied.
func errIncompatFlags(key string, incompat ...string) error {
	base := fmt.Sprintf("--%s may not be mixed", key)
	if len(incompat) == 0 {
		// kind of a weird error but better than nothing
		return errors.New(base)
	}

	return errors.Errorf("%s with --%s", base, strings.Join(incompat, " or --"))
}

// Convert pair of ratios to a single fraction.
func ratiosToSingleFraction(ratios []float64) (float32, error) {
	nrRatios := len(ratios)

	// Most validation already performed by tierRatioFlag type, this just prevents
	// incomplete or overvalue tier combinations and restricts to 1 or 2 tiers.
	if nrRatios != 2 && ratios[0] < 1 {
		return 0, errors.Errorf("want 2 ratio values got %d", nrRatios)
	}

	// Precision loss deemed acceptable with conversion from float64 to float32.
	return float32(ratios[0]), nil
}
