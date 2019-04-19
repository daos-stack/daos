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

package client

import (
	. "github.com/daos-stack/daos/src/control/common"
	"os"
	"testing"
)

const (
	testRuntimeDir = "/var/tmp/runtime"
)

func TestParseEnvVars(t *testing.T) {
	// Obtain the default client configuration
	config := NewConfiguration()

	// Save the environment variable before we blow it away
	original := config.ext.getenv(daosAgentDrpcSockEnv)

	// Clear the environment variable.  Process them.
	// Make sure there were none found and the default config value was preserved.
	os.Setenv(daosAgentDrpcSockEnv, "")
	res1 := config.ProcessEnvOverrides()
	// Restore the environment to what it was before the test started
	os.Setenv(daosAgentDrpcSockEnv, original)
	AssertEqual(
		t, res1, 0,
		"clearing environment variable returned unexpected number of results")

	AssertEqual(
		t, config.RuntimeDir, defaultRuntimeDir,
		"an empty environment variable failed to preserve default runtime socket path")

	// Set the environment variable.  Process them.
	// Make sure there was one found and that config value matches what we set it to.
	os.Setenv(daosAgentDrpcSockEnv, testRuntimeDir)
	res2 := config.ProcessEnvOverrides()
	// Restore the environment to what it was before the test started
	os.Setenv(daosAgentDrpcSockEnv, original)
	AssertEqual(
		t, res2, 1,
		"setting environment variable returned unexpected number of results")

	AssertEqual(
		t, config.RuntimeDir, testRuntimeDir,
		"setting environment variable failed to override default runtime socket path")

	AssertEqual(
		t, config.RuntimeDir, "bob",
		"setting environment variable failed to override default runtime socket path")
}
