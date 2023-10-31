package main

import (
	"context"
	"fmt"
	"hash/crc32"
	"math/rand"
	"os"
	"os/signal"
	"path/filepath"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	daosAPI "github.com/daos-stack/daos/src/control/lib/daos/api"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	defBufSize = 16*1024 + 1 // Big enough to not be inlined
)

type connectStressCmd struct {
	daosCmd

	Rewrite bool   `long:"rewrite" short:"r" description:"Rewrite the test file instead of recreating it"`
	Check   bool   `long:"checksum" short:"k" description:"Enable checksum verification of read buffer"`
	Dir     string `long:"test-dir" short:"d" description:"Parent directory for test files" default:"/testDir"`
	Count   uint   `long:"count" short:"c" description:"Connection count for this pool"`
	BufSize string `long:"buffer-size" short:"b" description:"Size of per-client test buffer"`
	Args    struct {
		Pool      string `positional-arg-name:"pool" required:"true"`
		Container string `positional-arg-name:"container" required:"true"`
	} `positional-args:"true"`
}

func newDaosClient(ctx context.Context, log logging.Logger, pool, cont string, cfg *clientCfg) (*daosClient, error) {
	connReq := daosAPI.PoolConnectReq{
		PoolID: pool,
		Flags:  uint(daosAPI.AccessFlagReadWrite),
	}
	poolInfo, err := daosAPI.PoolConnect(ctx, connReq)
	if err != nil {
		return nil, err
	}
	contConn, err := poolInfo.PoolConnection.OpenContainer(ctx, cont, daosAPI.ContainerOpenFlagReadWrite)
	if err != nil {
		return nil, err
	}

	if cfg == nil {
		cfg = &clientCfg{}
	}
	return &daosClient{
		log:  log,
		cfg:  cfg,
		pool: poolInfo.PoolConnection,
		cont: contConn,
	}, nil
}

type clientCfg struct {
	testDir   string
	checkSums bool
	rewrite   bool
	stats     *clientStats
}

type daosClient struct {
	log  logging.Logger
	cfg  *clientCfg
	pool *daosAPI.PoolHandle
	cont *daosAPI.ContainerHandle
}

func (c *daosClient) Start(ctx context.Context, idx uint, testBuf []byte, errOut chan<- error) {
	fs, err := daosAPI.MountFilesystem(c.pool, c.cont, daosAPI.AccessFlagReadWrite)
	if err != nil {
		errOut <- err
		return
	}

	if err := fs.MkdirAll(c.cfg.testDir, 0755); err != nil {
		errOut <- err
		return
	}
	testFile := fmt.Sprintf("/%s/test-%d", c.cfg.testDir, idx)
	c.log.Debugf("connecting client %d to %s", idx, testFile)

	var obj *daosAPI.FilesystemFile
	defer func() {
		if obj != nil {
			obj.Close()
		}
		fs.Unmount()
	}()

	var writeSum uint32
	if c.cfg.checkSums {
		writeSum = crc32.ChecksumIEEE(testBuf)
	}

	readBuf := make([]byte, len(testBuf))
	for {
		select {
		case <-ctx.Done():
			errOut <- nil
			return
		default:
			if obj == nil {
				obj, err = fs.Create(testFile)
				if err != nil {
					c.log.Errorf("failed to open %s: %s", testFile, err)
					errOut <- err
					return
				}
			}

			writeCount, err := obj.WriteAt(testBuf, 0)
			if err != nil {
				errOut <- err
				return
			}
			c.cfg.stats.AddWrite(uint64(writeCount))

			readCount, err := obj.ReadAt(readBuf, 0)
			if err != nil {
				errOut <- err
				return
			}
			c.cfg.stats.AddRead(uint64(readCount))
			if readCount != len(testBuf) {
				c.log.Errorf("%s: short read(%d < %d)", testFile, readCount, len(testBuf))
				continue
			}

			if c.cfg.checkSums {
				readSum := crc32.ChecksumIEEE(readBuf)
				if readSum != writeSum {
					c.log.Errorf("invalid checksum (%d != %d) on read", readSum, writeSum)
					continue
				}
			}

			if !c.cfg.rewrite {
				if err := obj.Close(); err != nil {
					errOut <- err
					return
				}

				if err := fs.Remove(testFile); err != nil {
					errOut <- err
					return
				}

				obj = nil
			}
		}
	}
}

