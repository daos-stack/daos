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
import java.nio.ByteBuffer;

import javax.annotation.concurrent.NotThreadSafe;

import io.daos.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import sun.nio.ch.DirectBuffer;

/**
 * This class mimics {@link java.io.File} class to represent DAOS FS object and provide similar methods, like
 * {@link #delete()}, {@link #createNewFile()}, {@link #exists()}, {@link #length()}. The creation of this object
 * doesn't involve any remote operation which is delayed and invoked on-demand.
 *
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

  protected DaosFile(String parentPath, String path, int accessFlags, DaosFsClient daosFsClient) {
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

    this.accessFlags = accessFlags;

    this.client = daosFsClient;
    if (this.client != null) {
      this.dfsPtr = daosFsClient.getDfsPtr();
      this.mode = client.getDefaultFileMode();
      this.objectType = client.getDefaultFileObjType();
      this.chunkSize = client.getDefaultFileChunkSize();
    } else { //no client, could be for test purpose
      this.dfsPtr = -1;
    }
  }

  protected DaosFile(DaosFile parent, String path, int accessFlags, DaosFsClient daosFsClient) {
    this(parent.path, path, accessFlags, daosFsClient);
    this.parent = parent;
  }

  protected DaosFile(String path, int accessFlags, DaosFsClient daosFsClient) {
    this((String) null, path, accessFlags, daosFsClient);
  }

  /**
   * create new file with default mode, object type and chunk size.
   * Parent directory should exist.
   *
   * @throws IOException
   * {@link DaosIOException}
   */
  public void createNewFile() throws IOException {
    createNewFile(false);
  }

  /**
   * create new file with default mode, object type and chunk size.
   * This operation will fail if parent directory doesn't exist and <code>createParent</code> is false.
   *
   * @param createParent
   * create directory if parent doesn't exist
   * @throws IOException
   * {@link DaosIOException}
   */
  public void createNewFile(boolean createParent) throws IOException {
    createNewFile(mode, objectType, chunkSize, createParent);
  }

  /**
   * create new file with mode, object type and chunk size as well as createParent.
   *
   * @param mode
   * should be octal number, like 0775
   * @param objectType
   * object type, see {@link io.daos.dfs.DaosFsClient.DaosFsClientBuilder#defaultFileType}
   * @param chunkSize
   * file chunk size
   * @param createParent
   * create directory if parent doesn't exist
   * @throws IOException
   * {@link DaosIOException}
   */
  public void createNewFile(int mode, DaosObjectType objectType, int chunkSize, boolean createParent)
          throws IOException {
    if (objId != 0) {
      throw new IOException("file existed already");
    }
    // parse path to get parent and name.
    // dfs lookup parent and then dfs open
    objId = client.createNewFile(dfsPtr, parentPath, name, mode, accessFlags, objectType.nameWithoutOc(), chunkSize,
            createParent);
    createCleaner();
  }

  /**
   * open FS object if hasn't opened yet.
   *
   * <p>
   * cleaner is created only open at the first time
   *
   * @param throwException
   * throw exception if true, otherwise, keep exception and return immediately
   * @throws DaosIOException
   * DaosIOException
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
      if (!(e instanceof DaosIOException)) { //unexpected exception
        throw new DaosIOException(e);
      }
      if (throwException) {
        throw (DaosIOException) e;
      } else { //verify error code to determine existence, if it's other error code, throw it anyway.
        DaosIOException de = (DaosIOException) e;
        if (de.getErrorCode() != Constants.ERROR_CODE_NOT_EXIST) {
          throw de;
        }
      }
    }
    if (isOpen()) {
      createCleaner();
    }
  }

  /**
   * check if file is opened from Daos.
   *
   * @return true if file is opened, false otherwise
   */
  public boolean isOpen() {
    return objId != 0;
  }

  /**
   * create cleaner for each opened {@link DaosFile} object. Cleaner calls {@link DaosFsClient#dfsRelease(long)}
   * to release opened FS object.
   *
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
   * delete FS object.
   *
   * @throws IOException
   * {@link DaosIOException}
   */
  public boolean delete() throws IOException {
    return delete(false);
  }

  /**
   * delete FS object. Non-empty directory will be deleted if
   * <code>force</code> set to true.
   *
   * @param force
   * force deletion if directory is not empty
   * @return true if FS object is deleted. False otherwise
   * @throws IOException
   * {@link DaosIOException}
   */
  public boolean delete(boolean force) throws IOException {
    boolean deleted = client.delete(dfsPtr, parentPath, name, force);
    if (deleted) {
      deleted();
    }
    return deleted;
  }

  /**
   * length of FS object.
   *
   * @return length in bytes
   * @throws IOException
   * {@link DaosIOException}
   */
  public long length() throws IOException {
    open(true);
    long size = client.dfsGetSize(dfsPtr, objId);
    return size;
  }

  /**
   * list all children of this directory.
   *
   * @return String array of file name. Empty string array is returned for empty directory
   * @throws IOException
   * {@link DaosIOException}
   */
  public String[] listChildren() throws IOException {
    open(true);
    //no limit to max returned entries for now
    String children = client.dfsReadDir(dfsPtr, objId, -1);
    return (children == null || (children = children.trim()).length() == 0) ?
            new String[]{} : children.split(",");
  }

  /**
   * set extended attribute.
   *
   * @param name
   * attribute name
   * @param value
   * attribute value
   * @param flags should be one of below value
   *              {@link Constants#SET_XATTRIBUTE_NO_CHECK} : no attribute name check
   *              {@link Constants#SET_XATTRIBUTE_CREATE}   : create or fail if attribute exits
   *              {@link Constants#SET_XATTRIBUTE_REPLACE}  : replace or fail if attribute doesn't exist
   * @throws IOException
   * {@link DaosIOException}
   */
  public void setExtAttribute(String name, String value, int flags) throws IOException {
    open(true);
    client.dfsSetExtAttr(dfsPtr, objId, name, value, flags);
  }

  /**
   * get extended attribute.
   *
   * @param name
   * attribute name
   * @param expectedValueLen expected value length. Make sure you give enough length so that actual value
   *                         is not truncated.
   * @return value in string value may be truncated if parameter <code>expectedValueLen</code> is less than
    actual value length
   * @throws IOException
   * {@link DaosIOException}
   */
  public String getExtAttribute(String name, int expectedValueLen) throws IOException {
    open(true);
    return client.dfsGetExtAttr(dfsPtr, objId, name, expectedValueLen);
  }

  /**
   * remove extended attribute.
   *
   * @param name
   * attribute name
   * @throws IOException
   * {@link DaosIOException}
   */
  public void remoteExtAttribute(String name) throws IOException {
    open(true);
    client.dfsRemoveExtAttr(dfsPtr, objId, name);
  }

  /**
   * get chunk size of this file.
   *
   * @throws IOException
   * {@link DaosIOException}
   */
  public long getChunkSize() throws IOException {
    open(true);
    return client.dfsGetChunkSize(objId);
  }

  /**
   * read <code>len</code> of data from file at <code>fileOffset</code> to <code>buffer</code> starting from
   * <code>bufferOffset</code>.
   *
   * <p>
   * Be note, caller should set <code>buffer</code> indices, like position, limit or marker, by itself based on
   * return value of this method.
   *
   * @param buffer       Must be instance of {@link DirectBuffer}
   * @param bufferOffset buffer offset
   * @param fileOffset   file offset
   * @param len          expected length in bytes read from file to buffer
   * @return actual read bytes
   * @throws IOException
   * {@link DaosIOException}
   */
  public long read(ByteBuffer buffer, long bufferOffset, long fileOffset, long len) throws IOException {
    open(true);
    //no asynchronous for now
    if (len > buffer.capacity() - bufferOffset) {
      throw new IOException(String.format("buffer (%d) has no enough space start at %d for reading %d " +
                      "bytes from file",
              buffer.capacity(), bufferOffset, len));
    }
    return client.dfsRead(dfsPtr, objId, ((DirectBuffer) buffer).address() + bufferOffset,
            fileOffset, len, 0);
  }

  /**
   * write <code>len</code> bytes to file starting at <code>fileOffset</code> from <code>buffer</code> at
   * <code>bufferOffset</code>.
   *
   * @param buffer       Must be instance of {@link DirectBuffer}
   * @param bufferOffset buffer offset
   * @param fileOffset   file offset
   * @param len          length in bytes of data to write
   * @return it's same as the parameter <code>len</code> since underlying DAOS FS doesn't give length of actual
    written data
   * @throws IOException
   * {@link DaosIOException}
   */
  public long write(ByteBuffer buffer, long bufferOffset, long fileOffset, long len) throws IOException {
    open(true);
    //no asynchronous for now
    if (len > buffer.capacity() - bufferOffset) {
      throw new IOException(String.format("buffer (%d) has no enough data start at %d for write %d bytes to file",
              buffer.capacity(), bufferOffset, len));
    }
    return client.dfsWrite(dfsPtr, objId, ((DirectBuffer) buffer).address() + bufferOffset,
            fileOffset, len, 0);
  }

  /**
   * create directory.
   *
   * @throws IOException
   * {@link DaosIOException}
   */
  public void mkdir() throws IOException {
    mkdir(mode);
  }

  /**
   * create directory with file <code>mode</code> specified. It should be octal value like 0755.
   *
   * @param mode
   * directory mode
   * @throws IOException
   * {@link DaosIOException}
   */
  public void mkdir(int mode) throws IOException {
    client.mkdir(path, mode, false);
  }

  /**
   * create this directory or all its ancestors if they are not existed.
   *
   * @throws IOException
   * {@link DaosIOException}
   */
  public void mkdirs() throws IOException {
    mkdirs(mode);
  }

  /**
   * same as {@link #mkdirs()} with file <code>mode</code> specified.
   *
   * <p>
   * check {@link #mkdir(int)} for <code>mode</code>
   *
   * @param mode
   * directory mode
   * @throws IOException
   * {@link DaosIOException}
   */
  public void mkdirs(int mode) throws IOException {
    client.mkdir(path, mode, true);
  }

  /**
   * check existence of file.
   *
   * @return true if file exists. false otherwise
   * @throws IOException
   * {@link DaosIOException}
   */
  public boolean exists() throws IOException {
    open(false);
    if (!this.isOpen()) {
      return false;
    }
    try {
      getStatAttributes(false);
    } catch (Exception e) {
      if (log.isDebugEnabled()) {
        log.debug("not exists", e);
      }
      if (!(e instanceof DaosIOException)) {
        throw new DaosIOException(e);
      }
      DaosIOException de = (DaosIOException) e;
      if (de.getErrorCode() != Constants.ERROR_CODE_NOT_EXIST) {
        throw de;
      }
      return false;
    }
    return true;
  }

  /**
   * rename this file to another file denoted by <code>destPath</code>.
   *
   * @param destPath
   * destination path
   * @return new DaosFile denoted by <code>destPath</code>
   * @throws IOException
   * {@link DaosIOException}
   */
  public DaosFile rename(String destPath) throws IOException {
    destPath = DaosUtils.normalize(destPath);
    if (path.equals(destPath)) {
      return this;
    }
    client.move(path, destPath);
    deleted();
    return new DaosFile(destPath, accessFlags, client);
  }

  private void deleted() {
    if (cleaner != null) {
      cleaned = true;
      cleaner = null;
    }
    objId = 0;
  }

  /**
   * check if this file is a directory.
   *
   * @return true if it's directory. false otherwise
   * @throws IOException
   * {@link DaosIOException}
   */
  public boolean isDirectory() throws IOException {
    return client.dfsIsDirectory(getMode());
  }

  /**
   * release DAOS FS object actively.
   */
  public void release() {
    if (cleaner != null) {
      cleaner.clean();
      cleaned = true;
    }
  }

  /**
   * get mode of this file.
   *
   * @return file mode
   * @throws IOException
   * {@link DaosIOException}
   */
  public int getMode() throws IOException {
    open(true);
    return client.dfsGetMode(objId);
  }

  /**
   * get stat attributes of this file. It's also used for checking existence of opened FS object.
   *
   * @param retrieve true if you want to retrieve attributes info back. false if you want to just check file existence.
   * @return StatAttributes
   * @throws IOException
   * {@link DaosIOException}
   * @see StatAttributes
   */
  StatAttributes getStatAttributes(boolean retrieve) throws IOException {
    open(true);
    ByteBuffer buffer = null;
    if (retrieve) {
      buffer = BufferAllocator.directBuffer(StatAttributes.objectSize());
    }
    client.dfsOpenedObjStat(dfsPtr, objId, buffer == null ? -1 : ((DirectBuffer) buffer).address());
    return buffer == null ? null : new StatAttributes(buffer);
  }

  /**
   * retrieve stat attributes of this file.
   *
   * @return StatAttributes
   * @throws IOException
   * {@link DaosIOException}
   * @see StatAttributes
   */
  public StatAttributes getStatAttributes() throws IOException {
    return getStatAttributes(true);
  }

  /**
   * parent {@link DaosFile}.
   *
   * @return parent file
   */
  public DaosFile getParent() {
    return parent;
  }

  /**
   * parent path of this file.
   *
   * @return parent path
   */
  public String getParentPath() {
    return parentPath;
  }

  /**
   * full path of this file.
   *
   * @return file path
   */
  public String getPath() {
    return path;
  }

  /**
   * name part of this file.
   *
   * @return file name
   */
  public String getName() {
    return name;
  }

  /**
   * get DAOS object id of this file.
   *
   * @return DAOS object id
   * @throws IOException
   * {@link DaosIOException}
   */
  protected long getObjId() throws IOException {
    open(true);
    return objId;
  }

  @Override
  public String toString() {
    return path;
  }
}
