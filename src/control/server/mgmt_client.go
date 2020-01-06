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

// delayRetry delays next retry.
func (msc *mgmtSvcClient) delayRetry(ctx context.Context) {
	select {
	case <-ctx.Done(): // break early if the parent context is canceled
	case <-time.After(retryDelay): // otherwise, block until after the delay duration
	}
}

func (msc *mgmtSvcClient) withConnection(ctx context.Context, ap string,
	fn func(context.Context, mgmtpb.MgmtSvcClient) error) error {

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

	return fn(ctx, mgmtpb.NewMgmtSvcClient(conn))
}

func (msc *mgmtSvcClient) LeaderAddress() (string, error) {
	if len(msc.cfg.AccessPoints) == 0 {
		return "", errors.New("no access points defined")
	}

	// TODO: Develop algorithm for determining current leader.
	// For now, just choose the first address.
	return msc.cfg.AccessPoints[0], nil
}

func (msc *mgmtSvcClient) retryOnErr(err error, ctx context.Context, prefix string) bool {
	if err != nil {
		msc.log.Debugf("%s: %v", prefix, err)
		msc.delayRetry(ctx)
		return true
	}

	return false
}

func (msc *mgmtSvcClient) retryOnStatus(status int32, ctx context.Context, prefix string) bool {
	if status != 0 {
		msc.log.Debugf("%s: %d", prefix, status)
		msc.delayRetry(ctx)
		return true
	}

	return false
}

func (msc *mgmtSvcClient) Join(ctx context.Context, req *mgmtpb.JoinReq) (resp *mgmtpb.JoinResp, joinErr error) {
	ap, err := msc.LeaderAddress()
	if err != nil {
		return nil, err
	}

	joinErr = msc.withConnection(ctx, ap,
		func(ctx context.Context, pbClient mgmtpb.MgmtSvcClient) error {
			if req.Addr == "" {
				req.Addr = msc.cfg.ControlAddr.String()
			}

			prefix := fmt.Sprintf("join(%s, %+v)", ap, *req)
			msc.log.Debugf(prefix + " begin")
			defer msc.log.Debugf(prefix + " end")

			for {
				var err error

				select {
				case <-ctx.Done():
					return errors.Wrap(ctx.Err(), prefix)
				default:
				}

				resp, err = pbClient.Join(ctx, req)
				if msc.retryOnErr(err, ctx, prefix) {
					continue
				}
				if resp == nil {
					return errors.New("unexpected nil response status")
				}
				// TODO: Stop retrying upon certain errors (e.g., "not
				// MS", "rank unavailable", and "excluded").
				if msc.retryOnStatus(resp.Status, ctx, prefix) {
					continue
				}

				return nil
			}
		})

	return
}

func (msc *mgmtSvcClient) PrepShutdown(ctx context.Context, destAddr string, req *mgmtpb.PrepShutdownReq) (resp *mgmtpb.DaosResp, stopErr error) {
	stopErr = msc.withConnection(ctx, destAddr,
		func(ctx context.Context, pbClient mgmtpb.MgmtSvcClient) error {

			prefix := fmt.Sprintf("prep shutdown(%s, %+v)", destAddr, *req)
			msc.log.Debugf(prefix + " begin")
			defer msc.log.Debugf(prefix + " end")

			for {
				var err error

				select {
				case <-ctx.Done():
					return errors.Wrap(ctx.Err(), prefix)
				default:
				}

				resp, err = pbClient.PrepShutdown(ctx, req)
				if msc.retryOnErr(err, ctx, prefix) {
					continue
				}
				if resp == nil {
					return errors.New("unexpected nil response status")
				}
				if msc.retryOnStatus(resp.Status, ctx, prefix) {
					continue
				}

				return nil
			}
		})

	return
}

// Stop calls function remotely over gRPC on server listening at destAddr.
//
// Shipped function issues KillRank requests using MgmtSvcClient over dRPC to
// terminates given rank.
func (msc *mgmtSvcClient) Stop(ctx context.Context, destAddr string, req *mgmtpb.KillRankReq) (resp *mgmtpb.DaosResp, stopErr error) {
	stopErr = msc.withConnection(ctx, destAddr,
		func(ctx context.Context, pbClient mgmtpb.MgmtSvcClient) error {

			prefix := fmt.Sprintf("stop(%s, %+v)", destAddr, *req)
			msc.log.Debugf(prefix + " begin")
			defer msc.log.Debugf(prefix + " end")

			for {
				var err error

				select {
				case <-ctx.Done():
					return errors.Wrap(ctx.Err(), prefix)
				default:
				}

				resp, err = pbClient.KillRank(ctx, req)
				if msc.retryOnErr(err, ctx, prefix) {
					continue
				}
				if resp == nil {
					return errors.New("unexpected nil response status")
				}
				// TODO: Stop retrying upon certain errors.
				if msc.retryOnStatus(resp.Status, ctx, prefix) {
					continue
				}

				return nil
			}
		})

	return
}

// Start calls function remotely over gRPC on server listening at destAddr.
//
// Shipped function issues StartRanks requests using MgmtSvcClient to
// restart the designated ranks as configured in persistent superblock.
func (msc *mgmtSvcClient) Start(ctx context.Context, destAddr string, req *mgmtpb.StartRanksReq) (resp *mgmtpb.StartRanksResp, restartErr error) {
	restartErr = msc.withConnection(ctx, destAddr,
		func(ctx context.Context, pbClient mgmtpb.MgmtSvcClient) error {

			prefix := fmt.Sprintf("start(%s, %+v)", destAddr, *req)
			msc.log.Debugf(prefix + " begin")
			defer msc.log.Debugf(prefix + " end")

			_, err := pbClient.StartRanks(ctx, req)

			return err
		})

	return
}

// Status calls function remotely over gRPC on server listening at destAddr.
//
// Shipped function issues PingRank requests using MgmtSvcClient to
// query the designated ranks for activity.
func (msc *mgmtSvcClient) Status(ctx context.Context, destAddr string, req *mgmtpb.PingRankReq) (resp *mgmtpb.DaosResp, statusErr error) {
	statusErr = msc.withConnection(ctx, destAddr,
		func(ctx context.Context, pbClient mgmtpb.MgmtSvcClient) error {

			prefix := fmt.Sprintf("status(%s, %+v)", destAddr, *req)
			msc.log.Debugf(prefix + " begin")
			defer msc.log.Debugf(prefix + " end")

			_, err := pbClient.PingRank(ctx, req)

			return err
		})

	return
}
