//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"io/ioutil"
	"net"
	"os"
	"os/signal"
	"os/user"
	"path/filepath"
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
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
)

type contextKey string

func (c contextKey) String() string {
	return "server context key " + string(c)
}

func (c contextKey) errMissing() error {
	return errors.New("context missing key " + string(c))
}

var (
	contextKeyCtlAddr     = contextKey("ctl-addr")
	contextKeyNetListener = contextKey("net-listener")
	contextKeyNetDevClass = contextKey("net-dev-class")
	contextKeyFaultDomain = contextKey("fault-domain")
)

func ctlAddrFromContext(ctx context.Context) (*net.TCPAddr, bool) {
	addr, ok := ctx.Value(contextKeyCtlAddr).(*net.TCPAddr)
	return addr, ok
}

func netListenerFromContext(ctx context.Context) (net.Listener, bool) {
	lis, ok := ctx.Value(contextKeyCtlAddr).(net.Listener)
	return lis, ok
}

func netDevClassFromContext(ctx context.Context) (uint32, bool) {
	ndc, ok := ctx.Value(contextKeyNetDevClass).(uint32)
	return ndc, ok
}

func faultDomainFromContext(ctx context.Context) (*system.FaultDomain, bool) {
	fd, ok := ctx.Value(contextKeyFaultDomain).(*system.FaultDomain)
	return fd, ok
}

const (
	iommuPath        = "/sys/class/iommu"
	minHugePageCount = 128
)

func cfgHasBdev(cfg *config.Server) bool {
	for _, engineCfg := range cfg.Engines {
		if len(engineCfg.Storage.Bdev.DeviceList) > 0 {
			return true
		}
	}

	return false
}

func iommuDetected() bool {
	// Simple test for now -- if the path exists and contains
	// DMAR entries, we assume that's good enough.
	dmars, err := ioutil.ReadDir(iommuPath)
	if err != nil {
		return false
	}

	return len(dmars) > 0
}

func raftDir(cfg *config.Server) string {
	if len(cfg.Engines) == 0 {
		return "" // can't save to SCM
	}
	return filepath.Join(cfg.Engines[0].Storage.SCM.MountPoint, "control_raft")
}

func hostname() string {
	hn, err := os.Hostname()
	if err != nil {
		return fmt.Sprintf("Hostname() failed: %s", err.Error())
	}
	return hn
}

func checkConfig(ctx context.Context, log *logging.LeveledLogger, cfg *config.Server) (context.Context, error) {
	err := cfg.Validate(log)
	if err != nil {
		return ctx, errors.Wrapf(err, "%s: validation failed", cfg.Path)
	}

	// Temporary notification while the feature is still being polished.
	if len(cfg.AccessPoints) > 1 {
		log.Info("\n*******\nNOTICE: Support for multiple access points is an alpha feature and is not well-tested!\n*******\n\n")
	}

	// Backup active config.
	config.SaveActiveConfig(log, cfg)

	if cfg.HelperLogFile != "" {
		if err := os.Setenv(pbin.DaosAdminLogFileEnvVar, cfg.HelperLogFile); err != nil {
			return ctx, errors.Wrap(err, "unable to configure privileged helper logging")
		}
	}

	if cfg.FWHelperLogFile != "" {
		if err := os.Setenv(pbin.DaosFWLogFileEnvVar, cfg.FWHelperLogFile); err != nil {
			return ctx, errors.Wrap(err, "unable to configure privileged firmware helper logging")
		}
	}

	fd, err := getFaultDomain(cfg)
	if err != nil {
		return ctx, err
	}
	log.Debugf("fault domain: %s", fd.String())
	ctx = context.WithValue(ctx, contextKeyFaultDomain, fd)

	return ctx, nil
}

