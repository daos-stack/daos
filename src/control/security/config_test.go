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

package security

import (
	"os"
	"strings"
	"bytes"
	"testing"
)

func InsecureTC() *TransportConfig{
	return &TransportConfig{
		AllowInsecure: true,
	}
}
func BadTC() *TransportConfig{
	return &TransportConfig{
		AllowInsecure: false,
		CertificateConfig: CertificateConfig{
			CARootPath: "testdata/certs/daosCA.crt",
			CertificatePath: "testdata/certs/bad.crt",
			PrivateKeyPath: "testdata/certs/bad.key",
			tlsKeypair:      nil,
			caPool:          nil,
		},
	}
}
func ServerTC() *TransportConfig {
	return &TransportConfig{
		AllowInsecure: false,
		CertificateConfig: CertificateConfig{
			CARootPath: "testdata/certs/daosCA.crt",
			CertificatePath: "testdata/certs/server.crt",
			PrivateKeyPath: "testdata/certs/server.key",
			tlsKeypair:      nil,
			caPool:          nil,
		},
	}
}
func AgentTC() *TransportConfig {
	return &TransportConfig{
		AllowInsecure: false,
		CertificateConfig: CertificateConfig{
			CARootPath: "testdata/certs/daosCA.crt",
			CertificatePath: "testdata/certs/agent.crt",
			PrivateKeyPath: "testdata/certs/agent.key",
			tlsKeypair:      nil,
			caPool:          nil,
		},
	}
}
func SetupTCFilePerms(t *testing.T, conf *TransportConfig) {
	if err := os.Chmod(conf.CARootPath, SafeCertPerm); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(conf.CertificatePath, SafeCertPerm); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(conf.PrivateKeyPath, SafeKeyPerm); err != nil {
		t.Fatal(err)
	}
}

func ValidateInsecure (t *testing.T, c *TransportConfig, err error){
	if err != nil {
		t.Fatal(err)
	}
	if c.tlsKeypair != nil || c.caPool != nil {
		t.Fatal("insecure config loaded certs")
	}
}
func ValidateGood (t *testing.T, c *TransportConfig, err error){
	if err != nil {
		t.Fatal(err)
	}
	if c.tlsKeypair == nil || c.caPool == nil {
		t.Fatal("certs did not load yet returned no error")
	}
}
func ValidateBad (t *testing.T, c *TransportConfig, err error){
	if err == nil {
		t.Fatal(err)
	}
}
func ValidateNil (t *testing.T, c *TransportConfig, err error){
	if err != nil &&
		strings.Compare(err.Error(), "nil TransportConfig") != 0 {
		t.Fatal(err)
	}
}
func TestPreLoadCertData(t *testing.T) {
	insecureTC := InsecureTC()
	serverTC := ServerTC()
	badTC := BadTC()

	// Setup permissions for tests below.
	SetupTCFilePerms(t, serverTC)
	SetupTCFilePerms(t, badTC)

	testCases := []struct {
		testname string
		config *TransportConfig
		Validate func(t *testing.T, c *TransportConfig, err error)
	}{
		{"InsecureTC", insecureTC, ValidateInsecure},
		{"GoodTC", serverTC, ValidateGood},
		{"BadTC", badTC, ValidateBad},
		{"NilTC", nil, ValidateNil},
	}

	for _, tc := range testCases {
		t.Run(tc.testname, func(t *testing.T) {
			err := tc.config.PreLoadCertData()
			tc.Validate(t, tc.config, err)
		})
	}
}
func TestReloadCertData(t *testing.T) {
	serverTC := ServerTC()
	agentTC := AgentTC()
	testTC := serverTC

	SetupTCFilePerms(t, serverTC)
	SetupTCFilePerms(t, agentTC)

	err := testTC.PreLoadCertData()
	if err != nil {
		t.Fatal(err)
	}
	beforeCert := testTC.tlsKeypair.Certificate[0]

	testTC.CertificatePath = agentTC.CertificatePath
	testTC.PrivateKeyPath = agentTC.PrivateKeyPath

	err = testTC.ReloadCertData()
	if err != nil {
		t.Fatal(err)
	}

	afterCert := testTC.tlsKeypair.Certificate[0]

	if bytes.Compare(beforeCert, afterCert) == 0 {
		t.Fatal("cert before and after reload is the same")
	}
}
func TestPrivateKey(t *testing.T) {
	config := ServerTC()

	SetupTCFilePerms(t, config)

	config.AllowInsecure = true

	key, err := config.PrivateKey()

	if key != nil && err != nil {
		t.Fatal("Insecure config returned a PrivateKey")
	}

	config.AllowInsecure = false
	key, err = config.PrivateKey()

	if key == nil || err != nil {
		t.Fatal("Unable to load keys from transportconfig")
	}

}
func TestPublicKey(t *testing.T) {
	config := ServerTC()

	SetupTCFilePerms(t, config)

	config.AllowInsecure = true

	key, err := config.PublicKey()

	if key != nil && err != nil {
		t.Fatal("Insecure config returned a PublicKey")
	}

	config.AllowInsecure = false
	key, err = config.PublicKey()

	if key == nil || err != nil {
		t.Fatal("Unable to load keys from transportconfig")
	}

}
