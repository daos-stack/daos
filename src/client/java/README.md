## Description

This module is DAOS Java client and DAOS DFS implementation of Hadoop FileSystem. There are two submodules,
daos-java and hadoop-daos.

### daos-java

It wraps most of common APIs from daos_fs.h, daos_uns.h and daos_obj.h, as well as some pool and container connection
related APIs from daos_pool.h and daos_cont.h. There are three parts, DAOS File (dfs), DAOS Object (obj) and DAOS Event
.

#### DAOS File (dfs)

You'll typically interact with three classes, DaosFsClient, DaosFile and DaosUns. You get DaosFsClient instance via
DaosFsClientBuilder. Then you get DaosFile instance of given POSIX path from the DaosFsClient. With DaosFile
instance, you can do some file read/write operations. For DAOS Unified Namespace, you can use DaosUns class.

* DaosFsClient

There will be single instance of DaosFsClient per pool and container. All DAOS DFS/UNS calls and init/finalize, are from
this class which has all native methods implementations in jni which call DAOS APIs directly. It provides a few public
APIs, move, delete, mkdir, exists, for simple non-repetitive file operations. They release all opened files, if any,
immediately. If you have multiple operations on same file in short period of time, you should use DaosFile which can be
instantiated by calling getFile methods.
It also has some DAOS UNS native methods which should be indirectly accessed via DaosUns.

* DaosFile

It's a simple and efficient representative of underlying DAOS file. You just need to give a posix-compatible path to
create a DaosFile instance. All later file operations can be done via this instance. It provides java File-like APIs to
make it friendly to Java developers. And you don't need to release DaosFile explicitly since it will be released
automatically if there is no reference to this DaosFile instance. You, of course, can release DaosFile explicitly if
you like or you have to. Besides, it's more efficient for multiple consecutive file operations since underlying DFS
object is cached and remain open until being released. Later DFS operations don't need to lookup repeatedly for each FS
operation.

* DaosUns

It wraps some DAOS UNS APIs, like resolving UNS path, as well as parsing DAOS UNS string attribute.

#### DAOS Object (obj)

Similar to dfs, you need to get instance of DaosObjClient via DaosObjClientBuilder and DaosObject from DaosObjClient.
Different than dfs, you need to use concrete classes of IODataDesc to read/write data to DAOS Object. IODataDesc
classes model after DAOS Object structures, like Dkey, Akey, single value and array value.

* IODataDesc

Top interface of all IO Description classes as well as Akey Entry classes.

* IODataDescBase

Abstract IO Description class implements common methods from IODataDesc. All concrete IO Description classes extend
this class.

* IODataDescSync

IO Description class for synchronous DAOS Object read/write. This class provides all DAOS Object value options, like
value type, element size.

* IOSimpleDataDesc

Similar to IODataDescSync, you can use this class to describe which dkey/akey and what position you want to read/write.
For simplicity, this class defaults value type to be array with element size of 1. It supports both synchronous and
asynchronous read/write. And this class can be reused for repeatible read/write.

* IOSimpleDDAsync

Similar to IOSimpleDataDesc, except it's non-reusable and only for asynchronous read/write.

#### Daos Event

To support asynchronous read/write in dfs and obj, we need Java corresponding of DAOS Event Queue and DAOS Event.
We put Daos Event related classes in DaosEventQueue. To use Daos Event, there are typical scenario.
- Get per-thread DaosEventQueue instance.
- Acquire event from the DaosEventQueue instance.
- Bind event to IO Description class, like IODfsDesc or IODataDesc.
- Read/write with given IO Description.
- Try to poll completed event with given DaosEventQueue instance until you got something or timed out.
- Get the IO Description in form of attachment from the completed event.

### hadoop-daos

It's DAOS FS implementation of Hadoop FileSystem based on daos-java. There are three main classes, DaosFileSystem,
DaosInputStream and DaosOutputStream.

* DaosFileSystem, it provides APIs to create file as DaosOutputStream, open file as DaosInputStream, list file
    status, create directory and so on. It also does file system initialization and finalization.
* DaosInputStream, for reading file, preload is also possible in this class.
* DaosOutputStream, for writing file.

It supports both synchronous and asynchronous. The default is asynchronous. You can change it by setting
"fs.daos.io.async" to false.

#### Hadoop DAOS FileSystem Configuration

##### DAOS URIs

* daos://\<pool UUID\>/\<container UUID\>/.
* daos://\<pool label\>/\<container label\>/.
* daos://[authority starts with "uns-id"]/\<uns path\>. The authority is optional.
   If the authority doesn't start with "uns-id", it will be considered as either pool UUID or label. You will get
unexpected parse error.

You may want to connect to two DAOS servers or two DFS instances mounted to different containers in one DAOS server from
same JVM. Then, you need to add authority to your UNS URI to make it unique since Hadoop caches filesystem instance
keyed by "schema + authority" in global (JVM).

##### core-site.xml Update

There are some DAOS specific configurations in
[core-site-daos-ref.xml](hadoop-daos/src/main/resources/core-site-daos-ref.xml) to be merged into Hadoop core-site.xml.

##### Tune More Configurations