func checkNetwork(ctx context.Context, log *logging.LeveledLogger, cfg *config.Server) (context.Context, error) {
	controlAddr, err := net.ResolveTCPAddr("tcp", fmt.Sprintf("0.0.0.0:%d", cfg.ControlPort))
	if err != nil {
		return nil, errors.Wrap(err, "unable to resolve daos_server control address")
	}
	ctx = context.WithValue(ctx, contextKeyCtlAddr, controlAddr)

	// Create and start listener on management network.
	lis, err := net.Listen("tcp4", controlAddr.String())
	if err != nil {
		return nil, errors.Wrap(err, "unable to listen on management interface")
	}
	ctx = context.WithValue(ctx, contextKeyNetListener, lis)

	ctx, err = netdetect.Init(ctx)
	if err != nil {
		return nil, err
	}

	// On a NUMA-aware system, emit a message when the configuration may be
	// sub-optimal.
	numaCount := netdetect.NumNumaNodes(ctx)
	if numaCount > 0 && len(cfg.Engines) > numaCount {
		log.Infof("NOTICE: Detected %d NUMA node(s); %d-server config may not perform as expected",
			numaCount, len(cfg.Engines))
	}

	return ctx, nil
}

func checkBlockStorage(log *logging.LeveledLogger, cfg *config.Server, bp *bdev.Provider) error {
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
		PCIWhitelist:  strings.Join(cfg.BdevInclude, " "),
		PCIBlacklist:  strings.Join(cfg.BdevExclude, " "),
		DisableVFIO:   cfg.DisableVFIO,
		DisableVMD:    cfg.DisableVMD || cfg.DisableVFIO || iommuDisabled,
		// TODO: pass vmd include/white list
	}

	if cfgHasBdev(cfg) {
		// The config value is intended to be per-engine, so we need to adjust
		// based on the number of engines.
		prepReq.HugePageCount = cfg.NrHugepages * len(cfg.Engines)

		// Perform these checks to avoid even trying a prepare if the system
		// isn't configured properly.
		if runningUser.Uid != "0" {
			if cfg.DisableVFIO {
				return FaultVfioDisabled
			}

			if iommuDisabled {
				return FaultIommuDisabled
			}
		}
	}

	log.Debugf("automatic NVMe prepare req: %+v", prepReq)
	if _, err := bp.Prepare(prepReq); err != nil {
		log.Errorf("automatic NVMe prepare failed (check configuration?)\n%s", err)
	}

	hugePages, err := getHugePageInfo()
	if err != nil {
		return errors.Wrap(err, "unable to read system hugepage info")
	}

	if cfgHasBdev(cfg) {
		// Double-check that we got the requested number of huge pages after prepare.
		if hugePages.Free < prepReq.HugePageCount {
			return FaultInsufficientFreeHugePages(hugePages.Free, prepReq.HugePageCount)
		}
	}

	return nil
}

