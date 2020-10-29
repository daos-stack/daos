//
// (C) Copyright 2019-2020 Intel Corporation.
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
	"bytes"
	"crypto"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"encoding/hex"
	"flag"
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"
)

var update = flag.Bool("update", false, "update .golden files")

func SignTestSetup(t *testing.T) (rsaKey, ecdsaKey crypto.PrivateKey, source []byte) {
	keyPath := "testdata/certs/daosCA.key"
	if err := os.Chmod(keyPath, MaxKeyPerm); err != nil {
		t.Fatal(err)
	}
	rsaKey, err := LoadPrivateKey(keyPath)
	if err != nil {
		t.Fatalf("Unable to load private key for %s: %s", keyPath, err.Error())
	}
	ecdsaKey, err = ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
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
	tokenSigner := DefaultTokenSigner()

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
	gen, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
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
