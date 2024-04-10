package main

import (
	"context"
	"crypto/rand"
	"fmt"
	"hash/crc32"
	"io"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	daosAPI "github.com/daos-stack/daos/src/control/lib/daos/client"
	"github.com/daos-stack/daos/src/control/lib/dfs"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	defBufSize = 16*1024 + 1 // Big enough to not be inlined
)

type connectStressCmd struct {
	daosCmd

	Append  bool   `long:"append" short:"a" description:"Keep appending to the test file up to max size"`
	Rewrite bool   `long:"rewrite" short:"r" description:"Rewrite the test file instead of recreating it"`
	Check   bool   `long:"checksum" short:"k" description:"Enable checksum verification of read buffer"`
	Dir     string `long:"test-dir" short:"d" description:"Parent directory for test files" default:"/testDir"`
	Count   uint   `long:"count" short:"c" description:"Connection count for this pool"`
	PerProc bool   `long:"per-proc" short:"p" description:"Each connection is made in a new process"`
	Size    string `long:"size" short:"s" description:"Size of per-client test buffer or max file size"`
	Args    struct {
		Pool      string `positional-arg-name:"pool" required:"true"`
		Container string `positional-arg-name:"container" required:"true"`
	} `positional-args:"true"`
}

func newDaosClient(ctx context.Context, log logging.Logger, pool, cont string, cfg *clientCfg) (*daosClient, error) {
	if cfg == nil {
		cfg = &clientCfg{}
	}
	return &daosClient{
		log:  log,
		cfg:  cfg,
		pool: pool,
		cont: cont,
	}, nil
}

type clientCfg struct {
	testDir   string
	append    bool
	checkSums bool
	rewrite   bool
	fileSize  uint64
	stats     *clientStats
}

type daosClient struct {
	log  logging.Logger
	cfg  *clientCfg
	pool string
	cont string
}

type statsReadWriter struct {
	w     io.Writer
	r     io.Reader
	stats *clientStats
}

func (srw *statsReadWriter) Write(buf []byte) (int, error) {
	n, err := srw.w.Write(buf)
	srw.stats.AddWrite(uint64(n))
	return n, err
}

func (srw *statsReadWriter) Read(buf []byte) (int, error) {
	n, err := srw.r.Read(buf)
	srw.stats.AddWrite(uint64(n))
	return n, err
}

