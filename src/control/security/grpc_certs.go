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
