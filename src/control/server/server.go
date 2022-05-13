//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"net"
	"os"
	"os/signal"
	"os/user"
	"sync"
	"syscall"
	"time"

	"github.com/pkg/errors"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

func processConfig(log *logging.LeveledLogger, cfg *config.Server) (*system.FaultDomain, error) {
	err := cfg.Validate(log)
	if err != nil {
		return nil, errors.Wrapf(err, "%s: validation failed", cfg.Path)
	}

	lookupNetIF := func(name string) (netInterface, error) {
		iface, err := net.InterfaceByName(name)
		if err != nil {
			return nil, errors.Wrapf(err, "unable to retrieve interface %q", name)
		}
		return iface, nil
	}
	for _, ec := range cfg.Engines {
		if err := checkFabricInterface(ec.Fabric.Interface, lookupNetIF); err != nil {
			return nil, err
		}
	}

	cfg.SaveActiveConfig(log)

	if err := setDaosHelperEnvs(cfg, os.Setenv); err != nil {
		return nil, err
	}

	faultDomain, err := getFaultDomain(cfg)
	if err != nil {
		return nil, err
	}
	log.Debugf("fault domain: %s", faultDomain.String())

	return faultDomain, nil
}

// server struct contains state and components of DAOS Server.
type server struct {
	log         *logging.LeveledLogger
	cfg         *config.Server
	hostname    string
	runningUser string
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
	grpcServer   *grpc.Server

	cbLock           sync.Mutex
	onEnginesStarted []func(context.Context) error
	onShutdown       []func()
}

func newServer(ctx context.Context, log *logging.LeveledLogger, cfg *config.Server, faultDomain *system.FaultDomain) (*server, error) {
	hostname, err := os.Hostname()
	if err != nil {
		return nil, errors.Wrap(err, "get hostname")
	}

	cu, err := user.Current()
	if err != nil {
		return nil, errors.Wrap(err, "get username")
	}

	harness := NewEngineHarness(log).WithFaultDomain(faultDomain)

	return &server{
		log:         log,
		cfg:         cfg,
		hostname:    hostname,
		runningUser: cu.Username,
		faultDomain: faultDomain,
		harness:     harness,
	}, nil
}

func track(msg string) (string, time.Time) {
	return msg, time.Now()
}

func (srv *server) logDuration(msg string, start time.Time) {
	srv.log.Debugf("%v: %v\n", msg, time.Since(start))
}

// createServices builds scaffolding for rpc and event services.
func (srv *server) createServices(ctx context.Context) error {
	dbReplicas, err := cfgGetReplicas(srv.cfg, net.ResolveTCPAddr)
	if err != nil {
		return errors.Wrap(err, "retrieve replicas from config")
	}

	// If this daos_server instance ends up being the MS leader,
	// this will record the DAOS system membership.
	sysdb, err := system.NewDatabase(srv.log, &system.DatabaseConfig{
		Replicas:   dbReplicas,
		RaftDir:    cfgGetRaftDir(srv.cfg),
		SystemName: srv.cfg.SystemName,
	})
	if err != nil {
		return errors.Wrap(err, "create system database")
	}
	srv.sysdb = sysdb
	srv.membership = system.NewMembership(srv.log, sysdb)

	// Create rpcClient for inter-server communication.
	cliCfg := control.DefaultConfig()
	cliCfg.TransportConfig = srv.cfg.TransportConfig
	rpcClient := control.NewClient(
		control.WithConfig(cliCfg),
		control.WithClientLogger(srv.log))

	// Create event distribution primitives.
	srv.pubSub = events.NewPubSub(ctx, srv.log)
	srv.OnShutdown(srv.pubSub.Close)
	srv.evtForwarder = control.NewEventForwarder(rpcClient, srv.cfg.AccessPoints)
	srv.evtLogger = control.NewEventLogger(srv.log)

	srv.ctlSvc = NewControlService(srv.log, srv.harness, srv.cfg, srv.pubSub)
	srv.mgmtSvc = newMgmtSvc(srv.harness, srv.membership, sysdb, rpcClient, srv.pubSub)

	return nil
}

// OnEnginesStarted adds callback functions to be called when all engines have
// started up.
func (srv *server) OnEnginesStarted(fns ...func(context.Context) error) {
	srv.cbLock.Lock()
	srv.onEnginesStarted = append(srv.onEnginesStarted, fns...)
	srv.cbLock.Unlock()
}