func (c *daosClient) Start(ctx context.Context, idx uint, testBuf []byte, errOut chan<- error) {
	openResp, err := daosAPI.PoolConnect(ctx, c.pool, "", daosAPI.PoolConnectFlagReadWrite)
	if err != nil {
		errOut <- err
		return
	}
	poolHdl := openResp.PoolConnection

	contHdl, err := poolHdl.OpenContainer(ctx, c.cont, daosAPI.ContainerOpenFlagReadWrite)
	if err != nil {
		errOut <- err
		return
	}

	fs, err := dfs.Mount(poolHdl, contHdl, os.O_RDWR, dfs.SysFlagNoCache)
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

	var f *dfs.File
	defer func() {
		if f != nil {
			if err := f.Close(); err != nil {
				c.log.Errorf("failed to close %s: %s", testFile, err)
			}
		}
		if fs != nil {
			if err := fs.Unmount(); err != nil {
				c.log.Errorf("failed to unmount DFS: %s", err)
			}
		}
		if contHdl != nil {
			if err := contHdl.Close(ctx); err != nil {
				c.log.Errorf("failed to close container: %s", err)
			}
		}
		if poolHdl != nil {
			if err := poolHdl.Disconnect(ctx); err != nil {
				c.log.Errorf("failed to disconnect pool: %s", err)
			}
		}
	}()

	if c.cfg.append {
		f, err = fs.Create(testFile)
		if err != nil {
			c.log.Errorf("failed to open %s: %s", testFile, err)
			errOut <- err
			return
		}

		srw := &statsReadWriter{
			stats: c.cfg.stats,
			r:     io.LimitReader(rand.Reader, int64(c.cfg.fileSize)),
		}
		if _, err = f.ReadFrom(srw); err != nil {
			c.log.Errorf("failed to write to %s: %s", testFile, err)
			errOut <- err
		} else {
			errOut <- nil
		}

		return
	}

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
			if f == nil {
				f, err = fs.Create(testFile)
				if err != nil {
					c.log.Errorf("failed to open %s: %s", testFile, err)
					errOut <- err
					return
				}
			}

			writeCount, err := f.WriteAt(testBuf, 0)
			if err != nil {
				errOut <- errors.Wrap(err, "write error")
				return
			}
			c.cfg.stats.AddWrite(uint64(writeCount))

			readCount, err := f.ReadAt(readBuf, 0)
			if err != nil {
				errOut <- errors.Wrap(err, "read error")
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
				if err := f.Close(); err != nil {
					errOut <- errors.Wrapf(err, "failed to close %s", testFile)
					return
				}

				if err := fs.Remove(testFile); err != nil {
					errOut <- errors.Wrapf(err, "failed to remove %s", testFile)
					return
				}

				f = nil
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
	if cmd.Count == 0 {
		cmd.Count = 1
	}

	if cmd.Size == "" {
		cmd.Size = "0"
	}
	bufSize, err := humanize.ParseBytes(cmd.Size)
	if err != nil {
		return errors.Wrapf(err, "unable to parse %q", cmd.Size)
	}
	if bufSize == 0 {
		bufSize = defBufSize
	}

	if cmd.Append && cmd.Rewrite {
		return errors.New("--append and --rewrite are incompatible")
	}
	if cmd.Append && cmd.Check {
		return errors.New("--check is not available with --append")
	}

	var testBuf []byte
	if !cmd.Append {
		testBuf = make([]byte, bufSize)
		if _, err := rand.Reader.Read(testBuf); err != nil {
			return errors.Wrap(err, "failed to fill testBuf")
		}
	}

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

	if cmd.PerProc {
		quiet = true
	}

	for ; ctx.Err() == nil && connections < cmd.Count; connections++ {
		cfg := &clientCfg{
			testDir:   filepath.Clean(cmd.Dir),
			stats:     cs,
			checkSums: cmd.Check,
			rewrite:   cmd.Rewrite,
			append:    cmd.Append,
			fileSize:  bufSize,
		}

		if cmd.PerProc && cmd.Count > 1 {
			idx := connections
			args := []string{"connect", cmd.Args.Pool, cmd.Args.Container, "--quiet"}
			if cfg.testDir != "" {
				args = append(args, "-d", fmt.Sprintf("%s/%d", cfg.testDir, idx))
			}
			if cfg.checkSums {
				args = append(args, "-k")
			}
			if cfg.rewrite {
				args = append(args, "-r")
			}
			if cfg.append {
				args = append(args, "-a")
			}
			if cmd.Size != "" {
				args = append(args, "-s", cmd.Size)
			}

			go func() {
				eCmd := exec.CommandContext(ctx, os.Args[0], args...)
				eCmd.Stdout = os.Stdout
				eCmd.Stderr = os.Stderr
				eCmd.Cancel = func() error {
					return eCmd.Process.Signal(os.Interrupt)
				}
				if err := eCmd.Run(); err != nil && !errors.Is(err, context.Canceled) {
					errChan <- errors.Wrapf(err, "child %d run failed", idx)
					return
				}
				errChan <- nil
			}()
			wg.Add(1)
		} else {
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
	}

	cmd.Infof("\nEstablished %d connections", connections)
	if !quiet {
		go func() {
			interval := time.Second
			var lastRead uint64
			var lastWrite uint64
			for {
				select {
				case <-ctx.Done():
					return
				default:
					curRead := cs.GetRead()
					readDelta := curRead - lastRead
					lastRead = curRead
					curWrite := cs.GetWrite()
					writeDelta := curWrite - lastWrite
					lastWrite = curWrite

					fmt.Fprintf(os.Stderr, "read: %s/s, write: %s/s\r",
						humanize.Bytes(readDelta), humanize.Bytes(writeDelta))
					time.Sleep(interval)
				}
			}
		}()
	}

	wg.Wait()
	return nil
}
