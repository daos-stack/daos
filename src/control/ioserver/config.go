package ioserver

import (
	"fmt"
	"os"
	"strconv"
	"strings"
	"unicode"
)

type (
	// ValidationError represents an error found while validating
	// an option
	ValidationError struct {
		Option       string
		ErrorMessage string
	}

	// SettableOption defines an interface to be implemented
	// by types which resolve a configuration option to a
	// command line argument or an environment variable
	SettableOption interface {
		Name() string
		Set(...interface{}) error
	}

	setterFunc func(...interface{}) error

	// CmdLineArg represents an option which resolves to a
	// command line argument (switch or option) passed to
	// an instance on start
	CmdLineArg struct {
		Description string
		LongOption  string
		ShortOption string
		Value       string

		optionName    string
		isEnabledBool bool
		validateFunc  func(*CmdLineArg) error
		setterFunc    setterFunc
	}

	// EnvVar represents an option which resolves
	// to an environment variable set in the instance's
	// environment before starting
	EnvVar struct {
		Description string
		Key         string
		Value       string

		optionName   string
		validateFunc func(*EnvVar) error
		setterFunc   setterFunc
	}

	// Config represents an instance's configuration
	// and comprises command line arguments as well
	// as environment variables
	Config struct {
		SystemName               string
		ShouldReformatSuperblock bool

		SettableOptions map[string]SettableOption

		setCmdLineArgs   []*CmdLineArg
		setEnvVars       []*EnvVar
		availableOptions []SettableOption
	}
)

