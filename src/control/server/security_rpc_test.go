//
// (C) Copyright 2019-2022 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"crypto"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha512"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/binary"
	"encoding/json"
	"encoding/pem"
	"math/big"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/security/auth"
)

func TestSrvSecurityModule_ID(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, nil)

	test.AssertEqual(t, mod.ID(), daos.ModuleSecurity, "wrong drpc module")
}

func insecureTransportConfig() *security.TransportConfig {
	return &security.TransportConfig{AllowInsecure: true}
}

func secureTransportConfig(certDir string) *security.TransportConfig {
	return &security.TransportConfig{
		CertificateConfig: security.CertificateConfig{
			ClientCertDir: certDir,
		},
	}
}

func TestSrv_SecurityModule_GetMethod(t *testing.T) {
	for name, tc := range map[string]struct {
		methodID  int32
		expMethod drpc.Method
		expErr    error
	}{
		"request-cred": {
			methodID:  daos.MethodValidateCredentials.ID(),
			expMethod: daos.MethodValidateCredentials,
		},
		"unknown": {
			methodID: -1,
			expErr:   errors.New("method ID -1"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			parent := test.MustLogContext(t)
			log := logging.FromContext(parent)

			mod := NewSecurityModule(log, insecureTransportConfig())

			method, err := mod.GetMethod(tc.methodID)

			test.CmpErr(t, tc.expErr, err)
			test.CmpAny(t, "", tc.expMethod, method)
		})
	}
}

func callValidateCreds(t *testing.T, mod *SecurityModule, body []byte) ([]byte, error) {
	t.Helper()
	return mod.HandleCall(test.Context(t), nil, daos.MethodValidateCredentials, body)
}

func TestSrvSecurityModule_ValidateCred_InvalidReq(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, insecureTransportConfig())
	// Put garbage in the body
	resp, err := callValidateCreds(t, mod, []byte{byte(123), byte(90), byte(255)})

	if resp != nil {
		t.Errorf("Expected no response, got %+v", resp)
	}

	test.CmpErr(t, drpc.UnmarshalingPayloadFailure(), err)
}

func getMarshaledValidateCredReq(t *testing.T, token *auth.Token, verifier *auth.Token) []byte {
	req := &auth.ValidateCredReq{
		Cred: &auth.Credential{
			Token:    token,
			Verifier: verifier,
			Origin:   "test",
		},
	}

	return marshal(t, req)
}

func marshal(t *testing.T, message proto.Message) []byte {
	bytes, err := proto.Marshal(message)
	if err != nil {
		t.Fatal("Couldn't marshal request")
	}
	return bytes
}

func expectValidateResp(t *testing.T, respBytes []byte, expResp *auth.ValidateCredResp) {
	if respBytes == nil {
		t.Error("Expected non-nil response")
	}

	resp := &auth.ValidateCredResp{}
	if err := proto.Unmarshal(respBytes, resp); err != nil {
		t.Fatalf("Couldn't unmarshal result: %v", err)
	}

	cmpOpts := test.DefaultCmpOpts()
	if diff := cmp.Diff(expResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("(-want, +got)\n%s", diff)
	}
}

func TestSrvSecurityModule_ValidateCred_NoCred(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, insecureTransportConfig())
	reqBytes := marshal(t, &auth.ValidateCredReq{})

	resp, err := callValidateCreds(t, mod, reqBytes)

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	expectValidateResp(t, resp, &auth.ValidateCredResp{
		Status: int32(daos.InvalidInput),
	})
}

func TestSrvSecurityModule_ValidateCred_NoToken(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, insecureTransportConfig())
	reqBytes := getMarshaledValidateCredReq(t, nil, &auth.Token{
		Flavor: auth.Flavor_AUTH_NONE,
		Data:   []byte{byte(1), byte(2)},
	})

	resp, err := callValidateCreds(t, mod, reqBytes)

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	expectValidateResp(t, resp, &auth.ValidateCredResp{
		Status: int32(daos.InvalidInput),
	})
}

