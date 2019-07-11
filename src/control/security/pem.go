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
	"crypto"
	"crypto/rsa"
	"crypto/tls"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"io/ioutil"
	"os"
	"strings"

	"github.com/pkg/errors"
)

const (
	SafeKeyPerm  os.FileMode = 0400
	SafeCertPerm os.FileMode = 0664
	SafeDirPerm  os.FileMode = 0700
)

type insecureError struct {
	filename string
	perms    os.FileMode
}

func (e *insecureError) Error() string {
	return fmt.Sprintf("%s has insecure permissions set to %#o",
		e.filename, e.perms)
}

func loadCertWithCustomCA(caRootPath, certPath, keyPath string) (*tls.Certificate, *x509.CertPool, error) {
	caPEM, err := LoadPEMData(caRootPath, SafeCertPerm)
	if err != nil {
		return nil, nil, errors.Wrapf(err, "could not load caRoot")
	}

	certPEM, err := LoadPEMData(certPath, SafeCertPerm)
	if err != nil {
		return nil, nil, errors.Wrapf(err, "could not load cert")
	}

	keyPEM, err := LoadPEMData(keyPath, SafeKeyPerm)
	if err != nil {
		return nil, nil, errors.Wrapf(err, "could not load key")
	}

	certificate, err := tls.X509KeyPair(certPEM, keyPEM)
	if err != nil {
		return nil, nil, errors.Wrapf(err, "could not create X509KeyPair")
	}

	certPool := x509.NewCertPool()

	added := certPool.AppendCertsFromPEM(caPEM)
	if !added {
		return nil, nil, errors.Wrapf(err, "unable to append caRoot to cert pool")
	}

	return &certificate, certPool, nil
}

func LoadPEMData(filePath string, perms os.FileMode) ([]byte, error) {
	statInfo, err := os.Stat(filePath)
	if err != nil {
		return nil, err
	}

	if !statInfo.Mode().IsRegular() {
		return nil, fmt.Errorf("%s is not a regular file", filePath)
	}
	if statInfo.Mode().Perm() != perms {
		return nil, &insecureError{filePath, perms}
	}

	fileContents, err := ioutil.ReadFile(filePath)
	if err != nil {
		return nil, err
	}
	return fileContents, nil
}

func LoadPrivateKey(keyPath string) (crypto.PrivateKey, error) {

	pemData, err := LoadPEMData(keyPath, SafeKeyPerm)

	if err != nil {
		return nil, err
	}

	block, extra := pem.Decode(pemData)

	if block == nil {
		return nil, fmt.Errorf("%s does not contain PEM data", keyPath)
	}

	if len(extra) != 0 {
		return nil, fmt.Errorf("Only one key allowed per file")
	}

	/* covers multiple key types */
	if !strings.HasSuffix(block.Type, "PRIVATE KEY") {
		return nil, fmt.Errorf("PEM Block is not a Private Key")
	}

	// Based on parsePrivateKey in golang.org/src/crypto/tls/tls.go
	// Pre OpenSSL 1.0.0 default key packaging
	rsaKey, err := x509.ParsePKCS1PrivateKey(block.Bytes)
	if err == nil {
		return rsaKey, nil
	}

	// Post OpenSSL 1.0.0 default key packaging
	key, err := x509.ParsePKCS8PrivateKey(block.Bytes)
	if err == nil {
		switch key := key.(type) {
		// TODO: Support key types other than RSA
		case *rsa.PrivateKey:
			return key, nil
		default:
			return nil, fmt.Errorf("%s contains an unsupported private key type",
				keyPath)
		}
	}
	return nil, fmt.Errorf("Invalid key data in PRIVATE KEY block")
}

func ValidateCertDirectory(certDir string) error {
	statInfo, err := os.Stat(certDir)
	if err != nil {
		return err
	}
	if !statInfo.IsDir() {
		return fmt.Errorf("Certificate directory path (%s) is not a directory", certDir)
	}
	if statInfo.Mode().Perm() != SafeDirPerm {
		return &insecureError{certDir, SafeDirPerm}
	}
	return nil
}