// OnShutdown adds callback functions to be called when the server shuts down.
func (srv *server) OnShutdown(fns ...func()) {
	srv.cbLock.Lock()
	srv.onShutdown = append(srv.onShutdown, fns...)
	srv.cbLock.Unlock()
}

func (srv *server) shutdown() {
	srv.cbLock.Lock()
	onShutdownCbs := srv.onShutdown
	srv.cbLock.Unlock()
	for _, fn := range onShutdownCbs {
		fn()
	}
}

// initNetwork resolves local address and starts TCP listener then calls
// netInit to process network configuration.
func (srv *server) initNetwork(ctx context.Context) error {
	defer srv.logDuration(track("time to init network"))

	ctlAddr, listener, err := createListener(srv.cfg.ControlPort, net.ResolveTCPAddr, net.Listen)
	if err != nil {
		return err
	}
	srv.ctlAddr = ctlAddr
	srv.listener = listener

	ndc, err := netInit(ctx, srv.log, srv.cfg)
	if err != nil {
		return err
	}
	srv.netDevClass = ndc
	srv.log.Infof("Network device class set to %q", netdetect.DevClassName(ndc))

	return nil
}

func (srv *server) initStorage() error {
	defer srv.logDuration(track("time to init storage"))

	if err := prepBdevStorage(srv, iommuDetected(), common.GetHugePageInfo); err != nil {
		return err
	}

	srv.log.Debug("running storage setup on server start-up, scanning storage devices")
	srv.ctlSvc.Setup()

	return nil
}

func (srv *server) createEngine(ctx context.Context, idx int, cfg *engine.Config) (*EngineInstance, error) {
	// Closure to join an engine instance to a system using control API.
	joinFn := func(ctxIn context.Context, req *control.SystemJoinReq) (*control.SystemJoinResp, error) {
		req.SetHostList(srv.cfg.AccessPoints)
		req.SetSystem(srv.cfg.SystemName)
		req.ControlAddr = srv.ctlAddr

		return control.SystemJoin(ctxIn, srv.mgmtSvc.rpcClient, req)
	}

	engine := NewEngineInstance(srv.log, storage.DefaultProvider(srv.log, idx, &cfg.Storage), joinFn,
		engine.NewRunner(srv.log, cfg)).WithHostFaultDomain(srv.harness.faultDomain)
	if idx == 0 {
		configureFirstEngine(ctx, engine, srv.sysdb, joinFn)
	}

	return engine, nil
}

// addEngines creates and adds engine instances to harness then starts
// goroutine to execute callbacks when all engines are started.
func (srv *server) addEngines(ctx context.Context) error {
	var allStarted sync.WaitGroup
	registerTelemetryCallbacks(ctx, srv)

	// Store cached NVMe device details retrieved on start-up (before
	// engines are started) so static details can be recovered by the engine
	// storage provider(s) during scan even if devices are in use.
	nvmeScanResp, err := srv.ctlSvc.NvmeScan(storage.BdevScanRequest{})
	if err != nil {
		srv.log.Errorf("nvme scan failed: %s", err)
		nvmeScanResp = &storage.BdevScanResponse{}
	}
	if nvmeScanResp == nil {
		return errors.New("nil nvme scan response received")
	}

	for i, c := range srv.cfg.Engines {
		engine, err := srv.createEngine(ctx, i, c)
		if err != nil {
			return err
		}

		if err := engine.storage.SetBdevCache(*nvmeScanResp); err != nil {
			return errors.Wrap(err, "setting engine storage bdev cache")
		}

		registerEngineEventCallbacks(engine, srv.hostname, srv.pubSub, &allStarted)

		if err := srv.harness.AddInstance(engine); err != nil {
			return err
		}
		// increment count of engines waiting to start
		allStarted.Add(1)
	}

	go func() {
		srv.log.Debug("waiting for engines to start...")
		allStarted.Wait()
		srv.log.Debug("engines have started")

		srv.cbLock.Lock()
		onEnginesStartedCbs := srv.onEnginesStarted
		srv.cbLock.Unlock()
		for _, cb := range onEnginesStartedCbs {
			if err := cb(ctx); err != nil {
				srv.log.Errorf("on engines started: %s", err)
			}
		}
	}()

	return nil
}