func TestSrvSecurityModule_ValidateCred_NoVerifier(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, insecureTransportConfig())
	reqBytes := getMarshaledValidateCredReq(t, &auth.Token{
		Flavor: auth.Flavor_AUTH_NONE,
		Data:   []byte{byte(1), byte(2)},
	}, nil)

	resp, err := callValidateCreds(t, mod, reqBytes)

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	expectValidateResp(t, resp, &auth.ValidateCredResp{
		Status: int32(daos.InvalidInput),
	})
}

func getValidToken(t *testing.T) *auth.Token {
	tokenData := &auth.Sys{
		Stamp: uint64(time.Now().Unix()),
		User:  "gooduser@",
		Group: "goodgroup@",
	}
	return &auth.Token{
		Flavor: auth.Flavor_AUTH_SYS,
		Data:   marshal(t, tokenData),
	}
}

func getVerifierForToken(t *testing.T, token *auth.Token, key crypto.PublicKey) *auth.Token {
	verifier, err := auth.VerifierFromToken(key, token)
	if err != nil {
		t.Fatalf("Couldn't get verifier: %v", err)
	}

	return &auth.Token{
		Flavor: auth.Flavor_AUTH_SYS,
		Data:   verifier,
	}
}

func TestSrvSecurityModule_ValidateCred_Insecure_OK(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, insecureTransportConfig())

	token := getValidToken(t)
	reqBytes := getMarshaledValidateCredReq(t, token, getVerifierForToken(t, token, nil))

	resp, err := callValidateCreds(t, mod, reqBytes)

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	expectValidateResp(t, resp, &auth.ValidateCredResp{
		Token: token,
	})
}

func TestSrvSecurityModule_ValidateCred_Insecure_BadVerifier(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, insecureTransportConfig())

	token := getValidToken(t)
	reqBytes := getMarshaledValidateCredReq(t, token, &auth.Token{Data: []byte{0x1}}) // junk verifier

	resp, err := callValidateCreds(t, mod, reqBytes)

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	expectValidateResp(t, resp, &auth.ValidateCredResp{
		Status: int32(daos.NoPermission),
	})
}

func generateTestCert(t *testing.T, dir string) crypto.PrivateKey {
	key, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		t.Fatalf("Failed to generate private key: %v", err)
	}

	cert := x509.Certificate{
		SerialNumber:          big.NewInt(1),
		IsCA:                  true,
		KeyUsage:              x509.KeyUsageDigitalSignature | x509.KeyUsageCertSign,
		BasicConstraintsValid: true,
	}

	bytes, err := x509.CreateCertificate(rand.Reader, &cert, &cert, &key.PublicKey, key)
	if err != nil {
		t.Fatalf("Failed to create certificate: %v", err)
	}

	path := filepath.Join(dir, "test.crt")
	f, err := os.Create(path)
	if err != nil {
		t.Fatalf("Failed to open cert file for writing: %v", err)
	}
	defer f.Close()

	if err := pem.Encode(f, &pem.Block{Type: "CERTIFICATE", Bytes: bytes}); err != nil {
		t.Fatalf("Failed to write cert file: %v", err)
	}

	return key
}

func TestSrvSecurityModule_ValidateCred_Secure_OK(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	tmpDir, tmpCleanup := test.CreateTestDir(t)
	defer tmpCleanup()

	key := generateTestCert(t, tmpDir)

	mod := NewSecurityModule(log, secureTransportConfig(tmpDir))
	token := getValidToken(t)

	reqBytes := getMarshaledValidateCredReq(t, token, getVerifierForToken(t, token, key))

	resp, err := callValidateCreds(t, mod, reqBytes)

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	expectValidateResp(t, resp, &auth.ValidateCredResp{
		Token: token,
	})
}

