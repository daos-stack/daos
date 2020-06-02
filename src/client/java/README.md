## Description
This module is DAOS Java client and DAOS DFS implementation of Hadoop FileSystem. There are two submodules,
daos-java and hadoop-daos.

### daos-java
It wraps most of common APIs from daos_fs.h and daos_uns.h, as well as some pool and container connection related APIs
from daos_pool.h and daos_cont.h. There are three main classes, DaosFsClient, DaosFile and DaosUns.

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

It wraps some DAOS UNS APIs, like creating, resolving and destroying UNS path, as well as parsing DAOS UNS string
attribute. Besides, this class can be run from command line in which you can call all DAOS UNS APIs with different
parameters. Use below command to show its usage in details.
    ```bash
    java -cp ./daos-java-1.1.0-shaded.jar io.daos.dfs.DaosUns --help
    ```

### hadoop-daos
It's DAOS FS implementation of Hadoop FileSystem based on daos-java. There are three main classes, DaosFileSystem,
DaosInputStream and DaosOutputStream.

* DaosFileSystem, it provides APIs to create file as DaosOutputStream, open file as DaosInputStream, list file
    status, create directory and so on. It also does file system initialization and finalization.
* DaosInputStream, for reading file, preload is also possible in this class.
* DaosOutputStream, for writing file.

#### Hadoop DAOS FileSystem Configuration

##### DAOS URIs
DAOS FileSystem binds to schema, "daos". And DAOS URIs are in the format of "daos://\<authority\>//\[optional path\]".
There are two ways to connect to DAOS from Hadoop DAOS FileSystem, depending on the "\<authority\>".

* Mapped DAOS pool/container UUIDs

The URI is "daos://<mapped pool UUID>:<mapped container UUID>", such as "daos://default:1". Please check description of
"fs.defaultFS" in [example](hadoop-daos/src/main/resources/daos-site-example.xml) for how to construct your URI by
mapping your UUIDs. In this way, all configurations are in daos-site.xml which should be put in right place, e.g.,
Java classpath, and loadable by Hadoop DAOS FileSystem.

* DAOS UNS Path

The URI is "daos://uns/<your path>". "<your path>" is your OS file path created by DAOS UNS method, DaosUns.create().
You can create the UNS path with below command.

    ```bash
    daos cont create --pool <pool UUID> --svc <svc list> -path <your path> --type=POSIX
    ```
Or

    ```bash
    java -Dpath="your path" -Dpool_id="your pool uuid" -cp ./daos-java-1.1.0-shaded.jar io.daos.dfs.DaosUns create
    ```

After creation, you can use below command to see what DAOS properties set to the path.

    ```path
    getfattr -d -m - <your path>
    ```

##### Tune More Configurations

If your DAOS URI is the mapped UUIDs, you can follow descriptions of each config item in
[example](hadoop-daos/src/main/resources/daos-site-example.xml) to set your own values in loadable daos-site.xml.

If your DAOS URI is the UNS path, your configurations, except those set by DAOS UNS creation, in daos-site.xml can still
be effective. To make configuration source consistent, an alternative to configuration file, daos-site.xml, is to set
all configurations to the UNS path. You put the configs to the same UNS path with below command.
   ```bash
   java -Dpath="your path" -Dattr=user.daos.hadoop -Dvalue="fs.daos.server.group=daos_server:fs.daos.pool.svc=0"
        -cp ./daos-java-1.1.0-shaded.jar io.daos.dfs.DaosUns setappinfo
   ```
For the "value" property, you need to follow pattern, key1=value1:key2=value2... And key* should be from
[example](hadoop-daos/src/main/resources/daos-site-example.xml). If value* contains characters of '=' or ':', you need
to escape the value with below command.
   ```bash
    java -Dop=escape-app-value -Dinput="daos_server:1=2" -cp ./daos-java-1.1.0-shaded.jar io.daos.dfs.DaosUns util
   ```
You'll get escaped value, "daos_server\u003a1\u003d2", for "daos_server:1=2".

If you configure the same property in both daos-site.mxl and UNS path, the value in daos-sitem.xml takes priority. If
user set Hadoop configuration before initializing Hadoop DAOS FileSystem, the user's configuration takes priority.

## Build
They are Java modules and built by Maven. Java 1.8 and Maven 3 are required to build these modules. After they are
installed, you can change to this <DAOS_INSTALL>/src/client/java folder and build by below command line.

    mvn -DskipITs clean install

daos-java module depends on DAOS which is assumed being installed under /usr/local/daos. If you have different
location, you need to set it with '-Ddaos.install.path=<your DAOS install dir>'. For example,

    mvn -DskipITs -Ddaos.install.path=/code/daos/install clean install
    
daos-java module also depends on protobuf 3.x and protobuf C plugin whose version should be in line with DAOS's protobuf
version, which is 3.x for now. The Java code and C code are generated from src/main/resources/DunsAttribute.proto and
put under src/main/java/io/daos/dfs/uns and src/main/native respectively. If you change DunsAttribute.proto or want to
regenerate these codes, you can build with below command.
    
    mvn -DskipITs -Dcompile.proto=true clean install 

Before issuing above command, you need [protobuf 3](https://github.com/protocolbuffers/protobuf.git) and its
[C plugin](https://github.com/protobuf-c/protobuf-c.git) installed.

If you have DAOS pool and DAOS container with type of posix, you can run integration test when build with below command.
Before running it, make sure you have DAOS environment properly setup, including server and user environment variables.

    mvn -Dpool_id=<your pool uuid> -Dcont_id=<your container uuid> clean install

User can go to each submodule and build it separately too. 

## Documentation
You can run below command to generate JavaDoc. There could be some error message during build. Just ignore them if your
final build status is success. Then go to target/site folder to find documentation.

    mvn site

## Run
Beside DAOS setup and environment variables, one more environment for JVM signal chaining should be set as below.

    export LD_PRELOAD=<YOUR JDK HOME>/jre/lib/amd64/libjsig.so

* daos-java Jars

There are three choices when put daos-java jar, depending on your application.<br/>
1, daos-java-\<version\>.jar, if your app has protobuf 3 in your classpath.<br/>
2, daos-java-\<version\>-protobuf3-shaded.jar, if your app don't have protobuf 3 or have protobuf 2 in your classpath.
<br/>
3, daos-java-\<version\>-shaded.jar, if you want to run daos-jar as standalone app.<br/>

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
 [debug logs](./doc/debugging.md).