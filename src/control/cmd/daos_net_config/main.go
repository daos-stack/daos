//
// (C) Copyright 2019 Intel Corporation.
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
package main

import (
	"errors"
	"flag"
	"fmt"
	"os"
	"strings"

	"github.com/daos-stack/daos/src/control/lib/netdetect"
)

func usage(flags *flag.FlagSet) {
	flags.Usage()
	os.Exit(1)
}

func exitWithError(err error) {
	if err == nil {
		err = errors.New("unknown error")
	}
	fmt.Fprintf(os.Stderr, "ERROR: %s\n", err)
	os.Exit(1)
}

func main() {
	flags := flag.NewFlagSet(os.Args[0], flag.ExitOnError)
	provider := flags.String("provider", "", "the fabric provider (e.g. ofi+sockets)")
	iface := flags.String("interface", "", "the fabric interface (e.g. eth0)")
	if err := flags.Parse(os.Args[1:]); err != nil {
		usage(flags)
	}

	if *provider == "" || *iface == "" {
		usage(flags)
	}

	vars := []string{
		fmt.Sprintf("CRT_PHY_ADDR_STR=%q", *provider),
		fmt.Sprintf("OFI_INTERFACE=%q", *iface),
	}

	if strings.EqualFold(*provider, "ofi+verbs") {
		alias, err := netdetect.GetDeviceAlias(*iface)
		if err != nil {
			exitWithError(err)
		}
		vars = append(vars, fmt.Sprintf("OFI_DOMAIN=%q", alias))
	}

	fmt.Println(strings.Join(vars, "\n"))
}
