//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

// Package pretty provides pretty-printers for complex response types.
package pretty

import (
	"fmt"
	"io"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

var (
	defaultPrintConfig = &PrintConfig{
		Verbose:       false,
		ShowHostPorts: false,
	}
)

type (
	// PrintConfig defines parameters for controlling formatter behavior.
	PrintConfig struct {
		// Verbose indicates that the output should include more detail.
		Verbose bool
		// ShowHostPorts indicates that the host output should include the network port.
		ShowHostPorts bool
	}

	// PrintConfigOption defines a config function.
	PrintConfigOption func(*PrintConfig)

	// hostErrorsGetter define an interface for responses which return
	// a HostErrorsMap.
	hostErrorsGetter interface {
		GetHostErrors() control.HostErrorsMap
	}
)

// PrintWithVerboseOutput toggles verbose output from the formatter.
func PrintWithVerboseOutput(verbose bool) PrintConfigOption {
	return func(cfg *PrintConfig) {
		cfg.Verbose = verbose
	}
}

// PrintWithHostPorts enables display of host ports in output.
func PrintWithHostPorts() PrintConfigOption {
	return func(cfg *PrintConfig) {
		cfg.ShowHostPorts = true
	}
}

// GetPrintConfig is a helper that returns a format configuration
// for a format function.
func GetPrintConfig(opts ...PrintConfigOption) *PrintConfig {
	cfg := &PrintConfig{}
	*cfg = *defaultPrintConfig
	for _, opt := range opts {
		opt(cfg)
	}
	return cfg
}

// getPrintHosts is a helper that transforms the given list of
// host strings according to the format configuration.
func getPrintHosts(in string, opts ...PrintConfigOption) string {
	var out []string
	fc := GetPrintConfig(opts...)

	for _, hostStr := range strings.Split(in, ",") {
		if fc.ShowHostPorts {
			out = append(out, hostStr)
			continue
		}

		hostPort := strings.Split(hostStr, ":")
		if len(hostPort) != 2 {
			out = append(out, hostStr)
			continue
		}
		out = append(out, hostPort[0])
	}

	return strings.Join(out, ",")
}

// PrintHostErrorsMap generates a human-readable representation of the supplied
// HostErrorsMap struct and writes it to the supplied io.Writer.
func PrintHostErrorsMap(hem control.HostErrorsMap, out io.Writer, opts ...PrintConfigOption) error {
	if len(hem) == 0 {
		return nil
	}

	setTitle := "Hosts"
	errTitle := "Error"

	tablePrint := txtfmt.NewTableFormatter(setTitle, errTitle)
	tablePrint.InitWriter(out)
	table := []txtfmt.TableRow{}

	for _, errStr := range hem.Keys() {
		errHosts := getPrintHosts(hem[errStr].HostSet.RangedString(), opts...)
		row := txtfmt.TableRow{setTitle: errHosts}

		// Unpack the root cause error. If it's a fault,
		// just print the description.
		hostErr := errors.Cause(hem[errStr].HostError)
		row[errTitle] = hostErr.Error()
		if f, ok := hostErr.(*fault.Fault); ok {
			row[errTitle] = f.Description
		}

		table = append(table, row)
	}

	tablePrint.Format(table)
	return nil
}

func PrintResponseErrors(resp hostErrorsGetter, out io.Writer, opts ...PrintConfigOption) error {
	if resp == nil {
		return errors.Errorf("nil %T", resp)
	}

	if len(resp.GetHostErrors()) > 0 {
		fmt.Fprintln(out, "Errors:")
		if err := PrintHostErrorsMap(resp.GetHostErrors(), txtfmt.NewIndentWriter(out), opts...); err != nil {
			return err
		}
		fmt.Fprintln(out)
	}

	return nil
}
