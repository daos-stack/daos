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

package common

import (
	"strconv"
	"strings"

	"github.com/pkg/errors"
)

// HasPort checks if addr specifies a port. This only works with IPv4
// addresses at the moment.
func HasPort(addr string) bool {
	return strings.Contains(addr, ":")
}

// SplitPort separates port from host in address and can apply default port if
// address doesn't contain one.
func SplitPort(addrPattern string, defaultPort int) (string, string, error) {
	var port string
	hp := strings.Split(addrPattern, ":")

	switch len(hp) {
	case 1:
		// no port specified, use default
		port = strconv.Itoa(defaultPort)
	case 2:
		port = hp[1]
		if port == "" {
			return "", "", errors.Errorf("invalid port %q", port)
		}
		if _, err := strconv.Atoi(port); err != nil {
			return "", "", errors.WithMessagef(err, "cannot parse %q",
				addrPattern)
		}
	default:
		return "", "", errors.Errorf("cannot parse %q", addrPattern)
	}

	if hp[0] == "" {
		return "", "", errors.Errorf("invalid host %q", hp[0])
	}

	return hp[0], port, nil
}
