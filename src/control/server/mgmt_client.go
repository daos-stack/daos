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
package server

import (
	"fmt"
	"net"
	"time"

	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"google.golang.org/grpc"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
)

const (
	retryDelay = 3 * time.Second
)

type (
	mgmtSvcClientCfg struct {
		AccessPoints    []string
		ControlAddr     *net.TCPAddr
		TransportConfig *security.TransportConfig
	}
	mgmtSvcClient struct {
		log logging.Logger
		cfg mgmtSvcClientCfg
	}
)

func newMgmtSvcClient(ctx context.Context, log logging.Logger, cfg mgmtSvcClientCfg) *mgmtSvcClient {
	return &mgmtSvcClient{
		log: log,
		cfg: cfg,
	}
}

func (msc *mgmtSvcClient) withConnection(ctx context.Context, fn func(context.Context, string, mgmtpb.MgmtSvcClient) error) error {
	ap, err := msc.LeaderAddress()
	if err != nil {
		return err
	}

	var opts []grpc.DialOption
	authDialOption, err := security.DialOptionForTransportConfig(msc.cfg.TransportConfig)
	if err != nil {
		return errors.Wrap(err, "Failed to determine dial option from TransportConfig")
	}

	// Setup Dial Options that will always be included.
	opts = append(opts, grpc.WithBlock(), grpc.WithBackoffMaxDelay(retryDelay),
		grpc.WithDefaultCallOptions(grpc.FailFast(false)), authDialOption)
	conn, err := grpc.DialContext(ctx, ap, opts...)
	if err != nil {
		return errors.Wrapf(err, "dial %s", ap)
	}
	defer conn.Close()

	return fn(ctx, ap, mgmtpb.NewMgmtSvcClient(conn))
}

func (msc *mgmtSvcClient) LeaderAddress() (string, error) {
	if len(msc.cfg.AccessPoints) == 0 {
		return "", errors.New("no access points defined")
	}

	// TODO: Develop algorithm for determining current leader.
	// For now, just choose the first address.
	return msc.cfg.AccessPoints[0], nil
}

func (msc *mgmtSvcClient) Join(ctx context.Context, req *mgmtpb.JoinReq) (resp *mgmtpb.JoinResp, joinErr error) {
	joinErr = msc.withConnection(ctx, func(ctx context.Context, ap string, pbClient mgmtpb.MgmtSvcClient) error {
		prefix := fmt.Sprintf("join(%s, %+v)", ap, *req)
		msc.log.Debugf(prefix + " begin")
		defer msc.log.Debugf(prefix + " end")

		if req.Addr == "" {
			req.Addr = msc.cfg.ControlAddr.String()
		}

		for {
			select {
			case <-ctx.Done():
				return errors.Wrap(ctx.Err(), prefix)
			default:
			}

			resp, err := pbClient.Join(ctx, req)
			if err != nil {
				msc.log.Debugf(prefix+": %v", err)
			} else {
				// TODO: Stop retrying upon certain errors (e.g., "not
				// MS", "rank unavailable", and "excluded").
				if resp.Status != 0 {
					msc.log.Debugf(prefix+": %d", resp.Status)
				} else {
					return nil
				}
			}

			// Delay next retry.
			delayCtx, cancelDelayCtx := context.WithTimeout(ctx, retryDelay)
			select {
			case <-delayCtx.Done():
			}
			cancelDelayCtx()
		}
	})

	return
}