type clientStats struct {
	readBytes  uint64
	writeBytes uint64
}

func (cs *clientStats) AddRead(read uint64) {
	if cs == nil {
		return
	}
	atomic.AddUint64(&cs.readBytes, read)
}

func (cs *clientStats) GetRead() uint64 {
	if cs == nil {
		return 0
	}
	return atomic.LoadUint64(&cs.readBytes)
}

func (cs *clientStats) AddWrite(write uint64) {
	if cs == nil {
		return
	}
	atomic.AddUint64(&cs.writeBytes, write)
}

func (cs *clientStats) GetWrite() uint64 {
	if cs == nil {
		return 0
	}
	return atomic.LoadUint64(&cs.writeBytes)
}

func (cmd *connectStressCmd) Execute(args []string) error {
	var quiet bool
	for _, arg := range args {
		if arg == "quiet" {
			quiet = true
		}
	}

	var connections uint
	defer func() {
		cmd.Infof("Connection count at exit: %d", connections)
	}()
	maxConn := cmd.Count
	if maxConn == 0 {
		maxConn = maxConn << 1
	}

	if cmd.BufSize == "" {
		cmd.BufSize = "0"
	}
	bufSize, err := humanize.ParseBytes(cmd.BufSize)
	if err != nil {
		return errors.Wrapf(err, "unable to parse %q", cmd.BufSize)
	}
	if bufSize == 0 {
		bufSize = defBufSize
	}
	testBuf := func() []byte {
		alnum := []byte("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")
		buf := make([]byte, bufSize)
		for i := 0; i < len(buf); i++ {
			buf[i] = alnum[rand.Intn(len(alnum))]
		}
		return buf
	}()

	cs := new(clientStats)
	ctx, cancel := context.WithCancel(cmd.daosCtx)
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM, syscall.SIGQUIT)
	go func() {
		sig := <-sigChan
		cmd.Infof("\ncaught %s; cleaning up\n", sig)
		cancel()
	}()

	var wg sync.WaitGroup
	errChan := make(chan error)
	go func() {
		for {
			err := <-errChan
			wg.Done()
			if err != nil {
				cmd.Errorf("client error: %s", err)
				cancel()
			}
		}
	}()

	for ; ctx.Err() == nil && connections < maxConn; connections++ {
		cfg := &clientCfg{
			testDir:   filepath.Clean(cmd.Dir),
			stats:     cs,
			checkSums: cmd.Check,
			rewrite:   cmd.Rewrite,
		}
		client, err := newDaosClient(cmd.daosCtx, cmd, cmd.Args.Pool, cmd.Args.Container, cfg)
		if err != nil {
			return err
		}
		wg.Add(1)

		go client.Start(ctx, connections, testBuf, errChan)
		if !quiet {
			fmt.Fprintf(os.Stderr, "Connections: %d\r", connections+1)
		}
	}

	cmd.Infof("\nEstablished %d connections", connections)
	interval := time.Second
	var lastRead uint64
	var lastWrite uint64
	for {
		select {
		case <-ctx.Done():
			wg.Wait()
			return nil
		default:
			if !quiet {
				curRead := cs.GetRead()
				readDelta := curRead - lastRead
				lastRead = curRead
				curWrite := cs.GetWrite()
				writeDelta := curWrite - lastWrite
				lastWrite = curWrite

				fmt.Fprintf(os.Stderr, "read: %s/s, write: %s/s\r",
					humanize.Bytes(readDelta), humanize.Bytes(writeDelta))
			}
			time.Sleep(interval)
		}
	}
}