// setupGrpc creates a new grpc server and registers services.
func (srv *server) setupGrpc() error {
	srvOpts, err := getGrpcOpts(srv.cfg.TransportConfig)
	if err != nil {
		return err
	}

	srv.grpcServer = grpc.NewServer(srvOpts...)
	ctlpb.RegisterCtlSvcServer(srv.grpcServer, srv.ctlSvc)

	srxSetting, err := getSrxSetting(srv.cfg)
	if err != nil {
		return err
	}
	srv.mgmtSvc.clientNetworkHint = &mgmtpb.ClientNetHint{
		Provider:        srv.cfg.Fabric.Provider,
		CrtCtxShareAddr: srv.cfg.Fabric.CrtCtxShareAddr,
		CrtTimeout:      srv.cfg.Fabric.CrtTimeout,
		NetDevClass:     srv.netDevClass,
		SrvSrxSet:       srxSetting,
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
	registerFollowerSubscriptions(srv)

	srv.sysdb.OnLeadershipGained(
		func(ctx context.Context) error {
			srv.log.Infof("MS leader running on %s", srv.hostname)
			srv.mgmtSvc.startJoinLoop(ctx)
			registerLeaderSubscriptions(srv)
			srv.log.Debugf("requesting sync GroupUpdate after leader change")
			go func() {
				for {
					select {
					case <-ctx.Done():
						return
					default:
						// Wait for at least one engine to be ready to service the
						// GroupUpdate request.
						for _, ei := range srv.harness.Instances() {
							if ei.IsReady() {
								srv.mgmtSvc.reqGroupUpdate(ctx, true)
								return
							}
						}
						srv.log.Debugf("no engines ready for GroupUpdate; waiting %s", groupUpdateInterval)
						time.Sleep(groupUpdateInterval)
					}
				}
			}()
			return nil
		},
		func(ctx context.Context) error {
			return srv.mgmtSvc.checkPools(ctx)
		},
	)
	srv.sysdb.OnLeadershipLost(func() error {
		srv.log.Infof("MS leader no longer running on %s", srv.hostname)
		registerFollowerSubscriptions(srv)
		return nil
	})
}

func (srv *server) start(ctx context.Context, shutdown context.CancelFunc) error {
	defer srv.logDuration(track("time server was listening"))

	go func() {
		_ = srv.grpcServer.Serve(srv.listener)
	}()
	defer srv.grpcServer.Stop()

	srv.log.Infof("%s v%s (pid %d) listening on %s", build.ControlPlaneName,
		build.DaosVersion, os.Getpid(), srv.ctlAddr)

	sigChan := make(chan os.Signal)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGQUIT, syscall.SIGTERM)
	go func() {
		sig := <-sigChan
		srv.log.Debugf("Caught signal: %s", sig)

		shutdown()
	}()

	drpcSetupReq := &drpcServerSetupReq{
		log:     srv.log,
		sockDir: srv.cfg.SocketDir,
		engines: srv.harness.Instances(),
		tc:      srv.cfg.TransportConfig,
		sysdb:   srv.sysdb,
		events:  srv.pubSub,
	}
	// Single daos_server dRPC server to handle all engine requests
	if err := drpcServerSetup(ctx, drpcSetupReq); err != nil {
		return errors.WithMessage(err, "dRPC server setup")
	}
	defer func() {
		if err := drpcCleanup(srv.cfg.SocketDir); err != nil {
			srv.log.Errorf("error during dRPC cleanup: %s", err)
		}
	}()

	return errors.Wrapf(srv.harness.Start(ctx, srv.sysdb, srv.cfg),
		"%s harness exited", build.ControlPlaneName)
}

// Start is the entry point for a daos_server instance.
func Start(log *logging.LeveledLogger, cfg *config.Server) error {
	faultDomain, err := processConfig(log, cfg)
	if err != nil {
		return err
	}

	// Create the root context here. All contexts should inherit from this one so
	// that they can be shut down from one place.
	ctx, shutdown := context.WithCancel(context.Background())
	defer shutdown()

	srv, err := newServer(ctx, log, cfg, faultDomain)
	if err != nil {
		return err
	}
	defer srv.shutdown()

	if err := srv.createServices(ctx); err != nil {
		return err
	}

	if err := srv.initNetwork(ctx); err != nil {
		return err
	}

	if err := srv.initStorage(); err != nil {
		return err
	}

	if err := srv.addEngines(ctx); err != nil {
		return err
	}

	if err := srv.setupGrpc(); err != nil {
		return err
	}

	srv.registerEvents()

	return srv.start(ctx, shutdown)
}
