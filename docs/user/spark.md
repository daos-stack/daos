# DAOS Hadoop Filesystem

Here, we describe the quick steps required to use the DAOS Hadoop filesystem
to access DAOS from Hadoop and Spark.

## Prerequisites

1. Linux OS
2. Java 8
3. Hadoop 2.7 or later
4. Spark 3.1 or later
5. DAOS Readiness

We assume that the DAOS servers and agents have already been deployed in the
environment. Otherwise, they can be deployed by following the
[DAOS Installation Guide](https://docs.daos.io/v2.2/admin/installation/).

## Maven Download

There are two artifacts to download, daos-java and hadoop-daos, from maven.
Here are maven dependencies.

You can download them with below commands if you have maven installed.
```bash
mvn dependency:get -Dartifact=io.daos:daos-java:<version> -Ddest=./
mvn dependency:get -Dartifact=io.daos:hadoop-daos:<version> -Ddest=./
```

Or search these artifacts from maven central(https://search.maven.org) and
download them manually.

You can also build artifacts by yourself.
see [Build DAOS Hadoop Filesystem](#builddaos) for details.


## Deployment

### JAR Files

`daos-java-<version>.jar` and `hadoop-daos-<version>.jar` need to be deployed
on every compute node that runs Spark or Hadoop.
Place them in a directory, e.g., `$SPARK_HOME/jars` for Spark and
`$HADOOP_HOME/share/hadoop/common/lib` for Hadoop, which is accessible to all
the nodes or copy them to every node.<br/>

### `daos-site-example.xml`

Extract from `hadoop-daos-<version>.jar` and rename to `daos-site.xml`. Then
copy it to your application config directory, e.g., `$SPARK_HOME/conf` for
Spark and `$HADOOP_HOME/etc/hadoop` for Hadoop.

## Configuring Hadoop

### Environment Variable

Export all DAOS-specific env variables in your application, e.g.,
`spark-env.sh` for Spark and `hadoop-env.sh` for Hadoop. Or you can simply put
env variables in your `.bashrc`.

Besides, you should have LD\_LIBRARY\_PATH include DAOS library path so that Java
can link to DAOS libs, like below.

```bash
$ export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:<DAOS_INSTALL>/lib64:<DAOS_INSTALL>/lib
```

### DAOS URI

In `daos-site.xml`, we default the DAOS URI as simplest form, "daos:///". For
other form of URIs, please check [DAOS More URIs](#uris).

If the DAOS pool and container have not been created, we can use the following
command to create them and get the pool UUID and container UUID.

```bash
$ export DAOS_POOL="mypool"
$ export DAOS_CONT="mycont"
$ dmg pool create --scm-size=<scm size> --nvme-size=<nvme size> $DAOS_POOL
$ daos cont create --label $DAOS_CONT --type POSIX $DAOS_POOL
```

After that, configure `daos-site.xml` with the pool and container created.

```xml
<configuration>
...
  <property>
    <name>fs.daos.pool.uuid</name>
    <value>your pool UUID</value>
    <description>UUID of DAOS pool</description>
  </property>
  <property>
    <name>fs.daos.container.uuid</name>
    <value>your container UUID</value>
    <description>UUID of DAOS container created with "--type posix"</description>
  </property>
...
</configuration>
```

Please put `daos-site.xml` in right place, e.g., Java classpath, and
loadable by Hadoop DAOS FileSystem.

### Validating Hadoop Access

If everything goes well, you should see `/user` directory being listed after
issuing below command.

```bash
$ hadoop fs -ls /
```

You can also play around with other Hadoop commands, like -copyFromLocal and
-copyToLocal. You can also start Yarn and run some mapreduce jobs on Yarn.
See [Run Map-Reduce in Hadoop](#mapreduce)

## Configuring Spark

To access DAOS Hadoop filesystem in Spark, add the jar files to the classpath
of the Spark executor and driver. This can be configured in Spark's
configuration file `spark-defaults.conf`.

```
spark.executor.extraClassPath   /path/to/daos-java-<version>.jar:/path/to/hadoop-daos-<version>.jar
spark.driver.extraClassPath     /path/to/daos-java-<version>.jar:/path/to/hadoop-daos-<version>.jar
```

### Validatng Spark Access

All Spark APIs that work with the Hadoop filesystem will work with DAOS. We can
use the `daos:///` URI to access files stored in DAOS. For example, to read the
`people.json` file from the root directory of DAOS filesystem, we can use the
following pySpark code:

```python
df = spark.read.json("daos:///people.json")
```

## Appendix

### <a name="builddaos"></a>Building DAOS Hadoop Filesystem

Below are the steps to build the Java jar files for the DAOS Java and DAOS
Hadoop filesystem. Spark and Hadoop require these jars in their classpath.
You can ignore this section if you already have the pre-built jars.

```bash
$ git clone https://github.com/daos-stack/daos.git
$ cd daos
$ git checkout <desired branch or commit>
## assume DAOS is built and installed to <daos_install> directory
$ cd src/client/java
## with-proto3-netty4-deps profile builds jars with protobuf 3 and netty-buffer 4 shaded
## It spares you potential third-party jar compatibility issue.
$ mvn -Pdistribute,with-proto3-netty4-deps clean package -DskipTests -Dgpg.skip -Ddaos.install.path=<daos_install>
```

After build, the package `daos-java-<version>-assemble.tgz` will be available
under `distribution/target`.

### <a name="uris"></a>DAOS More URIs

DAOS FileSystem binds to schema "daos".  DAOS URIs are in the format of
"daos://\[authority\]//\[path\]". Both authority and path are optional. There
are three types of DAOS URIs, DAOS UNS path, DAOS Non-UNS path and
Special UUID path depending on where you want the DAOS Filesystem to get
initialized and configured.

#### DAOS UNS Path

The simple form of URI is "daos:///\<your uns path\>\[/sub path\]".
"\<your path\>" is your OS file path created with the `daos` command or Java
DAOS UNS method, `DaosUns.create()`. The "\[sub path\]" is optional. You can
create the UNS path with below command.

```bash
$ daos cont create --label $DAOS_CONT --path <your_path> --type POSIX $DAOS_POOL
```
Or

```bash
$ java -Dpath="your_path" -Dpool_id=$DAOS_POOL -cp ./daos-java-<version>-shaded.jar io.daos.dfs.DaosUns create
```

After creation, you can use below command to see what DAOS properties set to
the path.

```path
$ getfattr -d -m - <your path>
```

#### DAOS Non-UNS Path

Check [Set DAOS URI and Pool/Container](#non-uns).

#### Special UUID Path
DAOS supports a specialized URI with pool/container UUIDs embedded. The format
is "daos://pool UUID/container UUID". As you can see, we don't need to find the
UUIDs from neither UNS path nor configuration like above two types of URIs.

You may want to connect to two DAOS servers or two DFS instances mounted to
different containers in one DAOS server from same JVM. Then, you need to add
authority to your URI to make it unique since Hadoop caches filesystem instance
keyed by "schema + authority" in global (JVM). It applies to the both types of
URIs described above.

### <a name="mapreduce"></a>Run Map-Reduce in Hadoop

Edit `$HADOOP_HOME/etc/hadoop/core-site.xml` to change fs.defaultFS to
`daos:///`. It is not recommended to set fs.defaultFS to a DAOS UNS path.
You may get an error complaining pool/container UUIDs cannot be found. It's
because Hadoop considers the default filesystem is DAOS since you configured
DAOS UNS URI. YARN has some working directories defaulting to local path
without schema, like "/tmp/yarn", which is then constructed as
"daos:///tmp/yarn". With this URI, Hadoop cannot connect to DAOS since no
pool/container UUIDs can be found if daos-site.xml is not provided too.

Then append below configuration to this file and
`$HADOOP_HOME/etc/hadoop/yarn-site.xml`.

```xml
<property>
    <name>fs.AbstractFileSystem.daos.impl</name>
    <value>io.daos.fs.hadoop.DaosAbsFsImpl</value>
</property>

```

DAOS has no data locality since it is remote storage. You need to add below
configuration to the scheduler configuration file, like
`capacity-scheduler.xml` in yarn.

```xml
<property>
  <name>yarn.scheduler.capacity.node-locality-delay</name>
  <value>-1</value>
</property>
```

Then replicate `daos-site.xml`, `core-site.xml`, `yarn-site.xml` and
`capacity-scheduler.xml` to other nodes.

#### Known Issues

If you use Omni-Path PSM2 provider in DAOS, you'll get connection issue in
Yarn container due to PSM2 resource not being released properly in time.
The PSM2 provides has known issues with DAOS and is not supported in
production environments.

### Tune More Configurations

If your DAOS URI is the non-UNS, you can follow descriptions of each
config item to set your own values in loadable `daos-site.xml`.

If your DAOS URI is the UNS path, your configurations, except those set by DAOS
UNS creation, in `daos-site.xml` can still be effective. To make configuration
source consistent, an alternative to the configuration file `daos-site.xml` is
to set all configurations to the UNS path. You put the configs to the same UNS
path with below command.

```bash
# install attr package if get "command not found" error
$ setfattr -n user.daos.hadoop -v "fs.daos.server.group=daos_server" <your path>
```

Or

```bash
$ java -Dpath="your path" -Dattr=user.daos.hadoop -Dvalue="fs.daos.server.group=daos_server"
    -cp ./daos-java-<version>-shaded.jar io.daos.dfs.DaosUns setappinfo
```

For the "value" property, you need to follow pattern, key1=value1:key2=value2..
.. And key\* should be from
[daos-site-example.xml](https://github.com/daos-stack/daos/blob/release/master/src/client/java/hadoop-daos/src/main/resources/daos-site-example.xml).
If value\* contains characters of '=' or ':', you need to escape the value with
below command.

```bash
$ java -Dop=escape-app-value -Dinput="daos_server:1=2" -cp ./daos-java-<version>-shaded.jar io.daos.dfs.DaosUns util
```

You'll get escaped value, "daos_server\u003a1\u003d2", for "daos_server:1=2".

If you configure the same property in both `daos-site.xml` and UNS path, the
value in `daos-site.xml` takes priority. If user sets Hadoop configuration
before initializing Hadoop DAOS FileSystem, the user's configuration takes
priority.

### PSM2 Issue

For some libfabric providers, like PSM2, signal chaining should be enabled to
better interoperate with DAOS and its dependencies which may install its own
signal handlers. It ensures that signal calls are intercepted so that they do
not actually replace the JVM's signal handlers if the handlers conflict with
those already installed by the JVM. Instead, these calls save the new signal
handlers, or "chain" them behind the JVM-installed handlers. Later, when any of
these signals are raised and found not to be targeted at the JVM, the DAOS's
handlers are invoked.

```bash
$ export LD_PRELOAD=<YOUR JDK HOME>/jre/lib/amd64/libjsig.so
```