The following config items starting from 6th can put into application, Hadoop config files and DAOS container
via "daos cont set-attr" command (see examples in next section for the command). The config priority is,
application > Hadoop config files > DAOS container. The items from 1st to 5th are item-specific.

        name                            default value           description

        1. fs.defaultFS                 no default              one of above DAOS URIs. Read from application or Hadoop
                                                                config files

        2. fs.daos.server.group         daos_server             DAOS server group. Read from application or Hadoop
                                                                config files

        3. fs.daos.pool.id              UUID or label           user should not set it directly
                                        parsed from DAOS URI

        4. fs.daos.container.id         UUID or label           user should not set it directly
                                        parsed from DAOS URI

        5. fs.daos.pool.flags           2                       daos pool access flags, 1 for readonly,
                                                                2 for read/write, 4 for execute. Read from application
                                                                or Hadoop config files

        6. fs.daos.choice               no default              multiple applications can use different choices to
                                                                apply their own configurations or override default
                                                                values. E.g., if Spark set "fs.daos.choice" to "spark".
                                                                DAOS FS will try to read "spark.fs.daos.*" (from item 6)
                                                                first. Then fall back to "fs.daos.*".

        7. fs.daos.read.buffer.size     1048576                 size of direct buffer for reading data from DAOS.
                                                                Default is 1m. Value range is 64k - 2g

        8. fs.daos.read.min.size        65536                   minimum size of direct buffer for reading data from
                                                                DAOS. Default is 64k. Value range is 64k - 2g. It should
                                                                be no more than fs.daos.read.buffer.size

        9. fs.daos.write.buffer.size    1048576                 size of direct buffer for writing data to DAOS. Default
                                                                is 1m. Value range is 64k - 2g

        10. fs.daos.block.size          134217728               size for splitting large file into blocks when read by
                                                                Hadoop. Default is 128m. Value range is 16m - 2m.

        11. fs.daos.chunk.size          1048576                 size of DAOS file chunk. Default is 1m. Value range is
                                                                4k - 2g.

        12. fs.daos.io.async            true                    perform DAOS IO asynchronously. Default is true.
                                                                Set to false to use synchronous IO.

## Build

They are Java modules and built by Maven. Java 1.8 and Maven 3 are required to build these modules. After they are
installed, you can change to this \<DAOS_INSTALL\>/src/client/java folder and build by below command line.

    mvn -DskipITs -Dgpg.skip clean install

The `daos-java-<version>.jar` shades protobuf 3 dependency with its package renamed from "com.google.protobuf" to
"com.google.protoshadebuf3".

daos-java module depends on DAOS which is assumed being installed under /usr/local/daos. If you have different
location, you need to set it with '-Ddaos.install.path=\<your DAOS install dir\>'. For example,

    mvn -DskipITs -Dgpg.skip -Ddaos.install.path=/code/daos/install clean install

daos-java module uses protobuf 3 to serialize/deserialize complex parameters between Java and C. The corresponding Java
code and C code are generated from src/main/resources/DunsAttribute.proto and put under src/main/java/io/daos/dfs/uns
and src/main/native respectively. If you change DunsAttribute.proto or want to regenerate these codes, you can build
with below command.

    mvn -DskipITs -Dgpg.skip -Dcompile.proto=true clean install

Before issuing above command, you need [protobuf 3](https://github.com/protocolbuffers/protobuf.git) and its
[C plugin](https://github.com/protobuf-c/protobuf-c.git) installed.

If you have DAOS pool and DAOS container with type of posix and both have labels defined, you can run integration test
when build with below command. Before running it, make sure you have DAOS environment properly setup, including server
and user environment variables.

    mvn -Dpool_id=<your pool uuid> -Dcont_id=<your container uuid> -Dpool_label=<pool label> -Dcont_label=<cont label>
        -Dgpg.skip clean install

User can go to each submodule and build it separately too.

For distribution, the default is for including two artifacts daos-jar and hadoop-daos, as well as core-site-daos-ref.xml,
which is to be merged with your Hadoop core-site.xml. The other choices are to include dependencies when build with
"-Pwith-deps" for all dependencies and "-Pwith-proto3-netty4-deps" for protobuf 3 and netty 4 dependencies.

## Documentation
You can run below command to generate JavaDoc. There could be some error message during build. Just ignore them if your
final build status is success. Then go to target/site folder to find documentation.

    mvn site

## Run
Beside DAOS setup and environment variables, one more environment for JVM signal chaining should be set as below.

    export LD_PRELOAD=<YOUR JDK HOME>/jre/lib/amd64/libjsig.so

* daos-java Jars

There are two choices when put daos-java jar, depending on your application.<br/>
1, `daos-java-<version>.jar`, if your app has protobuf 3 in your classpath.<br/>
2, `daos-java-<version>-protobuf3-netty4-shaded.jar`, if your app don't have protobuf3 or netty4 or different versions
in your classpath.<br/>

* core-site-daos-ref.xml

There are some DAOS specific configurations to be merged to your Hadoop core-site.xml. Check description tags inside
to determine which properties are required whilst some of them are optional.

* YARN

When run with Hadoop yarn, you need to add below configuration to core-site.xml.

```xml
<property>
<name>fs.AbstractFileSystem.daos.impl</name>
<value>io.daos.fs.hadoop.DaosAbsFsImpl</value>
</property>
```

DAOS has no data locality since it is remote storage. You need to add below configuration to scheduler configuration
file, like capacity-scheduler.xml in yarn.

```xml
<property>
  <name>yarn.scheduler.capacity.node-locality-delay</name>
  <value>-1</value>
</property>
```

## Contacts
For any questions, please post to our [user forum](https://daos.groups.io/g/daos). Bugs should be reported through our 
[issue tracker](https://jira.hpdd.intel.com/projects/DAOS) with a test case to reproduce the issue (when applicable) and
 [debug logs](./docs/debugging.md).
