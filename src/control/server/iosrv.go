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

package main

import (
	"fmt"
	"io/ioutil"
	"net"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	"github.com/pkg/errors"
	"github.com/satori/go.uuid"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	srvpb "github.com/daos-stack/daos/src/control/common/proto/srv"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/log"
	"github.com/golang/protobuf/proto"
)

func formatIosrvs(config *configuration, reformat bool) error {
	// Determine if an I/O server needs to createMS or bootstrapMS.
	addr, err := net.ResolveTCPAddr("tcp", fmt.Sprintf("0.0.0.0:%d", config.Port))
	if err != nil {
		return errors.WithStack(err)
	}
	createMS, bootstrapMS, err := checkMgmtSvcReplica(addr, config.AccessPoints)
	if err != nil {
		return err
	}

	for i := range config.Servers {
		// Only the first I/O server can be an MS replica.
		if i == 0 {
			err = formatIosrv(config, i, reformat, createMS, bootstrapMS)
		} else {
			err = formatIosrv(config, i, reformat, false, false)
		}
		if err != nil {
			return err
		}
	}

	return nil
}

// formatIosrv will prepare DAOS IO servers and store relevant metadata.
//
// If superblock exists and reformat not explicitly requested, no
// formatting of storage is required.
//
// If superblock does not exist, executing thread wait until administrator
// takes action to format storage via client API over gRPC channel.
//
// If format_override has been set in config and superblock does not exist,
// continue to create a new superblock without formatting.
func formatIosrv(
	config *configuration, i int, reformat, createMS, bootstrapMS bool) error {

	srv := config.Servers[i]

	op := "format"
	if reformat {
		op = "reformat"
	}
	op += " server " + srv.ScmMount

	if ok, err := config.ext.exists(iosrvSuperPath(srv.ScmMount)); err != nil {
		return errors.Wrap(err, op)
	} else if ok {
		log.Debugf("server %d has already been formatted\n", i)

		if reformat {
			return errors.New(op + ": reformat not implemented yet")
		}

		return nil
	}

	// check scm has been mounted before proceeding to write to it
	if err := config.checkMount(srv.ScmMount); err != nil {
		return errors.WithMessage(
			err,
			fmt.Sprintf(
				"server %d scm mount path (%s) not mounted",
				i, srv.ScmMount))
	}

	log.Debugf(op+" (createMS=%t bootstrapMS=%t)", createMS, bootstrapMS)

	if err := createIosrvSuper(config, i, reformat, createMS, bootstrapMS); err != nil {
		return errors.WithMessage(err, op)
	}

	return nil
}

// iosrvSuper is the per-I/O-server "superblock".
type iosrvSuper struct {
	UUID        string
	System      string
	Rank        *rank
	ValidRank   bool
	MS          bool
	CreateMS    bool
	BootstrapMS bool
}

// iosrvSuperPath returns the path to the I/O server superblock file.
func iosrvSuperPath(root string) string {
	return filepath.Join(root, "superblock")
}

// createIosrvSuper creates the superblock file for config.Servers[i]. Called
// when formatting an I/O server.
func createIosrvSuper(config *configuration, i int, reformat, createMS, bootstrapMS bool) error {
	// Initialize the superblock object.
	u, err := uuid.NewV4()
	if err != nil {
		return errors.Wrap(err, "generate server UUID")
	}
	super := &iosrvSuper{
		UUID:        u.String(),
		System:      config.SystemName,
		ValidRank:   createMS && bootstrapMS,
		MS:          createMS,
		CreateMS:    createMS,
		BootstrapMS: bootstrapMS,
	}
	if config.Servers[i].Rank != nil || createMS && bootstrapMS {
		super.Rank = new(rank)
		if config.Servers[i].Rank != nil {
			*super.Rank = *config.Servers[i].Rank
		}
	}

	// Write the superblock.
	return writeIosrvSuper(iosrvSuperPath(config.Servers[i].ScmMount), super)
}

func readIosrvSuper(path string) (*iosrvSuper, error) {
	b, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, errors.WithStack(err)
	}

	var s iosrvSuper
	if err = yaml.Unmarshal(b, &s); err != nil {
		return nil, errors.Wrapf(err, "unmarshal %s", path)
	}

	return &s, nil
}

