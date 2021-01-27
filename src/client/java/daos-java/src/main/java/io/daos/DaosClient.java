/*
 * (C) Copyright 2018-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

package io.daos;

import io.daos.dfs.*;
import org.apache.commons.lang.ObjectUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.CopyOption;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Deque;
import java.util.concurrent.ConcurrentLinkedDeque;

/**
 * Java DAOS client for common pool/container operations which is indirectly invoked via JNI.
 * Other types of client, like DFS client and Object client, should reference this base client
 * for pool/container related operations.
 *
 * <p>
 * Besides, it also does some common works, like,
 * <li>load dynamic library, libdaos-jni.so, at startup</li>
 * <li>register shutdown hook for closing all registered clients and finalize DAOS safely</li>
 */
public class DaosClient implements ForceCloseable {

  private DaosClientBuilder builder;

  private DaosPool pool;

  private DaosContainer container;

  private volatile boolean inited;

  private static volatile boolean finalized;

  public static final String LIB_NAME = "daos-jni";

  public static final Runnable FINALIZER;

  private static final Deque<ForceCloseable> connections = new ConcurrentLinkedDeque<>();

  private static final Logger log = LoggerFactory.getLogger(DaosClient.class);

  static {
    loadLib();
    FINALIZER = new Runnable() {
      @Override
      public void run() {
        try {
          closeAll();
          daosSafeFinalize();
          log.info("daos finalized");
          ShutdownHookManager.removeHook(this);
        } catch (Throwable e) {
          log.error("failed to finalize DAOS", e);
        }
      }
    };
    ShutdownHookManager.addHook(FINALIZER);
    if (log.isDebugEnabled()) {
      log.debug("daos finalizer hook added");
    }
  }

  /**
   * trigger static initializer
   */
  public static void initClient() {
  }

  private DaosClient(DaosClientBuilder builder) {
    this.builder = builder;
  }

  private static void loadLib() {
    try {
      System.loadLibrary(LIB_NAME);
      log.info("lib{}.so loaded from library", LIB_NAME);
    } catch (UnsatisfiedLinkError e) {
      loadFromJar();
    }
  }

  private static void loadFromJar() {
    File tempDir = null;
    String filePath = new StringBuilder("/lib").append(LIB_NAME).append(".so").toString();

    try {
      tempDir = Files.createTempDirectory("daos").toFile();
      tempDir.deleteOnExit();
      loadByPath(filePath, tempDir);
    } catch (IOException e) {
      if (tempDir != null) {
        tempDir.delete();
      }
      throw new RuntimeException("failed to load lib from jar, " + LIB_NAME, e);
    }
    log.info(filePath + " loaded from jar");
  }

  private static void loadByPath(String path, File tempDir) {
    File tempFile = null;
    String[] fields = path.split("/");
    String name = fields[fields.length - 1];

    try (InputStream is = DaosFsClient.class.getResourceAsStream(path)) {
      tempFile = new File(tempDir, name);
      tempFile.deleteOnExit();
      Files.copy(is, tempFile.toPath(), new CopyOption[]{StandardCopyOption.REPLACE_EXISTING});
      System.load(tempFile.getAbsolutePath());
    } catch (IOException e) {
      if (tempFile != null) {
        tempFile.delete();
      }
    }
  }

  /**
   * open pool.
   *
   * @param poolId      pool id
   * @param serverGroup DAOS server group
   * @param flags       see {@link DaosFsClient.DaosFsClientBuilder#poolFlags(int)}
   * @return pool pointer or pool handle
   * @throws IOException {@link DaosIOException}
   */
  public static native long daosOpenPool(String poolId, String serverGroup, int flags) throws IOException;

  /**
   * open container.
   *
   * @param poolPtr pointer to pool
   * @param contId  container id
   * @param flags   see {@link DaosFsClient.DaosFsClientBuilder#containerFlags(int)}
   * @return container pointer or container handle
   * @throws IOException {@link DaosIOException}
   */
  static native long daosOpenCont(long poolPtr, String contId, int flags) throws IOException;

