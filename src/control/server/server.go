//
// (C) Copyright 2018-2023 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwprov"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
	"github.com/daos-stack/daos/src/control/system/raft"
)

// non-exported package-scope function variable for mocking in unit tests
var osSetenv = os.Setenv

func processConfig(log logging.Logger, cfg *config.Server, fis *hardware.FabricInterfaceSet, mi *common.MemInfo, lookupNetIF ifLookupFn, affSrcs ...config.EngineAffinityFn) error {
	processFabricProvider(cfg)

	if err := cfg.SetEngineAffinities(log, affSrcs...); err != nil {
		return errors.Wrap(err, "failed to set engine affinities")
	}

	if err := cfg.Validate(log); err != nil {
		return errors.Wrapf(err, "%s: validation failed", cfg.Path)
	}

	if err := cfg.SetNrHugepages(log, mi); err != nil {
		return err
	}

	if err := cfg.SetRamdiskSize(log, mi); err != nil {
		return err
	}

	for _, ec := range cfg.Engines {
		if err := checkFabricInterface(ec.Fabric.Interface, lookupNetIF); err != nil {
			return err
		}

		if err := updateFabricEnvars(log, ec, fis); err != nil {
			return errors.Wrap(err, "update engine fabric envars")
		}
	}

	cfg.SaveActiveConfig(log)

	if err := setDaosHelperEnvs(cfg, osSetenv); err != nil {
		return err
	}

	return nil
}

func processFabricProvider(cfg *config.Server) {
	if shouldAppendRXM(cfg.Fabric.Provider) {
		cfg.WithFabricProvider(cfg.Fabric.Provider + ";ofi_rxm")
	}
}

func shouldAppendRXM(provider string) bool {
	return provider == "ofi+verbs"
}

