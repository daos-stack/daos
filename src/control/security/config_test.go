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
	"fmt"
	"io/ioutil"
	"os"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common/test"
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
	for name, tc := range map[string]struct {
		getCfg    func(t *testing.T) *TransportConfig
		setup     func(t *testing.T)
		config    *TransportConfig
		expLoaded bool
		expErr    error
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"insecure": {
			getCfg: func(t *testing.T) *TransportConfig {
				return InsecureTC()
			},
		},
		"cert success": {
			getCfg: func(t *testing.T) *TransportConfig {
				serverTC := ServerTC()
				setValidVerifyTime(t, serverTC)
				SetupTCFilePerms(t, serverTC)
				return serverTC
			},
			expLoaded: true,
		},
		"bad cert": {
			getCfg: func(t *testing.T) *TransportConfig {
				badTC := BadTC()
				SetupTCFilePerms(t, badTC)
				return badTC
			},
			expErr: errors.New("insecure permissions"),
		},
		"bad client dir": {
			getCfg: func(t *testing.T) *TransportConfig {
				clientDirTC := ServerTC()
				setValidVerifyTime(t, clientDirTC)
				clientDirTC.ClientCertDir = "testdata/badperms"
				SetupTCFilePerms(t, clientDirTC)
				return clientDirTC
			},
			expErr: FaultUnreadableCertFile("testdata/badperms"),
		},
		"expired cert": {
			getCfg: func(t *testing.T) *TransportConfig {
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
			var cfg *TransportConfig
			if tc.getCfg != nil {
				cfg = tc.getCfg(t)
			}

			err := cfg.PreLoadCertData()

			test.CmpErr(t, tc.expErr, err)
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

func TestSecurity_ClientUserMap(t *testing.T) {
	for name, tc := range map[string]struct {
		cfgYaml string
		expMap  ClientUserMap
		expErr  error
	}{
		"empty": {},
		"defaultKey": {
			cfgYaml: fmt.Sprintf(`
%d:
  user: whoops
`, defaultMapKey),
			expErr: errors.New("reserved"),
		},
		"invalid uid (negative)": {
			cfgYaml: `
-1:
  user: whoops
`,
			expErr: errors.New("invalid uid"),
		},
		"invalid uid (words)": {
			cfgYaml: `
blah:
  user: whoops
`,
			expErr: errors.New("invalid uid"),
		},
		"invalid mapped user": {
			cfgYaml: `
1234:
user: whoops
`,
			expErr: errors.New("unmarshal error"),
		},
		"good": {
			cfgYaml: `
default:
  user: banana
  group: rama
  groups: [ding, dong]
1234:
  user: abc
  group: def
  groups: [yabba, dabba, doo]
5678:
  user: ghi
  group: jkl
  groups: [mno, pqr, stu]
`,
			expMap: ClientUserMap{
				defaultMapKey: {
					User:   "banana",
					Group:  "rama",
					Groups: []string{"ding", "dong"},
				},
				1234: {
					User:   "abc",
					Group:  "def",
					Groups: []string{"yabba", "dabba", "doo"},
				},
				5678: {
					User:   "ghi",
					Group:  "jkl",
					Groups: []string{"mno", "pqr", "stu"},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			var result ClientUserMap
			err := yaml.Unmarshal([]byte(tc.cfgYaml), &result)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}
			if diff := cmp.Diff(tc.expMap, result); diff != "" {
				t.Fatalf("unexpected ClientUserMap (-want, +got)\n %s", diff)
			}

			for uid, exp := range tc.expMap {
				gotUser := result.Lookup(uid)
				if diff := cmp.Diff(exp.User, gotUser.User); diff != "" {
					t.Fatalf("unexpected User (-want, +got)\n %s", diff)
				}
			}

			if expDefUser, found := tc.expMap[defaultMapKey]; found {
				gotDefUser := result.Lookup(1234567)
				if diff := cmp.Diff(expDefUser, gotDefUser); diff != "" {
					t.Fatalf("unexpected DefaultUser (-want, +got)\n %s", diff)
				}
			}
		})
	}
}
