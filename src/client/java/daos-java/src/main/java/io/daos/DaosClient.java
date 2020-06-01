/*
 * (C) Copyright 2018-2019 Intel Corporation.
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

import io.daos.dfs.Constants;
import io.daos.dfs.DaosFsClient;
import io.daos.dfs.DaosIOException;
import io.daos.dfs.ShutdownHookManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.CopyOption;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;

public class DaosClient implements ForceCloseable {

  private String poolId;

  private String contId;

  private long poolPtr;

  private long contPtr;

  public static final String LIB_NAME = "daos-jni";

  public static final Runnable FINALIZER;

  private static final Queue<ForceCloseable> connections = new ConcurrentLinkedQueue<>();

  private static final Logger log = LoggerFactory.getLogger(DaosClient.class);

  static {
    loadLib();
    FINALIZER = new Runnable() {
      @Override
      public void run() {
        try {
          closeAll();
          daosFinalize();
          log.info("daos finalized");
          ShutdownHookManager.removeHook(this);
        } catch (IOException e) {
          log.error("failed to finalize DAOS", e);
        }
      }
    };
    ShutdownHookManager.addHook(FINALIZER);
    if (log.isDebugEnabled()) {
      log.debug("daos finalizer hook added");
    }
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
   * @param poolId
   * pool id
   * @param serverGroup
   * DAOS server group
   * @param ranks
   * pool ranks
   * @param flags
   * see {@link DaosFsClient.DaosFsClientBuilder#poolFlags(int)}
   * @return pool pointer or pool handle
   * @throws IOException
   * {@link DaosIOException}
   */
  static native long daosOpenPool(String poolId, String serverGroup, String ranks, int flags) throws IOException;

  /**
   * open container.
   *
   * @param poolPtr
   * pointer to pool
   * @param contId
   * container id
   * @param flags
   * see {@link DaosFsClient.DaosFsClientBuilder#containerFlags(int)}
   * @return container pointer or container handle
   * @throws IOException
   * {@link DaosIOException}
   */
  static native long daosOpenCont(long poolPtr, String contId, int flags) throws IOException;

  /**
   * close container.
   *
   * @param contPtr
   * pointer to container
   * @throws IOException
   * {@link DaosIOException}
   */
  static native void daosCloseContainer(long contPtr) throws IOException;

  /**
   * close pool.
   *
   * @param poolPtr
   * pointer to pool
   * @throws IOException
   * {@link DaosIOException}
   */
  static native void daosClosePool(long poolPtr) throws IOException;

  static synchronized void closeAll() throws IOException {
    for (ForceCloseable conn : connections) {
      conn.forceClose();
    }
  }

  @Override
  public void close() throws IOException {

  }

  @Override
  public void forceClose() throws IOException {

  }

  /**
   * finalize DAOS client.
   *
   * @throws IOException {@link DaosIOException}
   */
  static synchronized native void daosFinalize() throws IOException;

  public static class DaosClientBuilder implements Cloneable {
    private String poolId;
    private String contId;
    private String ranks = Constants.POOL_DEFAULT_RANKS;
    private String serverGroup = Constants.POOL_DEFAULT_SERVER_GROUP;
    private int containerFlags = Constants.ACCESS_FLAG_CONTAINER_READWRITE;
    private int poolFlags = Constants.ACCESS_FLAG_POOL_READWRITE;
    private int poolMode = Constants.MODE_POOL_GROUP_READWRITE | Constants.MODE_POOL_OTHER_READWRITE |
      Constants.MODE_POOL_USER_READWRITE;

    public DaosClientBuilder poolId(String poolId) {
      this.poolId = poolId;
      return this;
    }

    public DaosClientBuilder containerId(String contId) {
      this.contId = contId;
      return this;
    }

    /**
     * one or more ranks separated by ":".
     *
     * @param ranks
     * default is "0"
     * @return DaosFsClientBuilder
     */
    public DaosClientBuilder ranks(String ranks) {
      this.ranks = ranks;
      return this;
    }

    /**
     * set group name of server.
     *
     * @param serverGroup
     * default is 'daos_server'
     * @return DaosFsClientBuilder
     */
    public DaosClientBuilder serverGroup(String serverGroup) {
      this.serverGroup = serverGroup;
      return this;
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
    public DaosClientBuilder containerFlags(int containerFlags) {
      this.containerFlags = containerFlags;
      return this;
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
    public DaosClientBuilder poolMode(int poolMode) {
      this.poolMode = poolMode;
      return this;
    }

    /**
     * set pool flags for opening pool.
     *
     * @param poolFlags should be one of
     *                  {@link Constants#ACCESS_FLAG_POOL_READONLY}
     *                  {@link Constants#ACCESS_FLAG_POOL_READWRITE}
     *                  {@link Constants#ACCESS_FLAG_POOL_EXECUTE}
     *
     * <p>
     *                  Default is {@link Constants#ACCESS_FLAG_POOL_READWRITE}
     * @return DaosFsClientBuilder
     */
    public DaosClientBuilder poolFlags(int poolFlags) {
      this.poolFlags = poolFlags;
      return this;
    }
  }
}
