//
// (C) Copyright 2019-2022 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"crypto"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
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
	sectest "github.com/daos-stack/daos/src/control/security/test"
)

// testMachineName matches the node-scoped CN baked into the test cert
// chain (CertCNPrefixNode + testMachineName). Tests that exercise the
// happy path must pass this as the credential MachineName so the
// CN-to-machine cross-check passes.
const testMachineName = "testnode"

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
//
// Validation details (chain, CN policy, watermarks, payload binding,
// signatures) are covered by the security package's NodeCertPresentation
// tests. These tests cover the dRPC adapter: status mapping and CA loading.

type nodeCertHarness struct {
	mod      *SecurityModule
	poolUUID uuid.UUID
	poolCA   []byte
	certDir  string
	log      logging.Logger
}

func newNodeCertHarness(t *testing.T, log logging.Logger, maxSkew time.Duration) *nodeCertHarness {
	t.Helper()

	tmpDir, cleanup := test.CreateTestDir(t)
	t.Cleanup(cleanup)

	poolUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")
	rootPEM, rootKey := sectest.NewCA(t, "Test DAOS CA", nil, nil)
	poolCA, err := security.GeneratePoolCA(poolUUID, sectest.ParseCert(t, rootPEM), rootKey, 0)
	if err != nil {
		t.Fatal(err)
	}

	h := &nodeCertHarness{
		poolUUID: poolUUID,
		poolCA:   poolCA.CertPEM,
		certDir:  tmpDir,
		log:      log,
	}

	// Node cert + key on disk so presentations can be built through the
	// public agent-side path (NodeCertLoader.CertAndPoP).
	leafPEM, leafKeyPEM := sectest.NewLeaf(t, security.PoolCertCNPrefixNode+testMachineName,
		poolCA.Cert, poolCA.Key, time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
	certPath, keyPath := security.NodeCertPaths(tmpDir, poolUUID)
	sectest.WriteCertFiles(t, certPath, keyPath, leafPEM, leafKeyPEM)

	caPath := filepath.Join(tmpDir, "daosCA.crt")
	if err := os.WriteFile(caPath, rootPEM, 0644); err != nil {
		t.Fatal(err)
	}

	tc := &security.TransportConfig{}
	tc.CARootPath = caPath
	tc.CertMaxClockSkew = maxSkew
	h.mod = NewSecurityModule(log, tc)

	return h
}

// validReq builds a fully valid presentation via the public CertAndPoP path.
func (h *nodeCertHarness) validReq(t *testing.T, handle uuid.UUID) *auth.ValidateNodeCertReq {
	t.Helper()

	loader := security.NewNodeCertLoader(h.certDir)
	cert, pop, payload, err := loader.CertAndPoP(h.log, h.poolUUID, handle, testMachineName)
	if err != nil {
		t.Fatalf("CertAndPoP: %v", err)
	}
	return &auth.ValidateNodeCertReq{
		PoolCa:      h.poolCA,
		NodeCert:    cert.PEM,
		PopSig:      pop,
		PopPayload:  payload,
		PoolUuid:    h.poolUUID[:],
		MachineName: testMachineName,
	}
}

func (h *nodeCertHarness) callValidate(t *testing.T, req *auth.ValidateNodeCertReq) *auth.ValidateNodeCertResp {
	t.Helper()

	body, err := proto.Marshal(req)
	if err != nil {
		t.Fatal(err)
	}
	respBytes, err := h.mod.processValidateNodeCert(body)
	if err != nil {
		t.Fatalf("processValidateNodeCert: %v", err)
	}
	resp := &auth.ValidateNodeCertResp{}
	if err := proto.Unmarshal(respBytes, resp); err != nil {
		t.Fatal(err)
	}
	return resp
}

func TestSrvSecurityModule_ValidateNodeCert_StatusMapping(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	h := newNodeCertHarness(t, log, security.DefaultCertMaxClockSkew)
	handle := uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")

	for name, tc := range map[string]struct {
		mutate    func(*auth.ValidateNodeCertReq)
		expStatus daos.Status
	}{
		"valid": {
			mutate:    func(r *auth.ValidateNodeCertReq) {},
			expStatus: daos.Status(0),
		},
		"garbage cert": {
			mutate:    func(r *auth.ValidateNodeCertReq) { r.NodeCert = []byte("not a PEM") },
			expStatus: daos.BadCert,
		},
		"machine name mismatch": {
			mutate:    func(r *auth.ValidateNodeCertReq) { r.MachineName = "someone-else" },
			expStatus: daos.BadCert,
		},
		"tampered payload": {
			mutate: func(r *auth.ValidateNodeCertReq) {
				r.PopPayload = r.PopPayload[:8]
			},
			expStatus: daos.NoPermission,
		},
		"payload bound to other pool": {
			mutate: func(r *auth.ValidateNodeCertReq) {
				other := uuid.MustParse("99999999-9999-9999-9999-999999999999")
				r.PoolUuid = other[:]
			},
			expStatus: daos.NoPermission,
		},
		"unreadable watermarks": {
			mutate: func(r *auth.ValidateNodeCertReq) {
				r.CertWatermarks = []byte("{not json")
			},
			expStatus: daos.IOError,
		},
		"revoked by watermark": {
			mutate: func(r *auth.ValidateNodeCertReq) {
				wm, err := security.EncodeCertWatermarks(security.CertWatermarks{
					security.PoolCertCNPrefixNode + testMachineName: time.Now().Add(time.Minute),
				})
				if err != nil {
					t.Fatal(err)
				}
				r.CertWatermarks = wm
			},
			expStatus: daos.BadCert,
		},
	} {
		t.Run(name, func(t *testing.T) {
			req := h.validReq(t, handle)
			tc.mutate(req)
			resp := h.callValidate(t, req)
			test.AssertEqual(t, resp.Status, int32(tc.expStatus), "status didn't match")
		})
	}
}

func TestSrvSecurityModule_ValidateNodeCert_Stale(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	// A 1ns skew budget makes any honestly-stamped PoP stale.
	h := newNodeCertHarness(t, log, time.Nanosecond)
	req := h.validReq(t, uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"))
	resp := h.callValidate(t, req)
	test.AssertEqual(t, resp.Status, int32(daos.NoPermission), "status didn't match")
}

func TestSrvSecurityModule_ValidateNodeCert_BadDAOSCA(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	h := newNodeCertHarness(t, log, security.DefaultCertMaxClockSkew)
	h.mod.config.CARootPath = "/nonexistent/daosCA.crt"
	resp := h.callValidate(t, h.validReq(t, uuid.New()))
	test.AssertEqual(t, resp.Status, int32(daos.NoCert), "status didn't match")
}

func TestSrvSecurityModule_ValidateNodeCert_BadBody(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	h := newNodeCertHarness(t, log, security.DefaultCertMaxClockSkew)
	if _, err := h.mod.processValidateNodeCert([]byte("junk that is not a proto")); err == nil {
		t.Fatal("expected error for unparsable body")
	}
}
