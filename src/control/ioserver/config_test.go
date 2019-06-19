package ioserver_test

import (
	"fmt"
	"io/ioutil"
	"os"
	"reflect"
	"testing"

	"github.com/daos-stack/daos/src/control/ioserver"
)

func testArgExpected(t *testing.T, cfg *ioserver.Config, expected []string) {
	t.Helper()
	args, err := cfg.CmdLineArgs()
	if err != nil {
		t.Fatal(err)
	}
	if len(args) != len(expected) {
		t.Fatalf("Expected %v length to be %d, but it's %d", args, len(expected), len(args))
	}
	if !reflect.DeepEqual(args, expected) {
		t.Fatalf("expected %v, got %v", expected, args)
	}
}

func testStringArg(t *testing.T, cfg *ioserver.Config, name, expected, arg string) {
	t.Helper()
	if err := cfg.Set(name, arg); err != nil {
		t.Fatal(err)
	}
	testArgExpected(t, cfg, []string{expected})
	cfg.Reset()
}

func testIntArg(t *testing.T, cfg *ioserver.Config, name, expected string, arg int) {
	t.Helper()
	if err := cfg.Set(name, arg); err != nil {
		t.Fatal(err)
	}
	testArgExpected(t, cfg, []string{expected})
	cfg.Reset()
}

func testEnvExpected(t *testing.T, cfg *ioserver.Config, expected []string) {
	t.Helper()
	env, err := cfg.EnvVars()
	if err != nil {
		t.Fatal(err)
	}
	if len(env) != len(expected) {
		t.Fatalf("Expected %v length to be %d, but it's %d", env, len(expected), len(env))
	}
	if !reflect.DeepEqual(env, expected) {
		t.Fatalf("expected %v, got %v", expected, env)
	}
}

func testStringEnv(t *testing.T, cfg *ioserver.Config, key, expected, arg string) {
	t.Helper()
	if err := cfg.Set(key, arg); err != nil {
		t.Fatal(err)
	}
	testEnvExpected(t, cfg, []string{expected})
	cfg.Reset()
}

func testIntEnv(t *testing.T, cfg *ioserver.Config, key, expected string, arg int) {
	t.Helper()
	if err := cfg.Set(key, arg); err != nil {
		t.Fatal(err)
	}
	testEnvExpected(t, cfg, []string{expected})
	cfg.Reset()
}

