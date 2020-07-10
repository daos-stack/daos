[![Build Status](https://travis-ci.org/BBVA/raft-badger.svg?branch=master)](https://travis-ci.org/BBVA/raft-badger)
[![Coverage](https://codecov.io/gh/BBVA/raft-badger/branch/master/graph/badge.svg)](https://codecov.io/gh/BBVA/raft-badger)
[![GoReport](https://goreportcard.com/badge/github.com/bbva/raft-badger)](https://goreportcard.com/report/github.com/bbva/raft-badger)
[![GoDoc](https://godoc.org/github.com/bbva/raft-badger?status.svg)](https://godoc.org/github.com/bbva/raft-badger)

# raft-badger

This repository provides the `raftbadger` package. The package exports the
`BadgerStore` which is an implementation of both a `LogStore` and `StableStore`.

It is meant to be used as a backend for the `raft` [package here](https://github.com/hashicorp/raft).

This implementation uses [BadgerDB](https://github.com/dgraph-io/badger). BadgerDB is
a simple persistent key-value store written in pure Go. It has a Log-Structured-Merge (LSM) 
design and it's meant to be a performant alternative to non-Go based stores like 
[RocksDB](https://github.com/facebook/rocksdb).

## Documentation

The documentation for this package can be found on [Godoc](http://godoc.org/github.com/bbva/raft-badger) here.

## Contributions

Contributions are very welcome, see [CONTRIBUTING.md](https://github.com/BBVA/raft-badger/blob/master/CONTRIBUTING.md)
or skim [existing tickets](https://github.com/BBVA/raft-badger/issues) to see where you could help out.

## License

***raft-badger*** is Open Source and available under the Apache 2 License.