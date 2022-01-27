# DAOS

[![License](https://img.shields.io/badge/License-BSD--2--Clause--Patent-blue.svg)](./LICENSE)
[![Coverity Scan Build Status](https://img.shields.io/coverity/scan/3015.svg)](https://scan.coverity.com/projects/daos-stack-daos)
[![Codacy Badge](https://api.codacy.com/project/badge/Grade/4163f52ec65e4ba8991208288a9a15a6)](https://www.codacy.com/app/johann.lombardi/daos?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=daos-stack/daos&amp;utm_campaign=Badge_Grade)

Please file issues in our [Jira issue tracker](http://jira.daos.io)

DAOS is a SODA Foundation project
<a href="https://sodafoundation.io/">
<img src="https://sodafoundation.io/wp-content/uploads/2020/01/SODA_logo_outline_color_800x800.png"  width="200" height="200">
</a>

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

DAOS is licensed under the BSD-2-Clause Plus Patent License.
Please see the [LICENSE](./LICENSE) & [NOTICE](./NOTICE) files for more
information.

## Documentation

The DAOS documentation is available [online](https://daos-stack.github.io/).

This includes:
* [DAOS Architecture Overview](https://daos-stack.github.io/overview/terminology/)
* [Administration Guide](https://daos-stack.github.io/admin/hardware/) to install, manage
  and monitor a DAOS system.
* [User Guide](https://daos-stack.github.io/user/container/) documenting the
  DAOS native API, as well as the integration with POSIX, MPI-IO, HDF5, and Spark.
* [Release Notes](https://daos-stack.github.io/release/releaseNote_v1_0/)
  for the 1.0 release.
* [Developer documentation](https://daos-stack.github.io/dev/development/)
  to learn more about DAOS internals and contribute to the development effort.

More information can also be found on the [wiki](http://wiki.daos.io).

## Contacts

For any questions, please post to our [user forum](https://daos.groups.io/g/daos).
Bugs should be reported through our [issue tracker](http://jira.daos.io)
with a test case to reproduce the issue (when applicable) and [debug logs](./docs/debugging.md).