// NewConfig returns an initialized *Config with all
// available options defined but not set
func NewConfig() *Config {
	cfg := &Config{
		SettableOptions: make(map[string]SettableOption),
	}
	cfg.availableOptions = []SettableOption{
		&CmdLineArg{
			optionName:  "storagePath",
			Description: "SCM mount path",
			LongOption:  "storage",
			ShortOption: "s",
			setterFunc: func(i ...interface{}) error {
				arg, err := getSingleStringArg("storagePath", i...)
				if err != nil {
					return err
				}
				cfg.WithStoragePath(arg)
				return nil
			},
		},
		&CmdLineArg{
			optionName:  "serverRank",
			Description: "Server rank",
			LongOption:  "rank",
			ShortOption: "r",
			setterFunc: func(i ...interface{}) error {
				if len(i) == 1 {
					if arg, ok := i[0].(uint32); ok {
						cfg.WithRank(arg)
						return nil
					}
				}
				return vErr("serverRank", "invalid Set() arguments")
			},
		},
		&CmdLineArg{
			optionName:  "targetCount",
			Description: "Number of targets to use",
			LongOption:  "targets",
			ShortOption: "t",
			setterFunc: func(i ...interface{}) error {
				arg, err := getSingleIntArg("targetCount", i...)
				if err != nil {
					return err
				}
				cfg.WithTargetCount(arg)
				return nil
			},
		},
		&CmdLineArg{
			optionName:  "serviceThreadCore",
			Description: "Index of core to be used for service thread",
			LongOption:  "firstcore",
			ShortOption: "f",
			setterFunc: func(i ...interface{}) error {
				arg, err := getSingleIntArg("serviceThreadCore", i...)
				if err != nil {
					return err
				}
				cfg.WithServiceThreadCore(arg)
				return nil
			},
		},
		&CmdLineArg{
			optionName:  "xsHelperCount",
			Description: "Number of XS helpers per VOS target",
			LongOption:  "xshelpernr",
			ShortOption: "x",
			setterFunc: func(i ...interface{}) error {
				arg, err := getSingleIntArg("xsHelperCount", i...)
				if err != nil {
					return err
				}
				cfg.WithXSHelperCount(arg)
				return nil
			},
			validateFunc: func(arg *CmdLineArg) error {
				if arg.Value != "0" && arg.Value != "2" {
					return vErr("xsHelperCount", "must be either 0 or 2")
				}
				return nil
			},
		},
		&CmdLineArg{
			optionName:  "pathToNVMeConfig",
			Description: "Path to NVMe configuration file",
			LongOption:  "nvme",
			ShortOption: "n",
			setterFunc: func(i ...interface{}) error {
				arg, err := getSingleStringArg("pathToNVMeConfig", i...)
				if err != nil {
					return err
				}
				cfg.WithPathToNVMeConfig(arg)
				return nil
			},
			validateFunc: func(arg *CmdLineArg) error {
				opt := "pathToNVMeConfig"
				if arg.Value == "" {
					return vErr(opt, "cannot be empty")
				}
				_, err := os.Stat(arg.Value)
				if err != nil {
					return vErr(opt, err.Error())
				}
				return nil
			},
		},
		&CmdLineArg{
			optionName:  "attachInfoPath",
			Description: "Attach info path (non-PMIx clients)",
			LongOption:  "attach_info",
			ShortOption: "a",
			setterFunc: func(i ...interface{}) error {
				arg, err := getSingleStringArg("attachInfoPath", i...)
				if err != nil {
					return err
				}
				cfg.WithAttachInfoPath(arg)
				return nil
			},
			validateFunc: func(arg *CmdLineArg) error {
				opt := "attachInfoPath"
				if arg.Value == "" {
					return vErr(opt, "cannot be empty")
				}
				st, err := os.Stat(arg.Value)
				if err != nil {
					return vErr(opt, err.Error())
				}
				if !st.IsDir() {
					return vErr(opt, "must be a valid directory")
				}
				return nil
			},
		},
		&CmdLineArg{
			optionName:  "serverModules",
			Description: "Server modules to load",
			LongOption:  "modules",
			ShortOption: "m",
			setterFunc: func(args ...interface{}) error {
				stringArgs, err := toStringArgs("serverModules", args...)
				if err != nil {
					return err
				}
				cfg.WithModules(stringArgs...)
				return nil
			},
		},
		&CmdLineArg{
			optionName:  "serverGroup",
			Description: "Server group name",
			LongOption:  "group",
			ShortOption: "g",
			setterFunc: func(i ...interface{}) error {
				arg, err := getSingleStringArg("serverGroup", i...)
				if err != nil {
					return err
				}
				cfg.WithServerGroup(arg)
				return nil
			},
		},
		&CmdLineArg{
			optionName:  "socketDir",
			Description: "Directory for daos_io_server sockets",
			LongOption:  "socket_dir",
			ShortOption: "d",
			setterFunc: func(i ...interface{}) error {
				arg, err := getSingleStringArg("socketDir", i...)
				if err != nil {
					return err
				}
				cfg.WithSocketDir(arg)
				return nil
			},
			validateFunc: func(arg *CmdLineArg) error {
				opt := "socketDir"
				if arg.Value == "" {
					return vErr(opt, "cannot be empty")
				}
				st, err := os.Stat(arg.Value)
				if err != nil {
					return vErr(opt, err.Error())
				}
				if !st.IsDir() {
					return vErr(opt, "must be a valid directory")
				}
				return nil
			},
		},
		&CmdLineArg{
			optionName:  "sharedSegmentID",
			Description: "Shared segment ID for multiprocess mode in SPDK",
			LongOption:  "shm_id",
			ShortOption: "i",
			setterFunc: func(i ...interface{}) error {
				arg, err := getSingleIntArg("sharedSegmentID", i...)
				if err != nil {
					return err
				}
				cfg.WithSharedSegmentID(arg)
				return nil
			},
		},
		&EnvVar{
			optionName:  "cartTimeout",
			Description: "CaRT RPC Timeout",
			Key:         "CRT_TIMEOUT",
			setterFunc: func(i ...interface{}) error {
				arg, err := getSingleIntArg("cartTimeout", i...)
				if err != nil {
					return err
				}
				cfg.WithCartTimeout(arg)
				return nil
			},
		},
		&EnvVar{
			optionName:  "cartProvider",
			Description: "CaRT provider",
			Key:         "CRT_PHY_ADDR_STR",
			setterFunc: func(i ...interface{}) error {
				arg, err := getSingleStringArg("cartProvider", i...)
				if err != nil {
					return err
				}
				cfg.WithCartProvider(arg)
				return nil
			},
		},
		&EnvVar{
			optionName:  "cartContextShareAddress",
			Description: "CaRT context share address",
			Key:         "CRT_CTX_SHARE_ADDR",
			setterFunc: func(i ...interface{}) error {
				arg, err := getSingleIntArg("cartContextShareAddress", i...)
				if err != nil {
					return err
				}
				cfg.WithCartContextShareAddress(arg)
				return nil
			},
		},
		&EnvVar{
			optionName:  "fabricInterface",
			Description: "OFI interface",
			Key:         "OFI_INTERFACE",
			setterFunc: func(i ...interface{}) error {
				arg, err := getSingleStringArg("fabricInterface", i...)
				if err != nil {
					return err
				}
				cfg.WithFabricInterface(arg)
				return nil
			},
		},
		&EnvVar{
			optionName: "debugSubsystems",
			Key:        "DD_SUBSYS",
			setterFunc: func(args ...interface{}) error {
				stringArgs, err := toStringArgs("debugSubsystems", args...)
				if err != nil {
					return err
				}
				cfg.WithDebugSubsystems(stringArgs...)
				return nil
			},
		},
		&EnvVar{
			optionName:  "logMask",
			Description: "Log mask",
			Key:         "D_LOG_MASK",
			setterFunc: func(args ...interface{}) error {
				stringArgs, err := toStringArgs("logMask", args...)
				if err != nil {
					return err
				}
				cfg.WithLogMask(stringArgs...)
				return nil
			},
		},
		&EnvVar{
			optionName:  "logFile",
			Description: "Path to log file",
			Key:         "D_LOG_FILE",
			setterFunc: func(i ...interface{}) error {
				arg, err := getSingleStringArg("logFile", i...)
				if err != nil {
					return err
				}
				cfg.WithLogFile(arg)
				return nil
			},
		},
		&EnvVar{
			optionName:  "metadataCap",
			Description: "DAOS metadata cap",
			Key:         "DAOS_MD_CAP",
			setterFunc: func(i ...interface{}) error {
				arg, err := getSingleIntArg("metadataCap", i...)
				if err != nil {
					return err
				}
				cfg.WithMetadataCap(arg)
				return nil
			},
		},
	}

	// build up a map of name -> opt so that we don't
	// have to define it by hand
	for _, opt := range cfg.availableOptions {
		cfg.SettableOptions[opt.Name()] = opt
	}
	return cfg
}

