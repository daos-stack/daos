package main

import "os"
import "os/exec"
import "os/signal"
import "syscall"
import "log"
import "runtime"

func main() {
	runtime.GOMAXPROCS(1)

	// create a channel to retrieve signals
	sigchan := make(chan os.Signal, 2)
	signal.Notify(sigchan,
		syscall.SIGTERM,
		syscall.SIGINT,
		syscall.SIGQUIT,
		syscall.SIGKILL,
		syscall.SIGHUP)

	// setup cmd line to start the DAOS I/O server
	srv := exec.Command("daos_io_server", os.Args[1:]...)
	srv.Stdout = os.Stdout
	srv.Stderr = os.Stderr

	// I/O server should get a SIGTERM if this process dies
	srv.SysProcAttr = &syscall.SysProcAttr{
		Pdeathsig: syscall.SIGTERM,
	}

	// start the DAOS I/O server
	err := srv.Start()
	if err != nil {
		log.Fatal("DAOS I/O server failed to start: ", err)
	}

	// catch signals
	go func() {
		<-sigchan
		if err := srv.Process.Kill(); err != nil {
			log.Fatal("Failed to kill DAOS I/O server: ", err)
		}
	}()

	// wait for I/O server to return
	err = srv.Wait()
	if err != nil {
		log.Fatal("DAOS I/O server exited with error: ", err)
	}
}
