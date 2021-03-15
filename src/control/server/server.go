//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"net"
	"os"
	"os/signal"
	"os/user"
	"strings"
	"sync"
	"syscall"

	"github.com/pkg/errors"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/build"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
)

func processConfig(log *logging.LeveledLogger, cfg *config.Server) (*system.FaultDomain, error) {
	err := cfg.Validate(log)
	if err != nil {
		return nil, errors.Wrapf(err, "%s: validation failed", cfg.Path)
	}

	cfg.SaveActiveConfig(log)

	if err := setDaosHelperEnvs(cfg, os.Setenv); err != nil {
		return nil, err
	}

	fd, err := getFaultDomain(cfg)
	if err != nil {
		return nil, err
	}
	log.Debugf("fault domain: %s", fd.String())

	return fd, nil
}

// server struct contains state and components of DAOS Server.
type server struct {
	log         *logging.LeveledLogger
	cfg         *config.Server
	faultDomain *system.FaultDomain
	ctlAddr     *net.TCPAddr
	netDevClass uint32
	listener    net.Listener

	harness      *EngineHarness
	membership   *system.Membership
	sysdb        *system.Database
	pubSub       *events.PubSub
	evtForwarder *control.EventForwarder
	evtLogger    *control.EventLogger
	ctlSvc       *ControlService
	mgmtSvc      *mgmtSvc
	scmProvider  *scm.Provider
	bdevProvider *bdev.Provider
	grpcServer   *grpc.Server
}

func newServer(ctx context.Context, log *logging.LeveledLogger, cfg *config.Server, fd *system.FaultDomain) (*server, error) {
	var dbReplicas []*net.TCPAddr
	for _, ap := range cfg.AccessPoints {
		apAddr, err := net.ResolveTCPAddr("tcp", ap)
		if err != nil {
			return nil, config.FaultConfigBadAccessPoints
		}
		dbReplicas = append(dbReplicas, apAddr)
	}

	// If this daos_server instance ends up being the MS leader,
	// this will record the DAOS system membership.
	sysdb, err := system.NewDatabase(log, &system.DatabaseConfig{
		Replicas:   dbReplicas,
		RaftDir:    raftDir(cfg),
		SystemName: cfg.SystemName,
	})
	if err != nil {
		return nil, errors.Wrap(err, "failed to create system database")
	}
	membership := system.NewMembership(log, sysdb)
	harness := NewEngineHarness(log).WithFaultDomain(fd)

	// Create rpcClient for inter-server communication.
	cliCfg := control.DefaultConfig()
	cliCfg.TransportConfig = cfg.TransportConfig
	rpcClient := control.NewClient(
		control.WithConfig(cliCfg),
		control.WithClientLogger(log))

	// Create event distribution primitives.
	eventPubSub := events.NewPubSub(ctx, log)

	// Create storage subsystem providers.
	scmProvider := scm.DefaultProvider(log)
	bdevProvider := bdev.DefaultProvider(log)

	return &server{
		log:          log,
		cfg:          cfg,
		faultDomain:  fd,
		harness:      harness,
		membership:   membership,
		sysdb:        sysdb,
		pubSub:       eventPubSub,
		evtForwarder: control.NewEventForwarder(rpcClient, cfg.AccessPoints),
		evtLogger:    control.NewEventLogger(log),
		ctlSvc:       NewControlService(log, harness, bdevProvider, scmProvider, cfg, eventPubSub),
		mgmtSvc:      newMgmtSvc(harness, membership, sysdb, rpcClient, eventPubSub),
		scmProvider:  scmProvider,
		bdevProvider: bdevProvider,
	}, nil
}

func (srv *server) shutdown() {
	srv.pubSub.Close()
}

// initNetwork resolves local address and starts TCP listener, initializes net
// detect library and warns if configured number of engines is less than NUMA
// node count.
func (srv *server) initNetwork(ctx context.Context) (context.Context, func(), error) {
	ctlAddr, err := net.ResolveTCPAddr("tcp", fmt.Sprintf("0.0.0.0:%d", srv.cfg.ControlPort))
	if err != nil {
		return nil, nil, errors.Wrap(err, "unable to resolve daos_server control address")
	}
	srv.ctlAddr = ctlAddr

	// Create and start listener on management network.
	listener, err := net.Listen("tcp4", ctlAddr.String())
	if err != nil {
		return nil, nil, errors.Wrap(err, "unable to listen on management interface")
	}
	srv.listener = listener

	ctx, err = netdetect.Init(ctx)
	if err != nil {
		return nil, nil, err
	}

	// On a NUMA-aware system, emit a message when the configuration may be
	// sub-optimal.
	numaCount := netdetect.NumNumaNodes(ctx)
	if numaCount > 0 && len(srv.cfg.Engines) > numaCount {
		srv.log.Infof("NOTICE: Detected %d NUMA node(s); %d-server config may not perform as expected",
			numaCount, len(srv.cfg.Engines))
	}

	return ctx, func() {
		netdetect.CleanUp(ctx)
	}, nil
}

