# DAOS
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](./LICENSE)
[![Build Status](https://travis-ci.org/daos-stack/daos.svg?branch=master)](https://travis-ci.org/daos-stack/daos)
[![Coverity Scan Build Status](https://img.shields.io/coverity/scan/3015.svg)](https://scan.coverity.com/projects/daos-stack-daos)
[![Codacy Badge](https://api.codacy.com/project/badge/Grade/4163f52ec65e4ba8991208288a9a15a6)](https://www.codacy.com/app/johann.lombardi/daos?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=daos-stack/daos&amp;utm_campaign=Badge_Grade)


## What is DAOS?

The **D**istributed **A**synchronous **O**bject **S**torage (DAOS) is an
open-source software-defined object store designed from the ground up for
massively distributed Non Volatile Memory (NVM). DAOS takes advantage of next
generation NVM technology like Storage Class Memory (SCM) and NVM express (NVMe)
while presenting a key-value storage interface and providing features such as
transactional non-blocking I/O, advanced data protection with self-healing on
top of commodity hardware, end-to-end data integrity, fine-grained data control
and elastic storage to optimize performance and cost.

## License

DAOS is licensed under the Apache License Version 2.0.
Please see the [LICENSE](./LICENSE) & [NOTICE](./NOTICE) files for more
information.

## Documentations

The DAOS documentation is available [online](https://daos-stack.github.io).

This includes:
* [DAOS architecture overview](https://daos-stack.github.io/overview/terminology/)
* [Admin guide](https://daos-stack.github.io/admin/hardware/) to install, manage
  and monitor a DAOS system.
* [User guide](https://daos-stack.github.io/user/container/) documenting the
  DAOS native API, as well as the integration with HDF5, Spark, and POSIX.
* [Developer documentation](https://daos-stack.github.io/dev/development/)
  to learn more about DAOS internals and contribute to the development effort.

More information can also be found on the [wiki](https://wiki.hpdd.intel.com).

## Contacts

For any questions, please post to our [user forum](https://daos.groups.io/g/daos).
Bugs should be reported through our [issue tracker](https://jira.hpdd.intel.com/projects/DAOS)
with a test case to reproduce the issue (when applicable) and [debug logs](./doc/debugging.md).
