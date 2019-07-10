package main

import (
	"fmt"
	"os"

	"github.com/daos-stack/daos/src/control/log"
	"github.com/daos-stack/daos/src/control/server"
)

func main() {
	// Bootstrap default logger before config options get set.
	log.NewDefaultLogger(log.Debug, "", os.Stderr)

	if err := server.Main(); err != nil {
		fmt.Fprintf(os.Stderr, "fatal error: %s\n", err)
		log.Errorf("%+v", err)
		os.Exit(1)
	}
}