func (srv *server) initStorage() error {
	runningUser, err := user.Current()
	if err != nil {
		return errors.Wrap(err, "unable to lookup current user")
	}

	iommuDisabled := !iommuDetected()
	// Perform an automatic prepare based on the values in the config file.
	prepReq := bdev.PrepareRequest{
		// Default to minimum necessary for scan to work correctly.
		HugePageCount: minHugePageCount,
		TargetUser:    runningUser.Username,
		PCIWhitelist:  strings.Join(srv.cfg.BdevInclude, " "),
		PCIBlacklist:  strings.Join(srv.cfg.BdevExclude, " "),
		DisableVFIO:   srv.cfg.DisableVFIO,
		DisableVMD:    srv.cfg.DisableVMD || srv.cfg.DisableVFIO || iommuDisabled,
		// TODO: pass vmd include/white list
	}

	if cfgHasBdevs(srv.cfg) {
		// The config value is intended to be per-engine, so we need to adjust
		// based on the number of engines.
		prepReq.HugePageCount = srv.cfg.NrHugepages * len(srv.cfg.Engines)

		// Perform these checks to avoid even trying a prepare if the system
		// isn't configured properly.
		if runningUser.Uid != "0" {
			if srv.cfg.DisableVFIO {
				return FaultVfioDisabled
			}

			if iommuDisabled {
				return FaultIommuDisabled
			}
		}
	}

	// TODO: should be passing root context into prepare request to
	//       facilitate cancellation.
	srv.log.Debugf("automatic NVMe prepare req: %+v", prepReq)
	if _, err := srv.bdevProvider.Prepare(prepReq); err != nil {
		srv.log.Errorf("automatic NVMe prepare failed (check configuration?)\n%s", err)
	}

	hugePages, err := getHugePageInfo()
	if err != nil {
		return errors.Wrap(err, "unable to read system hugepage info")
	}

	if cfgHasBdevs(srv.cfg) {
		// Double-check that we got the requested number of huge pages after prepare.
		if hugePages.Free < prepReq.HugePageCount {
			return FaultInsufficientFreeHugePages(hugePages.Free, prepReq.HugePageCount)
		}
	}

	return nil
}