func writeIosrvSuper(path string, super *iosrvSuper) error {
	data, err := yaml.Marshal(super)
	if err != nil {
		return errors.Wrapf(err, "marshal %s", path)
	}

	return common.WriteFileAtomic(path, data, 0600)
}

// iosrv represents an I/O server.
//
// NOTE: The superblock supercedes the format-time configuration in config.
type iosrv struct {
	super   *iosrvSuper
	config  *configuration
	index   int
	cmd     *exec.Cmd
	sigchld chan os.Signal
	ready   chan *srvpb.NotifyReadyReq
	conn    *drpc.ClientConnection
}

func newIosrv(config *configuration, i int) (*iosrv, error) {
	super, err := readIosrvSuper(iosrvSuperPath(config.Servers[i].ScmMount))
	if err != nil {
		return nil, err
	}

	srv := &iosrv{
		super:   super,
		config:  config,
		index:   i,
		sigchld: make(chan os.Signal, 1),
		ready:   make(chan *srvpb.NotifyReadyReq),
		conn:    getDrpcClientConnection(config.SocketDir),
	}

	return srv, nil
}

func (srv *iosrv) start() (err error) {
	defer func() {
		err = errors.WithMessagef(err, "start server %s", srv.config.Servers[srv.index].ScmMount)
	}()

	// Process bdev config parameters and write nvme.conf to SCM to be
	// consumed by SPDK in I/O server process. Populates env & cli opts.
	if err = srv.config.parseNvme(srv.index); err != nil {
		err = errors.WithMessage(
			err, "nvme config could not be processed")
		return
	}

	if err = srv.startCmd(); err != nil {
		return
	}
	defer func() {
		if err != nil {
			srv.stopCmd()
		}
	}()

	// Wait for the notifyReady request or the SIGCHLD from the I/O server.
	// If we receive the notifyReady request, the I/O server is ready for
	// dRPC connections.
	var ready *srvpb.NotifyReadyReq
	select {
	case ready = <-srv.ready:
	case <-srv.sigchld:
		return errors.New("received SIGCHLD")
	}

	// If we are launched using orterun and DAOS_PMIXLESS isn't set, use
	// the old bootstrapping method.
	if _, ok := os.LookupEnv("PMIX_RANK"); ok {
		if _, ok := os.LookupEnv("DAOS_PMIXLESS"); !ok {
			return
		}
	}

	if err = srv.setRank(ready); err != nil {
		return
	}

	if srv.super.CreateMS {
		log.Debugf("create MS (bootstrap=%t)", srv.super.BootstrapMS)
		if err = srv.callCreateMS(); err != nil {
			return
		}
		srv.super.CreateMS = false
		srv.super.BootstrapMS = false
		if err = srv.writeSuper(); err != nil {
			return
		}
	}

	if srv.super.MS {
		err = srv.callStartMS()
	}

	return
}

func (srv *iosrv) wait() error {
	if err := srv.cmd.Wait(); err != nil {
		return errors.Wrapf(err, "wait server %s", srv.config.Servers[srv.index].ScmMount)
	}
	return nil
}

func (srv *iosrv) startCmd() error {
	// Exec io_server with generated cli opts from config context.
	srv.cmd = exec.Command("daos_io_server", srv.config.Servers[srv.index].CliOpts...)
	srv.cmd.Stdout = os.Stdout
	srv.cmd.Stderr = os.Stderr
	srv.cmd.Env = os.Environ()

	// Populate I/O server environment with values from config before starting.
	srv.config.populateEnv(srv.index, &srv.cmd.Env)

	// I/O server should get a SIGKILL if this process dies.
	srv.cmd.SysProcAttr = &syscall.SysProcAttr{
		Pdeathsig: syscall.SIGKILL,
	}

	signal.Notify(srv.sigchld, syscall.SIGCHLD)

	// Start the DAOS I/O server.
	err := srv.cmd.Start()
	if err != nil {
		return errors.WithStack(err)
	}

	return nil
}

func (srv *iosrv) stopCmd() error {
	// Ignore potential errors, as the I/O server may have already died.
	srv.cmd.Process.Kill()

	if err := srv.cmd.Wait(); err != nil {
		return errors.WithStack(err)
	}

	return nil
}

