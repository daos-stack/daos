# DAOS
[![Build Status](https://travis-ci.org/daos-stack/daos.svg?branch=master)](https://travis-ci.org/daos-stack/daos)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](./LICENSE)
[![Codacy Badge](https://api.codacy.com/project/badge/Grade/4163f52ec65e4ba8991208288a9a15a6)](https://www.codacy.com/app/johann.lombardi/daos?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=daos-stack/daos&amp;utm_campaign=Badge_Grade)

> :warning: **Warning:** DAOS is under heavy development. Use at your own risk.

## What is DAOS?

The **D**istributed **A**synchronous **O**bject **S**torage (DAOS) is an open-source software-defined object store designed from the ground up for massively distributed Non Volatile Memory (NVM). DAOS takes advantage of next generation NVM technology like Storage Class Memory (SCM) and NVM express (NVMe) while presenting a key-value storage interface and providing features such as transactional non-blocking I/O, advanced data protection with self healing on top of commodity hardware, end-to-end data integrity, fine grained data control and elastic storage to optimize performance and cost.

## License

DAOS is licensed under the Apache License Version 2.0. Please see the [LICENSE](./LICENSE) & [NOTICE](./NOTICE) files for more information.

## Documentations

Operation manual:
* [building, installing and running DAOS](./doc/quickstart.md)
* [testing DAOS](./doc/testing.md)
* [debugging DAOS](./doc/debugging.md)

User guide:
* [DAOS overview](./doc/overview.md)
* [I/O middleware integration](./doc/middleware.md)
* [DAOS API](./src/include/) and [man pages](./doc/man/man3)

Developer zone:
* [contributing to DAOS](./doc/contributing.md)
* [DAOS coding rules](./doc/coding.md)
* [DAOS internals](./doc/internals.md)

More information can be found on the [wiki](https://wiki.hpdd.intel.com/display/DC/Resources).

## Contacts

For any questions, please post to our [user forum](https://daos.groups.io/g/daos). Bugs should be reported through our [issue tracker](https://jira.hpdd.intel.com/projects/DAOS) with a test case to reproduce the issue (when applicable) and [debug logs](./doc/debugging.md).
