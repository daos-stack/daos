//
// (C) Copyright 2018-2020 Intel Corporation.
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
	"context"
	"fmt"
	"os"
	"os/signal"
	"path"
	"path/filepath"
	"syscall"

	flags "github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	agentSockName     = "agent.sock"
	defaultConfigFile = "daos_agent.yml"
)

type cliOptions struct {
	AllowProxy bool       `long:"allow-proxy" description:"Allow proxy configuration via environment"`
	Debug      bool       `short:"d" long:"debug" description:"Enable debug output"`
	JSONLog    bool       `short:"J" long:"json-logging" description:"Enable JSON-formatted log output"`
	ConfigPath string     `short:"o" long:"config-path" description:"Path to agent configuration file"`
	Insecure   bool       `short:"i" long:"insecure" description:"have agent attempt to connect without certificates"`
	RuntimeDir string     `short:"s" long:"runtime_dir" description:"Path to agent communications socket"`
	LogFile    string     `short:"l" long:"logfile" description:"Full path and filename for daos agent log file"`
	NetworkDbg bool       `short:"n" long:"netdebug" description:"Enable extended network device scan debug output"`
	Version    versionCmd `command:"version" description:"Print daos_agent version"`
	NetScan    netScanCmd `command:"scan" description:"Perform local fabric scan"`
}

type versionCmd struct{}
type netScanCmd struct {
	Debug          bool   `short:"d" long:"debug" description:"Enable debug output"`
	FabricProvider string `short:"p" long:"provider" description:"Filter device list to those that support the given OFI provider (default is all providers)"`
}

func (cmd *versionCmd) Execute(_ []string) error {
	fmt.Printf("daos_agent version %s\n", build.DaosVersion)
	os.Exit(0)
	return nil
}

func (cmd *netScanCmd) Execute(args []string) error {
	var provider string
	defer os.Exit(0)
	log := logging.NewCommandLineLogger()

	if cmd.Debug {
		log.WithLogLevel(logging.LogLevelDebug)
		netdetect.SetLogger(log)
	}

	numaAware, err := netdetect.NumaAware()
	if err != nil {
		exitWithError(log, err)
		return nil
	}

	if !numaAware {
		fmt.Printf("This system is not NUMA aware")
	}

	switch {
	case len(cmd.FabricProvider) > 0:
		provider = cmd.FabricProvider
		fmt.Printf("Scanning fabric for provider: %s\n", provider)
	default:
		fmt.Printf("Scanning fabric for all providers\n")
	}

	results, err := netdetect.ScanFabric(provider)
	if err != nil {
		exitWithError(log, err)
		return nil
	}

	if provider == "" {
		provider = "All"
	}
	fmt.Printf("\nFabric scan found %d devices matching the provider spec: %s\n\n", len(results), provider)

	for _, sr := range results {
		fmt.Printf("%v\n\n", sr)
	}

	return nil
}

func exitWithError(log logging.Logger, err error) {
	log.Errorf("%s: %v", path.Base(os.Args[0]), err)
	os.Exit(1)
}

func main() {
	var opts cliOptions
	log := logging.NewCommandLineLogger()

	if err := agentMain(log, &opts); err != nil {
		exitWithError(log, err)
	}
}

// applyCmdLineOverrides will overwrite Configuration values with any non empty
// data provided, usually from the commandline.
func applyCmdLineOverrides(log logging.Logger, c *client.Configuration, opts *cliOptions) {

	if opts.RuntimeDir != "" {
		log.Debugf("Overriding socket path from config file with %s", opts.RuntimeDir)
		c.RuntimeDir = opts.RuntimeDir
	}

	if opts.LogFile != "" {
		log.Debugf("Overriding LogFile path from config file with %s", opts.LogFile)
		c.LogFile = opts.LogFile
	}
	if opts.Insecure {
		log.Debugf("Overriding AllowInsecure from config file with %t", opts.Insecure)
		c.TransportConfig.AllowInsecure = true
	}
}

