# support collect-log command

Support collect-log command will collect the logs on the servers or individual clients
for debugging purpose.This options is available for `daos_server`, `dmg` and `daos_agent` binaries.
It will collect the specific logs, config and other DAOS related metrics and system information.

`dmg support collect-log` is the single command, which will initiate the log collection
over gRPC,collect and copy the logs on each servers.Logs will be Rsync to the admin node.

`daos_server support collect-log` command will collect the information on that particular DAOS server.It will not collect the `dmg` command information as `dmg` command needs to be run from the admin node.

`daos_agent support collect-log` will collect the information on the client and collect the
DAOS client side log with other system related information.

## List of items collected as part of `dmg support collect-log`

* dmg network,storage and system command output
* daos server config
* helper_log_file mention in daos server config
* control_log_file mention in daos server config
* engines log_file mention in daos server config
* daos metrics for all the engines
* daos_server dump-topology, version output
* system information

## List of items collected as part of `daos_server support collect-log`

* daos server config
* helper_log_file mention in daos server config
* control_log_file mention in daos server config
* engines log_file mention in daos server config
* daos metrics for all the engines
* daos_server dump-topology, version output
* system information

## List of items collected as part of `daos_agent support collect-log`

* daos agent config
* log_file mention in daos agent config
* daos client log if it's set `D_LOG_FILE`
* daos_agent dump-topology, net-scan, version output
* system information

# support collect-log command options

support collect-log help describe the use of each options.

```
# dmg support collect-log  --help
Usage:
  dmg [OPTIONS] support collect-log [collect-log-OPTIONS]

Application Options:
      --allow-proxy         Allow proxy configuration via environment
  -i, --insecure            Have dmg attempt to connect without certificates
  -d, --debug               Enable debug output
      --log-file=           Log command output to the specified file
  -j, --json                Enable JSON output
  -J, --json-logging        Enable JSON-formatted log output
  -o, --config-path=        Client config file path

Help Options:
  -h, --help                Show this help message

[collect-log command options]
      -l, --host-list=      A comma separated list of addresses <ipv4addr/hostname> to connect to
      -s, --stop-on-error   Stop the collect-log command on very first error
      -t, --target-folder=  Target Folder location where log will be copied
      -z, --archive         Archive the log/config files
      -c, --extra-logs-dir= Collect the Logs from given directory
      -D, --start-date=     Specify the start date, the day from log will be collected, Format: MM-DD
      -F, --end-date=       Specify the end date, the day till the log will be collected, Format: MM-DD
      -S, --log-start-time= Specify the log collection start time, Format: HH:MM:SS
      -E, --log-end-time=   Specify the log collection end time, Format: HH:MM:SS
      -e, --log-type=       collect specific logs only admin,control,server and ignore everything else
```
