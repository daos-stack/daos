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
	"crypto/tls"
	"crypto/x509"
	"fmt"

	"github.com/pkg/errors"
	"google.golang.org/grpc/credentials"
)

func LoadServerCreds(caRootPath, certPath, keyPath string) (credentials.TransportCredentials, error) {

	caPEM, err := LoadPEMData(caRootPath, SafeCertPerm)
	if err != nil {
		return nil, errors.Wrapf(err, "could not load caRoot")
	}

	certPEM, err := LoadPEMData(certPath, SafeCertPerm)
	if err != nil {
		return nil, errors.Wrapf(err, "could not load cert")
	}

	keyPEM, err := LoadPEMData(keyPath, SafeKeyPerm)
	if err != nil {
		return nil, errors.Wrapf(err, "could not load key")
	}

	certificate, err := tls.X509KeyPair(certPEM, keyPEM)
	if err != nil {
		return nil, errors.Wrapf(err, "could not create X509KeyPair")
	}

	certPool := x509.NewCertPool()

	added := certPool.AppendCertsFromPEM(caPEM)
	if !added {
		return nil, errors.Wrapf(err, "unable to append caRoot to cert pool")
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

	caPEM, err := LoadPEMData(caRootPath, SafeCertPerm)
	if err != nil {
		return nil, errors.Wrapf(err, "could not load caRoot")
	}

	certPEM, err := LoadPEMData(certPath, SafeCertPerm)
	if err != nil {
		return nil, errors.Wrapf(err, "could not load cert")
	}

	keyPEM, err := LoadPEMData(keyPath, SafeKeyPerm)
	if err != nil {
		return nil, errors.Wrapf(err, "could not load key")
	}

	certificate, err := tls.X509KeyPair(certPEM, keyPEM)
	if err != nil {
		return nil, errors.Wrapf(err, "could not create X509KeyPair")
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
