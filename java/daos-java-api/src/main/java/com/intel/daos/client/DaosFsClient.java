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

import org.apache.commons.lang.ObjectUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;

/**
 * Java DAOS FS Client for all local/remote DAOS operations which is indirectly invoked via JNI.
 *
 * Before any operation, there is some preparation work to be done in this class.
 * <li>load dynamic library, libdaos-jni.so</li>
 * <li>load error code</li>
 * <li>start cleaner thread</li>
 * <li>register shutdown hook for DAOS finalization</li>
 * <li>create/open pool and container</li>
 * <li>mount DAOS FS on container</li>
 *
 * User cannot instantiate this class directly since we need to make sure single instance of {@linkplain DaosFsClient}
 * per pool and container. User should get this object from {@link DaosFsClientBuilder}.
 *
 * After getting {@linkplain DaosFsClient}, user usually get {@link DaosFile} object via {@linkplain #newFile} methods.
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

  private static final Logger log = LoggerFactory.getLogger(DaosFsClient.class);

  //make it non-daemon so that all DAOS file object can be released
  private static final Executor cleanerExe = Executors.newSingleThreadExecutor((r) -> {
    Thread thread = new Thread(r, "DAOS file object cleaner thread");
    thread.setDaemon(false);
    return thread;
  });

  //keyed by poolId+contId
  private static final Map<String, DaosFsClient> pcFsMap = new ConcurrentHashMap<>();

  static {
    loadLib();
    loadErrorCode();
    cleanerExe.execute(new Cleaner.CleanerTask());
    ShutdownHookManager.addHook(() -> {
      try{
        daosFinalize();
      }catch (IOException e){
        log.error("failed to finalize DAOS", e);
      }
    });
  }

  private static void loadLib() {}

  private static void loadErrorCode(){}

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

  private void init() throws IOException{
    if(inited){
      return;
    }
    if (poolId == null) {
      poolId = createPool(builder);
    }

    poolPtr = daosOpenPool(poolId, builder.serverGroup,
            builder.poolMode,
            builder.poolScmSize);

    if (contId == null) {
      contId = createContainer(poolPtr);
    }
    contPtr = daosOpenCont(poolPtr, contId, builder.poolMode);
    dfsPtr = mountFileSystem(poolPtr, contPtr, builder.readOnlyFs);

    ShutdownHookManager.addHook(() -> {
      try{
        disconnect();
      }catch (IOException e){
        log.error("failed to disconnect FS client", e);
      }
    });
    inited = true;
  }

  public long getDfsPtr() {
    return dfsPtr;
  }

  public static String createPool(DaosFsClientBuilder builder)throws IOException {
    return daosCreatePool(builder.serverGroup,
                          builder.poolMode,
                          builder.poolScmSize,
                          builder.poolNvmeSize);
  }

  public static String createContainer(long poolPtr)throws IOException {
    return daosCreateContainer(poolPtr);
  }

  public static synchronized long mountFileSystem(long poolPtr, long contPtr, boolean readOnly) throws IOException{
    return dfsMountFs(poolPtr, contPtr, readOnly);
  }

  public void disconnect() throws IOException{
    if (inited && dfsPtr != 0) {
      dfsUnmountFs(dfsPtr);
      daosCloseContainer(contPtr);
      daosClosePool(poolPtr);
    }
    inited = false;
    pcFsMap.remove(poolId+contId);
  }

  public DaosFile newFile(String path) {
    return newFile(path, builder.defFileAccessFlag, builder.defFileMode);
  }

  public DaosFile newFile(String path, int accessFlags, int mode) {
    DaosFile daosFile = new DaosFile(path, this);
    daosFile.setAccessFlags(accessFlags);
    daosFile.setMode(mode);
    daosFile.setObjectType(builder.defFileObjType);
    return daosFile;
  }

  public DaosFile newFile(String parent, String path){
    return newFile(parent, path, builder.defFileAccessFlag, builder.defFileMode);
  }

  public DaosFile newFile(String parent, String path, int accessFlags, int mode) {
    DaosFile daosFile = new DaosFile(parent, path, this);
    daosFile.setAccessFlags(accessFlags);
    daosFile.setMode(mode);
    daosFile.setObjectType(builder.defFileObjType);
    return daosFile;
  }

  public DaosFile newFile(DaosFile parent, String path){
    return newFile(parent, path, builder.defFileAccessFlag, builder.defFileMode);
  }

  public DaosFile newFile(DaosFile parent, String path, int accessFlags, int mode) {
    DaosFile daosFile = new DaosFile(parent, path, this);
    daosFile.setAccessFlags(accessFlags);
    daosFile.setMode(mode);
    daosFile.setObjectType(builder.defFileObjType);
    return daosFile;
  }

  //non-native methods
  public void move(String srcPath, String destPath)throws IOException {
    move(dfsPtr, srcPath, destPath);
  }

  public void delete(String path)throws IOException{
    path = DaosUtils.normalize(path);
    String[] pc = DaosUtils.parsePath(path);
    delete(dfsPtr, pc.length==2 ? pc[0]:null, pc[1]);
  }

  public void mkdir(String path, int mode, boolean recursive)throws IOException{
    mkdir(dfsPtr, path, mode, recursive);
  }

  /**
   * move file object denoted by <code>srcPath</code> to new path denoted by <code>destPath</code>
   *
   * TODO: dfs_release all open file object
   * @param dfsPtr
   * @param srcPath full path of source
   * @param destPath full path of destination
   * @throws IOException
   */
  protected native void move(long dfsPtr, String srcPath, String destPath)throws IOException;

  /**
   * make directory denoted by <code>path</code>
   *
   * TODO: dfs_release all open file object
   *
   * @param dfsPtr
   * @param path full path
   * @param mode
   * @param recursive true to create all ancestors, false to create just itself
   * @throws IOException
   */
  protected native void mkdir(long dfsPtr, String path, int mode, boolean recursive)throws IOException;


  /**
   * create new file
   *
   * //TODO: dfs_release parent object
   * @param dfsPtr
   * @param parentPath null for root
   * @param name
   * @param mode
   * @param accessFlags
   * @param objectId
   * @throws IOException
   *
   * @return DAOS FS object id
   */
  protected native long createNewFile(long dfsPtr, String parentPath, String name, int mode, int accessFlags,
                                      int objectId)throws IOException;

  /**
   * delete file with <code>name</code> from <code>parentPath</code>
   *
   * @param dfsPtr
   * @param parentPath null for root
   * @param name
   * @throws IOException
   *
   * @return true for deleted successfully, false for other cases, like not existed or failed to delete
   */
  protected native boolean delete(long dfsPtr, String parentPath, String name)throws IOException;


  //DAOS corresponding methods

  /**
   * create DAOS pool
   *
   * @param serverGroup
   * @param mode
   * @param scmSize
   * @param nvmeSize
   * @throws IOException
   *
   * @return poold id
   */
  protected static native String daosCreatePool(String serverGroup, int mode, long scmSize, long nvmeSize)throws IOException;

  /**
   * create DAOS container in DAOS pool specified by <code>poolPtr</code>
   *
   * @param poolPtr
   * @throws IOException
   *
   * @return
   */
  protected static native String daosCreateContainer(long poolPtr)throws IOException;

  /**
   * open pool
   *
   * @param poolId
   * @param serverGroup
   * @param mode
   * @param scmSize
   * @throws IOException
   *
   * @return pool pointer or pool handle
   */
  protected static native long daosOpenPool(String poolId, String serverGroup, int mode, long scmSize)throws IOException;

  /**
   * open container
   *
   * @param poolPtr
   * @param contId
   * @param mode
   * @throws IOException
   *
   * @return container pointer or container handle
   */
  protected static native long daosOpenCont(long poolPtr, String contId, int mode)throws IOException;

  /**
   * close container
   *
   * @param contPtr
   * @throws IOException
   */
  protected static native void daosCloseContainer(long contPtr)throws IOException;

  /**
   * close pool
   * @param poolPtr
   * @throws IOException
   */
  protected static native void daosClosePool(long poolPtr)throws IOException;


  //DAOS FS corresponding methods

  /**
   * set prefix
   * @param dfsPtr
   * @param prefix
   * @throws IOException
   *
   * @return 0 for success, others for failure
   */
  protected native int dfsSetPrefix(long dfsPtr, String prefix)throws IOException;

  /**
   * open a file with opened parent specified by <code>parentObjId</code>
   *
   * TODO: make sure buffer is set in the same order as StatAttributes instantiation
   *
   * @param dfsPtr
   * @param parentObjId
   * @param name
   * @param flags
   * @param bufferAddress address of direct {@link java.nio.ByteBuffer} for holding all information of
   *                      {@link StatAttributes}, -1 if you don't want to get {@link StatAttributes}
   * @throws IOException
   *
   * @return DAOS FS object id
   */
  protected native long dfsLookup(long dfsPtr, long parentObjId, String name, int flags, long bufferAddress)throws IOException;

  /**
   * Same as {@link #dfsLookup(long, long, String, int, long)} except parent file is not opened.
   *
   * @param dfsPtr
   * @param parentPath
   * @param name
   * @param flags
   * @param bufferAddress address of direct {@link java.nio.ByteBuffer} for holding all information of
   *                      {@link StatAttributes}
   * @throws IOException
   *
   * @return DAOS FS object id
   */
  protected native long dfsLookup(long dfsPtr, String parentPath, String name, int flags, long bufferAddress)throws IOException;

  /**
   * get file length for opened FS object
   *
   * @param dfsPtr
   * @param objId
   * @throws IOException
   *
   * @return file length
   */
  protected native long dfsGetSize(long dfsPtr, long objId)throws IOException;

  /**
   * duplicate opened FS object
   *
   * @param dfsPtr
   * @param objId
   * @param flags
   * @throws IOException
   *
   * @return FS object id of duplication
   */
  protected native long dfsDup(long dfsPtr, long objId, int flags)throws IOException;

  /**
   * release opened FS object
   *
   * @param dfsPtr
   * @param objId
   * @throws IOException
   *
   * @return
   */
  protected native long dfsRelease(long dfsPtr, long objId)throws IOException;

  /**
   * read data from file to buffer
   *
   * @param dfsPtr
   * @param objId
   * @param bufferAddress
   * @param offset
   * @param len
   * @param eventNo
   * @throws IOException
   *
   * @return number of bytes actual read
   */
  protected native long dfsRead(long dfsPtr, long objId, long bufferAddress, long offset, long len, int eventNo)throws IOException;

  /**
   * write data from buffer to file
   *
   * @param dfsPtr
   * @param objId
   * @param bufferAddress
   * @param offset
   * @param len
   * @param eventNo
   * @throws IOException
   *
   * @return number of bytes actual written
   */
  protected native long dfsWrite(long dfsPtr, long objId, long bufferAddress, long offset, long len,
                              int eventNo)throws IOException;

  /**
   * read children
   *
   * @param dfsPtr
   * @param objId
   * @param maxEntries, -1 for no limit
   * @return string array of file name
   */
  protected native String[] dfsReadDir(long dfsPtr, long objId, int maxEntries)throws IOException;

  /**
   * get FS object status attribute into direct {@link java.nio.ByteBuffer}
   *
   * @param dfsPtr
   * @param objId
   * @param bufferAddress
   * @throws IOException
   */
  protected native void dfsOpenedObjStat(long dfsPtr, long objId, long bufferAddress)throws IOException;

  /**
   * set extended attribute
   *
   * @param dfsPtr
   * @param objId
   * @param name
   * @param value
   * @param flags
   * @throws IOException
   */
  protected native void dfsSetExtAttr(long dfsPtr, long objId, String name, String value, int flags)throws IOException;

  /**
   * get extended attribute
   *
   * @param dfsPtr
   * @param objId
   * @param name
   * @return
   * @throws IOException
   */
  protected native String dfsGetExtAttr(long dfsPtr, long objId, String name)throws IOException;

  /**
   * remove extended attribute
   *
   * @param dfsPtr
   * @param objId
   * @param name
   * @return
   * @throws IOException
   */
  protected native String dfsRemoveExtAttr(long dfsPtr, long objId, String name)throws IOException;

  /**
   * get chunk size
   *
   * @param objId
   * @return
   * @throws IOException
   */
  protected static native long dfsGetChunkSize(long objId)throws IOException;

  /**
   * get mode
   *
   * @param objId
   * @return
   * @throws IOException
   */
  protected static native int dfsGetMode(long objId)throws IOException;

  /**
   * check if it's directory by providing mode
   * @param mode
   * @return
   * @throws IOException
   */
  protected static native boolean dfsIsDirectory(int mode)throws IOException;

  /**
   * mount FS on container
   *
   * @param poolPtr
   * @param contPtr
   * @param readOnly
   * @return FS client pointer
   */
  protected static native long dfsMountFs(long poolPtr, long contPtr, boolean readOnly)throws IOException;

  /**
   * unmount FS
   * @param dfsPtr
   * @throws IOException
   */
  protected static native void dfsUnmountFs(long dfsPtr)throws IOException;

  /**
   * finalize DAOS client
   * @throws IOException
   */
  protected static native void daosFinalize()throws IOException;


  /**
   * A builder for constructing Java DAOS FS Client. All parameters should be specified here. This builder
   * makes sure single instance of {@link DaosFsClient} per pool and container.
   *
   * Please note that new pool and new container will be created if their ids (poolId and containerId) are {@code null}.
   *
   */
  public static class DaosFsClientBuilder{
    private String poolId;
    private String contId;
    private String svc;
    private String serverGroup;
    private int poolMode;
    private long poolScmSize;
    private long poolNvmeSize;
    private int defFileAccessFlag;
    private int defFileMode;
    private DaosObjectType defFileObjType = DaosObjectType.OC_SX;
    private boolean readOnlyFs;
    private boolean shareFsClient = true;

    public DaosFsClientBuilder poolId(String poolId){
      this.poolId = poolId;
      return this;
    }

    public DaosFsClientBuilder containerId(String contId){
      this.contId = contId;
      return this;
    }

    public DaosFsClientBuilder svc(String svc){
      this.svc = svc;
      return this;
    }

    public DaosFsClientBuilder serverGroup(String serverGroup){
      this.serverGroup = serverGroup;
      return this;
    }

    public DaosFsClientBuilder poolMode(int poolMode){
      this.poolMode = poolMode;
      return this;
    }

    public DaosFsClientBuilder poolScmSize(long poolScmSize){
      this.poolScmSize = poolScmSize;
      return this;
    }

    public DaosFsClientBuilder poolNvmeSize(long poolNvmeSize){
      this.poolNvmeSize = poolNvmeSize;
      return this;
    }

    public DaosFsClientBuilder defFileAccessFlag(int defFileAccessFlag){
      this.defFileAccessFlag = defFileAccessFlag;
      return this;
    }

    public DaosFsClientBuilder defFileMode(int defFileMode){
      this.defFileMode = defFileMode;
      return this;
    }

    public DaosFsClientBuilder defFileType(DaosObjectType defFileObjType){
      this.defFileObjType = defFileObjType;
      return this;
    }

    public DaosFsClientBuilder readOnlyFs(boolean readOnlyFs){
      this.readOnlyFs = readOnlyFs;
      return this;
    }

    public DaosFsClientBuilder shareFsClient(boolean shareFsClient){
      this.shareFsClient = shareFsClient;
      return this;
    }

    @Override
    public DaosFsClientBuilder clone(){
      return (DaosFsClientBuilder)ObjectUtils.clone(this);
    }

    public DaosFsClient build()throws IOException{
      DaosFsClientBuilder copied = this.clone();
      DaosFsClient client;
      if(poolId != null){
        if(contId != null){
          client = getClientForCont(copied);
        }else {
          client = new DaosFsClient(poolId, copied);
        }
      }else {
        client = new DaosFsClient(copied);
      }
      client.init();
      return client;
    }

    private DaosFsClient getClientForCont(DaosFsClientBuilder builder) {
      DaosFsClient client;
      if(!builder.shareFsClient){
        return new DaosFsClient(poolId, contId, builder);
      }
      //check existing client
      String key = poolId + contId;
      client = pcFsMap.get(key);
      if(client == null) {
        client = new DaosFsClient(poolId, contId, builder);
        pcFsMap.putIfAbsent(key, client);
      }
      return pcFsMap.get(key);
    }
  }
}
