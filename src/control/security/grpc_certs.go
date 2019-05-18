package security

import (
	"crypto/tls"
	"crypto/x509"
	"fmt"

	"google.golang.org/grpc/credentials"
)

func LoadServerCreds(caRootPath, certPath, keyPath string) (credentials.TransportCredentials, error) {

	caPEM, err := loadPEMData(caRootPath)
	if err != nil {
		return nil, fmt.Errorf("could not load caRoot: %s", err)
	}

	certPEM, err := loadPEMData(certPath)
	if err != nil {
		return nil, fmt.Errorf("could not load cert: %s", err)
	}

	keyPEM, err := loadPEMData(keyPath)
	if err != nil {
		return nil, fmt.Errorf("could not load key: %s, err")
	}

	certificate, err := tls.X509KeyPair(certPEM, keyPEM)
	if err != nil {
		return nil, fmt.Errorf("could not create X509KeyPair: %s", err)
	}

	certPool := x509.NewCertPool()

	added := certPool.AppendCertsFromPEM(caPEM)
	if !added {
		return nil, fmt.Errorf("unable to append caRoot to cert pool")
	}

	tlsConfig := tls.Config{
		ClientAuth:   tls.RequireAndVerifyClientCert,
		Certificates: []tls.Certificate{certificate},
		ClientCAs:    certPool,
	}
	creds := credentials.NewTLS(&tlsConfig)
	return creds, nil
}

func LoadClientCreds(caRootPath, certPath, keyPath, serverName string) (credentials.TransportCredentials, error) {

	caPEM, err := loadPEMData(caRootPath)
	if err != nil {
		return nil, fmt.Errorf("could not load caRoot: %s", err)
	}

	certPEM, err := loadPEMData(certPath)
	if err != nil {
		return nil, fmt.Errorf("could not load cert: %s", err)
	}

	keyPEM, err := loadPEMData(keyPath)
	if err != nil {
		return nil, fmt.Errorf("could not load key: %s, err")
	}

	certificate, err := tls.X509KeyPair(certPEM, keyPEM)
	if err != nil {
		return nil, fmt.Errorf("could not create X509KeyPair: %s", err)
	}

	certPool := x509.NewCertPool()

	added := certPool.AppendCertsFromPEM(caPEM)
	if !added {
		return nil, fmt.Errorf("unable to append caRoot to cert pool")
	}

	tlsConfig := tls.Config{
		ServerName:   serverName,
		Certificates: []tls.Certificate{certificate},
		RootCAs:      certPool,
	}
	creds := credentials.NewTLS(&tlsConfig)
	return creds, nil
}
