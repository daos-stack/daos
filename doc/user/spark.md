# Getting Started with the DAOS Hadoop Filesystem

Here, we describe the steps required to build and deploy the DAOS Hadoop
filesystem, and the configurations to access DAOS in Spark and Hadoop. We
assume DAOS servers and agents have already been deployed in the environment.
Otherwise, they can be deployed by following the
[DAOS installation guide](https://daos-stack.github.io/admin/installation/).

## Build DAOS Hadoop Filesystem

Below are the steps to build the Java jar files for the DAOS Java and DAOS
Hadoop filesystem. These jar files are required when running Spark and Hadoop.
You can ignore this section if you already have the pre-built jars.

```bash
$ git clone https://github.com/daos-stack/daos.git
$ cd daos
$ git checkout <desired branch or commit>
## assume DAOS is built and installed to <daos_install> directory
$ cd src/client/java
$ mvn clean package -DskipITs -Ddaos.install.path=<daos_install>
```

After build, the package daos-java-<version>-assemble.tgz will be available
under distribution/target.

## Deploy DAOS Hadoop Filesystem

After unzipping `daos-java-<version>-assemble.tgz`, you will get the
following files.

### `daos-java-<version>.jar` and `hadoop-daos-<version>.jar`<br/>
They need to be deployed on every compute node that runs Spark or Hadoop.
Place them in a directory, e.g., $SPARK_HOME/jars for Spark and
$HADOOP_HOME/share/hadoop/common/lib for Hadoop, which are accessible to all
the nodes or copy them to every node.<br/>

### `daos-site-example.xml`<br/>
You have two choices, mapped pool/container UUIDs and DAOS UNS path, to
construct DAOS URI. If you choose the first choice, you have to copy the file
to your application config directory, e.g., $SPARK_HOME/conf for Spark and
$HADOOP_HOME/etc/hadoop for Hadoop. Then do some proper configuration and
rename it to daos-site.xml. For the second choice, daos-site.xml is optional.
See next section for details.

## Configure DAOS Hadoop FileSystem

### DAOS Environment Variable

Export all DAOS related env variables and the following env variable in
your application, e.g., spark-env.sh for Spark and hadoop-env.sh for Hadoop.
The following env enables signal chaining in JVM to better interoperate with
DAOS native code that installs its own signal handlers. It ensures that signal
calls are intercepted so that they do not actually replace the JVM's signal
handlers if the handlers conflict with those already installed by the JVM.
Instead, these calls save the new signal handlers, or "chain" them behind the
JVM-installed handlers. Later, when any of these signals are raised and found
not to be targeted at the JVM, the DAOS's handlers are invoked.

```bash
$ export LD_PRELOAD=<YOUR JDK HOME>/jre/lib/amd64/libjsig.so
```

### DAOS URIs

DAOS FileSystem binds to schema, "daos". And DAOS URIs are in the format of
"daos://\<authority\>//\[optional path\]". There are two ways to connect to
DAOS from Hadoop DAOS FileSystem, depending on the "\<authority\>".

* Mapped DAOS pool/container UUIDs

The URI is "daos://\<mapped pool UUID\>:\<mapped container UUID\>", such as
"daos://default:1". Please check description of "fs.defaultFS" in
[example](hadoop-daos/src/main/resources/daos-site-example.xml) for how to
construct your URI by mapping your UUIDs. In this way, all configurations are
in daos-site.xml which should be put in right place, e.g., Java classpath, and
loadable by Hadoop DAOS FileSystem.

If the DAOS pool and container have not been created, we can use the following
command to create them and get the pool UUID, container UUID, and service
replicas.

```bash
$ dmg pool create --scm-size=<scm size> --nvme-size=<nvme size>
$ daos cont create --pool <pool UUID> --svc <service replicas> --type POSIX
```

After that, configure daos-site.xml with the pool and container created.

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
  <property>
    <name>fs.daos.pool.svc</name>
    <value>your pool service replicas</value>
    <description>service list separated by ":" if more than one service</description>
  </property>
...
</configuration>
```
See [link](#daossite) for details about this configuration.

* DAOS UNS Path

The URI is "daos://uns:\<sequence\>/\<your uns path\>\[/sub path\]".
"\<your path\>" is your OS file path created by DAOS UNS method,
DaosUns.create(). "\[sub path\]" is optional. "\<sequence\>" is to identify the
unique UNS path in Hadoop. You can create the UNS path with below command.

```bash
$ daos cont create --pool <pool UUID> --svc <svc list> -path <your path> --type=POSIX
```
Or

```bash
$ java -Dpath="your path" -Dpool_id="your pool uuid" -cp ./daos-java-1.1.0-shaded.jar io.daos.dfs.DaosUns create
```

After creation, you can use below command to see what DAOS properties set to
the path.

```path
$ getfattr -d -m - <your path>
```

### Tune More Configurations

If your DAOS URI is the mapped UUIDs, you can follow descriptions of each
config item in [example](hadoop-daos/src/main/resources/daos-site-example.xml)
to set your own values in loadable daos-site.xml.

If your DAOS URI is the UNS path, your configurations, except those set by DAOS
UNS creation, in daos-site.xml can still be effective. To make configuration
source consistent, an alternative to configuration file, daos-site.xml, is to
set all configurations to the UNS path. You put the configs to the same UNS
path with below command.

```bash
# install attr package if get "command not found" error
$ setfattr -n user.daos.hadoop -v "fs.daos.server.group=daos_server:fs.daos.pool.svc=0" <your path>
```
Or

```bash
$ java -Dpath="your path" -Dattr=user.daos.hadoop -Dvalue="fs.daos.server.group=daos_server:fs.daos.pool.svc=0"
    -cp ./daos-java-1.1.0-shaded.jar io.daos.dfs.DaosUns setappinfo
```

For the "value" property, you need to follow pattern, key1=value1:key2=value2..
.. And key* should be from
[example](hadoop-daos/src/main/resources/daos-site-example.xml). If value*
contains characters of '=' or ':', you need to escape the value with below
command.

```bash
$ java -Dop=escape-app-value -Dinput="daos_server:1=2" -cp ./daos-java-1.1.0-shaded.jar io.daos.dfs.DaosUns util
```

You'll get escaped value, "daos_server\u003a1\u003d2", for "daos_server:1=2".

If you configure the same property in both daos-site.mxl and UNS path, the
value in daos-sitem.xml takes priority. If user set Hadoop configuration before
initializing Hadoop DAOS FileSystem, the user's configuration takes priority.

### Configure Spark to Use DAOS

To access DAOS Hadoop filesystem in Spark, add the jar files to the classpath
of the Spark executor and driver. This can be configured in Spark's
configuration file spark-defaults.conf.

```
spark.executor.extraClassPath   /path/to/daos-java-<version>.jar:/path/to/hadoop-daos-<version>.jar
spark.driver.extraClassPath     /path/to/daos-java-<version>.jar:/path/to/hadoop-daos-<version>.jar
```

#### Access DAOS in Spark

All Spark APIs that work with the Hadoop filesystem will work with DAOS. We use
the `daos://` URI to access files stored in DAOS. For example, to read
people.json file from the root directory of DAOS filesystem, we can use the
following pySpark code:

```python
df = spark.read.json("daos://default:1/people.json")
```

### Configure Hadoop to Use DAOS

Edit $HADOOP_HOME/etc/hadoop/core-site.xml to change fs.defaultFS to
“daos://default:1” or "daos://uns/\<your path\>". Then append below configuration
to this file and $HADOOP_HOME/etc/hadoop/yarn-site.xml.

```xml
<property>
    <name>fs.AbstractFileSystem.daos.impl</name>
    <value>io.daos.fs.hadoop.DaosAbsFsImpl</value>
</property>

```

DAOS has no data locality since it is remote storage. You need to add below
configuration to scheduler configuration file, like capacity-scheduler.xml in
yarn.

```xml
<property>
  <name>yarn.scheduler.capacity.node-locality-delay</name>
  <value>-1</value>
</property>
```

Then replicate daos-site.xml, core-site.xml, yarn-site.xml and
capacity-scheduler.xml to other nodes.

#### Access DAOS in Hadoop

If everything goes well, you should see “/user” directory being listed after
issuing below command.

```bash
$ hadoop fs -ls /
```

You can also play around with other Hadoop commands, like -copyFromLocal and
-copyToLocal. You can also start Yarn and run some mapreduce jobs on Yarn. Just
make sure you have DAOS URI, “daos://default:1/”, set correctly in your job.

#### Known Issues

If you use Omni-path PSM2 provider in DAOS, you'll get connection issue in
Yarn container due to PSM2 resource not being released properly in time.

## <a name="daossite"><a>daos-site.xml
The default pool and container are configured by `fs.daos.pool.uuid` and
`fs.daos.container.uuid`. The default DAOS filesystem can be accessed by URI
`daos://default:1` in Spark and Hadoop. In HDFS, the URI is composed by a
master host name (or IP address) and a port for example hdfs://<HostName>:8020.
In DAOS, we don't use host name and port to connect, instead we use pool UUID
and container UUID to specify the DFS filesystem. We do not put the UUIDs in
URI as UUID is not a valid port number. Instead, the hostname `default` maps to
the default pool configured by `fs.daos.pool.uuid` and the port 1 maps to the
default container configured by `fs.daos.container.uuid`.

It is also possible to configure multiple pools and containers in the
daos-site.xml and use different URI to access them. For example, to access
another container in the default pool using URI `daos://default:2`, we can
configure the container UUID in `c2.fs.daos.container.uuid`. To access another
pool and container using `daos://pool1:3`, we can configure the pool UUID in
`pool1.fs.daos.pool.uuid` and container UUID in `c3.fs.daos.container.uuid`.
See examples,

```
"daos://default:1" reads values of "fs.daos.pool.uuid" and "fs.daos.container.uuid"
"daos://default:2" reads values of "fs.daos.pool.uuid" and "c2.fs.daos.container.uuid"
"daos://pool1:3" reads values of "pool1.fs.daos.pool.uuid" and "c3.fs.daos.container.uuid"
```

Different URIs represent different DAOS filesystem and they can also be
configured with different settings like the read buffer size, etc. For example,
to configure the filesystem represented by `daos://default:2`, we use property
name prefixed with c2, i.e., `*c2.fs.daos.*`. To configure the filesystem
represented by `daos://pool1:3`, we use property name prefixed with pool1c3,
i.e., `*pool1c3.fs.daos.*`. If no specific configurations are set, they fall
back to the configuration set for the default pool and container started with
`*fs.daos*.`.

One tricky example is to access same DAOS filesystem with two Hadoop FileSystem
instances. One instance is configured with preload enabled in the daos-site.xml.
The other instance is preload disabled. With above design, you can use two
different URIs, `daos://default:1` and `daos://default:2`. In the daos-site.xml,
you can set `fs.daos.container.uuid` and `c2.fs.daos.container.uuid` to same the
container UUID. Then set `fs.daos.preload.size` to a value greater than 0 and
`c2.fs.daos.preload.size` to 0.
