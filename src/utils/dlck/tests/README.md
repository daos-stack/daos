# DLCK Test Helper

Simple utility which takes an existing VOS pool and creates and populates a container inside this pool according to provided parameters.

## Basic usage for MD-on-PMEM pool

`dlck_test_helper` accepts the same parameters as `daos_engine` and `dlck`, for example:

```sh
./dlck_test_helper --file 102d950b-7cd7-4a02-9ae2-30d08d1f5900,0 --storage /dev/shm/daos
```

**Note**: To use `dlck_test_helper` against an MD-on-SSD pool provide the `--nvme` parameter.

## Container's contents

Default parameters are hand-picked to produce a small but interesting enough VOS tree for testing purposes, which may look like this:

```
OID 471378945.1581695481
├── DKEY 53262252
│   └── AKEY 3409551c
├── DKEY 1e81b75f
│   ├── AKEY 1e81b75f
│   └── AKEY 53262252 (x2)
└── DKEY 3409551c
    └── AKEY 1e81b75f
OID 1185667690.2136093645
├── DKEY 53262252
│   ├── AKEY 3409551c
│   └── AKEY 1e81b75f
└── DKEY 17d9b927
    └── AKEY 17d9b927
```

**Note**: Since all OIDs and keys are generated randomly and picked at random while generating updates, the effective VOS tree changes when parameters or the implementation change.

You may control the output to some extent by providing extra parameters: `--oids_num`, `--keys_num` and `--updates_num`.
