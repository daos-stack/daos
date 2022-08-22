//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build go1.15
// +build go1.15

package security

import (
	"crypto/tls"
	"crypto/x509"

	"github.com/pkg/errors"
)

// As of Go 1.15 default certificate validation does not allow certificates
// that only contain a CommonName with no SubjectAlternativeName. DAOS certificate
// usage is a one certificate per component type model not a certificate per host
// model. Because of this we do not generate certificates with SubjectAlternativeNames
// at those SANs must be able to be validated against the hosts hostname. To handle
// the case where this behavior is not possible to support Go 1.15 and later added
// the ability to define custom connection validation functions for TLS connections.
// To support the pre Go 1.15 behavior we define custom validation functions which still
// validate the certificate chain however they do not validate the SubjectAlternativeName.
// On the client side we still ensure the CommonName for the server is correct and
// validate the certificate chain.

func serverTLSConfig(cfg *TransportConfig) *tls.Config {
	return &tls.Config{
		ClientAuth:               tls.RequireAndVerifyClientCert,
		Certificates:             []tls.Certificate{*cfg.tlsKeypair},
		ClientCAs:                cfg.caPool,
		MinVersion:               tls.VersionTLS12,
		MaxVersion:               tls.VersionTLS12,
		PreferServerCipherSuites: true,
		CipherSuites: []uint16{
			tls.TLS_RSA_WITH_AES_256_GCM_SHA384,
		},
		VerifyConnection: func(cs tls.ConnectionState) error {
			opts := x509.VerifyOptions{
				Roots:         cfg.caPool,
				Intermediates: x509.NewCertPool(),
				KeyUsages:     []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth},
			}
			for _, cert := range cs.PeerCertificates[1:] {
				opts.Intermediates.AddCert(cert)
			}
			_, err := cs.PeerCertificates[0].Verify(opts)
			return err
		},
	}
}

const ServerCommonName = "server"

func clientTLSConfig(cfg *TransportConfig) *tls.Config {
	return &tls.Config{
		Certificates:             []tls.Certificate{*cfg.tlsKeypair},
		RootCAs:                  cfg.caPool,
		MinVersion:               tls.VersionTLS12,
		MaxVersion:               tls.VersionTLS12,
		PreferServerCipherSuites: true,
		CipherSuites: []uint16{
			tls.TLS_RSA_WITH_AES_256_GCM_SHA384,
		},
		// InsecureSkipVerify disables the default verifier and instead
		// uses our customer verifier which effectively does the same thing.
		InsecureSkipVerify: true,

		// The custom verification code will take the loaded CA pool and
		// based on the received peer certificate build the intermediate
		// certificate chain and verify the received certificate against
		// that chain. This is the same as the normal verification process
		// except it will not validate the hostname of the SubjectAlternativeName.
		// After the certificate is verified it will ensure the CommonName
		// of the received certificate is "server" to ensure we are
		// communicating with a DAOS server.
		VerifyConnection: func(cs tls.ConnectionState) error {
			opts := x509.VerifyOptions{
				Roots:         cfg.caPool,
				Intermediates: x509.NewCertPool(),
			}
			for _, cert := range cs.PeerCertificates[1:] {
				opts.Intermediates.AddCert(cert)
			}
			_, err := cs.PeerCertificates[0].Verify(opts)
			if err != nil {
				return err
			}
			if cs.PeerCertificates[0].Subject.CommonName != ServerCommonName {
				return errors.New("Server certificate does not identify as Server")
			}
			return nil
		},
	}
}
