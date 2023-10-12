### Fault injection in C

```c
daos_fail_init();
daos_fail_loc_set(DAOS_VOS_AGG_RANDOM_YIELD | DAOS_FAIL_ALWAYS);
/** do the thing you want to test and to trigger the fault */
daos_fail_fini();
```

As far as I am aware all fault codes live here: https://github.com/daos-stack/daos/blob/master/src/include/daos/common.h#L764
An example in an actual test: https://github.com/daos-stack/daos/blob/master/src/vos/tests/vts_aggregate.c#L1305

Fault injection in YAML e.g. for manual testing `daos_engine` behavior
I did not find a good documentation so I can only refer to DLCK's test plan:
https://daosio.atlassian.net/wiki/spaces/DAOS/pages/12257067018/DLCK+Test+Plan#Fault-injection (WIP)

Create a yaml file with the fault you want to inject e.g.
```c
fault_config:
  - id: 131328 # DLCK_FAULT_CREATE_LOG_DIR
```

As far as I know, the decimal value has to be exactly calculated from the common.h file's values for specific faults.
Inject an `D_FI_CONFIG=src/utils/dlck/tests/fault_injection_dlck.yaml` environment variable in the
engine's config or directly in the shell if you use an executable of some sort.

*Note:* I guess you also need a debug build so the fault injection would work.
