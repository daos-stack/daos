//
// (C) Copyright 2018 Intel Corporation.
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

package security_test

import (
	"security"
	"testing"
	. "utils/test"
)

// DomainCreds tests
func TestDomainCreds_Info(t *testing.T) {
	creds := &security.DomainCreds{}
	info := creds.Info()

	AssertEqual(t, info.SecurityProtocol, "domain", "Wrong SecurityProtocol")
	AssertEqual(t, info.SecurityVersion, "1.0", "Wrong SecurityVersion")
	AssertEqual(t, info.ServerName, "localhost", "Wrong ServerName")
}

func TestDomainCreds_ClientHandshake(t *testing.T) {
	creds := &security.DomainCreds{}
	conn, authInfo, err := creds.ClientHandshake(nil, "",
		nil)

	AssertEqual(t, conn, nil, "Expect the conn to match the nil we passed")
	AssertEqual(t, err, nil, "Expect no error")

	switch authInfoType := authInfo.(type) {
	case *security.DomainInfo:
		// Expected type
	default:
		t.Errorf("Bad type: %T", authInfoType)
	}
}
