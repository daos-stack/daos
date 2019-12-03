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

  private DaosFile parent;

  private Cleaner cleaner;

  private long objId;

  private Exception lastException;

  private volatile boolean cleaned;

  private static final Logger log = LoggerFactory.getLogger(DaosFile.class);

  protected DaosFile(String parentPath, String path, DaosFsClient daosFsClient) {
    String pnor = DaosUtils.normalize(parentPath);
    String nor = DaosUtils.normalize(path);
    if(nor == null || nor.length() == 0){
      throw new IllegalArgumentException("invalid path");
    }

    int slash = nor.lastIndexOf('/');
    boolean hasParent = pnor.length() > 0;
    StringBuilder sb = new StringBuilder();
    if(slash > 0){
      if(hasParent){
        sb.append(pnor).append('/');
      }
      this.parentPath = sb.append(nor.substring(0, slash)).toString();
      sb.setLength(0);
      this.name = nor.substring(slash+1);
      if(hasParent){
        sb.append(pnor).append('/');
      }
      this.path = sb.append(nor).toString();
    }else{
      this.parentPath = pnor;
      this.name = nor;
      if(hasParent){
        sb.append(pnor).append('/');
      }
      this.path = sb.append(name).toString();
    }
    this.client = daosFsClient;
    this.dfsPtr = daosFsClient.getDfsPtr();
  }

  protected DaosFile(DaosFile parent, String path, DaosFsClient daosFsClient) {
    this(parent.path, path, daosFsClient);
    this.parent = parent;
  }

  protected DaosFile(String path, DaosFsClient daosFsClient) {
    this((String)null, path, daosFsClient);
  }

  public void createNewFile() throws IOException {
    createNewFile(objectType);
  }

  public void createNewFile(DaosObjectType objectType) throws IOException {
    if(objId != 0){
      throw new IOException("file existed already");
    }
    //parse path to get parent and name.
    //dfs lookup parent and then dfs open
    objId = client.createNewFile(dfsPtr, parentPath, name, mode, accessFlags, objectType.getValue());
    createCleaner();
  }

  /**
   * open FS object if hasn't opened yet.
   *
   * cleaner is created only open at the first time
   * @param throwException
   * throw exception if true, otherwise, keep exception and return immediately
   *
   * @throws DaosIOException
   */
  private void open(boolean throwException)throws DaosIOException {
    if(objId != 0){
      return;
    }

    try {
      if (parentPath == null) {
        objId = client.dfsLookup(dfsPtr, parent == null ? -1 : parent.getObjId(), name, accessFlags, -1);
      } else {
        objId = client.dfsLookup(dfsPtr, parentPath, name, accessFlags, -1);
      }
    }catch (Exception e){
      if(throwException){
        throw new DaosIOException(e);
      }else{//TODO: verify error code to determine existence, if it's other error code, throw it anyway.
        lastException = e;
        return;
      }
    }

    createCleaner();
  }

  /**
   * create cleaner for each opened {@link DaosFile} object. Cleaner calls {@link DaosFsClient#dfsRelease(long, long)}
   * to release opened FS object.
   *
   * If object is deleted in advance, no {@link DaosFsClient#dfsRelease(long, long)} will be called.
   */
  private void createCleaner() {
    if(cleaner != null){
      throw new IllegalStateException("Cleaner created already");
    }
    cleaned = false;
    //clean object by invoking dfs release
    cleaner = Cleaner.create(this, () -> {
      if(!cleaned) {
        try {
          client.dfsRelease(dfsPtr, objId);
        }catch (IOException e){
          log.error("failed to release fs object "+objId, e);
        }
      }
    });
  }

  /**
   * delete FS object
   * @throws IOException
   */
  public boolean delete() throws IOException{
    boolean deleted = client.delete(dfsPtr, parentPath, path);
    if(cleaner != null) {
      cleaned = true;
    }
    return deleted;
  }

  public long length()throws IOException{
    open(true);
    long size = client.dfsGetSize(dfsPtr, objId);
    return size;
  }

  public String[] listChildren() throws IOException{
    open(true);
    //no limit to max returned entries for now
    String[] children = client.dfsReadDir(dfsPtr, objId, -1);
    return children;
  }

  public void setExtAttribute(String name, String value)throws IOException{
    open(true);
    client.dfsSetExtAttr(dfsPtr, objId, name, value, 0);
  }

  public void read(ByteBuffer buffer, long bufferOffset, long fileOffset, long len)throws IOException{
    open(true);
    //no asynchronous for now
    client.dfsRead(dfsPtr, objId, ((DirectBuffer)buffer).address() + bufferOffset, fileOffset, len, 0);
  }

  public void write(ByteBuffer buffer, long bufferOffset, long fileOffset, long len)throws IOException {
    open(true);
    //no asynchronous for now
    client.dfsWrite(dfsPtr, objId, ((DirectBuffer)buffer).address() + bufferOffset, fileOffset, len, 0);
  }


  public void mkdir() throws IOException {
    client.mkdir(path, mode,false);
  }

  public void mkdirs() throws IOException {
    client.mkdir(path, mode, true);
  }

  public boolean exists()throws IOException{
    open(false);
    try {
      getStatAttributes();
    }catch (Exception e){//TODO: verify error code to determine existence
      return false;
    }
    return true;
  }

  public DaosFile rename(String destPath)throws IOException{
    destPath = DaosUtils.normalize(destPath);
    if(path.equals(destPath)){
      return this;
    }
    client.move(path, destPath);
    return new DaosFile(destPath, client);
  }

  public boolean isDirectory() throws IOException{
    open(true);
    return client.dfsIsDirectory(getMode());
  }

  public int getAccessFlags() {
    return accessFlags;
  }

  protected void setAccessFlags(int accessFlags) {
    this.accessFlags = accessFlags;
  }

  public int getMode() throws IOException{
    open(true);
    return client.dfsGetMode(objId);
  }

  public StatAttributes getStatAttributes()throws IOException{
    open(true);
    ByteBuffer buffer = BufferAllocator.directBuffer(StatAttributes.objectSize());
    client.dfsOpenedObjStat(dfsPtr, objId, ((DirectBuffer) buffer).address());
    return new StatAttributes(buffer);
  }

  protected void setMode(int mode) {
    this.mode = mode;
  }

  public void setObjectType(DaosObjectType objectType) {
    this.objectType = objectType;
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

  protected long getObjId() throws IOException{
    open(true);
    return objId;
  }
}
