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
package common

import (
	"os"
	"strconv"
	"strings"
	"time"
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

const (
	// Use ISO8601 format for timestamps as it's
	// widely supported by parsers (e.g. javascript, etc).
	iso8601NoMicro = "2006-01-02T15:04:05Z0700"
	iso8601        = "2006-01-02T15:04:05.000000Z0700"
)

// FormatTime returns ISO8671 formatted representation of timestamp with
// microsecond resolution.
func FormatTime(t time.Time) string {
	return t.Format(iso8601)
}

// FormatTimeNoMicro returns ISO8671 formatted representation of timestamp with
// second resolution.
func FormatTimeNoMicro(t time.Time) string {
	return t.Format(iso8601NoMicro)
}
