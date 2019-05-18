package security

import (
	"crypto"
	"crypto/rsa"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"io/ioutil"
	"os"
	"strings"
)

const (
	safePermissions os.FileMode = 0700
)

func loadPEMData(filePath string) ([]byte, error) {
	statInfo, err := os.Stat(filePath)
	if err == nil {
		if !statInfo.Mode().IsRegular() {
			return nil, fmt.Errorf("%s is not a regular file", filePath)
		}
		if statInfo.Mode() != safePermissions {
			return nil, fmt.Errorf("%s has insecure permissions set to %#o",
				filePath, safePermissions)
		}
	} else {
		return nil, err
	}

	fileContents, err := ioutil.ReadFile(filePath)
	if err != nil {
		return nil, err
	}
	return fileContents, nil
}
func loadPrivateKey(keyPath string) (crypto.PrivateKey, error) {

	pemData, err := loadPEMData(keyPath)

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
	return key, nil
}

func validateCertDirectory(certDir string) error {
	statInfo, err := os.Stat(certDir)
	if err == nil {
		if !statInfo.IsDir() {
			return fmt.Errorf("Path specified as the certificate directory (%s) is not a directory", certDir)
		}
		if statInfo.Mode() != safePermissions {
			return fmt.Errorf("Directory %s has insecure permissions please set to 0700", certDir)
		}
		return nil
	}
	return err
}

//func loadCertificate(certPath string) (*x509.Certificate, error) {
//
//	pemBlock, remainder := pem.Decode(fileContents)
//	if pemBlock == nil {
//		return nil, fmt.Errorf("Certificate %s must have at least one block", certPath)
//	}
//
//	if pemBlock.Type != "CERTIFICATE" {
//		return nil, fmt.Errorf("File %s does not contain a certificate", certPath)
//	}
//
//	return pemBlock, nil
//}

//LoadCertificates inspects the passed in direcotry to see if it contains the
// necessary certificates to sign the credentials. If the neccary files are
// not found it will return an error unless allowInsecure is true in which it
// will issue credentials but not sign them.
//func LoadCertificates(certDir string, allowInsecure bool) error {
//
//	m.allowInsecure = allowInsecure
//
//	err := validateCertDirectory(certDir)
//	if err != nil {
//		return err
//	}
//	// Start by loading the CA cert
//	filePath := filepath.Join(certDir, daosCACertName)
//	cert, err := loadCertificate(filePath)
//	if err != nil {
//		return nil, err
//	}
//
//	filePath = filepath.Join(certDir, agentCertName)
//	cert, err = loadCertificate(filePath)
//	if err != nil {
//		return nil, err
//	}
//
//	return nil
//}
