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

package io.daos.dfs;

import java.io.IOException;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import io.daos.*;
import org.apache.commons.lang.ObjectUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * A shareable Java DAOS DFS client to wrap all DFS related operations, including
 * <li>mount/unmount DAOS FS on container</li>
 * <li>call DFS methods, including release DFS files in the cleaner executor</li>
 * <li>construct {@link DaosFile} instance</li>
 * It registers itself to shutdown manager in {@link DaosClient} to release resources in case of abnormal shutdown.
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
public final class DaosFsClient extends ShareableClient implements ForceCloseable {

  private long dfsPtr;

  private long contPtr;

  public static final String ROOT_CONT_UUID = "ffffffff-ffff-ffff-ffff-ffffffffffff";

  private static final Logger log = LoggerFactory.getLogger(DaosFsClient.class);

  static {
    DaosClient.initClient();
  }

  private final ExecutorService cleanerExe = Executors.newSingleThreadExecutor((r) -> {
    Thread thread = new Thread(r, "DAOS file object cleaner thread");
    thread.setDaemon(true);
    return thread;
  });

  // keyed by poolId+contId
  private static final Map<String, DaosFsClient> pcFsMap = new ConcurrentHashMap<>();

  private DaosFsClient(String poolId, String contId, DaosFsClientBuilder builder) {
    super(poolId, contId, builder);
  }

  private void init() throws IOException {
    if (isInited()) {
      return;
    }
    DaosFsClientBuilder builder = getBuilder();
    setClient(builder.buildDaosClient());
    DaosClient client = getClient();
    if (builder.getContId() != null && !ROOT_CONT_UUID.equals(builder.getContId())) {
      contPtr = client.getContPtr();
      dfsPtr = mountFileSystem(client.getPoolPtr(), contPtr, builder.readOnlyFs);
      if (log.isDebugEnabled()) {
        log.debug("mounted FS {}", dfsPtr);
      }
    } else {
      setContId(ROOT_CONT_UUID);
      contPtr = -1;
      dfsPtr = mountFileSystem(client.getPoolPtr(), -1, builder.readOnlyFs);
      if (log.isDebugEnabled()) {
        log.debug("mounted FS {} on root container", dfsPtr);
      }
    }

    cleanerExe.execute(new Cleaner.CleanerTask());
    if (log.isDebugEnabled()) {
      log.debug("cleaner task running");
    }
    client.registerForShutdown(this);
    setInited(true);
    log.info("DaosFsClient for {}, {} initialized", builder.getPoolId(), builder.getContId());
  }

  public long getDfsPtr() {
    return dfsPtr;
  }