func (srv *server) addEngines(ctxNet context.Context) error {
	// Create a closure to be used for joining engine instances.
	joinInstance := func(ctxIn context.Context, req *control.SystemJoinReq) (*control.SystemJoinResp, error) {
		req.SetHostList(srv.cfg.AccessPoints)
		req.SetSystem(srv.cfg.SystemName)
		req.ControlAddr = srv.ctlAddr

		return control.SystemJoin(ctxIn, srv.mgmtSvc.rpcClient, req)
	}

	var allStarted sync.WaitGroup
	for idx, engineCfg := range srv.cfg.Engines {
		// Provide special handling for the ofi+verbs provider.
		// Mercury uses the interface name such as ib0, while OFI uses the
		// device name such as hfi1_0 CaRT and Mercury will now support the
		// new OFI_DOMAIN environment variable so that we can specify the
		// correct device for each.
		if strings.HasPrefix(engineCfg.Fabric.Provider, "ofi+verbs") && !engineCfg.HasEnvVar("OFI_DOMAIN") {
			deviceAlias, err := netdetect.GetDeviceAlias(ctxNet, engineCfg.Fabric.Interface)
			if err != nil {
				return errors.Wrapf(err, "failed to resolve alias for %s", engineCfg.Fabric.Interface)
			}
			envVar := "OFI_DOMAIN=" + deviceAlias
			engineCfg.WithEnvVars(envVar)
		}

		// Indicate whether VMD devices have been detected and can be used.
		engineCfg.Storage.Bdev.VmdDisabled = srv.bdevProvider.IsVMDDisabled()

		// TODO: ClassProvider should be encapsulated within bdevProvider
		bcp, err := bdev.NewClassProvider(srv.log, engineCfg.Storage.SCM.MountPoint, &engineCfg.Storage.Bdev)
		if err != nil {
			return err
		}

		engine := NewEngineInstance(srv.log, bcp, srv.scmProvider, joinInstance, engine.NewRunner(srv.log, engineCfg)).
			WithHostFaultDomain(srv.harness.faultDomain)
		if err := srv.harness.AddInstance(engine); err != nil {
			return err
		}

		// Register callback to publish I/O Engine process exit events.
		engine.OnInstanceExit(publishInstanceExitFn(srv.pubSub.Publish, hostname(), engine.Index()))

		allStarted.Add(1)
		var onceReady sync.Once
		engine.OnReady(func(_ context.Context) error {
			// Indicate that engine has been started, only do this
			// the first time that the engine starts as shared
			// memory persists between engine restarts.
			onceReady.Do(func() {
				allStarted.Done()
			})
			return nil
		})

		if idx == 0 {
			netDevClass, err := srv.cfg.GetDeviceClassFn(engineCfg.Fabric.Interface)
			if err != nil {
				return err
			}
			srv.netDevClass = netDevClass

			if !srv.sysdb.IsReplica() {
				continue
			}

			// Start the system db after instance 0's SCM is
			// ready.
			var onceStorageReady sync.Once
			engine.OnStorageReady(func(ctxIn context.Context) (err error) {
				onceStorageReady.Do(func() {
					err = errors.Wrap(srv.sysdb.Start(ctxIn),
						"failed to start system db",
					)
				})
				return
			})

			if !srv.sysdb.IsBootstrap() {
				continue
			}

			// For historical reasons, we reserve rank 0 for the first
			// instance on the raft bootstrap server. This implies that
			// rank 0 will always be associated with a MS replica, but
			// it is not guaranteed to always be the leader.
			engine.joinSystem = func(ctxIn context.Context, req *control.SystemJoinReq) (*control.SystemJoinResp, error) {
				if sb := engine.getSuperblock(); !sb.ValidRank {
					engine.log.Debug("marking bootstrap instance as rank 0")
					req.Rank = 0
					sb.Rank = system.NewRankPtr(0)
				}
				return joinInstance(ctxIn, req)
			}
		}
	}

	go func() {
		allStarted.Wait()

		if cfg.TelemetryPort == 0 {
			return
		}

		log.Debug("starting Prometheus exporter")
		if err := startPrometheusExporter(ctx, log, cfg.TelemetryPort, harness.Instances()); err != nil {
			log.Errorf("failed to start prometheus exporter: %s", err)
		}
	}()

	return nil
}

// discoverStorage scans local storage hardware.
func (srv *server) discoverStorage() error {
	return srv.ctlSvc.Setup()
}

func (srv *server) setupGrpc() error {
	// Create new grpc server, register services and start serving.
	unaryInterceptors := []grpc.UnaryServerInterceptor{
		unaryErrorInterceptor,
		unaryStatusInterceptor,
	}
	streamInterceptors := []grpc.StreamServerInterceptor{
		streamErrorInterceptor,
	}
	tcOpt, err := security.ServerOptionForTransportConfig(srv.cfg.TransportConfig)
	if err != nil {
		return err
	}
	srvOpts := []grpc.ServerOption{tcOpt}

	uintOpt, err := unaryInterceptorForTransportConfig(srv.cfg.TransportConfig)
	if err != nil {
		return err
	}
	if uintOpt != nil {
		unaryInterceptors = append(unaryInterceptors, uintOpt)
	}
	sintOpt, err := streamInterceptorForTransportConfig(srv.cfg.TransportConfig)
	if err != nil {
		return err
	}
	if sintOpt != nil {
		streamInterceptors = append(streamInterceptors, sintOpt)
	}
	srvOpts = append(srvOpts, []grpc.ServerOption{
		grpc.ChainUnaryInterceptor(unaryInterceptors...),
		grpc.ChainStreamInterceptor(streamInterceptors...),
	}...)

	srv.grpcServer = grpc.NewServer(srvOpts...)
	ctlpb.RegisterCtlSvcServer(srv.grpcServer, srv.ctlSvc)

	srv.mgmtSvc.clientNetworkCfg = &config.ClientNetworkCfg{
		Provider:        srv.cfg.Fabric.Provider,
		CrtCtxShareAddr: srv.cfg.Fabric.CrtCtxShareAddr,
		CrtTimeout:      srv.cfg.Fabric.CrtTimeout,
		NetDevClass:     srv.netDevClass,
	}
	mgmtpb.RegisterMgmtSvcServer(srv.grpcServer, srv.mgmtSvc)

	tSec, err := security.DialOptionForTransportConfig(srv.cfg.TransportConfig)
	if err != nil {
		return err
	}
	srv.sysdb.ConfigureTransport(srv.grpcServer, tSec)

	return nil
}

