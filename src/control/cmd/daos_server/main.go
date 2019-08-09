package main

import (
	"fmt"
	"os"

	log "github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server"
)

func main() {
	if err := server.Main(); err != nil {
		fmt.Fprintf(os.Stderr, "fatal error: %s\n", err)
		log.Errorf("%+v", err)
		os.Exit(1)
	}
}