  /**
   * close container.
   *
   * @param contPtr pointer to container
   * @throws IOException {@link DaosIOException}
   */
  static native void daosCloseContainer(long contPtr) throws IOException;

  /**
   * create event queue with given number of events.
   *
   * @param nbrOfEvents number of events to associate with the queue
   * @return the handler of event queue
   */
  public static native long createEventQueue(int nbrOfEvents) throws IOException;

  /**
   * poll completed events without wait.
   *
   * @param eqWrapperHdl  handle of EQ wrapper
   * @param memoryAddress memory address of ByteBuf to hold indices of completed events
   * @param nbrOfEvents   maximum number of events to complete
   * @param timeoutMs
   * @throws IOException
   */
  public static native void pollCompleted(long eqWrapperHdl, long memoryAddress,
                                          int nbrOfEvents, int timeoutMs) throws IOException;

  /**
   * destroy event queue identified by <code>queueHdl</code>.
   *
   * @param queueHdl queue handler
   */
  public static native void destroyEventQueue(long queueHdl) throws IOException;

  /**
   * close pool.
   *
   * @param poolPtr pointer to pool
   * @throws IOException {@link DaosIOException}
   */
  public static native void daosClosePool(long poolPtr) throws IOException;

  static synchronized void closeAll() throws IOException {
    ForceCloseable c;
    while ((c = connections.peek()) != null) {
      c.forceClose();
      connections.remove(c);
    }
  }

  private void init() throws IOException {
    if (inited) {
      return;
    }

    pool = DaosPool.getInstance(builder.poolId, builder.serverGroup,
        builder.poolFlags);

    if (builder.contId != null) {
      container = DaosContainer.getInstance(builder.contId, pool.getPoolPtr(), builder.containerFlags);
    } else {
      log.warn("container UUID is not set");
    }
    inited = true;
    registerForShutdown(this);
    log.info("DaosClient for {}, {} initialized", builder.poolId, builder.contId);
  }

  public long getPoolPtr() {
    return pool == null ? 0 : pool.getPoolPtr();
  }

  public long getContPtr() {
    return container == null ? 0 : container.getContPtr();
  }

  /**
   * register {@link ForceCloseable} object to release resources in case of abnormal shutdown.
   * The resource releasing will be performed in reverse order of registering.
   *
   * @param closeable
   */
  public void registerForShutdown(ForceCloseable closeable) {
    connections.push(closeable);
  }

  @Override
  public void close() throws IOException {
    disconnect();
  }

  @Override
  public void forceClose() throws IOException {
    disconnect();
  }

  private synchronized void disconnect() throws IOException {
    if (inited && pool != null) {
      if (container != null) {
        container.close();
      }
      pool.close();
      log.info("DaosClient for {}, {} disconnected", builder.poolId, builder.contId);
    }
    inited = false;
  }

  /**
   * finalize DAOS safely to avoid finalizing multiple times
   *
   * @throws IOException
   */
  public static synchronized void daosSafeFinalize() throws IOException {
    if (!finalized) {
      daosFinalize();
      finalized = true;
    }
  }

  /**
   * finalize DAOS client.
   *
   * @throws IOException {@link DaosIOException}
   */
  private static native void daosFinalize() throws IOException;

  @Override
  public String toString() {
    return "DaosClient{" +
        "inited=" + inited +
        '}';
  }

  /**
   * A builder to build {@link DaosClient}. A subclass can extend it to build its own client.
   */
  public static class DaosClientBuilder<T extends DaosClientBuilder<T>> implements Cloneable {
    private String poolId;
    private String contId;
    private String ranks = Constants.POOL_DEFAULT_RANKS;
    private String serverGroup = Constants.POOL_DEFAULT_SERVER_GROUP;
    private int containerFlags = Constants.ACCESS_FLAG_CONTAINER_READWRITE;
    private int poolFlags = Constants.ACCESS_FLAG_POOL_READWRITE;
    private int poolMode = Constants.MODE_POOL_GROUP_READWRITE | Constants.MODE_POOL_OTHER_READWRITE |
        Constants.MODE_POOL_USER_READWRITE;

