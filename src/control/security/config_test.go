//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"bytes"
	"crypto"
	"os"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
)

func InsecureTC() *TransportConfig {
	return &TransportConfig{
		AllowInsecure: true,
	}
}

func BadTC() *TransportConfig {
	return &TransportConfig{
		AllowInsecure: false,
		CertificateConfig: CertificateConfig{
			CARootPath:      "testdata/certs/daosCA.crt",
			CertificatePath: "testdata/certs/bad.crt",
			PrivateKeyPath:  "testdata/certs/bad.key",
		},
	}
}

func ServerTC() *TransportConfig {
	return &TransportConfig{
		AllowInsecure: false,
		CertificateConfig: CertificateConfig{
			CARootPath:      "testdata/certs/daosCA.crt",
			CertificatePath: "testdata/certs/server.crt",
			PrivateKeyPath:  "testdata/certs/server.key",
			maxKeyPerms:     MaxUserOnlyKeyPerm,
		},
	}
}

func AgentTC() *TransportConfig {
	return &TransportConfig{
		AllowInsecure: false,
		CertificateConfig: CertificateConfig{
			CARootPath:      "testdata/certs/daosCA.crt",
			CertificatePath: "testdata/certs/agent.crt",
			PrivateKeyPath:  "testdata/certs/agent.key",
			maxKeyPerms:     MaxUserOnlyKeyPerm,
		},
	}
}

func SetupTCFilePerms(t *testing.T, conf *TransportConfig) {
	if err := os.Chmod(conf.CARootPath, MaxCertPerm); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(conf.CertificatePath, MaxCertPerm); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(conf.PrivateKeyPath, MaxUserOnlyKeyPerm); err != nil {
		t.Fatal(err)
	}
}

func TestPreLoadCertData(t *testing.T) {
	insecureTC := InsecureTC()
	serverTC := ServerTC()
	badTC := BadTC()

	clientDirTC := ServerTC()
	clientDirTC.ClientCertDir = "testdata/badperms"

	for name, tc := range map[string]struct {
		setup     func(t *testing.T)
		config    *TransportConfig
		expLoaded bool
		expErr    error
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"insecure": {
			config: insecureTC,
		},
		"cert success": {
			setup: func(t *testing.T) {
				SetupTCFilePerms(t, serverTC)
			},
			config:    serverTC,
			expLoaded: true,
		},
		"bad cert": {
			setup: func(t *testing.T) {
				SetupTCFilePerms(t, badTC)
			},
			config: badTC,
			expErr: errors.New("insecure permissions"),
		},
		"bad client dir": {
			setup: func(t *testing.T) {
				SetupTCFilePerms(t, clientDirTC)
			},
			config: clientDirTC,
			expErr: FaultUnreadableCertFile(clientDirTC.ClientCertDir),
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.setup != nil {
				tc.setup(t)
			}
			err := tc.config.PreLoadCertData()

			test.CmpErr(t, tc.expErr, err)
			if tc.config == nil {
				return
			}
			test.AssertEqual(t, tc.expLoaded, tc.config.tlsKeypair != nil, "")
			test.AssertEqual(t, tc.expLoaded, tc.config.caPool != nil, "")
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

	if bytes.Equal(beforeCert, afterCert) {
		t.Fatal("cert before and after reload is the same")
	}
}

func ValidateInsecurePrivateKey(t *testing.T, key crypto.PrivateKey, err error) {
	if err != nil {
		t.Fatalf("Unable to Load PrivateKey from TransportConfig: %s", err)
	}
	if key != nil {
		t.Fatal("Insecure config returned a PrivateKey")
	}
}

func ValidatePrivateKey(t *testing.T, key crypto.PrivateKey, err error) {
	if err != nil {
		t.Fatalf("Unable to Load PrivateKey from TransportConfig: %s", err)
	}
	if key == nil {
		t.Fatal("TransportConfig is missing a PrivateKey")
	}
}

func TestPrivateKey(t *testing.T) {
	insecureTC := InsecureTC()
	TC := ServerTC()

	SetupTCFilePerms(t, TC)

	testCases := []struct {
		testname string
		config   *TransportConfig
		Validate func(t *testing.T, key crypto.PrivateKey, err error)
	}{
		{"InsecurePrivateKey", insecureTC, ValidateInsecurePrivateKey},
		{"GoodPrivateKey", TC, ValidatePrivateKey},
	}

	for _, tc := range testCases {
		t.Run(tc.testname, func(t *testing.T) {
			key, err := tc.config.PrivateKey()
			tc.Validate(t, key, err)
		})
	}
}

func ValidateInsecurePublicKey(t *testing.T, key crypto.PublicKey, err error) {
	if err != nil {
		t.Fatalf("Unable to Load PublicKey from TransportConfig: %s", err)
	}
	if key != nil {
		t.Fatal("Insecure config returned a PublicKey")
	}
}

func ValidatePublicKey(t *testing.T, key crypto.PublicKey, err error) {
	if err != nil {
		t.Fatalf("Unable to Load PublicKey from TransportConfig: %s", err)
	}
	if key == nil {
		t.Fatal("TransportConfig is missing a PublicKey")
	}
}

func TestPublicKey(t *testing.T) {
	insecureTC := InsecureTC()
	TC := ServerTC()

	SetupTCFilePerms(t, TC)

	testCases := []struct {
		testname string
		config   *TransportConfig
		Validate func(t *testing.T, key crypto.PublicKey, err error)
	}{
		{"InsecurePublicKey", insecureTC, ValidateInsecurePublicKey},
		{"GoodPublicKey", TC, ValidatePublicKey},
	}

	for _, tc := range testCases {
		t.Run(tc.testname, func(t *testing.T) {
			key, err := tc.config.PublicKey()
			tc.Validate(t, key, err)
		})
	}
}

func TestSecurity_DefaultTransportConfigs(t *testing.T) {
	for name, tc := range map[string]struct {
		genTransportConfig func() *TransportConfig
		expResult          *TransportConfig
	}{
		"admin": {
			genTransportConfig: DefaultClientTransportConfig,
			expResult: &TransportConfig{
				CertificateConfig: CertificateConfig{
					ServerName:      defaultServer,
					CARootPath:      defaultCACert,
					CertificatePath: defaultAdminCert,
					PrivateKeyPath:  defaultAdminKey,
					maxKeyPerms:     MaxGroupKeyPerm,
				},
			},
		},
		"agent": {
			genTransportConfig: DefaultAgentTransportConfig,
			expResult: &TransportConfig{
				CertificateConfig: CertificateConfig{
					ServerName:      defaultServer,
					CARootPath:      defaultCACert,
					CertificatePath: defaultAgentCert,
					PrivateKeyPath:  defaultAgentKey,
					maxKeyPerms:     MaxUserOnlyKeyPerm,
				},
			},
		},
		"server": {
			genTransportConfig: DefaultServerTransportConfig,
			expResult: &TransportConfig{
				CertificateConfig: CertificateConfig{
					ServerName:      defaultServer,
					CARootPath:      defaultCACert,
					ClientCertDir:   defaultClientCertDir,
					CertificatePath: defaultServerCert,
					PrivateKeyPath:  defaultServerKey,
					maxKeyPerms:     MaxUserOnlyKeyPerm,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.genTransportConfig()

			if diff := cmp.Diff(tc.expResult, result, cmp.AllowUnexported(
				TransportConfig{},
				CertificateConfig{},
			)); diff != "" {
				t.Fatalf("(want-, got+)\n %s", diff)
			}
		})
	}
}
