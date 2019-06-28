package client_test

import (
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"reflect"
	"testing"

	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/common"
)

func getDefaultConfig(t *testing.T) *client.Configuration {
	t.Helper()

	defaultConfig := client.NewConfiguration()
	absPath, err := common.GetAbsInstallPath(defaultConfig.Path)
	if err != nil {
		t.Fatal(err)
	}
	defaultConfig.Path = absPath

	return defaultConfig
}

func getTestFile(t *testing.T) *os.File {
	t.Helper()

	testFile, err := ioutil.TempFile("", t.Name())
	if err != nil {
		t.Fatal(err)
	}

	return testFile
}

func TestLoadConfigDefaultsNoFile(t *testing.T) {
	defaultConfig := getDefaultConfig(t)

	cfg, err := client.GetConfig("")
	if err != nil {
		t.Fatal(err)
	}

	if !reflect.DeepEqual(cfg, defaultConfig) {
		t.Fatalf("%v != %v", cfg, defaultConfig)
	}
}

func TestLoadConfigFromDefaultFile(t *testing.T) {
	defaultConfig := getDefaultConfig(t)
	if err := os.MkdirAll(path.Dir(defaultConfig.Path), 0755); err != nil {
		t.Fatal(err)
	}
	f, err := os.Create(defaultConfig.Path)
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(defaultConfig.Path)

	_, err = f.WriteString(fmt.Sprintf("name: %s\n", t.Name()))
	if err != nil {
		t.Fatal(err)
	}
	f.Close()

	cfg, err := client.GetConfig("")
	if err != nil {
		t.Fatal(err)
	}

	if cfg.SystemName != t.Name() {
		t.Fatalf("expected %q, got %q", t.Name(), cfg.SystemName)
	}
}

func TestLoadConfigFromFile(t *testing.T) {
	testFile := getTestFile(t)
	defer os.Remove(testFile.Name())

	_, err := testFile.WriteString(fmt.Sprintf("name: %s\n", t.Name()))
	if err != nil {
		t.Fatal(err)
	}
	testFile.Close()

	cfg, err := client.GetConfig(testFile.Name())
	if err != nil {
		t.Fatal(err)
	}

	if cfg.SystemName != t.Name() {
		t.Fatalf("expected %q, got %q", t.Name(), cfg.SystemName)
	}
}

func TestLoadConfigFailures(t *testing.T) {
	testFile := getTestFile(t)
	defer os.Remove(testFile.Name())

	_, err := testFile.WriteString("fneep blip blorp\nquack moo\n")
	if err != nil {
		t.Fatal(err)
	}
	testFile.Close()

	_, err = client.GetConfig(testFile.Name())
	if err == nil {
		t.Fatalf("Expected GetConfig() to fail on unparseable file")
	}

	if err := os.Chmod(testFile.Name(), 0000); err != nil {
		t.Fatal(err)
	}

	_, err = client.GetConfig(testFile.Name())
	if err == nil {
		t.Fatalf("Expected GetConfig() to fail on unreadable file")
	}
}
