//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