func TestSrvSecurityModule_ValidateCred_Secure_LoadingCertFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, secureTransportConfig("some/fake/path"))
	token := getValidToken(t)

	reqBytes := getMarshaledValidateCredReq(t, token, getVerifierForToken(t, token, nil))

	resp, err := callValidateCreds(t, mod, reqBytes)

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	expectValidateResp(t, resp, &auth.ValidateCredResp{
		Status: int32(daos.NoCert),
	})
}

func TestSrvSecurityModule_ValidateCred_Secure_BadVerifier(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	tmpDir, tmpCleanup := test.CreateTestDir(t)
	defer tmpCleanup()

	_ = generateTestCert(t, tmpDir)

	mod := NewSecurityModule(log, secureTransportConfig(tmpDir))
	token := getValidToken(t)

	// unsigned hash instead of signed by cert
	reqBytes := getMarshaledValidateCredReq(t, token, getVerifierForToken(t, token, nil))

	resp, err := callValidateCreds(t, mod, reqBytes)

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	expectValidateResp(t, resp, &auth.ValidateCredResp{
		Status: int32(daos.NoPermission),
	})
}

// --- Node cert validation tests ---

type testCertChain struct {
	caCert     *x509.Certificate
	caKey      *ecdsa.PrivateKey
	poolCACert *x509.Certificate
	poolCAKey  *ecdsa.PrivateKey
	nodeCert   *x509.Certificate
	nodeKey    *ecdsa.PrivateKey
	caCertPEM  []byte
	poolCAPEM  []byte
	nodePEM    []byte
}

func generateTestCertChain(t *testing.T) *testCertChain {
	t.Helper()
	tc := &testCertChain{}

	// DAOS CA (root)
	tc.caKey, _ = ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	serial, _ := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	caTemplate := &x509.Certificate{
		SerialNumber:          serial,
		Subject:               pkix.Name{CommonName: "Test DAOS CA"},
		NotBefore:             time.Now().Add(-time.Minute),
		NotAfter:              time.Now().Add(time.Hour),
		KeyUsage:              x509.KeyUsageCertSign | x509.KeyUsageCRLSign,
		BasicConstraintsValid: true,
		IsCA:                  true,
	}
	caDER, _ := x509.CreateCertificate(rand.Reader, caTemplate, caTemplate, &tc.caKey.PublicKey, tc.caKey)
	tc.caCert, _ = x509.ParseCertificate(caDER)
	tc.caCertPEM = pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: caDER})

	// Pool CA (intermediate)
	tc.poolCAKey, _ = ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	serial, _ = rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	poolCATemplate := &x509.Certificate{
		SerialNumber:          serial,
		Subject:               pkix.Name{CommonName: "Test Pool CA"},
		NotBefore:             time.Now().Add(-time.Minute),
		NotAfter:              time.Now().Add(time.Hour),
		KeyUsage:              x509.KeyUsageCertSign | x509.KeyUsageCRLSign,
		BasicConstraintsValid: true,
		IsCA:                  true,
		MaxPathLen:            0,
		MaxPathLenZero:        true,
	}
	poolCADER, _ := x509.CreateCertificate(rand.Reader, poolCATemplate, tc.caCert, &tc.poolCAKey.PublicKey, tc.caKey)
	tc.poolCACert, _ = x509.ParseCertificate(poolCADER)
	tc.poolCAPEM = pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: poolCADER})

	// Node cert
	tc.nodeKey, _ = ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	serial, _ = rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	nodeTemplate := &x509.Certificate{
		SerialNumber: serial,
		Subject:      pkix.Name{CommonName: security.CertCNPrefixNode + "testnode"},
		NotBefore:    time.Now().Add(-time.Minute),
		NotAfter:     time.Now().Add(time.Hour),
		KeyUsage:     x509.KeyUsageDigitalSignature,
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth},
	}
	nodeDER, _ := x509.CreateCertificate(rand.Reader, nodeTemplate, tc.poolCACert, &tc.nodeKey.PublicKey, tc.poolCAKey)
	tc.nodeCert, _ = x509.ParseCertificate(nodeDER)
	tc.nodePEM = pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: nodeDER})

	return tc
}