    public T poolId(String poolId) {
      this.poolId = poolId;
      return (T) this;
    }

    public String getPoolId() {
      return poolId;
    }

    public T containerId(String contId) {
      this.contId = contId;
      return (T) this;
    }

    public String getContId() {
      return contId;
    }

    /**
     * one or more ranks separated by ":".
     *
     * @param ranks default is "0"
     * @return DaosFsClientBuilder
     */
    public T ranks(String ranks) {
      this.ranks = ranks;
      return (T) this;
    }

    /**
     * set group name of server.
     *
     * @param serverGroup default is 'daos_server'
     * @return DaosFsClientBuilder
     */
    public T serverGroup(String serverGroup) {
      this.serverGroup = serverGroup;
      return (T) this;
    }

    /**
     * set container mode when open container.
     *
     * @param containerFlags should be one of {@link Constants#ACCESS_FLAG_CONTAINER_READONLY},
     *                       {@link Constants#ACCESS_FLAG_CONTAINER_READWRITE} and
     *                       {@link Constants#ACCESS_FLAG_CONTAINER_NOSLIP}
     *                       Default value is {@link Constants#ACCESS_FLAG_CONTAINER_READWRITE}
     * @return DaosFsClientBuilder
     */
    public T containerFlags(int containerFlags) {
      this.containerFlags = containerFlags;
      return (T) this;
    }

    /**
     * set pool mode for creating pool
     *
     * @param poolMode should be one or combination of below three groups.
     *                 <li>
     *                 user:
     *                 {@link Constants#MODE_POOL_USER_READONLY}
     *                 {@link Constants#MODE_POOL_USER_READWRITE}
     *                 {@link Constants#MODE_POOL_USER_EXECUTE}
     *                 </li>
     *                 <li>
     *                 group:
     *                 {@link Constants#MODE_POOL_GROUP_READONLY}
     *                 {@link Constants#MODE_POOL_GROUP_READWRITE}
     *                 {@link Constants#MODE_POOL_GROUP_EXECUTE}
     *                 </li>
     *                 <li>
     *                 other:
     *                 {@link Constants#MODE_POOL_OTHER_READONLY}
     *                 {@link Constants#MODE_POOL_OTHER_READWRITE}
     *                 {@link Constants#MODE_POOL_OTHER_EXECUTE}
     *                 </li>
     * @return DaosFsClientBuilder
     */
    public T poolMode(int poolMode) {
      this.poolMode = poolMode;
      return (T) this;
    }

    /**
     * set pool flags for opening pool.
     *
     * @param poolFlags should be one of
     *                  {@link Constants#ACCESS_FLAG_POOL_READONLY}
     *                  {@link Constants#ACCESS_FLAG_POOL_READWRITE}
     *                  {@link Constants#ACCESS_FLAG_POOL_EXECUTE}
     *
     *                  <p>
     *                  Default is {@link Constants#ACCESS_FLAG_POOL_READWRITE}
     * @return DaosFsClientBuilder
     */
    public T poolFlags(int poolFlags) {
      this.poolFlags = poolFlags;
      return (T) this;
    }

    @Override
    public DaosClientBuilder clone() throws CloneNotSupportedException {
      return (DaosClientBuilder) super.clone();
    }

    /**
     * create new instance of {@link DaosClient} from this builder.
     * Subclass may return object of different hierarchy. So return type is {@link Object}
     * Instead of {@link DaosClient}.
     *
     * @return new instance of DaosClient
     * @throws IOException
     */
    public Object build() throws IOException {
      if (poolId == null) {
        throw new IllegalArgumentException("need pool UUID");
      }
      DaosClient client = new DaosClient((DaosClientBuilder) ObjectUtils.clone(this));
      client.init();
      return client;
    }
  }
}
