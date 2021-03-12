//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// +build !go1.15

package security

import "crypto/tls"

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
	}
}

func clientTLSConfig(cfg *TransportConfig) *tls.Config {
	return &tls.Config{
		ServerName:               cfg.ServerName,
		Certificates:             []tls.Certificate{*cfg.tlsKeypair},
		RootCAs:                  cfg.caPool,
		MinVersion:               tls.VersionTLS12,
		MaxVersion:               tls.VersionTLS12,
		PreferServerCipherSuites: true,
		CipherSuites: []uint16{
			tls.TLS_RSA_WITH_AES_256_GCM_SHA384,
		},
	}
}