func makePoP(t *testing.T, key *ecdsa.PrivateKey, poolUUID, handleUUID []byte) (payload, sig []byte) {
	t.Helper()
	payload = make([]byte, popPayloadLen)
	copy(payload[0:16], poolUUID)
	copy(payload[16:32], handleUUID)
	binary.BigEndian.PutUint64(payload[32:40], uint64(time.Now().Unix()))

	h := sha512.Sum384(payload)
	var err error
	sig, err = ecdsa.SignASN1(rand.Reader, key, h[:])
	if err != nil {
		t.Fatalf("sign PoP: %v", err)
	}
	return payload, sig
}

func writeCAToDir(t *testing.T, dir string, caPEM []byte) string {
	t.Helper()
	path := filepath.Join(dir, "daosCA.crt")
	if err := os.WriteFile(path, caPEM, 0644); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestSrvSecurityModule_ValidateNodeCert(t *testing.T) {
	chain := generateTestCertChain(t)

	poolUUID := make([]byte, 16)
	handleUUID := make([]byte, 16)
	rand.Read(poolUUID)
	rand.Read(handleUUID)

	validPayload, validSig := makePoP(t, chain.nodeKey, poolUUID, handleUUID)

	stalePayload := make([]byte, popPayloadLen)
	copy(stalePayload[0:16], poolUUID)
	copy(stalePayload[16:32], handleUUID)
	binary.BigEndian.PutUint64(stalePayload[32:40], uint64(time.Now().Add(-10*time.Minute).Unix()))
	staleHash := sha512.Sum384(stalePayload)
	staleSig, _ := ecdsa.SignASN1(rand.Reader, chain.nodeKey, staleHash[:])

	rogueKey, _ := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	rogueSerial, _ := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	rogueTmpl := &x509.Certificate{
		SerialNumber: rogueSerial,
		Subject:      pkix.Name{CommonName: "rogue"},
		NotBefore:    time.Now().Add(-time.Minute),
		NotAfter:     time.Now().Add(time.Hour),
		KeyUsage:     x509.KeyUsageDigitalSignature,
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth},
	}
	rogueDER, _ := x509.CreateCertificate(rand.Reader, rogueTmpl, rogueTmpl, &rogueKey.PublicKey, rogueKey)
	roguePEM := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: rogueDER})

	for name, tc := range map[string]struct {
		nodePEM   []byte
		poolCAPEM []byte
		payload   []byte
		pop       []byte
		expStatus daos.Status
	}{
		"valid chain and PoP": {
			nodePEM:   chain.nodePEM,
			poolCAPEM: chain.poolCAPEM,
			payload:   validPayload,
			pop:       validSig,
			expStatus: 0,
		},
		"self-signed node cert": {
			nodePEM:   roguePEM,
			poolCAPEM: chain.poolCAPEM,
			payload:   make([]byte, popPayloadLen),
			pop:       []byte{1, 2, 3},
			expStatus: daos.BadCert,
		},
		"bad PoP signature": {
			nodePEM:   chain.nodePEM,
			poolCAPEM: chain.poolCAPEM,
			payload:   validPayload,
			pop:       []byte{0xDE, 0xAD},
			expStatus: daos.NoPermission,
		},
		"expired timestamp": {
			nodePEM:   chain.nodePEM,
			poolCAPEM: chain.poolCAPEM,
			payload:   stalePayload,
			pop:       staleSig,
			expStatus: daos.NoPermission,
		},
		"bad payload length": {
			nodePEM:   chain.nodePEM,
			poolCAPEM: chain.poolCAPEM,
			payload:   []byte{1, 2, 3},
			pop:       []byte{4, 5, 6},
			expStatus: daos.InvalidInput,
		},
	} {
		t.Run(name, func(t *testing.T) {
			parent := test.MustLogContext(t)
			log := logging.FromContext(parent)

			tmpDir, cleanup := test.CreateTestDir(t)
			defer cleanup()

			caPath := writeCAToDir(t, tmpDir, chain.caCertPEM)

			mod := NewSecurityModule(log, &security.TransportConfig{
				CertificateConfig: security.CertificateConfig{
					CARootPath: caPath,
				},
			})

			req := &auth.ValidateNodeCertReq{
				PoolCa:          tc.poolCAPEM,
				NodeCert:        tc.nodePEM,
				NodeCertPop:     tc.pop,
				NodeCertPayload: tc.payload,
				PoolId:          uuid.Must(uuid.FromBytes(poolUUID)).String(),
			}
			reqBytes := marshal(t, req)

			respBytes, err := mod.HandleCall(test.Context(t), nil, daos.MethodValidateNodeCert, reqBytes)
			if err != nil {
				t.Fatalf("HandleCall error: %v", err)
			}

			resp := &auth.ValidateNodeCertResp{}
			if err := proto.Unmarshal(respBytes, resp); err != nil {
				t.Fatalf("unmarshal response: %v", err)
			}

			if daos.Status(resp.Status) != tc.expStatus {
				t.Errorf("expected status %v, got %v", tc.expStatus, daos.Status(resp.Status))
			}
		})
	}
}

