package client_test

import (
	"fmt"
	"io/ioutil"
	"os"
	"path"
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

	common.AssertEqual(t, cfg, defaultConfig,
		"loaded config doesn't match default config")
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
	defer os.RemoveAll(path.Dir(defaultConfig.Path))
	defaultConfig.SystemName = t.Name()

	_, err = f.WriteString(fmt.Sprintf("name: %s\n", t.Name()))
	if err != nil {
		t.Fatal(err)
	}
	f.Close()

	cfg, err := client.GetConfig("")
	if err != nil {
		t.Fatal(err)
	}

	common.AssertEqual(t, cfg, defaultConfig,
		"loaded config doesn't match written config")
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

	common.AssertEqual(t, cfg.SystemName, t.Name(),
		"loaded config doesn't match written config")
}

func TestLoadConfigFailures(t *testing.T) {
	testFile := getTestFile(t)
	defer os.Remove(testFile.Name())

	_, err := testFile.WriteString("fneep blip blorp\nquack moo\n")
	if err != nil {
		t.Fatal(err)
	}
	testFile.Close()

	t.Run("unparseable file", func(t *testing.T) {
		_, err := client.GetConfig(testFile.Name())
		if err == nil {
			t.Fatal("Expected GetConfig() to fail on unparseable file")
		}
	})

	if err := os.Chmod(testFile.Name(), 0000); err != nil {
		t.Fatal(err)
	}

	t.Run("unreadable file", func(t *testing.T) {
		_, err := client.GetConfig(testFile.Name())
		if err == nil {
			t.Fatal("Expected GetConfig() to fail on unreadable file")
		}
	})

	t.Run("nonexistent file", func(t *testing.T) {
		_, err := client.GetConfig("/this/is/a/bad/path.yml")
		if err == nil {
			t.Fatal("Expected GetConfig() to fail on nonexistent file")
		}
	})
}
