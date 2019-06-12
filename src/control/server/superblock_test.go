package main

import (
	"io/ioutil"
	"os"
	"path"
	"strconv"
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/ioserver"
)

// Quick test to demonstrate writing/reading superblock in a temp dir
// in order to avoid mocking OS calls
func TestSuperblock(t *testing.T) {
	testDir, err := ioutil.TempDir("", "TestSuperblock-*")
	defer os.RemoveAll(testDir)
	if err != nil {
		t.Fatal(err)
	}
	storagePath := "/mnt/daos-test"
	if err := os.MkdirAll(path.Join(testDir, storagePath), 0755); err != nil {
		t.Fatal(err)
	}

	rank := 42
	systemName := "testSystem"
	testInstance := &managedInstance{
		cfg: ioserver.NewConfig().
			WithStoragePath(storagePath).
			WithServerGroup(systemName).
			WithRank(uint32(rank)),
		fsRoot: testDir,
	}
	if err := testInstance.CreateSuperblock(); err != nil {
		t.Fatal(err)
	}

	sb, err := testInstance.ReadSuperblock()
	if err != nil {
		t.Fatal(err)
	}

	AssertEqual(t, sb.Rank, strconv.Itoa(rank), "superblock rank")
	AssertEqual(t, sb.System, systemName, "superblock system")
}