  @Override
  protected DaosFsClientBuilder getBuilder() {
    return (DaosFsClientBuilder)super.getBuilder();
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
   * @param force
   * close client forcibly?
   * @throws IOException
   * {@link DaosIOException}
   */
  @Override
  protected synchronized void disconnect(boolean force) throws IOException {
    decrementRef();
    DaosFsClientBuilder builder = getBuilder();
    if (force || getRefCnt() <= 0) {
      if (isInited() && dfsPtr != 0) {
        log.debug("dfsptr: " + dfsPtr);
        cleanerExe.shutdownNow();
        if (log.isDebugEnabled()) {
          log.debug("cleaner stopped");
        }
        log.debug("dfsptr: " + dfsPtr);
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
        }
        if (force) {
          getClient().forceClose();
        } else {
          getClient().close();
        }
        log.info("DaosFsClient for {}, {} disconnected", builder.getPoolId(), builder.getContId());
      }
      setInited(false);
      pcFsMap.remove(builder.getPoolId() + builder.getContId());
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
    return getFile(path, getBuilder().defaultFileAccessFlags);
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
    return getFile(parent, path, getBuilder().defaultFileAccessFlags);
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
    return getFile(parent, path, getBuilder().defaultFileAccessFlags);
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
    mkdir(DaosUtils.normalize(path), getBuilder().defaultFileMode, recursive);
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
      objId = dfsLookup(dfsPtr, DaosUtils.normalize(path), getBuilder().defaultFileAccessFlags, -1);
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

  // DAOS FS corresponding methods

  /**
   * set prefix.
   *
   * @param dfsPtr
   * pointer to dfs object
   * @param prefix
   * path prefix
   * @throws IOException
   * {@link DaosIOException}
   */
  native void dfsSetPrefix(long dfsPtr, String prefix) throws IOException;

  /**
   * open a file with opened parent specified by <code>parentObjId</code>.
   *
   * <p>
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
   * create UNS path with given data in <code>bufferAddress</code> in pool <code>poolHandle</code>.
   * A new container will be created with some properties from <code>attribute</code>.
   * Object type, pool UUID and container UUID are set to extended attribute of <code>path</code>.
   *
   * @param poolHandle
   * handle of pool
   * @param path
   * OS file path to set duns attributes. make sure file not existing
   * @param bufferAddress
   * buffer memory address of direct buffer which holds <code>DunsAttribute</code> data serialized by
   *     protocol buffer
   * @param buffLen
   * length of buffer
   * @return UUID of container
   * @throws IOException
   * {@link DaosIOException}
   */
  static native String dunsCreatePath(long poolHandle, String path, long bufferAddress, int buffLen) throws IOException;

  /**
   * extract and parse extended attributes from given <code>path</code>.
   *
   * @param path
   * OS file path
   * @return UNS attribute info in binary get from path, including object type, pool UUID and container UUID.
   *     user should deserialize the data by {@link io.daos.dfs.uns.DunsAttribute}
   * @throws IOException
   * {@link DaosIOException}
   */
  static native byte[] dunsResolvePath(String path) throws IOException;

  /**
   * get application info stored in <code>attrName</code> from <code>path</code>.
   *
   * @param path
   * OS file path
   * @param attrName
   * app-specific attribute name
   * @param maxLen
   * maximum length of attribute value
   * @return application info in string, key1=value1;key2=value2...
   * @throws IOException
   * {@link DaosIOException}
   */
  static native String dunsGetAppInfo(String path, String attrName, int maxLen) throws IOException;

  /**
   * set application info to <code>attrName</code> on <code>path</code>.
   *
   * @param path
   * OS file path
   * @param attrName
   * app-specific attribute name
   * @param value
   * application info in string, key1=value1;key2=value2...
   * @throws IOException
   * {@link DaosIOException}
   */
  static native void dunsSetAppInfo(String path, String attrName, String value) throws IOException;

  /**
   * Destroy a container and remove the path associated with it in the UNS.
   *
   * @param poolHandle
   * pool handle
   * @param path
   * OS file path
   * @throws IOException
   * {@link DaosIOException}
   */
  static native void dunsDestroyPath(long poolHandle, String path) throws IOException;

  /**
   * parse input string to UNS attribute.
   *
   * @param input
   * attribute string
   * @return UNS attribute info in binary.
   *     user should deserialize the data by {@link io.daos.dfs.uns.DunsAttribute}
   * @throws IOException
   * {@link DaosIOException}
   */
  static native byte[] dunsParseAttribute(String input) throws IOException;

  /**
   * finalize DAOS client.
   *
   * @throws IOException
   * {@link DaosIOException}
   */
  static synchronized native void daosFinalize() throws IOException;

  //------------------native methods end------------------

  int getDefaultFileAccessFlags() {
    return getBuilder().defaultFileAccessFlags;
  }

  int getDefaultFileMode() {
    return getBuilder().defaultFileMode;
  }

  DaosObjectType getDefaultFileObjType() {
    return getBuilder().defaultFileObjType;
  }

  int getDefaultFileChunkSize() {
    return getBuilder().defaultFileChunkSize;
  }

  /**
   * A builder for constructing Java DAOS FS Client. All parameters should be specified here. This builder
   * makes sure single instance of {@link DaosFsClient} per pool and container.
   *
   * <p>
   * poolId should be set at least. ROOT container will be used if containerId is not set.
   */
  public static class DaosFsClientBuilder extends DaosClient.DaosClientBuilder<DaosFsClientBuilder> {
    private int defaultFileChunkSize = Constants.FILE_DEFAULT_CHUNK_SIZE;
    private int defaultFileAccessFlags = Constants.ACCESS_FLAG_FILE_READWRITE;
    private int defaultFileMode = Constants.FILE_DEFAULT_FILE_MODE;
    private DaosObjectType defaultFileObjType = DaosObjectType.OC_SX;
    private boolean readOnlyFs = false;
    private boolean shareFsClient = true;

    /**
     * set default file access flag.
     *
     * @param defaultFileAccessFlags should be one of
     *                               {@link Constants#ACCESS_FLAG_FILE_CREATE}
     *                               {@link Constants#ACCESS_FLAG_FILE_READONLY}
     *                               {@link Constants#ACCESS_FLAG_FILE_READWRITE}
     *                               {@link Constants#ACCESS_FLAG_FILE_EXCL}
     *     default is {@link Constants#ACCESS_FLAG_FILE_READWRITE}
     * @return DaosFsClientBuilder
     */
    public DaosFsClientBuilder defaultFileAccessFlags(int defaultFileAccessFlags) {
      this.defaultFileAccessFlags = defaultFileAccessFlags;
      return this;
    }

    /**
     * set default file mode. You can override this value when create new file by
     * Scalling {@link DaosFile#createNewFile(int, DaosObjectType, int, boolean)}.
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
     * calling {@link DaosFile#createNewFile(int, DaosObjectType, int, boolean)}.
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
     * calling {@link DaosFile#createNewFile(int, DaosObjectType, int, boolean)}.
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
    @Override
    public DaosFsClient build() throws IOException {
      String poolId = getPoolId();
      String contId = getContId();
      DaosFsClientBuilder builder = (DaosFsClientBuilder) ObjectUtils.clone(this);
      DaosFsClient fsClient;
      if (!builder.shareFsClient) {
        fsClient = new DaosFsClient(poolId, contId, builder);
      } else {
        //check existing client
        if (poolId == null) {
          throw new IllegalArgumentException("need pool UUID.");
        }
        if (contId == null) {
          contId = ROOT_CONT_UUID;
        }
        String key = poolId + contId;
        fsClient = pcFsMap.get(key);
        if (fsClient == null) {
          fsClient = new DaosFsClient(poolId, contId, builder);
          pcFsMap.putIfAbsent(key, fsClient);
        }
        fsClient = pcFsMap.get(key);
      }
      synchronized (fsClient) {
        fsClient.init();
        fsClient.incrementRef();
      }
      return fsClient;
    }

    protected DaosClient buildDaosClient() throws IOException {
      return (DaosClient) super.build();
    }
  }
}