// TestSetters verifies that there is a test for every settable option
// TODO: Run each option as a subtest? Maybe just for the ones with
// custom validators?
func TestSetters(t *testing.T) {
	cfg := ioserver.NewConfig()
	for name, opt := range cfg.SettableOptions {
		switch opt := opt.(type) {
		case *ioserver.CmdLineArg:
			switch name {
			case "storagePath":
				testVal := "/mnt/foo/bar"
				expected := fmt.Sprintf("--%s=%s", opt.LongOption, testVal)
				testStringArg(t, cfg, name, expected, testVal)
			case "serverGroup":
				testVal := "super_daos_server"
				expected := fmt.Sprintf("--%s=%s", opt.LongOption, testVal)
				testStringArg(t, cfg, name, expected, testVal)
			case "socketDir":
				testDir, err := ioutil.TempDir("", fmt.Sprintf("%s-*-socketDir", t.Name()))
				if err != nil {
					t.Fatal(err)
				}
				defer os.RemoveAll(testDir)
				expected := fmt.Sprintf("--%s=%s", opt.LongOption, testDir)
				testStringArg(t, cfg, name, expected, testDir)
			case "sharedSegmentID":
				testVal := 12345
				expected := fmt.Sprintf("--%s=%d", opt.LongOption, testVal)
				testIntArg(t, cfg, name, expected, testVal)
			case "targetCount":
				testVal := 42
				expected := fmt.Sprintf("--%s=%d", opt.LongOption, testVal)
				testIntArg(t, cfg, name, expected, testVal)
			case "serviceThreadCore":
				testVal := 3
				expected := fmt.Sprintf("--%s=%d", opt.LongOption, testVal)
				testIntArg(t, cfg, name, expected, testVal)
			case "xsHelperCount":
				testVal := 2
				expected := fmt.Sprintf("--%s=%d", opt.LongOption, testVal)
				testIntArg(t, cfg, name, expected, testVal)
			case "pathToNVMeConfig":
				testFile, err := ioutil.TempFile("", fmt.Sprintf("%s-*-pathToNVMeConfig", t.Name()))
				if err != nil {
					t.Fatal(err)
				}
				defer os.Remove(testFile.Name())
				expected := fmt.Sprintf("--%s=%s", opt.LongOption, testFile.Name())
				testStringArg(t, cfg, name, expected, testFile.Name())
			case "attachInfoPath":
				testDir, err := ioutil.TempDir("", fmt.Sprintf("%s-*-attachInfoPath", t.Name()))
				if err != nil {
					t.Fatal(err)
				}
				defer os.RemoveAll(testDir)
				expected := fmt.Sprintf("--%s=%s", opt.LongOption, testDir)
				testStringArg(t, cfg, name, expected, testDir)
			case "serverModules":
				// simple case
				testVal := "foo,bar,baz"
				expected := fmt.Sprintf("--%s=%s", opt.LongOption, testVal)
				testStringArg(t, cfg, name, expected, testVal)

				// variadic args
				if err := cfg.Set(name, "foo", "bar", "baz"); err != nil {
					t.Fatal(err)
				}
				testArgExpected(t, cfg, []string{expected})
				cfg.Reset()
			case "serverRank":
				rank := uint32(128)
				expected := fmt.Sprintf("--%s=%d", opt.LongOption, rank)
				if err := cfg.Set(name, rank); err != nil {
					t.Fatal(err)
				}
				testArgExpected(t, cfg, []string{expected})
				cfg.Reset()
			default:
				t.Fatalf("Untested option: %s", name)
			}
		case *ioserver.EnvVar:
			switch name {
			case "metadataCap":
				testVal := 1024
				expected := fmt.Sprintf("%s=%d", opt.Key, testVal)
				testIntEnv(t, cfg, name, expected, testVal)
			case "fabricInterface":
				testVal := "eth1000"
				expected := fmt.Sprintf("%s=%s", opt.Key, testVal)
				testStringEnv(t, cfg, name, expected, testVal)
			case "cartTimeout":
				testVal := 30
				expected := fmt.Sprintf("%s=%d", opt.Key, testVal)
				testIntEnv(t, cfg, name, expected, testVal)
			case "cartProvider":
				testVal := "foo+bar"
				expected := fmt.Sprintf("%s=%s", opt.Key, testVal)
				testStringEnv(t, cfg, name, expected, testVal)
			case "cartContextShareAddress":
				testVal := 11
				expected := fmt.Sprintf("%s=%d", opt.Key, testVal)
				testIntEnv(t, cfg, name, expected, testVal)
			case "logFile":
				testVal := "/path/to/logfile"
				expected := fmt.Sprintf("%s=%s", opt.Key, testVal)
				testStringEnv(t, cfg, name, expected, testVal)
			case "logMask":
				// simple case
				testVal := "oink,moo=err,squeak"
				expected := fmt.Sprintf("%s=%s", opt.Key, testVal)
				testStringEnv(t, cfg, name, expected, testVal)

				// variadic args
				if err := cfg.Set(name, "oink", "moo=err", "squeak"); err != nil {
					t.Fatal(err)
				}
				testEnvExpected(t, cfg, []string{expected})
				cfg.Reset()
			case "debugSubsystems":
				// simple case
				testVal := "bip,blorp,nom"
				expected := fmt.Sprintf("%s=%s", opt.Key, testVal)
				testStringEnv(t, cfg, name, expected, testVal)

				// variadic args
				if err := cfg.Set(name, "bip", "blorp", "nom"); err != nil {
					t.Fatal(err)
				}
				testEnvExpected(t, cfg, []string{expected})
				cfg.Reset()
			default:
				t.Fatalf("Untested option: %s", name)
			}
		default:
			t.Fatalf("Untested option: %s", name)
		}
	}
}

func TestMultiple(t *testing.T) {
	cfg := ioserver.NewConfig()

	if err := cfg.Set("debugSubsystems", "cow,dog,cat"); err != nil {
		t.Fatal(err)
	}
	if err := cfg.Set("serviceThreadCore", 11); err != nil {
		t.Fatal(err)
	}

	cfg.WithFabricInterface("realFast0")
	cfg.WithTargetCount(42)

	testEnvExpected(t, cfg, []string{
		"DD_SUBSYS=cow,dog,cat",
		"OFI_INTERFACE=realFast0",
	})
	testArgExpected(t, cfg, []string{
		"--firstcore=11",
		"--targets=42",
	})
}
