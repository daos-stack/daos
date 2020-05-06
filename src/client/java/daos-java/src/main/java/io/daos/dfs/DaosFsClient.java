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

package io.daos.dfs;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.CopyOption;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import org.apache.commons.lang.ObjectUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Java DAOS FS Client for all local/remote DAOS operations which is indirectly invoked via JNI.
 *
 * <p>
 * Before any operation, there is some preparation work to be done in this class.
 * <li>load dynamic library, libdaos-jni.so</li>
 * <li>load error code</li>
 * <li>start cleaner thread</li>
 * <li>register shutdown hook for DAOS finalization</li>
 * <li>create/open pool and container</li>
 * <li>mount DAOS FS on container</li>
 *
 * <p>
 * If you have <code>poolId</code> specified, but no <code>containerId</code>, DAOS FS will be mounted on
 * non-readonly root container with UUID {@link #ROOT_CONT_UUID}. Thus, you need to make sure the pool
 * doesn't have root container yet.
 *
 * <p>
 * User cannot instantiate this class directly since we need to make sure single instance of
 * {@linkplain DaosFsClient client}  per pool and container. User should get this object from
 * {@link DaosFsClientBuilder}.
 *
 * <p>
 * After getting {@linkplain DaosFsClient client}, user usually get {@link DaosFile} object via {@linkplain #getFile}
 * methods.
 *
 * <p>
 * For example, user can simply create {@linkplain DaosFsClient client} and {@link DaosFile} with all default values
 * by following code.
 * <code>
 * DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
 * builder.poolId(poolId).containerId(contId);
 * DaosFsClient client = builder.build();
 *
 * DaosFile daosFile = client.getFile("/path");
 * if (!daosFile.exists()){
 * daosFile.mkdir();
 * }
 * </code>
 *
 * <p>
 * User can also call some utility methods directly in this object for convenience without creating new
 * {@link DaosFile}. User should prefer these utility methods to {@link DaosFile} if it's only one-shot DAOS file
 * operation. Whilst {@link DaosFile} should be created for multiple DAOS file operations because DAOS FS object is
 * cached inside. We don't need to query/open DAOS FS object for each DAOS operation.
 *
 * @see DaosFsClientBuilder
 * @see DaosFile
 * @see Cleaner
 */
public final class DaosFsClient {

  private String poolId;

  private String contId;

  private long poolPtr;

  private long contPtr;

  private long dfsPtr;

  private DaosFsClientBuilder builder;

  private volatile boolean inited;

  private int refCnt;

  public static final String LIB_NAME = "daos-jni";

  public static final String ROOT_CONT_UUID = "ffffffff-ffff-ffff-ffff-ffffffffffff";

  public static final Runnable FINALIZER;

  private static final Logger log = LoggerFactory.getLogger(DaosFsClient.class);

  private final ExecutorService cleanerExe = Executors.newSingleThreadExecutor((r) -> {
    Thread thread = new Thread(r, "DAOS file object cleaner thread");
    thread.setDaemon(true);
    return thread;
  });

  // keyed by poolId+contId
  private static final Map<String, DaosFsClient> pcFsMap = new ConcurrentHashMap<>();

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

  private static void loadByPath(String path, File tempDir) throws IOException {
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

  private DaosFsClient(String poolId, String contId, DaosFsClientBuilder builder) {
    this.poolId = poolId;
    this.contId = contId;
    this.builder = builder;
  }

  private DaosFsClient(String poolId, DaosFsClientBuilder builder) {
    this(poolId, null, builder);
  }

  private DaosFsClient(DaosFsClientBuilder builder) {
    this(null, null, builder);
  }

  private synchronized void init() throws IOException {
    if (inited) {
      return;
    }

    poolPtr = daosOpenPool(poolId, builder.serverGroup,
            builder.ranks,
            builder.poolFlags);
    if (log.isDebugEnabled()) {
      log.debug("opened pool {}", poolPtr);
    }

    if (contId != null && !ROOT_CONT_UUID.equals(contId)) {
      contPtr = daosOpenCont(poolPtr, contId, builder.containerFlags);
      if (log.isDebugEnabled()) {
        log.debug("opened container {}", contPtr);
      }
      dfsPtr = mountFileSystem(poolPtr, contPtr, builder.readOnlyFs);
      if (log.isDebugEnabled()) {
        log.debug("mounted FS {}", dfsPtr);
      }
    } else {
      contId = ROOT_CONT_UUID;
      contPtr = -1;
      dfsPtr = mountFileSystem(poolPtr, -1, builder.readOnlyFs);
      if (log.isDebugEnabled()) {
        log.debug("mounted FS {} on root container", dfsPtr);
      }
    }

    cleanerExe.execute(new Cleaner.CleanerTask());
    if (log.isDebugEnabled()) {
      log.debug("cleaner task running");
    }

    inited = true;
    log.info("DaosFsClient for {}, {} initialized", poolId, contId);
  }

  public long getDfsPtr() {
    return dfsPtr;
  }

  public String getPoolId() {
    return poolId;
  }

  public String getContId() {
    return contId;
  }

  /**
   * mount DAOS FS on specific pool and container.
   *
   * @param poolPtr
   * pointer to pool object
   * @param contPtr
   * pointer to container object
   * @param readOnly
   * is read only file system
   * @return pointer of native mounted DAOS FS object.
   * @throws IOException
   * {@link DaosIOException}
   */
  public static synchronized long mountFileSystem(long poolPtr, long contPtr, boolean readOnly) throws IOException {
    if (contPtr != -1) {
      return dfsMountFs(poolPtr, contPtr, readOnly);
    }
    return dfsMountFsOnRoot(poolPtr);
  }

  /**
   * Decrement reference first. If there is no more reference, then disconnect this client from DAOS server.
   * After actual disconnect,
   * - DAOS FS is unmounted
   * - container is closed
   * - pool is closed
   * - cleaner task is stopped
   * - this instance is removed from cache
   *
   * <p>
   * User should call this method before application exiting to release resources. It's <B>synchronized</B> method
   * so that the two <B>synchronized</B> methods, {@link #incrementRef()} and {@link #decrementRef()}, for maintaining
   * references can be safely called.
   *
   * <p>
   * If there are still references to this object, no actual disconnect will be called. User can call
   * {@link #getRefCnt()} method to double-check the reference count of this object.
   *
   * @throws IOException
   * {@link DaosIOException}
   */
  public synchronized void disconnect() throws IOException {
    disconnect(false);
  }

  private synchronized void disconnect(boolean force) throws IOException {
    decrementRef();
    if (force || refCnt <= 0) {
      if (inited && dfsPtr != 0) {
        cleanerExe.shutdownNow();
        if (log.isDebugEnabled()) {
          log.debug("cleaner stopped");
        }
        if (contPtr == -1) {
          dfsUnmountFsOnRoot(dfsPtr);
          if (log.isDebugEnabled()) {
            log.debug("FS unmounted {} from root container", dfsPtr);
          }
        } else {
          dfsUnmountFs(dfsPtr);
          if (log.isDebugEnabled()) {
            log.debug("FS unmounted {}", dfsPtr);
          }
          daosCloseContainer(contPtr);
          if (log.isDebugEnabled()) {
            log.debug("closed container {}", contPtr);
          }
        }
        daosClosePool(poolPtr);
        log.info("DaosFsClient for {}, {} disconnected", poolId, contId);
      }
      inited = false;
      pcFsMap.remove(poolId + contId);
    }
  }

  /**
   * Get {@link DaosFile} denoted by <code>path</code>.
   *
   * @param path
   * path of file
   * @return DaosFile
   */
  public DaosFile getFile(String path) {
    return getFile(path, builder.defaultFileAccessFlags);
  }

  /**
   * Get {@link DaosFile} denoted by <code>path</code> with giving <code>accessFlags</code>.
   *
   * @param path
   * path of file
   * @param accessFlags
   * file access flags, see {@link DaosFsClientBuilder#defaultFileAccessFlags(int)} for its possible values
   * @return DaosFile
   */
  public DaosFile getFile(String path, int accessFlags) {
    return new DaosFile(path, accessFlags, this);
  }

  /**
   * Get {@link DaosFile} denoted by <code>parent</code> and <code>path</code>.
   *
   * @param parent
   * parent path of file
   * @param path
   * path of file
   * @return DaosFile
   */
  public DaosFile getFile(String parent, String path) {
    return getFile(parent, path, builder.defaultFileAccessFlags);
  }

  /**
   * Get {@link DaosFile} denoted by <code>parent</code> and <code>path</code> with giving <code>accessFlags</code>.
   *
   * @param parent
   * parent path of file
   * @param path
   * path of file
   * @param accessFlags
   * file access flags, see {@link DaosFsClientBuilder#defaultFileAccessFlags(int)} for its possible values
   * @return DaosFile
   */
  public DaosFile getFile(String parent, String path, int accessFlags) {
    return new DaosFile(parent, path, accessFlags, this);
  }

  /**
   * Get {@link DaosFile} denoted by <code>parent</code> object and <code>path</code>.
   *
   * @param parent
   * parent file object
   * @param path
   * path of file
   * @return DaosFile
   */
  public DaosFile getFile(DaosFile parent, String path) {
    return getFile(parent, path, builder.defaultFileAccessFlags);
  }

  /**
   * Get {@link DaosFile} denoted by <code>parent</code> object and <code>path</code> with giving
   * <code>accessFlags</code>.
   *
   * @param parent
   * parent file object
   * @param path
   * path of file
   * @param accessFlags
   * file access flags, see {@link DaosFsClientBuilder#defaultFileAccessFlags(int)} for its possible values
   * @return DaosFile
   */
  public DaosFile getFile(DaosFile parent, String path, int accessFlags) {
    return new DaosFile(parent, path, accessFlags, this);
  }

  /**
   * move file from <code>srcPath</code> to <code>destPath</code>.
   *
   * @param srcPath
   * source path
   * @param destPath
   * destination path
   * @throws IOException
   * {@link DaosIOException}
   */
  public void move(String srcPath, String destPath) throws IOException {
    move(dfsPtr, DaosUtils.normalize(srcPath), DaosUtils.normalize(destPath));
  }

  /**
   * move file from <code>srcName</code> under directory denoted by <code>srcParentObjId</code>
   * to <code>destName</code> under directory denoted by <code>destParentObjId</code>.
   * This method is more efficient than {@link #move(String, String)} since we don't need to open
   * both source directory and destination directory.
   *
   * @param srcParentObjId
   * object id of source directory
   * @param srcName
   * source file name without any path
   * @param destParentObjId
   * object id of destination directory
   * @param destName
   * destination file name without any path
   * @throws IOException
   * {@link DaosIOException}
   */
  public void move(long srcParentObjId, String srcName, long destParentObjId, String destName) throws IOException {
    srcName = DaosUtils.normalize(srcName);
    if (srcName.indexOf('/') >= 0) {
      throw new IllegalArgumentException("srcName should not contain any path");
    }
    if (destName.indexOf('/') >= 0) {
      throw new IllegalArgumentException("destName should not contain any path");
    }
    move(dfsPtr, srcParentObjId, srcName, destParentObjId, destName);
  }

  /**
   * delete file or directory denoted by <code>path</code>. Non-empty directory will be deleted
   * if <code>force</code> is true.
   *
   * @param path
   * path of file to be deleted
   * @param force
   * force delete if directory is not empty
   * @return true for deleted successfully, false for other cases, like not existed or failed to delete
   * @throws IOException
   * {@link DaosIOException}
   */
  public boolean delete(String path, boolean force) throws IOException {
    path = DaosUtils.normalize(path);
    String[] pc = DaosUtils.parsePath(path);
    return delete(dfsPtr, pc.length == 2 ? pc[0] : null, pc[1], force);
  }

  /**
   * delete file or directory denoted by <code>path</code> without force deletion.
   *
   * @param path
   * path of file to be deleted
   * @return true for deleted successfully, false for other cases, like not existed or failed to delete
   * @throws IOException
   * {@link DaosIOException}
   */
  public boolean delete(String path) throws IOException {
    return delete(DaosUtils.normalize(path), false);
  }

  /**
   * create directory denoted by <code>path</code>.
   * If <code>recursive</code> is true, ancestor path will be created if they don't exist.
   *
   * @param path
   * path of directory
   * @param recursive
   * create directories recursively
   * @throws IOException
   * {@link DaosIOException}
   */
  public void mkdir(String path, boolean recursive) throws IOException {
    mkdir(DaosUtils.normalize(path), builder.defaultFileMode, recursive);
  }

  /**
   * create directory denoted by <code>path</code> with giving <code>mode</code>.
   * If <code>recursive</code> is true, ancestor path will be created if they don't exist.
   *
   * @param path
   * path of directory
   * @param mode
   * file mode, see {@link DaosFsClientBuilder#defaultFileMode(int)} for its possible values
   * @param recursive
   * create directories recursively
   * @throws IOException
   * {@link DaosIOException}
   */
  public void mkdir(String path, int mode, boolean recursive) throws IOException {
    try {
      mkdir(dfsPtr, DaosUtils.normalize(path), mode, recursive);
    } catch (IOException e) {
      if (recursive && (e instanceof DaosIOException)) {
        if (((DaosIOException) e).getErrorCode() == Constants.ERROR_CODE_FILE_EXIST) {
          return;
        }
      }
      throw e;
    }
  }

  /**
   * check existence of file denoted by <code>path</code>.
   *
   * @param path
   * path of file
   * @return true if exists. false otherwise
   * @throws IOException
   * {@link DaosIOException}
   */
  public boolean exists(String path) throws IOException {
    long objId = 0;
    try {
      objId = dfsLookup(dfsPtr, DaosUtils.normalize(path), builder.defaultFileAccessFlags, -1);
      return true;
    } catch (Exception e) {
      if (!(e instanceof DaosIOException)) { //unexpected exception
        throw new DaosIOException(e);
      }
      //verify error code to determine existence, if it's other error code, throw it anyway.
      DaosIOException de = (DaosIOException) e;
      if (de.getErrorCode() != Constants.ERROR_CODE_NOT_EXIST) {
        throw de;
      }
      return false;
    } finally {
      if (objId > 0) {
        dfsRelease(objId);
      }
    }
  }

  static synchronized void closeAll() throws IOException {
    for (Map.Entry<String, DaosFsClient> entry : pcFsMap.entrySet()) {
      entry.getValue().disconnect(true);
    }
  }

  // ------------------native methods------------------

  /**
   * move file object denoted by <code>srcPath</code> to new path denoted by <code>destPath</code>.
   *
   * @param dfsPtr
   * pointer of dfs object
   * @param srcPath
   * full path of source
   * @param destPath
   * full path of destination
   * @throws IOException
   * {@link DaosIOException}
   */
  native void move(long dfsPtr, String srcPath, String destPath) throws IOException;

  /**
   * move file from <code>srcName</code> under directory denoted by <code>srcParentObjId</code>
   * to <code>destName</code> under directory denoted by <code>destParentObjId</code>.
   * This method is more efficient than {@link #move(String, String)} since we don't need to open
   * both source directory and destination directory.
   *
   * @param dfsPtr
   * pointer of dfs object
   * @param srcParentObjId
   * object id of source directory
   * @param srcName
   * source name
   * @param destParentObjId
   * object id of destination directory
   * @param destName
   * destination name
   * @throws IOException
   * {@link DaosIOException}
   */
  native void move(long dfsPtr, long srcParentObjId, String srcName, long destParentObjId, String destName)
          throws IOException;

  /**
   * make directory denoted by <code>path</code>.
   *
   * @param dfsPtr
   * pointer of dfs object
   * @param path
   * full path
   * @param mode
   * file mode, see {@link DaosFsClientBuilder#defaultFileMode(int)} for possible values
   * @param recursive
   * true to create all ancestors, false to create just itself
   * @throws IOException
   * {@link DaosIOException}
   */
  native void mkdir(long dfsPtr, String path, int mode, boolean recursive) throws IOException;

  /**
   * create new file.
   *
   * @param dfsPtr
   * pointer of dfs object
   * @param parentPath
   * null for root
   * @param name
   * file name
   * @param mode
   * file mode, see {@link DaosFsClientBuilder#defaultFileMode(int)} for possible values
   * @param accessFlags
   * file access flags, see {@link DaosFsClientBuilder#defaultFileAccessFlags(int)} for its possible values
   * @param objType
   * object type in string, see {@link DaosFsClientBuilder#defaultFileObjType} for its possible values
   * @param chunkSize
   * file chunk size
   * @param createParent
   * if create directory if parent doesn't exist
   * @return DAOS FS object id
   * @throws IOException
   * {@link DaosIOException}
   */
  native long createNewFile(long dfsPtr, String parentPath, String name, int mode, int accessFlags,
                            String objType, int chunkSize, boolean createParent) throws IOException;

  /**
   * delete file with <code>name</code> from <code>parentPath</code>.
   *
   * @param dfsPtr
   * pointer of dfs object
   * @param parentPath
   * null for root
   * @param name
   * file name
   * @param force
   * true to delete directory if it's not empty
   * @return true for deleted successfully, false for other cases, like not existed or failed to delete
   * @throws IOException
   * {@link DaosIOException}
   */
  native boolean delete(long dfsPtr, String parentPath, String name, boolean force) throws IOException;

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
   * see {@link DaosFsClientBuilder#poolFlags(int)}
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
   * see {@link DaosFsClientBuilder#containerFlags(int)}
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


  // DAOS FS corresponding methods

  /**
   * set prefix.
   *
   * @param dfsPtr
   * pointer to dfs object
   * @param prefix
   * path prefix
   * @return 0 for success, others for failure
   * @throws IOException
   * {@link DaosIOException}
   */
  native int dfsSetPrefix(long dfsPtr, String prefix) throws IOException;

  /**
   * open a file with opened parent specified by <code>parentObjId</code>.
   *
 * <p>
   * TODO: make sure buffer is set in the same order as StatAttributes instantiation
   *
   * @param dfsPtr
   * pointer of dfs object
   * @param parentObjId
   * parent object id
   * @param name
   * file name
   * @param flags
   * file flags
   * @param bufferAddress address of direct {@link java.nio.ByteBuffer} for holding all information of
   *                      {@link StatAttributes}, -1 if you don't want to get {@link StatAttributes}
   * @return DAOS FS object id
   * @throws IOException
   * {@link DaosIOException}
   */
  native long dfsLookup(long dfsPtr, long parentObjId, String name, int flags, long bufferAddress) throws IOException;

  /**
   * Same as {@link #dfsLookup(long, long, String, int, long)} except parent file is not opened.
   *
   * @param dfsPtr
   * pointer of dfs object
   * @param path
   * file path
   * @param flags
   * file flags
   * @param bufferAddress address of direct {@link java.nio.ByteBuffer} for holding all information of
   *                      {@link StatAttributes}
   * @return DAOS FS object id
   * @throws IOException
   * {@link DaosIOException}
   */
  native long dfsLookup(long dfsPtr, String path, int flags, long bufferAddress) throws IOException;

  /**
   * get file length for opened FS object.
   *
   * @param dfsPtr
   * pointer of dfs object
   * @param objId
   * object id
   * @return file length
   * @throws IOException
   * {@link DaosIOException}
   */
  native long dfsGetSize(long dfsPtr, long objId) throws IOException;

  /**
   * duplicate opened FS object.
   *
   * @param dfsPtr
   * pointer of dfs object
   * @param objId
   * object id
   * @param flags
   * file flags
   * @return FS object id of duplication
   * @throws IOException
   * {@link DaosIOException}
   */
  native long dfsDup(long dfsPtr, long objId, int flags) throws IOException;

  /**
   * release opened FS object.
   *
   * @param objId
   * object id
   * @throws IOException
   * {@link DaosIOException}
   */
  native void dfsRelease(long objId) throws IOException;

  /**
   * read data from file to buffer.
   *
   * @param dfsPtr
   * pointer of dfs object
   * @param objId
   * object id
   * @param bufferAddress
   * address of direct buffer
   * @param offset
   * file offset
   * @param len
   * length in byte to read from file
   * @param eventNo
   * event no.
   * @return number of bytes actual read
   * @throws IOException
   * {@link DaosIOException}
   */
  native long dfsRead(long dfsPtr, long objId, long bufferAddress, long offset, long len, int eventNo)
          throws IOException;

  /**
   * write data from buffer to file.
   *
   * @param dfsPtr
   * pointer of dfs object
   * @param objId
   * object id
   * @param bufferAddress
   * address of direct buffer
   * @param offset
   * file offset
   * @param len
   * length in byte to write to file
   * @param eventNo
   * event no.
   * @return number of bytes actual written
   * @throws IOException
   * {@link DaosIOException}
   */
  native long dfsWrite(long dfsPtr, long objId, long bufferAddress, long offset, long len,
                       int eventNo) throws IOException;

  /**
   * read children.
   *
   * @param dfsPtr
   * pointer of dfs object
   * @param objId
   * object id
   * @param maxEntries
   * -1 for no limit
   * @return file names separated by ','
   */
  native String dfsReadDir(long dfsPtr, long objId, int maxEntries) throws IOException;

  /**
   * get FS object status attribute into direct {@link java.nio.ByteBuffer}.
   * Order of fields to be read.
   * objId = buffer.getLong();
   * mode = buffer.getInt();
   * uid = buffer.getInt();
   * gid = buffer.getInt();
   * blockCnt = buffer.getLong();
   * blockSize = buffer.getLong();
   * length = buffer.getLong();
   * accessTime = buffer.getLong() buffer.getLong();
   * modifyTime = buffer.getLong() buffer.getLong();
   * createTime = buffer.getLong() buffer.getLong();
   * file = buffer.get() > 0;
   *
   * @param dfsPtr
   * pointer of dfs object
   * @param objId
   * object id
   * @param bufferAddress memory address of direct buffer. -1 if you don't want to retrieve stat actually. It's usually
   *                      for checking file existence.
   * @throws IOException
   * {@link DaosIOException}
   */
  native void dfsOpenedObjStat(long dfsPtr, long objId, long bufferAddress) throws IOException;

  /**
   * set extended attribute.
   *
   * @param dfsPtr
   * pointer of dfs object
   * @param objId
   * object id
   * @param name
   * attribute name
   * @param value
   * attribute value
   * @param flags
   * attribute flags, possible values are,
   * {@link Constants#SET_XATTRIBUTE_REPLACE}
   * {@link Constants#SET_XATTRIBUTE_CREATE}
   * {@link Constants#SET_XATTRIBUTE_NO_CHECK}
   * @throws IOException
   * {@link DaosIOException}
   */
  native void dfsSetExtAttr(long dfsPtr, long objId, String name, String value, int flags) throws IOException;

  /**
   * get extended attribute.
   *
   * @param dfsPtr
   * pointer of dfs object
   * @param objId
   * object id
   * @param name
   * attribute name
   * @param expectedValueLen
   * expected value length
   * @return attribute value
   * @throws IOException
   * {@link DaosIOException}
   */
  native String dfsGetExtAttr(long dfsPtr, long objId, String name, int expectedValueLen) throws IOException;

  /**
   * remove extended attribute.
   *
   * @param dfsPtr
   * pointer of dfs object
   * @param objId
   * object id
   * @param name
   * attribute name
   * @throws IOException
   * {@link DaosIOException}
   */
  native void dfsRemoveExtAttr(long dfsPtr, long objId, String name) throws IOException;

  /**
   * get chunk size.
   *
   * @param objId
   * object id
   * @return chunk size
   * @throws IOException
   * {@link DaosIOException}
   */
  static native long dfsGetChunkSize(long objId) throws IOException;

  /**
   * get mode.
   *
   * @param objId
   * object id
   * @return file mode
   * @throws IOException
   * {@link DaosIOException}
   */
  static native int dfsGetMode(long objId) throws IOException;

  /**
   * check if it's directory by providing mode.
   *
   * @param mode
   * file mode
   * @return true if directory. false otherwise
   * @throws IOException
   * {@link DaosIOException}
   */
  static native boolean dfsIsDirectory(int mode) throws IOException;

  /**
   * mount FS on container.
   *
   * @param poolPtr
   * pointer to pool
   * @param contPtr
   * pointer to container
   * @param readOnly
   * read only filesystem
   * @return FS client pointer
   */
  static native long dfsMountFs(long poolPtr, long contPtr, boolean readOnly) throws IOException;

  /**
   * mount FS on non-readonly root container.
   *
   * @param poolPtr
   * pointer to pool
   * @return pointer to dfs object
   * @throws IOException
   * {@link DaosIOException}
   */
  static native long dfsMountFsOnRoot(long poolPtr) throws IOException;

  /**
   * unmount FS from root container.
   *
   * @param poolPtr
   * pointer to pool
   * @throws IOException
   * {@link DaosIOException}
   */
  static native void dfsUnmountFsOnRoot(long poolPtr) throws IOException;

  /**
   * unmount FS.
   *
   * @param dfsPtr
   * pointer to dfs object
   * @throws IOException
   * {@link DaosIOException}
   */
  static native void dfsUnmountFs(long dfsPtr) throws IOException;

  /**
   * finalize DAOS client.
   *
   * @throws IOException
   * {@link DaosIOException}
   */
  static synchronized native void daosFinalize() throws IOException;

  //------------------native methods end------------------


  int getDefaultFileAccessFlags() {
    return builder.defaultFileAccessFlags;
  }

  int getDefaultFileMode() {
    return builder.defaultFileMode;
  }

  DaosObjectType getDefaultFileObjType() {
    return builder.defaultFileObjType;
  }

  int getDefaultFileChunkSize() {
    return builder.defaultFileChunkSize;
  }

  /**
   * increase reference count by one.
   *
   * @throws IllegalStateException if this client is disconnected.
   */
  public synchronized void incrementRef() {
    if (!inited) {
      throw new IllegalStateException("DaosFsClient is not initialized or disconnected.");
    }
    refCnt++;
  }

  /**
   * decrease reference count by one.
   */
  public synchronized void decrementRef() {
    refCnt--;
  }

  /**
   * get reference count.
   *
   * @return reference count
   */
  public synchronized int getRefCnt() {
    return refCnt;
  }

  /**
   * A builder for constructing Java DAOS FS Client. All parameters should be specified here. This builder
   * makes sure single instance of {@link DaosFsClient} per pool and container.
   *
   * <p>
   * Please note that new pool and new container will be created if their ids (poolId and containerId) are {@code null}.
   */
  public static class DaosFsClientBuilder implements Clonable {
    private String poolId;
    private String contId;
    private String ranks = Constants.POOL_DEFAULT_RANKS;
    private String serverGroup = Constants.POOL_DEFAULT_SERVER_GROUP;
    private int containerFlags = Constants.ACCESS_FLAG_CONTAINER_READWRITE;
    private int poolFlags = Constants.ACCESS_FLAG_POOL_READWRITE;
    private int poolMode = Constants.MODE_POOL_GROUP_READWRITE | Constants.MODE_POOL_OTHER_READWRITE |
            Constants.MODE_POOL_USER_READWRITE;
    private int defaultFileChunkSize = Constants.FILE_DEFAULT_CHUNK_SIZE;
    private int defaultFileAccessFlags = Constants.ACCESS_FLAG_FILE_READWRITE;
    private int defaultFileMode = Constants.FILE_DEFAULT_FILE_MODE;
    private DaosObjectType defaultFileObjType = DaosObjectType.OC_SX;
    private boolean readOnlyFs = false;
    private boolean shareFsClient = true;

    public DaosFsClientBuilder poolId(String poolId) {
      this.poolId = poolId;
      return this;
    }

    public DaosFsClientBuilder containerId(String contId) {
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
    public DaosFsClientBuilder ranks(String ranks) {
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
    public DaosFsClientBuilder serverGroup(String serverGroup) {
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
    public DaosFsClientBuilder containerFlags(int containerFlags) {
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
    public DaosFsClientBuilder poolMode(int poolMode) {
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
    public DaosFsClientBuilder poolFlags(int poolFlags) {
      this.poolFlags = poolFlags;
      return this;
    }

    /**
     * set default file access flag.
     *
     * @param defaultFileAccessFlags should be one of
     *                               {@link Constants#ACCESS_FLAG_FILE_CREATE}
     *                               {@link Constants#ACCESS_FLAG_FILE_READONLY}
     *                               {@link Constants#ACCESS_FLAG_FILE_READWRITE}
     *                               {@link Constants#ACCESS_FLAG_FILE_EXCL}
     *
     * <p>
     * default is {@link Constants#ACCESS_FLAG_FILE_READWRITE}
     * @return DaosFsClientBuilder
     */
    public DaosFsClientBuilder defaultFileAccessFlags(int defaultFileAccessFlags) {
      this.defaultFileAccessFlags = defaultFileAccessFlags;
      return this;
    }

    /**
     * set default file mode. You can override this value when create new file by
     * calling {@link DaosFile#createNewFile(int, DaosObjectType, int, boolean)}
     *
     * @param defaultFileMode
     * should be octal value. Default is 0755
     * @return DaosFsClientBuilder
     */
    public DaosFsClientBuilder defaultFileMode(int defaultFileMode) {
      this.defaultFileMode = defaultFileMode;
      return this;
    }

    /**
     * set default file type. You can override this value when create new file by
     * calling {@link DaosFile#createNewFile(int, DaosObjectType, int, boolean)}
     *
     * @param defaultFileObjType
     * default is {@link DaosObjectType#OC_SX}
     * @return DaosFsClientBuilder
     */
    public DaosFsClientBuilder defaultFileType(DaosObjectType defaultFileObjType) {
      this.defaultFileObjType = defaultFileObjType;
      return this;
    }

    /**
     * set default file chunk size. You can override this value when create new file by
     * calling {@link DaosFile#createNewFile(int, DaosObjectType, int, boolean)}
     *
     * @param defaultFileChunkSize
     * default is 0. DAOS will decide what default is. 1MB for now.
     * @return DaosFsClientBuilder
     */
    public DaosFsClientBuilder defaultFileChunkSize(int defaultFileChunkSize) {
      this.defaultFileChunkSize = defaultFileChunkSize;
      return this;
    }

    /**
     * set FS readonly.
     *
     * @param readOnlyFs
     * default is false
     * @return DaosFsClientBuilder
     */
    public DaosFsClientBuilder readOnlyFs(boolean readOnlyFs) {
      this.readOnlyFs = readOnlyFs;
      return this;
    }

    /**
     * share {@link DaosFsClient} instance or not.
     *
     * @param shareFsClient
     * default is true
     * @return DaosFsClientBuilder
     */
    public DaosFsClientBuilder shareFsClient(boolean shareFsClient) {
      this.shareFsClient = shareFsClient;
      return this;
    }

    @Override
    public DaosFsClientBuilder clone() throws CloneNotSupportedException {
      return (DaosFsClientBuilder) super.clone();
    }

    /**
     * Either return existing {@link DaosFsClient} instance or create new instance. Reference count of returned client
     * is increased by one.
     *
     * @return DaosFsClient
     * @throws IOException
     * {@link DaosIOException}
     */
    public DaosFsClient build() throws IOException {
      DaosFsClientBuilder copied = (DaosFsClientBuilder) ObjectUtils.clone(this);
      DaosFsClient client;
      if (poolId != null) {
        client = getClientForCont(copied);
      } else {
        throw new IllegalArgumentException("need pool UUID.");
      }
      client.init();
      client.incrementRef();
      return client;
    }

    private DaosFsClient getClientForCont(DaosFsClientBuilder builder) {
      DaosFsClient client;
      if (!builder.shareFsClient) {
        return new DaosFsClient(poolId, contId, builder);
      }
      //check existing client
      if (contId == null) {
        contId = ROOT_CONT_UUID;
      }
      String key = poolId + contId;
      client = pcFsMap.get(key);
      if (client == null) {
        client = new DaosFsClient(poolId, contId, builder);
        pcFsMap.putIfAbsent(key, client);
      }
      return pcFsMap.get(key);
    }
  }
}