func agentMain(log *logging.LeveledLogger, opts *cliOptions) error {
	log.Info("Starting daos_agent:")

	p := flags.NewParser(opts, flags.HelpFlag|flags.PassDoubleDash)
	p.SubcommandsOptional = true

	_, err := p.Parse()
	if err != nil {
		return err
	}

	if !opts.AllowProxy {
		common.ScrubProxyVariables()
	}

	if opts.JSONLog {
		log.WithJSONOutput()
	}

	if opts.Debug {
		log.WithLogLevel(logging.LogLevelDebug)
		log.Debug("debug output enabled")
	}

	if opts.NetworkDbg {
		if !opts.Debug {
			log.WithLogLevel(logging.LogLevelDebug)
		}
		log.Debug("extended network debug enabled")
		netdetect.SetLogger(log)
	}

	ctx, shutdown := context.WithCancel(context.Background())
	defer shutdown()

	if opts.ConfigPath == "" {
		defaultConfigPath := path.Join(build.ConfigDir, defaultConfigFile)
		if _, err := os.Stat(defaultConfigPath); err == nil {
			opts.ConfigPath = defaultConfigPath
		}
	}

	// Load the configuration file using the supplied path or the
	// default path if none provided.
	config, err := client.GetConfig(log, opts.ConfigPath)
	if err != nil {
		log.Errorf("An unrecoverable error occurred while processing the configuration file: %s", err)
		return err
	}

	// Override configuration with any commandline values given
	applyCmdLineOverrides(log, config, opts)

	sockPath := filepath.Join(config.RuntimeDir, agentSockName)
	log.Debugf("Full socket path is now: %s", sockPath)

	if config.LogFile != "" {
		f, err := common.AppendFile(config.LogFile)
		if err != nil {
			log.Errorf("Failure creating log file: %s", err)
			return err
		}
		defer f.Close()
		log.Infof("Using logfile: %s", config.LogFile)

		// Create an additional set of loggers which append everything
		// to the specified file.
		log.WithErrorLogger(logging.NewErrorLogger("agent", f)).
			WithInfoLogger(logging.NewInfoLogger("agent", f)).
			WithDebugLogger(logging.NewDebugLogger(f))
	}

	err = config.TransportConfig.PreLoadCertData()
	if err != nil {
		return errors.Wrap(err, "Unable to load Cerificate Data")
	}

	// Setup signal handlers so we can block till we get SIGINT or SIGTERM
	signals := make(chan os.Signal, 1)
	finish := make(chan bool, 1)
	signal.Notify(signals, syscall.SIGINT, syscall.SIGTERM)

	drpcServer, err := drpc.NewDomainSocketServer(ctx, log, sockPath)
	if err != nil {
		log.Errorf("Unable to create socket server: %v", err)
		return err
	}

	enabled := atm.NewBool(os.Getenv("DAOS_AGENT_DISABLE_CACHE") != "true")
	if enabled.IsFalse() {
		log.Debugf("GetAttachInfo agent caching has been disabled\n")
	}

	numaAware, err := netdetect.NumaAware()
	if err != nil {
		return err
	}

	if !numaAware {
		log.Debugf("This system is not NUMA aware")
	}

	drpcServer.RegisterRPCModule(NewSecurityModule(log, config.TransportConfig))
	drpcServer.RegisterRPCModule(&mgmtModule{
		log:       log,
		sys:       config.SystemName,
		ap:        config.AccessPoints[0],
		tcfg:      config.TransportConfig,
		aiCache:   &attachInfoCache{log: log, enabled: enabled},
		numaAware: numaAware,
	})

	err = drpcServer.Start()
	if err != nil {
		log.Errorf("Unable to start socket server on %s: %v", sockPath, err)
		return err
	}

	log.Infof("Listening on %s", sockPath)

	// Anonymous goroutine to wait on the signals channel and tell the
	// program to finish when it receives a signal. Since we only notify on
	// SIGINT and SIGTERM we should only catch this on a kill or ctrl+c
	// The syntax looks odd but <- Channel means wait on any input on the
	// channel.
	go func() {
		<-signals
		finish <- true
	}()
	<-finish
	drpcServer.Shutdown()

	return nil
}