func TestSrvSecurityModule_ValidateNodeCert_Watermarks(t *testing.T) {
	chain := generateTestCertChain(t)

	poolUUID := make([]byte, 16)
	rand.Read(poolUUID)

	// The test chain's node cert was issued with NotBefore = now - 1m.
	nodeNotBefore := chain.nodeCert.NotBefore
	nodeCN := chain.nodeCert.Subject.CommonName

	for name, tc := range map[string]struct {
		watermarks map[string]string // CN → RFC3339 (key "__raw__" stuffs a raw blob)
		expStatus  daos.Status
	}{
		"no watermarks set": {
			watermarks: nil,
			expStatus:  0,
		},
		"watermark for unrelated CN is ignored": {
			watermarks: map[string]string{
				"node:other-node": nodeNotBefore.Add(time.Hour).UTC().Format(time.RFC3339),
			},
			expStatus: 0,
		},
		"watermark earlier than cert NotBefore allows cert": {
			watermarks: map[string]string{
				nodeCN: nodeNotBefore.Add(-time.Hour).UTC().Format(time.RFC3339),
			},
			expStatus: 0,
		},
		"watermark equal to cert NotBefore allows cert": {
			// Comparison is strictly less-than: equal is valid.
			watermarks: map[string]string{
				nodeCN: nodeNotBefore.UTC().Format(time.RFC3339),
			},
			expStatus: 0,
		},
		"watermark after cert NotBefore revokes cert": {
			watermarks: map[string]string{
				nodeCN: nodeNotBefore.Add(time.Hour).UTC().Format(time.RFC3339),
			},
			expStatus: daos.BadCert,
		},
		"malformed watermarks blob rejects cert": {
			watermarks: map[string]string{"__raw__": "not-json"},
			expStatus:  daos.BadCert,
		},
		"bad timestamp in blob rejects cert": {
			watermarks: map[string]string{nodeCN: "not-a-date"},
			expStatus:  daos.BadCert,
		},
	} {
		t.Run(name, func(t *testing.T) {
			parent := test.MustLogContext(t)
			log := logging.FromContext(parent)

			tmpDir, cleanup := test.CreateTestDir(t)
			defer cleanup()

			caPath := writeCAToDir(t, tmpDir, chain.caCertPEM)

			mod := NewSecurityModule(log, &security.TransportConfig{
				CertificateConfig: security.CertificateConfig{
					CARootPath: caPath,
				},
			})

			var watermarksBlob []byte
			if raw, ok := tc.watermarks["__raw__"]; ok {
				watermarksBlob = []byte(raw)
			} else if len(tc.watermarks) > 0 {
				blob, err := json.Marshal(tc.watermarks)
				if err != nil {
					t.Fatal(err)
				}
				watermarksBlob = blob
			}

			// Use a fresh handle UUID per sub-test to avoid the
			// replay cache used by the replay test.
			perCaseHandle := make([]byte, 16)
			rand.Read(perCaseHandle)
			perCasePayload, perCaseSig := makePoP(t, chain.nodeKey, poolUUID, perCaseHandle)

			req := &auth.ValidateNodeCertReq{
				PoolCa:          chain.poolCAPEM,
				NodeCert:        chain.nodePEM,
				NodeCertPop:     perCaseSig,
				NodeCertPayload: perCasePayload,
				PoolId:          uuid.Must(uuid.FromBytes(poolUUID)).String(),
				CertWatermarks:  watermarksBlob,
			}
			reqBytes := marshal(t, req)

			respBytes, err := mod.HandleCall(test.Context(t), nil, daos.MethodValidateNodeCert, reqBytes)
			if err != nil {
				t.Fatalf("HandleCall error: %v", err)
			}

			resp := &auth.ValidateNodeCertResp{}
			if err := proto.Unmarshal(respBytes, resp); err != nil {
				t.Fatalf("unmarshal response: %v", err)
			}

			if daos.Status(resp.Status) != tc.expStatus {
				t.Errorf("expected status %v, got %v (detail=%q)",
					tc.expStatus, daos.Status(resp.Status), resp.Detail)
			}
		})
	}
}

