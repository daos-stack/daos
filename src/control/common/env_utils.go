//
// (C) Copyright 2019-2020 Intel Corporation.
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
	"os"
	"strconv"
	"strings"
)

// ScrubEnvironment modifies the environment variables set for
// this process and any children which inherit its environment
// by unsetting any variables supplied in the blacklist.
func ScrubEnvironment(blacklist []string) {
	for _, key := range blacklist {
		os.Unsetenv(key)
	}
}

// ScrubEnvironmentExcept modifies the environment variables set for
// this process and any children which inherit its environment
// by unsetting any variables that are not supplied in the whitelist.
func ScrubEnvironmentExcept(whitelist []string) {
	lookup := make(map[string]struct{})
	for _, key := range whitelist {
		lookup[key] = struct{}{}
	}

	for _, keyVal := range os.Environ() {
		key := strings.SplitN(keyVal, "=", 2)[0]
		if _, inList := lookup[key]; !inList {
			os.Unsetenv(key)
		}
	}
}

// DisableProxyScrubEnv disabled removal of proxy variables from the process
// environment.
const DisableProxyScrubEnv = "DAOS_DISABLE_PROXY_SCRUB"

func proxyScrubIsDisabled() bool {
	val, set := os.LookupEnv(DisableProxyScrubEnv)
	if !set {
		return false
	}

	disabled, err := strconv.ParseBool(val)
	if err != nil {
		return false
	}
	return disabled
}

// ScrubProxyVariables removes proxy variables from the process environment.
func ScrubProxyVariables() {
	proxyVars := []string{
		"http_proxy", "https_proxy", "ftp_proxy", "socks_proxy", "no_proxy",
		"HTTP_PROXY", "HTTPS_PROXY", "FTP_PROXY", "SOCKS_PROXY", "NO_PROXY",
	}

	if !proxyScrubIsDisabled() {
		ScrubEnvironment(proxyVars)
	}
}