// server struct contains state and components of DAOS Server.
type server struct {
	log         logging.Logger
	cfg         *config.Server
	hostname    string
	runningUser *user.User
	faultDomain *system.FaultDomain
	ctlAddr     *net.TCPAddr
	netDevClass hardware.NetDevClass
	listener    net.Listener

	harness      *EngineHarness
	membership   *system.Membership
	sysdb        *raft.Database
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

func newServer(log logging.Logger, cfg *config.Server, faultDomain *system.FaultDomain) (*server, error) {
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
		runningUser: cu,
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

// CreateDatabaseConfig creates a new database configuration.
func CreateDatabaseConfig(cfg *config.Server) (*raft.DatabaseConfig, error) {
	dbReplicas, err := cfgGetReplicas(cfg, net.LookupIP)
	if err != nil {
		return nil, errors.Wrap(err, "unable to retrieve replicas from config")
	}

	raftDir := cfgGetRaftDir(cfg)
	if raftDir == "" {
		return nil, errors.New("raft directory not available (missing SCM or control metadata in config?)")
	}

	return &raft.DatabaseConfig{
		Replicas:   dbReplicas,
		RaftDir:    raftDir,
		SystemName: cfg.SystemName,
	}, nil
}

// newManagementDatabase creates a new instance of the raft-backed management database.
func newManagementDatabase(log logging.Logger, cfg *config.Server) (*raft.Database, error) {
	dbCfg, err := CreateDatabaseConfig(cfg)
	if err != nil {
		return nil, errors.Wrap(err, "unable to create database config")
	}

	// If this daos_server instance ends up being the MS leader,
	// this will record the DAOS system membership.
	return raft.NewDatabase(log, dbCfg)
}

// createServices builds scaffolding for rpc and event services.
func (srv *server) createServices(ctx context.Context) (err error) {
	srv.sysdb, err = newManagementDatabase(srv.log, srv.cfg)
	if err != nil {
		return
	}
	srv.membership = system.NewMembership(srv.log, srv.sysdb)

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

	srv.ctlSvc = NewControlService(srv.log, srv.harness, srv.cfg, srv.pubSub,
		hwprov.DefaultFabricScanner(srv.log))
	srv.mgmtSvc = newMgmtSvc(srv.harness, srv.membership, srv.sysdb, rpcClient, srv.pubSub)

	if err := srv.mgmtSvc.systemProps.UpdateCompPropVal(daos.SystemPropertyDaosSystem, func() string {
		return srv.cfg.SystemName
	}); err != nil {
		return err
	}

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

func (srv *server) setCoreDumpFilter() error {
	if srv.cfg.CoreDumpFilter == 0 {
		return nil
	}

	srv.log.Debugf("setting core dump filter to 0x%x", srv.cfg.CoreDumpFilter)

	// Set core dump filter.
	if err := writeCoreDumpFilter(srv.log, "/proc/self/coredump_filter", srv.cfg.CoreDumpFilter); err != nil {
		return errors.Wrap(err, "failed to set core dump filter")
	}

	return nil
}

// initNetwork resolves local address and starts TCP listener.
func (srv *server) initNetwork() error {
	defer srv.logDuration(track("time to init network"))

	ctlAddr, err := getControlAddr(ctlAddrParams{
		port:           srv.cfg.ControlPort,
		replicaAddrSrc: srv.sysdb,
		lookupHost:     net.LookupIP,
	})
	if err != nil {
		return err
	}

	listener, err := createListener(ctlAddr, net.Listen)
	if err != nil {
		return err
	}
	srv.ctlAddr = ctlAddr
	srv.listener = listener

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

// addEngines creates and adds engine instances to harness then starts goroutine to execute
// callbacks when all engines are started.
func (srv *server) addEngines(ctx context.Context) error {
	var allStarted sync.WaitGroup
	registerTelemetryCallbacks(ctx, srv)

	iommuEnabled, err := hwprov.DefaultIOMMUDetector(srv.log).IsIOMMUEnabled()
	if err != nil {
		return err
	}

	// Allocate hugepages and rebind NVMe devices to userspace drivers.
	if err := prepBdevStorage(srv, iommuEnabled); err != nil {
		return err
	}

	// Retrieve NVMe device details (before engines are started) so static details can be
	// recovered by the engine storage provider(s) during scan even if devices are in use.
	nvmeScanResp, err := scanBdevStorage(srv)
	if err != nil {
		return err
	}

	if len(srv.cfg.Engines) == 0 {
		return nil
	}

	nrEngineBdevsIdx := -1
	nrEngineBdevs := -1
	for i, c := range srv.cfg.Engines {
		engine, err := srv.createEngine(ctx, i, c)
		if err != nil {
			return errors.Wrap(err, "creating engine instances")
		}

		if err := setEngineBdevs(engine, nvmeScanResp, &nrEngineBdevsIdx, &nrEngineBdevs); err != nil {
			return errors.Wrap(err, "setting engine bdevs")
		}

		registerEngineEventCallbacks(srv, engine, &allStarted)

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
	srvOpts, err := getGrpcOpts(srv.log, srv.cfg.TransportConfig, srv.sysdb.IsLeader)
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
		NetDevClass:     uint32(srv.netDevClass),
		SrvSrxSet:       srxSetting,
		EnvVars:         srv.cfg.ClientEnvVars,
	}
	mgmtpb.RegisterMgmtSvcServer(srv.grpcServer, srv.mgmtSvc)

	tSec, err := security.DialOptionForTransportConfig(srv.cfg.TransportConfig)
	if err != nil {
		return err
	}
	if err := srv.sysdb.ConfigureTransport(srv.grpcServer, tSec); err != nil {
		return err
	}

	return nil
}

func (srv *server) registerEvents() {
	registerFollowerSubscriptions(srv)

	srv.sysdb.OnLeadershipGained(
		func(ctx context.Context) error {
			srv.log.Infof("MS leader running on %s", srv.hostname)
			srv.mgmtSvc.startLeaderLoops(ctx)
			registerLeaderSubscriptions(srv)
			srv.log.Debugf("requesting immediate GroupUpdate after leader change")
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
			return srv.mgmtSvc.checkPools(ctx, true)
		},
	)
	srv.sysdb.OnLeadershipLost(func() error {
		srv.log.Infof("MS leader no longer running on %s", srv.hostname)
		registerFollowerSubscriptions(srv)
		return nil
	})
}

func (srv *server) start(ctx context.Context) error {
	defer srv.logDuration(track("time server was listening"))

	go func() {
		_ = srv.grpcServer.Serve(srv.listener)
	}()
	defer srv.grpcServer.Stop()

	// noop on release builds
	control.StartPProf(srv.log)

	srv.log.Infof("%s v%s (pid %d) listening on %s", build.ControlPlaneName,
		build.DaosVersion, os.Getpid(), srv.ctlAddr)

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

	srv.mgmtSvc.startAsyncLoops(ctx)
	return errors.Wrapf(srv.harness.Start(ctx, srv.sysdb, srv.cfg),
		"%s harness exited", build.ControlPlaneName)
}

func waitFabricReady(ctx context.Context, log logging.Logger, cfg *config.Server) error {
	ifaces := make([]string, 0, len(cfg.Engines))
	for _, eng := range cfg.Engines {
		ifaces = append(ifaces, eng.Fabric.Interface)
	}

	// Skip wait if no fabric interfaces specified in config.
	if len(ifaces) == 0 {
		return nil
	}

	if err := hardware.WaitFabricReady(ctx, log, hardware.WaitFabricReadyParams{
		StateProvider:  hwprov.DefaultNetDevStateProvider(log),
		FabricIfaces:   ifaces,
		IterationSleep: time.Second,
	}); err != nil {
		return err
	}

	return nil
}

func genFiAffFn(fis *hardware.FabricInterfaceSet) config.EngineAffinityFn {
	return func(l logging.Logger, e *engine.Config) (uint, error) {
		fi, err := fis.GetInterfaceOnNetDevice(e.Fabric.Interface, e.Fabric.Provider)
		if err != nil {
			return 0, err
		}
		return fi.NUMANode, nil
	}
}

func lookupIF(name string) (netInterface, error) {
	iface, err := net.InterfaceByName(name)
	if err != nil {
		return nil, errors.Wrapf(err,
			"unable to retrieve interface %q", name)
	}
	return iface, nil
}

// Start is the entry point for a daos_server instance.
func Start(log logging.Logger, cfg *config.Server) error {
	if err := common.CheckDupeProcess(); err != nil {
		return err
	}

	// Create the root context here. All contexts should inherit from this one so
	// that they can be shut down from one place.
	ctx, shutdown := context.WithCancel(context.Background())
	defer shutdown()

	hwprovFini, err := hwprov.Init(log)
	if err != nil {
		return err
	}
	defer hwprovFini()

	if err := waitFabricReady(ctx, log, cfg); err != nil {
		return err
	}

	scanner := hwprov.DefaultFabricScanner(log)

	fis, err := scanner.Scan(ctx, cfg.Fabric.Provider)
	if err != nil {
		return errors.Wrap(err, "scan fabric")
	}

	mi, err := common.GetMemInfo()
	if err != nil {
		return errors.Wrapf(err, "retrieve system memory info")
	}

	if err = processConfig(log, cfg, fis, mi, lookupIF, genFiAffFn(fis)); err != nil {
		return err
	}

	faultDomain, err := getFaultDomain(cfg)
	if err != nil {
		return err
	}
	log.Debugf("fault domain: %s", faultDomain.String())

	srv, err := newServer(log, cfg, faultDomain)
	if err != nil {
		return err
	}
	defer srv.shutdown()

	if err := srv.setCoreDumpFilter(); err != nil {
		return err
	}

	if srv.netDevClass, err = getFabricNetDevClass(cfg, fis); err != nil {
		return err
	}

	if err := srv.createServices(ctx); err != nil {
		return err
	}

	if err := srv.initNetwork(); err != nil {
		return err
	}

	if err := srv.addEngines(ctx); err != nil {
		return err
	}

	if err := srv.setupGrpc(); err != nil {
		return err
	}

	srv.registerEvents()

	sigChan := make(chan os.Signal)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGQUIT, syscall.SIGTERM)
	go func() {
		sig := <-sigChan
		srv.log.Debugf("Caught signal: %s", sig)
		shutdown()
	}()

	return srv.start(ctx)
}
