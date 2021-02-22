//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"
)

func GetServerTransportCredentials(cfg *TransportConfig) (credentials.TransportCredentials, error) {
	if cfg == nil {
		return nil, errors.New("nil TransportConfig")
	}

	if cfg.tlsKeypair == nil || cfg.caPool == nil {
		err := cfg.PreLoadCertData()
		if err != nil {
			return nil, err
		}
	}

	creds := credentials.NewTLS(serverTLSConfig(cfg))
	return creds, nil
}

func GetClientTransportCredentials(cfg *TransportConfig) (credentials.TransportCredentials, error) {
	if cfg == nil {
		return nil, errors.New("nil TransportConfig")
	}

	if cfg.tlsKeypair == nil || cfg.caPool == nil {
		err := cfg.PreLoadCertData()
		if err != nil {
			return nil, err
		}
	}

	creds := credentials.NewTLS(clientTLSConfig(cfg))
	return creds, nil
}

func ServerOptionForTransportConfig(cfg *TransportConfig) (grpc.ServerOption, error) {
	if cfg == nil {
		return nil, errors.New("nil TransportConfig")
	}

	if cfg.AllowInsecure {
		return grpc.Creds(nil), nil
	}

	creds, err := GetServerTransportCredentials(cfg)
	if err != nil {
		return nil, err
	}

	return grpc.Creds(creds), nil
}

func DialOptionForTransportConfig(cfg *TransportConfig) (grpc.DialOption, error) {
	if cfg == nil {
		return nil, errors.New("nil TransportConfig")
	}

	if cfg.AllowInsecure {
		return grpc.WithInsecure(), nil
	}

	if cfg.ServerName == "" {
		return nil, errors.New("No ServerName set in TransportConfig")
	}

	creds, err := GetClientTransportCredentials(cfg)
	if err != nil {
		return nil, err
	}

	return grpc.WithTransportCredentials(creds), nil
}