// setRank determines the rank and send a SetRank request to the I/O server.
func (srv *iosrv) setRank(ready *srvpb.NotifyReadyReq) error {
	r := nilRank
	if srv.super.Rank != nil {
		r = *srv.super.Rank
	}

	if !srv.super.ValidRank || !srv.super.MS {
		resp, err := mgmtJoin(srv.config.AccessPoints[0], &mgmtpb.JoinReq{
			Uuid:  srv.super.UUID,
			Rank:  uint32(r),
			Uri:   ready.Uri,
			Nctxs: ready.Nctxs,
			Addr:  fmt.Sprintf("0.0.0.0:%d", srv.config.Port),
		})
		if err != nil {
			return err
		} else if resp.State == mgmtpb.JoinResp_OUT {
			return errors.Errorf("rank %d excluded", resp.Rank)
		}
		r = rank(resp.Rank)

		if !srv.super.ValidRank {
			srv.super.Rank = new(rank)
			*srv.super.Rank = r
			srv.super.ValidRank = true
			if err := srv.writeSuper(); err != nil {
				return err
			}
		}
	}

	if err := srv.callSetRank(r); err != nil {
		return err
	}

	return nil
}

func (srv *iosrv) callCreateMS() error {
	req := &mgmtpb.CreateMsReq{}
	if srv.super.BootstrapMS {
		req.Bootstrap = true
		req.Uuid = srv.super.UUID
		req.Addr = fmt.Sprintf("0.0.0.0:%d", srv.config.Port)
	}

	dresp, err := makeDrpcCall(srv.conn, mgmtModuleID, createMS, req)
	if err != nil {
		return err
	}

	resp := &mgmtpb.DaosResponse{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return errors.Wrap(err, "unmarshal CreateMS response")
	}
	if resp.Status != mgmtpb.DaosRequestStatus_SUCCESS {
		return errors.Errorf("CreateMS: %d\n", resp.Status)
	}

	return nil
}

func (srv *iosrv) callStartMS() error {
	dresp, err := makeDrpcCall(srv.conn, mgmtModuleID, startMS, nil)
	if err != nil {
		return err
	}

	resp := &mgmtpb.DaosResponse{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return errors.Wrap(err, "unmarshal StartMS response")
	}
	if resp.Status != mgmtpb.DaosRequestStatus_SUCCESS {
		return errors.Errorf("StartMS: %d\n", resp.Status)
	}

	return nil
}

func (srv *iosrv) callSetRank(rank rank) error {
	dresp, err := makeDrpcCall(srv.conn, mgmtModuleID, setRank, &mgmtpb.SetRankReq{Rank: uint32(rank)})
	if err != nil {
		return err
	}

	resp := &mgmtpb.DaosResponse{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return errors.Wrap(err, "unmarshall SetRank response")
	}
	if resp.Status != mgmtpb.DaosRequestStatus_SUCCESS {
		return errors.Errorf("SetRank: %d\n", resp.Status)
	}

	return nil
}

func (srv *iosrv) writeSuper() error {
	return writeIosrvSuper(iosrvSuperPath(srv.config.Servers[srv.index].ScmMount), srv.super)
}

// mgmtJoin sends the Join request to MS, retrying indefinitely until MS
// responses.
func mgmtJoin(ap string, req *mgmtpb.JoinReq) (*mgmtpb.JoinResp, error) {
	log.Debugf("join(%s, %+v)", ap, *req)

	conn, err := grpc.Dial(ap, grpc.WithInsecure(), grpc.WithBlock(),
		grpc.WithBackoffMaxDelay(5*time.Second),
		grpc.WithDefaultCallOptions(grpc.FailFast(false)))
	if err != nil {
		return nil, errors.Wrapf(err, "dial %s", ap)
	}
	defer conn.Close()

	client := mgmtpb.NewMgmtSvcClient(conn)

	resp, err := client.Join(context.Background(), req)
	if err != nil {
		return nil, errors.Wrapf(err, "join %s %v", ap, *req)
	}
	if resp.Status != mgmtpb.DaosRequestStatus_SUCCESS {
		return nil, errors.Errorf("join %s %v: %d\n", ap, *req, resp.Status)
	}

	return resp, nil
}
