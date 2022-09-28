//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bytes"
	"encoding/csv"
	"fmt"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/dustin/go-humanize"
)

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

func parseUint64Array(in string) (out []uint64, err error) {
	arr, err := csv.NewReader(strings.NewReader(in)).Read()
	if err != nil {
		return
	}

	out = make([]uint64, len(arr))
	for idx, elemStr := range arr {
		out[idx], err = humanize.ParseBytes(elemStr)
		if err != nil {
			return
		}
	}
	return
}
