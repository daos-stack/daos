package main

import (
	"os"

	"github.com/daos-stack/daos/src/control/log"
	"github.com/daos-stack/daos/src/control/server"
)

func main() {
	if err := server.Main(); err != nil {
		log.Errorf("%+v", err)
		os.Exit(1)
	}
}