func TestSrvSecurityModule_ValidateNodeCert_Replay(t *testing.T) {
	chain := generateTestCertChain(t)

	poolUUID := make([]byte, 16)
	handleUUID := make([]byte, 16)
	rand.Read(poolUUID)
	rand.Read(handleUUID)

	payload, sig := makePoP(t, chain.nodeKey, poolUUID, handleUUID)

	parent := test.MustLogContext(t)
	log := logging.FromContext(parent)

	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	caPath := writeCAToDir(t, tmpDir, chain.caCertPEM)

	mod := NewSecurityModule(log, &security.TransportConfig{
		CertificateConfig: security.CertificateConfig{
			CARootPath: caPath,
		},
	})

	req := &auth.ValidateNodeCertReq{
		PoolCa:          chain.poolCAPEM,
		NodeCert:        chain.nodePEM,
		NodeCertPop:     sig,
		NodeCertPayload: payload,
		PoolId:          uuid.Must(uuid.FromBytes(poolUUID)).String(),
	}
	reqBytes := marshal(t, req)

	// First call should succeed
	respBytes, err := mod.HandleCall(test.Context(t), nil, daos.MethodValidateNodeCert, reqBytes)
	if err != nil {
		t.Fatalf("first call error: %v", err)
	}
	resp := &auth.ValidateNodeCertResp{}
	if err := proto.Unmarshal(respBytes, resp); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if resp.Status != 0 {
		t.Fatalf("first call: expected success, got status %d", resp.Status)
	}

	// Replay with same handle UUID should be rejected
	respBytes, err = mod.HandleCall(test.Context(t), nil, daos.MethodValidateNodeCert, reqBytes)
	if err != nil {
		t.Fatalf("replay call error: %v", err)
	}
	resp = &auth.ValidateNodeCertResp{}
	if err := proto.Unmarshal(respBytes, resp); err != nil {
		t.Fatalf("unmarshal: %v", err)
	}
	if daos.Status(resp.Status) != daos.NoPermission {
		t.Errorf("replay: expected NoPermission, got %v", daos.Status(resp.Status))
	}
}