// WithRank sets the MPI rank
func (cfg *Config) WithRank(rank uint32) *Config {
	arg := cfg.getArg("serverRank")
	arg.Value = strconv.Itoa(int(rank))
	return cfg.updateArgument(arg)
}

// WithTargetCount sets the desired number of targets to use
func (cfg *Config) WithTargetCount(numTargets int) *Config {
	arg := cfg.getArg("targetCount")
	arg.Value = strconv.Itoa(numTargets)
	return cfg.updateArgument(arg)
}

// WithStoragePath sets the path to the SCM mountpoint
func (cfg *Config) WithStoragePath(mountPoint string) *Config {
	arg := cfg.getArg("storagePath")
	arg.Value = mountPoint
	return cfg.updateArgument(arg)
}

// WithServiceThreadCore sets the core to be used by the service thread
func (cfg *Config) WithServiceThreadCore(idx int) *Config {
	arg := cfg.getArg("serviceThreadCore")
	arg.Value = strconv.Itoa(idx)
	return cfg.updateArgument(arg)

}

// WithXSHelperCount sets the number of XS helpers per VOS target
func (cfg *Config) WithXSHelperCount(numHelpers int) *Config {
	arg := cfg.getArg("xsHelperCount")
	arg.Value = strconv.Itoa(numHelpers)
	return cfg.updateArgument(arg)
}

// WithModules sets the list of server modules to be loaded
func (cfg *Config) WithModules(modList ...string) *Config {
	arg := cfg.getArg("serverModules")
	arg.Value = strings.Join(modList, ",")
	return cfg.updateArgument(arg)
}

// WithServerGroup sets the DAOS server group identifier
func (cfg *Config) WithServerGroup(groupName string) *Config {
	arg := cfg.getArg("serverGroup")
	arg.Value = groupName
	return cfg.updateArgument(arg)
}

// WithSocketDir sets the path to the socket directory
func (cfg *Config) WithSocketDir(socketDir string) *Config {
	arg := cfg.getArg("socketDir")
	arg.Value = socketDir
	return cfg.updateArgument(arg)
}

// WithPathToNVMeConfig sets the path to the NVMe configuration
// file to be used by SPDK
func (cfg *Config) WithPathToNVMeConfig(cfgPath string) *Config {
	arg := cfg.getArg("pathToNVMeConfig")
	arg.Value = cfgPath
	return cfg.updateArgument(arg)
}

// WithSharedSegmentID sets the NVMe shared segment ID
func (cfg *Config) WithSharedSegmentID(id int) *Config {
	arg := cfg.getArg("sharedSegmentID")
	arg.Value = strconv.Itoa(id)
	return cfg.updateArgument(arg)
}

