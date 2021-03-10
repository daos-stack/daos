//
// (C) Copyright 2019-2021 Intel Corporation.
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

	"github.com/golang/protobuf/proto"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/security/auth"
)

func TestSrvSecurityModule_ID(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, nil)

	common.AssertEqual(t, mod.ID(), drpc.ModuleSecurity, "wrong drpc module")
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

func TestSrvSecurityModule_BadMethod(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, insecureTransportConfig())
	method, err := mod.ID().GetMethod(-1)
	if method != nil {
		t.Errorf("Expected no method to be returned, got %+v", method)
	}

	common.CmpErr(t, errors.New("invalid method -1 for module Security"), err)
}

func callValidateCreds(mod *SecurityModule, body []byte) ([]byte, error) {
	return mod.HandleCall(nil, drpc.MethodValidateCredentials, body)
}

func TestSrvSecurityModule_ValidateCred_InvalidReq(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, insecureTransportConfig())
	// Put garbage in the body
	resp, err := callValidateCreds(mod, []byte{byte(123), byte(90), byte(255)})

	if resp != nil {
		t.Errorf("Expected no response, got %+v", resp)
	}

	common.CmpErr(t, drpc.UnmarshalingPayloadFailure(), err)
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

	cmpOpts := common.DefaultCmpOpts()
	if diff := cmp.Diff(expResp, resp, cmpOpts...); diff != "" {
		t.Fatalf("(-want, +got)\n%s", diff)
	}
}

func TestSrvSecurityModule_ValidateCred_NoCred(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, insecureTransportConfig())
	reqBytes := marshal(t, &auth.ValidateCredReq{})

	resp, err := callValidateCreds(mod, reqBytes)

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	expectValidateResp(t, resp, &auth.ValidateCredResp{
		Status: int32(drpc.DaosInvalidInput),
	})
}

func TestSrvSecurityModule_ValidateCred_NoToken(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, insecureTransportConfig())
	reqBytes := getMarshaledValidateCredReq(t, nil, &auth.Token{
		Flavor: auth.Flavor_AUTH_NONE,
		Data:   []byte{byte(1), byte(2)},
	})

	resp, err := callValidateCreds(mod, reqBytes)

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	expectValidateResp(t, resp, &auth.ValidateCredResp{
		Status: int32(drpc.DaosInvalidInput),
	})
}

func TestSrvSecurityModule_ValidateCred_NoVerifier(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, insecureTransportConfig())
	reqBytes := getMarshaledValidateCredReq(t, &auth.Token{
		Flavor: auth.Flavor_AUTH_NONE,
		Data:   []byte{byte(1), byte(2)},
	}, nil)

	resp, err := callValidateCreds(mod, reqBytes)

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	expectValidateResp(t, resp, &auth.ValidateCredResp{
		Status: int32(drpc.DaosInvalidInput),
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
	defer common.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, insecureTransportConfig())

	token := getValidToken(t)
	reqBytes := getMarshaledValidateCredReq(t, token, getVerifierForToken(t, token, nil))

	resp, err := callValidateCreds(mod, reqBytes)

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	expectValidateResp(t, resp, &auth.ValidateCredResp{
		Token: token,
	})
}

func TestSrvSecurityModule_ValidateCred_Insecure_BadVerifier(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, insecureTransportConfig())

	token := getValidToken(t)
	reqBytes := getMarshaledValidateCredReq(t, token, &auth.Token{Data: []byte{0x1}}) // junk verifier

	resp, err := callValidateCreds(mod, reqBytes)

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	expectValidateResp(t, resp, &auth.ValidateCredResp{
		Status: int32(drpc.DaosNoPermission),
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
	defer common.ShowBufferOnFailure(t, buf)

	tmpDir, tmpCleanup := common.CreateTestDir(t)
	defer tmpCleanup()

	key := generateTestCert(t, tmpDir)

	mod := NewSecurityModule(log, secureTransportConfig(tmpDir))
	token := getValidToken(t)

	reqBytes := getMarshaledValidateCredReq(t, token, getVerifierForToken(t, token, key))

	resp, err := callValidateCreds(mod, reqBytes)

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	expectValidateResp(t, resp, &auth.ValidateCredResp{
		Token: token,
	})
}

func TestSrvSecurityModule_ValidateCred_Secure_LoadingCertFailed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	mod := NewSecurityModule(log, secureTransportConfig("some/fake/path"))
	token := getValidToken(t)

	reqBytes := getMarshaledValidateCredReq(t, token, getVerifierForToken(t, token, nil))

	resp, err := callValidateCreds(mod, reqBytes)

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	expectValidateResp(t, resp, &auth.ValidateCredResp{
		Status: int32(drpc.DaosBadPath),
	})
}

func TestSrvSecurityModule_ValidateCred_Secure_BadVerifier(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	tmpDir, tmpCleanup := common.CreateTestDir(t)
	defer tmpCleanup()

	_ = generateTestCert(t, tmpDir)

	mod := NewSecurityModule(log, secureTransportConfig(tmpDir))
	token := getValidToken(t)

	// unsigned hash instead of signed by cert
	reqBytes := getMarshaledValidateCredReq(t, token, getVerifierForToken(t, token, nil))

	resp, err := callValidateCreds(mod, reqBytes)

	if err != nil {
		t.Errorf("Expected no error, got: %v", err)
	}

	expectValidateResp(t, resp, &auth.ValidateCredResp{
		Status: int32(drpc.DaosNoPermission),
	})
}
