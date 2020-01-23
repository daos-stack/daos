## Description
This module is DAOS DFS Java API and DAOS DFS implementation of Hadoop FileSystem. There are two submodules,
daos-java-api and hadoop-daos.

### daos-java-api
It wraps most of common APIs from daos_fs.h, as well as some pool creation related APIs from daos_mgmt.h and some pool
and container connection related APIs from daos_api.h. There are two main classes, DaosFsClient and DaosFile.

* DaosFsClient
There will be single instance of DaosFsClient per pool and container. All DAOS DFS calls and init/finalize, are from
this class which has all native methods implementations in jni which call DAOS APIs directly. It provides a few public
APIs, move, delete, mkdir, exists, for simple non-repetitive file operations. They release all opened files, if any,
immediately. If you have multiple operations on same file in short period of time, you should use DaosFile which can be
instantiated by calling getFile methods.

* DaosFile
It's a simple and efficient representative of underlying DAOS file. You just need to give a posix-compatible path to
create a DaosFile instance. All later file operations can be done via this instance. It provides java File-like APIs to
make it friendly to Java developers. And you don't need to release DaosFile explicitly since it will be released
automatically if there is no reference to this DaosFile instance. You, of course, can release DaosFile explicitly if
you like or you have to. Besides, it's more efficient for multiple consecutive file operations since underlying DFS
object is cached and remain open until being released. Later DFS operations don't
need to lookup repeatedly for each FS operation.

### hadoop-daos
It's DAOS FS implementation of Hadoop FileSystem based on daos-java-api. There are three main classes, DaosFileSystem,
DaosInputStream and DaosOutputStream.

* DaosFileSystem, it provides APIs to create file as DaosOutputStream, open file as DaosInputStream, list file
    status, create directory and so on. It also does file system initialization and finalization.
* DaosInputStream, for reading file, preload is also possible in this class.
* DaosOutputStream, for writing file.

## Build
It's Java module and built by Maven. Java 1.8 and Maven 3 are required to build this module. After they are installed,
you can change to this <DAOS>/java folder and build by below command line.

    mvn -DskipITs clean install

daos-java-api module depends on DAOS which is assumed being installed under /usr/local/daos. If you have different
location, you need to set it with '-Ddaos.install.path=<your DAOS install dir>'. For example,

    mvn -DskipITs -Ddaos.install.path=/code/daos/install clean install

If you have DAOS pool and DAOS container with type of posix, you can run integration test when build with below command.
Before running it, make sure you have DAOS environment properly setup, including server and user environment variables.

    mvn -Dpool_id=<your pool uuid> -Dcont_id=<your container uuid> clean install

User can go to each submodule and build it separately too. 

## Documentation
You can run below command to generate JavaDoc. There could be some error message during build. Just ignore them if your
final build status is success. Then go to target/site folder to find documentation.

    mvn site

## Contacts
For any questions, please post to our [user forum](https://daos.groups.io/g/daos). Bugs should be reported through our 
[issue tracker](https://jira.hpdd.intel.com/projects/DAOS) with a test case to reproduce the issue (when applicable) and
 [debug logs](./doc/debugging.md).