// WithAttachInfoPath sets the path to the, um, attachInfo, whatever that is
func (cfg *Config) WithAttachInfoPath(attachPath string) *Config {
	arg := cfg.getArg("attachInfoPath")
	arg.Value = attachPath
	return cfg.updateArgument(arg)
}

// WithCartTimeout sets the timeout Value for CaRT RPCs
func (cfg *Config) WithCartTimeout(timeout int) *Config {
	ev := cfg.getEnv("cartTimeout")
	ev.Value = strconv.Itoa(timeout)
	return cfg.updateEnvironment(ev)
}

// WithCartProvider sets the CaRT provider string
func (cfg *Config) WithCartProvider(provString string) *Config {
	ev := cfg.getEnv("cartProvider")
	ev.Value = provString
	return cfg.updateEnvironment(ev)
}

// WithFabricInterface sets the fabric interface to be used by this instance
func (cfg *Config) WithFabricInterface(ifaceString string) *Config {
	ev := cfg.getEnv("fabricInterface")
	ev.Value = ifaceString
	return cfg.updateEnvironment(ev)
}

// WithLogMask sets the logging subsystem mask
func (cfg *Config) WithLogMask(masks ...string) *Config {
	ev := cfg.getEnv("logMask")
	ev.Value = strings.Join(masks, ",")
	return cfg.updateEnvironment(ev)
}

// WithLogFile sets the path to the log file
func (cfg *Config) WithLogFile(logFile string) *Config {
	ev := cfg.getEnv("logFile")
	ev.Value = logFile
	return cfg.updateEnvironment(ev)
}

// WithMetadataCap sets a cap on metadata or something
func (cfg *Config) WithMetadataCap(cap int) *Config {
	ev := cfg.getEnv("metadataCap")
	ev.Value = strconv.Itoa(cap)
	return cfg.updateEnvironment(ev)
}

// WithCartContextShareAddress sets this thing whatever it is
func (cfg *Config) WithCartContextShareAddress(shareAddr int) *Config {
	ev := cfg.getEnv("cartContextShareAddress")
	ev.Value = strconv.Itoa(shareAddr)
	return cfg.updateEnvironment(ev)
}

// WithDebugSubsystems sets this Value
func (cfg *Config) WithDebugSubsystems(subsystems ...string) *Config {
	ev := cfg.getEnv("debugSubsystems")
	ev.Value = strings.Join(subsystems, ",")
	return cfg.updateEnvironment(ev)
}

// CmdLineArgs validates all options which resolve to
// command line arguments and returns them as a []string
// suitable for use with an *exec.Cmd
func (cfg *Config) CmdLineArgs() ([]string, error) {
	args := make([]string, 0, len(cfg.setCmdLineArgs))

	for _, arg := range cfg.setCmdLineArgs {
		if arg.Value == "" && !arg.isEnabledBool {
			return nil, vErr(arg.optionName, "cannot have an empty Value")
		}
		if hasWhiteSpace(arg.Value) {
			return nil, vErr(arg.optionName, "cannot have whitespace in Value")
		}
		if arg.validateFunc != nil {
			if err := arg.validateFunc(arg); err != nil {
				return nil, err
			}
		}
		args = append(args, arg.String())
	}

	return args, nil
}

// EnvVars validates all options which resolve to
// environment variables and returns them as a []string
// suitable for use with an *exec.Cmd
func (cfg *Config) EnvVars() ([]string, error) {
	vars := make([]string, 0, len(cfg.setEnvVars))

	// TODO: validateFunc these as a set?
	for _, ev := range cfg.setEnvVars {
		if ev.validateFunc != nil {
			if ev.Value == "" {
				return nil, vErr(ev.optionName, "cannot have an empty Value")
			}
			if hasWhiteSpace(ev.Value) {
				return nil, vErr(ev.optionName, "cannot have whitespace in Value")
			}
			if err := ev.validateFunc(ev); err != nil {
				return nil, err
			}
		}
		vars = append(vars, ev.String())
	}

	return vars, nil
}

// Set attempts to resolve the supplied option name to a SettableOption
// and set the provided value
func (cfg *Config) Set(optionName string, args ...interface{}) error {
	if opt, found := cfg.SettableOptions[optionName]; found {
		return opt.Set(args...)
	}
	return fmt.Errorf("%q is not a valid option", optionName)
}

