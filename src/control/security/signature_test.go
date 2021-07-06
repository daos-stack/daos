//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"bytes"
	"crypto"
	"crypto/ecdsa"
	"crypto/elliptic"
	crand "crypto/rand"
	"encoding/hex"
	"flag"
	"io/ioutil"
	mrand "math/rand"
	"os"
	"path/filepath"
	"testing"
)

var update = flag.Bool("update", false, "update .golden files")

func SeededSigner() *TokenSigner {
	//This should ensure we get the same signature every time for testing purposes.
	r := mrand.New(mrand.NewSource(1))
	return &TokenSigner{
		randPool: r,
	}
}

func SignTestSetup(t *testing.T) (rsaKey, ecdsaKey crypto.PrivateKey, source []byte) {
	keyPath := "testdata/certs/daosCA.key"
	if err := os.Chmod(keyPath, MaxKeyPerm); err != nil {
		t.Fatal(err)
	}
	rsaKey, err := LoadPrivateKey(keyPath)
	if err != nil {
		t.Fatalf("Unable to load private key for %s: %s", keyPath, err.Error())
	}
	ecdsaKey, err = ecdsa.GenerateKey(elliptic.P256(), crand.Reader)
	if err != nil {
		t.Fatal("Failed to generate ecdsa key for testing")
	}
	source, err = ioutil.ReadFile("testdata/certs/source.txt")
	if err != nil {
		t.Fatal("Failed to read in source file for Sign test.")
	}
	return
}

func TestSign(t *testing.T) {

	rsaKey, ecdsaKey, source := SignTestSetup(t)
	tokenSigner := SeededSigner()

	testCases := []struct {
		name string
		data []byte
		key  crypto.PrivateKey
	}{
		{"RSA", source, rsaKey},
		{"ImproperKey", source, ecdsaKey},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			golden := filepath.Join("testdata", "certs", tc.name+".golden")
			result, err := tokenSigner.Sign(tc.key, tc.data)
			if err != nil {
				result = []byte(err.Error())
			}
			if *update {
				err := ioutil.WriteFile(golden, result, 0644)
				if err != nil {
					t.Errorf("failed to update golden file %s", golden)
				}
			}
			expected, err := ioutil.ReadFile(golden)
			if err != nil {
				t.Errorf("unable to read golden file %s", golden)
			}

			if !bytes.Equal(result, expected) {
				t.Errorf("result %s; expected %s", hex.Dump(result), hex.Dump(expected))
			}
		})
	}
}

func VerifyTestSetup(t *testing.T) (rsaKey, ecdsaKey crypto.PublicKey, source []byte) {
	certPath := "testdata/certs/daosCA.crt"
	if err := os.Chmod(certPath, MaxCertPerm); err != nil {
		t.Fatal(err)
	}
	cert, err := LoadCertificate(certPath)
	if err != nil {
		t.Fatalf("Unable to load certificate for %s: %s", certPath, err.Error())
	}
	rsaKey = cert.PublicKey
	gen, err := ecdsa.GenerateKey(elliptic.P256(), crand.Reader)
	if err != nil {
		t.Fatal("Failed to generate ecdsa key for testing")
	}
	ecdsaKey = gen.Public()
	source, err = ioutil.ReadFile("testdata/certs/source.txt")
	if err != nil {
		t.Fatal("Failed to read in source file for Sign test.")
	}
	return
}

func TestVerify(t *testing.T) {
	rsaKey, ecdsaKey, source := VerifyTestSetup(t)
	tokenSigner := DefaultTokenSigner()

	testCases := []struct {
		name string
		data []byte
		key  crypto.PublicKey
	}{
		{"RSA", source, rsaKey},
		{"ImproperKey", source, ecdsaKey},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			golden := filepath.Join("testdata", "certs", tc.name+".golden")
			expected, err := ioutil.ReadFile(golden)
			if err != nil {
				t.Errorf("unable to read golden file %s", golden)
			}

			err = tokenSigner.Verify(tc.key, tc.data, expected)
			if err != nil {
				if !bytes.Equal(expected, []byte(err.Error())) {
					t.Errorf("result %s; expected %s", err.Error(), expected)
				}
			}
		})
	}
}