func (srv *server) registerEvents() {
	// Forward published actionable events (type RASTypeStateChange) to the
	// management service leader, behavior is updated on leadership change.
	srv.pubSub.Subscribe(events.RASTypeStateChange, srv.evtForwarder)

	// Log events on the host that they were raised (and first published) on.
	srv.pubSub.Subscribe(events.RASTypeAny, srv.evtLogger)

	srv.sysdb.OnLeadershipGained(func(ctx context.Context) error {
		srv.log.Infof("MS leader running on %s", hostname())
		srv.mgmtSvc.startJoinLoop(ctx)

		// Stop forwarding events to MS and instead start handling
		// received forwarded (and local) events.
		srv.pubSub.Reset()
		srv.pubSub.Subscribe(events.RASTypeAny, srv.evtLogger)
		srv.pubSub.Subscribe(events.RASTypeStateChange, srv.membership)
		srv.pubSub.Subscribe(events.RASTypeStateChange, srv.sysdb)
		srv.pubSub.Subscribe(events.RASTypeStateChange,
			events.HandlerFunc(func(ctx context.Context, evt *events.RASEvent) {
				switch evt.ID {
				case events.RASSwimRankDead:
					// Mark the rank as unavailable for membership in
					// new pools, etc.
					if err := srv.membership.MarkRankDead(system.Rank(evt.Rank)); err != nil {
						srv.log.Errorf("failed to mark rank %d as dead: %s", evt.Rank, err)
						return
					}
					mgmtSvc.reqGroupUpdate(ctx)
				}
			}))

		return nil
	})
	srv.sysdb.OnLeadershipLost(func() error {
		srv.log.Infof("MS leader no longer running on %s", hostname())

		// Stop handling received forwarded (in addition to local)
		// events and start forwarding events to the new MS leader.
		srv.pubSub.Reset()
		srv.pubSub.Subscribe(events.RASTypeAny, srv.evtLogger)
		srv.pubSub.Subscribe(events.RASTypeStateChange, srv.evtForwarder)

		return nil
	})
}

func (srv *server) start(ctx context.Context, shutdown context.CancelFunc) error {
	go func() {
		_ = srv.grpcServer.Serve(srv.listener)
	}()
	defer srv.grpcServer.Stop()

	srv.log.Infof("%s v%s (pid %d) listening on %s", build.ControlPlaneName,
		build.DaosVersion, os.Getpid(), srv.ctlAddr)

	sigChan := make(chan os.Signal)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGQUIT, syscall.SIGTERM)
	go func() {
		// SIGKILL I/O Engine immediately on exit.
		// TODO: Re-enable attempted graceful shutdown of I/O Engines.
		sig := <-sigChan
		srv.log.Debugf("Caught signal: %s", sig)

		shutdown()
	}()

	return errors.Wrapf(srv.harness.Start(ctx, srv.sysdb, srv.pubSub, srv.cfg),
		"%s exited with error", build.DataPlaneName)
}

// Start is the entry point for a daos_server instance.
func Start(log *logging.LeveledLogger, cfg *config.Server) error {
	fd, err := processConfig(log, cfg)
	if err != nil {
		return err
	}

	// Create the root context here. All contexts should inherit from this one so
	// that they can be shut down from one place.
	ctx, shutdown := context.WithCancel(context.Background())
	defer shutdown()

	srv, err := newServer(ctx, log, cfg, fd)
	if err != nil {
		return err
	}
	defer srv.shutdown()

	ctxNet, shutdownNet, err := srv.initNetwork(ctx)
	if err != nil {
		return err
	}
	defer shutdownNet()

	if err := srv.initStorage(); err != nil {
		return err
	}

	if err := srv.addEngines(ctxNet); err != nil {
		return err
	}

	if err := srv.discoverStorage(); err != nil {
		return err
	}

	if err := srv.setupGrpc(); err != nil {
		return err
	}

	srv.registerEvents()

	return srv.start(ctx, shutdown)
}