type serverComponents struct {
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

func createComponents(ctx context.Context, log *logging.LeveledLogger, cfg *config.Server) (*serverComponents, error) {
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
	fd, ok := faultDomainFromContext(ctx)
	if !ok {
		return nil, contextKeyFaultDomain.errMissing()
	}
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

	return &serverComponents{
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

func addEngines(ctx context.Context, log *logging.LeveledLogger, cfg *config.Server, components *serverComponents) (context.Context, error) {
	ctlAddr, ok := ctlAddrFromContext(ctx)
	if !ok {
		return ctx, contextKeyCtlAddr.errMissing()
	}

	// Create a closure to be used for joining engine instances.
	joinInstance := func(ctxIn context.Context, req *control.SystemJoinReq) (*control.SystemJoinResp, error) {
		req.SetHostList(cfg.AccessPoints)
		req.SetSystem(cfg.SystemName)
		req.ControlAddr = ctlAddr

		return control.SystemJoin(ctxIn, components.mgmtSvc.rpcClient, req)
	}

	for idx, engineCfg := range cfg.Engines {
		// Provide special handling for the ofi+verbs provider.
		// Mercury uses the interface name such as ib0, while OFI uses the
		// device name such as hfi1_0 CaRT and Mercury will now support the
		// new OFI_DOMAIN environment variable so that we can specify the
		// correct device for each.
		if strings.HasPrefix(engineCfg.Fabric.Provider, "ofi+verbs") && !engineCfg.HasEnvVar("OFI_DOMAIN") {
			deviceAlias, err := netdetect.GetDeviceAlias(ctx, engineCfg.Fabric.Interface)
			if err != nil {
				return ctx, errors.Wrapf(err, "failed to resolve alias for %s", engineCfg.Fabric.Interface)
			}
			envVar := "OFI_DOMAIN=" + deviceAlias
			engineCfg.WithEnvVars(envVar)
		}

		// Indicate whether VMD devices have been detected and can be used.
		engineCfg.Storage.Bdev.VmdDisabled = components.bdevProvider.IsVMDDisabled()

		// TODO: ClassProvider should be encapsulated within bdevProvider
		bcp, err := bdev.NewClassProvider(log, engineCfg.Storage.SCM.MountPoint, &engineCfg.Storage.Bdev)
		if err != nil {
			return ctx, err
		}

		engine := NewEngineInstance(log, bcp, components.scmProvider, joinInstance, engine.NewRunner(log, engineCfg)).
			WithHostFaultDomain(components.harness.faultDomain)
		if err := components.harness.AddInstance(engine); err != nil {
			return ctx, err
		}
		// Register callback to publish I/O Engine process exit events.
		engine.OnInstanceExit(publishInstanceExitFn(components.pubSub.Publish, hostname(), engine.Index()))

		if idx == 0 {
			netDevClass, err := cfg.GetDeviceClassFn(engineCfg.Fabric.Interface)
			if err != nil {
				return ctx, err
			}
			ctx = context.WithValue(ctx, contextKeyNetDevClass, netDevClass)

			if !components.sysdb.IsReplica() {
				continue
			}

			// Start the system db after instance 0's SCM is
			// ready.
			var once sync.Once
			engine.OnStorageReady(func(ctxIn context.Context) (err error) {
				once.Do(func() {
					err = errors.Wrap(components.sysdb.Start(ctxIn),
						"failed to start system db",
					)
				})
				return
			})

			if !components.sysdb.IsBootstrap() {
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

	return ctx, nil
}

func setupGrpcServer(ctx context.Context, cfg *config.Server, components *serverComponents) error {
	// Create new grpc server, register services and start serving.
	unaryInterceptors := []grpc.UnaryServerInterceptor{
		unaryErrorInterceptor,
		unaryStatusInterceptor,
	}
	streamInterceptors := []grpc.StreamServerInterceptor{
		streamErrorInterceptor,
	}
	tcOpt, err := security.ServerOptionForTransportConfig(cfg.TransportConfig)
	if err != nil {
		return err
	}
	srvOpts := []grpc.ServerOption{tcOpt}

	uintOpt, err := unaryInterceptorForTransportConfig(cfg.TransportConfig)
	if err != nil {
		return err
	}
	if uintOpt != nil {
		unaryInterceptors = append(unaryInterceptors, uintOpt)
	}
	sintOpt, err := streamInterceptorForTransportConfig(cfg.TransportConfig)
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

	components.grpcServer = grpc.NewServer(srvOpts...)
	ctlpb.RegisterCtlSvcServer(components.grpcServer, components.ctlSvc)

	netDevClass, ok := netDevClassFromContext(ctx)
	if !ok {
		return contextKeyNetDevClass.errMissing()
	}
	components.mgmtSvc.clientNetworkCfg = &config.ClientNetworkCfg{
		Provider:        cfg.Fabric.Provider,
		CrtCtxShareAddr: cfg.Fabric.CrtCtxShareAddr,
		CrtTimeout:      cfg.Fabric.CrtTimeout,
		NetDevClass:     netDevClass,
	}
	mgmtpb.RegisterMgmtSvcServer(components.grpcServer, components.mgmtSvc)

	tSec, err := security.DialOptionForTransportConfig(cfg.TransportConfig)
	if err != nil {
		return err
	}
	components.sysdb.ConfigureTransport(components.grpcServer, tSec)

	return nil
}

func registerEvents(log *logging.LeveledLogger, components *serverComponents) {
	// Forward published actionable events (type RASTypeStateChange) to the
	// management service leader, behavior is updated on leadership change.
	components.pubSub.Subscribe(events.RASTypeStateChange, components.evtForwarder)

	// Log events on the host that they were raised (and first published) on.
	components.pubSub.Subscribe(events.RASTypeAny, components.evtLogger)

	components.sysdb.OnLeadershipGained(func(ctx context.Context) error {
		log.Infof("MS leader running on %s", hostname())
		components.mgmtSvc.startJoinLoop(ctx)

		// Stop forwarding events to MS and instead start handling
		// received forwarded (and local) events.
		components.pubSub.Reset()
		components.pubSub.Subscribe(events.RASTypeAny, components.evtLogger)
		components.pubSub.Subscribe(events.RASTypeStateChange, components.membership)
		components.pubSub.Subscribe(events.RASTypeStateChange, components.sysdb)
		components.pubSub.Subscribe(events.RASTypeStateChange, events.HandlerFunc(func(ctx context.Context, evt *events.RASEvent) {
			switch evt.ID {
			case events.RASSwimRankDead:
				// Mark the rank as unavailable for membership in
				// new pools, etc.
				if err := components.membership.MarkRankDead(system.Rank(evt.Rank)); err != nil {
					log.Errorf("failed to mark rank %d as dead: %s", evt.Rank, err)
					return
				}
				// FIXME CART-944: We should be able to update the
				// primary group in order to remove the dead rank,
				// but for the moment this will cause problems.
				if err := components.mgmtSvc.doGroupUpdate(ctx); err != nil {
					log.Errorf("GroupUpdate failed: %s", err)
				}
			}
		}))

		return nil
	})
	components.sysdb.OnLeadershipLost(func() error {
		log.Infof("MS leader no longer running on %s", hostname())

		// Stop handling received forwarded (in addition to local)
		// events and start forwarding events to the new MS leader.
		components.pubSub.Reset()
		components.pubSub.Subscribe(events.RASTypeAny, components.evtLogger)
		components.pubSub.Subscribe(events.RASTypeStateChange, components.evtForwarder)

		return nil
	})
}

func serverStart(ctx context.Context, shutdown context.CancelFunc, log *logging.LeveledLogger, cfg *config.Server, components *serverComponents) error {
	listener, ok := netListenerFromContext(ctx)
	if !ok {
		return contextKeyNetDevClass.errMissing()
	}

	go func() {
		_ = components.grpcServer.Serve(listener)
	}()
	defer components.grpcServer.Stop()

	ctlAddr, ok := ctlAddrFromContext(ctx)
	if !ok {
		return contextKeyCtlAddr.errMissing()
	}
	log.Infof("%s v%s (pid %d) listening on %s", build.ControlPlaneName, build.DaosVersion, os.Getpid(), ctlAddr)

	sigChan := make(chan os.Signal)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGQUIT, syscall.SIGTERM)
	go func() {
		// SIGKILL I/O Engine immediately on exit.
		// TODO: Re-enable attempted graceful shutdown of I/O Engines.
		sig := <-sigChan
		log.Debugf("Caught signal: %s", sig)

		shutdown()
	}()

	return errors.Wrapf(components.harness.Start(ctx, components.sysdb, components.pubSub, cfg),
		"%s exited with error", build.DataPlaneName)
}

// Start is the entry point for a daos_server instance.
func Start(log *logging.LeveledLogger, cfg *config.Server) (err error) {
	// Create the root context here. All contexts should inherit from this one so
	// that they can be shut down from one place.
	ctx, shutdown := context.WithCancel(context.Background())
	defer shutdown()

	ctx, err = checkConfig(ctx, log, cfg)
	if err != nil {
		return err
	}

	ctx, err = checkNetwork(ctx, log, cfg)
	if err != nil {
		return err
	}
	defer netdetect.CleanUp(ctx)

	components, err := createComponents(ctx, log, cfg)
	if err != nil {
		return err
	}
	defer components.pubSub.Close()

	if err := checkBlockStorage(log, cfg, components.bdevProvider); err != nil {
		return err
	}

	ctx, err = addEngines(ctx, log, cfg, components)
	if err != nil {
		return err
	}

	// Setup the control service by scanning local storage hardware.
	if err := components.ctlSvc.Setup(); err != nil {
		return err
	}

	if err := setupGrpcServer(ctx, cfg, components); err != nil {
		return err
	}

	registerEvents(log, components)

	return serverStart(ctx, shutdown, log, cfg, components)
}
