//
// (C) Copyright 2019-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"crypto"
	"crypto/rsa"
	"crypto/tls"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"os"
	"strings"

	"github.com/pkg/errors"
)

const (
	MaxUserOnlyKeyPerm os.FileMode = 0400
	MaxGroupKeyPerm    os.FileMode = 0440
	MaxCertPerm        os.FileMode = 0664
	MaxDirPerm         os.FileMode = 0750
)

type badPermsError struct {
	filename string
	perms    os.FileMode
	fixperms os.FileMode
}

func (e *badPermsError) Error() string {
	return fmt.Sprintf("%s has insecure permissions %#o (suggested: %#o)",
		e.filename, e.perms.Perm(), e.fixperms.Perm())
}

func checkMaxPermissions(filePath string, mode os.FileMode, modeMax os.FileMode) error {
	maxPermMask := (^modeMax.Perm()) & 0777
	maskedPermissions := maxPermMask & mode.Perm()

	if maskedPermissions != 0 {
		return &badPermsError{filePath, mode, modeMax}
	}
	return nil
}

func isInvalidCert(err error) bool {
	_, isCertErr := errors.Cause(err).(x509.CertificateInvalidError)

	return isCertErr
}

func loadCertWithCustomCA(caRootPath, certPath, keyPath string, maxKeyPerm os.FileMode) (*tls.Certificate, *x509.CertPool, error) {
	caPEM, err := LoadPEMData(caRootPath, MaxCertPerm)
	if err != nil {
		switch {
		case os.IsNotExist(err):
			return nil, nil, FaultMissingCertFile(caRootPath)
		case os.IsPermission(err):
			return nil, nil, FaultUnreadableCertFile(caRootPath)
		case isInvalidCert(err):
			return nil, nil, FaultInvalidCertFile(caRootPath, err)
		default:
			return nil, nil, errors.Wrapf(err, "could not load caRoot")
		}
	}

	certPEM, err := LoadPEMData(certPath, MaxCertPerm)
	if err != nil {
		switch {
		case os.IsNotExist(err):
			return nil, nil, FaultMissingCertFile(certPath)
		case os.IsPermission(err):
			return nil, nil, FaultUnreadableCertFile(certPath)
		case isInvalidCert(err):
			return nil, nil, FaultInvalidCertFile(certPath, err)
		default:
			return nil, nil, errors.Wrapf(err, "could not load cert")
		}
	}

	keyPEM, err := LoadPEMData(keyPath, maxKeyPerm)
	if err != nil {
		switch {
		case os.IsNotExist(err):
			return nil, nil, FaultMissingCertFile(keyPath)
		case os.IsPermission(err):
			return nil, nil, FaultUnreadableCertFile(keyPath)
		case isInvalidCert(err):
			return nil, nil, FaultInvalidCertFile(keyPath, err)
		default:
			return nil, nil, errors.Wrapf(err, "could not load key")
		}
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

// LoadPEMData handles security checking on the PEM file based on perms and
// returns the bytes in the PEM file
func LoadPEMData(filePath string, perms os.FileMode) ([]byte, error) {
	statInfo, err := os.Stat(filePath)
	if err != nil {
		return nil, err
	}

	if !statInfo.Mode().IsRegular() {
		return nil, fmt.Errorf("%s is not a regular file", filePath)
	}

	err = checkMaxPermissions(filePath, statInfo.Mode(), perms)
	if err != nil {
		return nil, err
	}

	fileContents, err := os.ReadFile(filePath)
	if err != nil {
		return nil, err
	}
	return fileContents, nil
}

// LoadCertificate loads the certificate specified at the given path into an
// x509 Certificate object
func LoadCertificate(certPath string) (*x509.Certificate, error) {
	pemData, err := LoadPEMData(certPath, MaxCertPerm)

	if err != nil {
		return nil, err
	}

	block, extra := pem.Decode(pemData)

	if block == nil {
		return nil, fmt.Errorf("%s does not contain PEM data", certPath)
	}

	if len(extra) != 0 {
		return nil, fmt.Errorf("Only one cert allowed per file")
	}

	cert, err := x509.ParseCertificate(block.Bytes)
	return cert, err
}

// LoadPrivateKey loads the private key specified at the given path into an
// crypto.PrivateKey interface compliant object.
func LoadPrivateKey(keyPath string) (crypto.PrivateKey, error) {
	pemData, err := LoadPEMData(keyPath, MaxUserOnlyKeyPerm)

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
	return nil, errors.Wrapf(err, "Invalid key data in PRIVATE KEY block")
}

// ValidateCertDirectory ensures the certificate directory has safe permissions
// set on it.
func ValidateCertDirectory(certDir string) error {
	statInfo, err := os.Stat(certDir)
	if err != nil {
		return err
	}
	if !statInfo.IsDir() {
		return fmt.Errorf("Certificate directory path (%s) is not a directory", certDir)
	}
	err = checkMaxPermissions(certDir, statInfo.Mode(), MaxDirPerm)
	if err != nil {
		return err
	}
	return nil
}
