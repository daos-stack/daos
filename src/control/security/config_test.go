//
// (C) Copyright 2019-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"bytes"
	"crypto"
	"crypto/x509"
	"encoding/pem"
	"io/ioutil"
	"os"
	"path/filepath"
	"syscall"
	"testing"
	"time"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/fault"
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

func getCert(t *testing.T, path string) *x509.Certificate {
	buf, err := ioutil.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}

	block, _ := pem.Decode(buf)
	if block == nil {
		t.Fatal("failed to parse test certificate PEM")
	}

	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		t.Fatal(err)
	}

	return cert
}

func setValidVerifyTime(t *testing.T, cfg *TransportConfig) {
	if cfg.AllowInsecure {
		return
	}
	cert := getCert(t, cfg.CertificatePath)
	cfg.CertificateConfig.verifyTime = cert.NotBefore
}

func setExpiredVerifyTime(t *testing.T, cfg *TransportConfig) {
	if cfg.AllowInsecure {
		return
	}
	cert := getCert(t, cfg.CertificatePath)
	cfg.CertificateConfig.verifyTime = cert.NotAfter.Add(time.Second)
}

func TestPreLoadCertData(t *testing.T) {
	clientDir := func(dir string) string {
		return filepath.Join(dir, "client")
	}

	for name, tc := range map[string]struct {
		getCfg    func(t *testing.T, dir string) *TransportConfig
		setup     func(t *testing.T) (string, func())
		config    *TransportConfig
		expLoaded bool
		expErr    error
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"insecure": {
			getCfg: func(t *testing.T, _ string) *TransportConfig {
				return InsecureTC()
			},
		},
		"cert success": {
			getCfg: func(t *testing.T, _ string) *TransportConfig {
				serverTC := ServerTC()
				setValidVerifyTime(t, serverTC)
				SetupTCFilePerms(t, serverTC)
				return serverTC
			},
			expLoaded: true,
		},
		"bad cert": {
			getCfg: func(t *testing.T, _ string) *TransportConfig {
				badTC := BadTC()
				SetupTCFilePerms(t, badTC)
				return badTC
			},
			expErr: errors.New("insecure permissions"),
		},
		"client dir doesn't exist": {
			setup: test.CreateTestDir,
			getCfg: func(t *testing.T, dir string) *TransportConfig {
				conf := ServerTC()
				setValidVerifyTime(t, conf)
				conf.ClientCertDir = "a thing that does not exist"
				return conf
			},
			expErr: syscall.ENOENT,
		},
		"client dir not a directory": {
			setup: test.CreateTestDir,
			getCfg: func(t *testing.T, dir string) *TransportConfig {
				clientDirTC := ServerTC()
				setValidVerifyTime(t, clientDirTC)

				filePath := clientDir(dir)
				if err := os.WriteFile(filePath, []byte("some stuff"), 0400); err != nil {
					t.Fatal(err)
				}
				clientDirTC.ClientCertDir = filePath
				return clientDirTC
			},
			expErr: syscall.ENOTDIR,
		},
		"can't access client dir": {
			setup: func(t *testing.T) (string, func()) {
				dir, cleanup := test.CreateTestDir(t)
				if err := os.Mkdir(clientDir(dir), 0220); err != nil {
					t.Fatal(err)
				}

				return dir, cleanup
			},
			getCfg: func(t *testing.T, dir string) *TransportConfig {
				clientDirTC := ServerTC()
				setValidVerifyTime(t, clientDirTC)

				clientDirTC.ClientCertDir = clientDir(dir)
				return clientDirTC
			},
			expErr: FaultUnreadableCertFile(""),
		},
		"expired cert": {
			getCfg: func(t *testing.T, _ string) *TransportConfig {
				serverTC := ServerTC()
				setExpiredVerifyTime(t, serverTC)
				SetupTCFilePerms(t, serverTC)
				return serverTC
			},
			expLoaded: true,
			expErr:    FaultInvalidCertFile(ServerTC().CertificatePath, nil),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var testDir string
			if tc.setup != nil {
				dir, teardown := tc.setup(t)
				defer teardown()
				testDir = dir
			}

			var cfg *TransportConfig
			if tc.getCfg != nil {
				cfg = tc.getCfg(t, testDir)
			}

			err := cfg.PreLoadCertData()

			// If it's a fault, we don't need to check the exact paths passed in
			if fault.IsFault(tc.expErr) && fault.IsFault(err) {
				expCode := tc.expErr.(*fault.Fault).Code
				if !fault.IsFaultCode(err, expCode) {
					t.Fatalf("expected fault: %s, got: %s", tc.expErr, err)
				}
			} else {
				test.CmpErr(t, tc.expErr, err)
			}

			if cfg == nil {
				return
			}
			test.AssertEqual(t, tc.expLoaded, cfg.tlsKeypair != nil, "")
			test.AssertEqual(t, tc.expLoaded, cfg.caPool != nil, "")
		})
	}
}

func TestReloadCertData(t *testing.T) {
	serverTC := ServerTC()
	agentTC := AgentTC()
	testTC := serverTC

	SetupTCFilePerms(t, serverTC)
	SetupTCFilePerms(t, agentTC)

	setValidVerifyTime(t, testTC)
	err := testTC.PreLoadCertData()
	if err != nil {
		t.Fatal(err)
	}
	beforeCert := testTC.tlsKeypair.Certificate[0]

	testTC.CertificatePath = agentTC.CertificatePath
	testTC.PrivateKeyPath = agentTC.PrivateKeyPath

	setValidVerifyTime(t, testTC)
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
			setValidVerifyTime(t, tc.config)

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
			setValidVerifyTime(t, tc.config)

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
