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

package com.intel.daos.client;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import sun.nio.ch.DirectBuffer;

import javax.annotation.concurrent.NotThreadSafe;
import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * This class mimics {@link java.io.File} class to represent DAOS FS object and provide similar methods, like
 * {@link #delete()}, {@link #createNewFile()}, {@link #exists()}, {@link #length()}. The creation of this object
 * doesn't involve any remote operation which is delayed and invoked on-demand.
 * <p>
 * For each instance of {@link DaosFile}, its parent path, file name and path (parent path + / + file name) are
 * generated for later DAOS access convenience.
 *
 * @see DaosFsClient
 * @see Cleaner
 */
@NotThreadSafe
public class DaosFile {

  private final String path;

  private final String name;

  private final DaosFsClient client;

  private final long dfsPtr;

  private final String parentPath;

  private int accessFlags;

  private int mode;

  private DaosObjectType objectType;

  private int chunkSize;

  private DaosFile parent;

  private Cleaner cleaner;

  private long objId;

  private volatile boolean cleaned;

  private static final Logger log = LoggerFactory.getLogger(DaosFile.class);

  protected DaosFile(String parentPath, String path, DaosFsClient daosFsClient) {
    String pnor = DaosUtils.normalize(parentPath);
    String nor = DaosUtils.normalize(pnor.length() == 0 ? path : pnor + "/" + path);
    if (nor == null || nor.length() == 0) {
      throw new IllegalArgumentException("invalid path after normalizing " + nor);
    }
    this.path = nor;
    int slash = nor.lastIndexOf('/');
    if (slash > 0) {
      this.parentPath = nor.substring(0, slash);
      this.name = nor.substring(slash + 1);
    } else if (slash < 0) {
      this.parentPath = "";
      this.name = nor;
    } else {
      if (nor.length() == 1) {
        this.parentPath = "";
        this.name = "/";
      } else {
        this.parentPath = "/";
        this.name = nor.substring(1);
      }
    }
    this.client = daosFsClient;
    if (this.client != null) {
      this.dfsPtr = daosFsClient.getDfsPtr();
    } else {
      this.dfsPtr = -1;
    }
  }

  protected DaosFile(DaosFile parent, String path, DaosFsClient daosFsClient) {
    this(parent.path, path, daosFsClient);
    this.parent = parent;
  }

  protected DaosFile(String path, DaosFsClient daosFsClient) {
    this((String) null, path, daosFsClient);
  }

  public void createNewFile() throws IOException {
    createNewFile(mode, objectType, chunkSize);
  }

  /**
   * create new file with mode, object type and chunk size
   *
   * @param mode       should be octal number, like 0775
   * @param objectType
   * @param chunkSize
   * @throws IOException
   */
  public void createNewFile(int mode, DaosObjectType objectType, int chunkSize) throws IOException {
    if (objId != 0) {
      throw new IOException("file existed already");
    }
    //parse path to get parent and name.
    //dfs lookup parent and then dfs open
    objId = client.createNewFile(dfsPtr, parentPath, name, mode, accessFlags, objectType.getValue(), chunkSize);
    createCleaner();
  }

  /**
   * open FS object if hasn't opened yet.
   * <p>
   * cleaner is created only open at the first time
   *
   * @param throwException throw exception if true, otherwise, keep exception and return immediately
   * @throws DaosIOException
   */
  private void open(boolean throwException) throws DaosIOException {
    if (objId != 0) {
      return;
    }
    try {
      if (parent != null && parent.isOpen()) {
        objId = client.dfsLookup(dfsPtr, parent.getObjId(), name, accessFlags, -1);
      } else {
        objId = client.dfsLookup(dfsPtr, path, accessFlags, -1);
      }
    } catch (Exception e) {
      if (!(e instanceof DaosIOException)) {//unexpected exception
        throw new DaosIOException(e);
      }
      if (throwException) {
        throw (DaosIOException) e;
      } else {//verify error code to determine existence, if it's other error code, throw it anyway.
        DaosIOException de = (DaosIOException) e;
        if (de.getErrorCode() != Constants.ERROR_CODE_NOT_EXIST){
          throw de;
        }
      }
    }
    if(isOpen()) {
      createCleaner();
    }
  }

  public boolean isOpen() {
    return objId != 0;
  }

  /**
   * create cleaner for each opened {@link DaosFile} object. Cleaner calls {@link DaosFsClient#dfsRelease(long)}
   * to release opened FS object.
   * <p>
   * If object is deleted in advance, no {@link DaosFsClient#dfsRelease(long)} will be called.
   */
  private void createCleaner() {
    if (cleaner != null) {
      throw new IllegalStateException("Cleaner created already");
    }
    cleaned = false;
    //clean object by invoking dfs release
    cleaner = Cleaner.create(this, () -> {
      if (!cleaned) {
        try {
          client.dfsRelease(objId);
          if (log.isDebugEnabled()) {
            log.debug("file {} released", path);
          }
        } catch (IOException e) {
          log.error("failed to release fs object with " + path, e);
        }
      }
    });
  }

  /**
   * delete FS object
   *
   * @throws IOException
   */
  public boolean delete() throws IOException {
    return delete(false);
  }

  /**
   * delete FS object. Non-empty directory will be deleted if
   * <code>force</code> set to true
   *
   * @param force
   * @return
   * @throws IOException
   */
  public boolean delete(boolean force) throws IOException {
    boolean deleted = client.delete(dfsPtr, parentPath, name, force);
    if (cleaner != null) {
      cleaned = true;
    }
    return deleted;
  }

  public long length() throws IOException {
    open(true);
    long size = client.dfsGetSize(dfsPtr, objId);
    return size;
  }

  public String[] listChildren() throws IOException {
    open(true);
    //no limit to max returned entries for now
    String children = client.dfsReadDir(dfsPtr, objId, -1);
    return children.split(",");
  }

  /**
   * set extended attribute
   *
   * @param name
   * @param value
   * @param flags should be one of below value
   *              {@link Constants#SET_XATTRIBUTE_NO_CHECK} : no attribute name check
   *              {@link Constants#SET_XATTRIBUTE_CREATE}   : create or fail if attribute exits
   *              {@link Constants#SET_XATTRIBUTE_REPLACE}  : replace or fail if attribute doesn't exist
   * @throws IOException
   */
  public void setExtAttribute(String name, String value, int flags) throws IOException {
    open(true);
    client.dfsSetExtAttr(dfsPtr, objId, name, value, flags);
  }

  public String getExtAttribute(String name, int expectedValueLen) throws IOException {
    open(true);
    return client.dfsGetExtAttr(dfsPtr, objId, name, expectedValueLen);
  }

  public void remoteExtAttribute(String name) throws IOException {
    open(true);
    client.dfsRemoveExtAttr(dfsPtr, objId, name);
  }

  public void getChunkSize() throws IOException {
    open(true);
    client.dfsGetChunkSize(objId);
  }

  public long read(ByteBuffer buffer, long bufferOffset, long fileOffset, long len) throws IOException {
    open(true);
    //no asynchronous for now
    if (len > buffer.capacity() - bufferOffset) {
      throw new IOException(String.format("buffer (%d) has no enough space start at %d for reading %d bytes from file",
              buffer.capacity(), bufferOffset, len));
    }
    return client.dfsRead(dfsPtr, objId, ((DirectBuffer) buffer).address() + bufferOffset, fileOffset, len, 0);
  }

  public long write(ByteBuffer buffer, long bufferOffset, long fileOffset, long len) throws IOException {
    open(true);
    //no asynchronous for now
    if (len > buffer.capacity() - bufferOffset) {
      throw new IOException(String.format("buffer (%d) has no enough data start at %d for write %d bytes to file",
              buffer.capacity(), bufferOffset, len));
    }
    return client.dfsWrite(dfsPtr, objId, ((DirectBuffer) buffer).address() + bufferOffset, fileOffset, len, 0);
  }

  public void mkdir() throws IOException {
    mkdir(mode);
  }

  public void mkdir(int mode) throws IOException {
    client.mkdir(path, mode, false);
  }

  public void mkdirs() throws IOException {
    mkdirs(mode);
  }

  public void mkdirs(int mode) throws IOException {
    client.mkdir(path, mode, true);
  }

  public boolean exists() throws IOException {
    open(false);
    if (!this.isOpen()){
      return false;
    }
    try {
      getStatAttributes(false);
    } catch (Exception e) {
      if (!(e instanceof DaosIOException)){
        throw new DaosIOException(e);
      }
      DaosIOException de = (DaosIOException)e;
      if (de.getErrorCode() != Constants.ERROR_CODE_NOT_EXIST){
        throw de;
      }
      return false;
    }
    return true;
  }

  public DaosFile rename(String destPath) throws IOException {
    destPath = DaosUtils.normalize(destPath);
    if (path.equals(destPath)) {
      return this;
    }
    client.move(path, destPath);
    return new DaosFile(destPath, client);
  }

  public boolean isDirectory() throws IOException {
    return client.dfsIsDirectory(getMode());
  }

  /**
   * release DAOS FS object actively
   */
  public void release() {
    if (cleaner != null) {
      cleaner.clean();
      cleaned = true;
    }
  }

  void setAccessFlags(int accessFlags) {
    this.accessFlags = accessFlags;
  }

  public int getMode() throws IOException {
    open(true);
    return client.dfsGetMode(objId);
  }

  StatAttributes getStatAttributes(boolean retrieve) throws IOException {
    open(true);
    ByteBuffer buffer = null;
    if (retrieve) {
      buffer = BufferAllocator.directBuffer(StatAttributes.objectSize());
    }
    client.dfsOpenedObjStat(dfsPtr, objId, buffer==null ? -1:((DirectBuffer) buffer).address());
    return buffer==null ? null:new StatAttributes(buffer);
  }

  public StatAttributes getStatAttributes() throws IOException {
    return getStatAttributes(true);
  }

  void setObjectType(DaosObjectType objectType) {
    this.objectType = objectType;
  }

  void setChunkSize(int chunkSize) {
    this.chunkSize = chunkSize;
  }

  void setMode(int mode) {
    this.mode = mode;
  }

  public DaosFile getParent() {
    return parent;
  }

  public String getParentPath() {
    return parentPath;
  }

  public String getPath() {
    return path;
  }

  public String getName() {
    return name;
  }

  protected long getObjId() throws IOException {
    open(true);
    return objId;
  }
}