// Get attempts to resolve the supplied option name to a SettableOption
// and returns the set value
func (cfg *Config) Get(optionName string) (string, error) {
	if opt, found := cfg.SettableOptions[optionName]; found {
		switch opt := opt.(type) {
		case *CmdLineArg:
			for _, setOpt := range cfg.setCmdLineArgs {
				if setOpt.optionName == opt.optionName {
					return setOpt.Value, nil
				}
			}
			return "", fmt.Errorf("option %q was not set", optionName)
		case *EnvVar:
			for _, setOpt := range cfg.setEnvVars {
				if setOpt.optionName == opt.optionName {
					return setOpt.Value, nil
				}
			}
			return "", fmt.Errorf("option %q was not set", optionName)
		default:
			return "", fmt.Errorf("unhandled option %q", optionName)
		}
	}
	return "", fmt.Errorf("%q is not a valid option", optionName)
}

// Reset reverts the *Config to an unset state
func (cfg *Config) Reset() {
	cfg.setCmdLineArgs = nil
	cfg.setEnvVars = nil
}

func (ve *ValidationError) Error() string {
	return fmt.Sprintf("Config validation error: %s: %s", ve.Option, ve.ErrorMessage)
}

func vErr(option, message string) *ValidationError {
	return &ValidationError{
		Option:       option,
		ErrorMessage: message,
	}
}

func hasWhiteSpace(in string) bool {
	for _, r := range in {
		if unicode.IsSpace(r) {
			return true
		}
	}
	return false
}

func toStringArgs(optionName string, args ...interface{}) ([]string, error) {
	stringArgs := make([]string, len(args))
	for idx, val := range args {
		if str, ok := val.(string); ok {
			stringArgs[idx] = str
			continue
		}
		return nil, vErr(optionName, "invalid input")
	}
	return stringArgs, nil
}

func getSingleStringArg(optionName string, i ...interface{}) (string, error) {
	if len(i) == 1 {
		if arg, ok := i[0].(string); ok {
			return arg, nil
		}
	}
	return "", vErr(optionName, "invalid input")
}

func getSingleIntArg(optionName string, i ...interface{}) (int, error) {
	if len(i) == 1 {
		if arg, ok := i[0].(int); ok {
			return arg, nil
		}
	}
	return 0, vErr(optionName, "invalid input")
}

// Name returns the configuration name
func (ev *EnvVar) Name() string {
	return ev.optionName
}

// Set sets the value
func (ev *EnvVar) Set(args ...interface{}) error {
	if ev.setterFunc == nil {
		return fmt.Errorf("EnvVar %s has no setterFunc", ev.optionName)
	}
	return ev.setterFunc(args...)
}

func (ev *EnvVar) String() string {
	return fmt.Sprintf("%s=%s", ev.Key, ev.Value)
}

// Name returns the configuration name
func (arg *CmdLineArg) Name() string {
	return arg.optionName
}

// Set sets the value
func (arg *CmdLineArg) Set(args ...interface{}) error {
	if arg.setterFunc == nil {
		return fmt.Errorf("CmdLineArg %s has no setterFunc", arg.optionName)
	}
	return arg.setterFunc(args...)
}

func (arg *CmdLineArg) String() string {
	if arg.isEnabledBool {
		if arg.LongOption != "" {
			return fmt.Sprintf("--%s", arg.LongOption)
		}
		return fmt.Sprintf("-%s", arg.ShortOption)
	}
	if arg.LongOption != "" {
		return fmt.Sprintf("--%s=%s", arg.LongOption, arg.Value)
	}
	return fmt.Sprintf("-%s %s", arg.ShortOption, arg.Value)
}

func (cfg *Config) getArg(argName string) *CmdLineArg {
	// NB: May panic if misused!
	return cfg.SettableOptions[argName].(*CmdLineArg)
}

func (cfg *Config) getEnv(envName string) *EnvVar {
	// NB: May panic if misused!
	return cfg.SettableOptions[envName].(*EnvVar)
}

func (cfg *Config) updateEnvironment(newEnv *EnvVar) *Config {
	for i, ev := range cfg.setEnvVars {
		if ev.optionName == newEnv.optionName {
			cfg.setEnvVars[i] = newEnv
			return cfg
		}
	}
	cfg.setEnvVars = append(cfg.setEnvVars, newEnv)
	return cfg
}

func (cfg *Config) updateArgument(newArg *CmdLineArg) *Config {
	for i, arg := range cfg.setCmdLineArgs {
		if arg.optionName == newArg.optionName {
			cfg.setCmdLineArgs[i] = newArg
			return cfg
		}
	}
	cfg.setCmdLineArgs = append(cfg.setCmdLineArgs, newArg)
	return cfg
}
