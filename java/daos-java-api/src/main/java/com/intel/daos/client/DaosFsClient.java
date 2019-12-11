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

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.CopyOption;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Map;
import java.util.concurrent.*;

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
 * If you have <code>poolId</code> specified, but no <code>containerId</code>, DAOS FS will be mounted on
 * non-readonly root container with UUID {@link #ROOT_CONT_UUID}. Thus, you need to make sure the pool
 * doesn't have root container yet.
 *
 * User cannot instantiate this class directly since we need to make sure single instance of {@linkplain DaosFsClient}
 * per pool and container. User should get this object from {@link DaosFsClientBuilder}.
 *
 * After getting {@linkplain DaosFsClient}, user usually get {@link DaosFile} object via {@linkplain #getFile} methods.
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

  public static final String LIB_NAME = "daos-jni";

  public static final String ROOT_CONT_UUID = "ffffffff-ffff-ffff-ffff-ffffffffffff";

  private static final Logger log = LoggerFactory.getLogger(DaosFsClient.class);

  //make it non-daemon so that all DAOS file object can be released
  private final ExecutorService cleanerExe = Executors.newSingleThreadExecutor((r) -> {
    Thread thread = new Thread(r, "DAOS file object cleaner thread");
    thread.setDaemon(false);
    return thread;
  });

  //keyed by poolId+contId
  private static final Map<String, DaosFsClient> pcFsMap = new ConcurrentHashMap<>();

  static {
    loadLib();
    ShutdownHookManager.addHook(() -> {
      try{
        daosFinalize();
      }catch (IOException e){
        log.error("failed to finalize DAOS", e);
      }
    });
  }

  private static void loadLib() {
    try {
      log.info("loading lib{}.so", LIB_NAME);
      System.loadLibrary(LIB_NAME);
    } catch (UnsatisfiedLinkError e) {
      log.info("failed to load from lib directory. loading from jar instead " + LIB_NAME, e);
      loadFromJar();
    }
  }

  private static void loadFromJar() {
    File tempDir = null;

    try {
      tempDir = Files.createTempDirectory("daos").toFile();
      tempDir.deleteOnExit();
      String filePath = new StringBuilder("/lib").append(LIB_NAME).append("so").toString();
      loadByPath(filePath, tempDir);
    } catch (IOException e) {
      if (tempDir != null) {
        tempDir.delete();
      }
      throw new RuntimeException("failed to load lib from jar, "+LIB_NAME, e);
    }
  }

  private static void loadByPath(String path, File tempDir) throws IOException {
    File tempFile = null;
    String[] fields = path.split("/");
    String name = fields[fields.length - 1];

    try (InputStream is = DaosFsClient.class.getResourceAsStream(path)){
      tempFile = new File(tempDir, name);
      tempFile.deleteOnExit();
      Files.copy(is, tempFile.toPath(), new CopyOption[]{StandardCopyOption.REPLACE_EXISTING});
      System.load(tempFile.getAbsolutePath());
    } catch (IOException e){
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

  private synchronized void init() throws IOException{
    if(inited){
      return;
    }
    if (poolId == null) {
      poolId = createPool(builder);
    }

    poolPtr = daosOpenPool(poolId, builder.serverGroup,
            builder.ranks,
            builder.poolFlags);
    log.info("opened pool {}", poolPtr);

    if (contId != null) {
      contPtr = daosOpenCont(poolPtr, contId, builder.containerFlags);
      log.info("opened container {}", contPtr);
      dfsPtr = mountFileSystem(poolPtr, contPtr, builder.readOnlyFs);
      log.info("mounted FS {}", dfsPtr);
    }else{
      contId = ROOT_CONT_UUID;
      contPtr = -1;
      dfsPtr = mountFileSystem(poolPtr, -1, builder.readOnlyFs);
      log.info("mounted FS {} on root container", dfsPtr);
    }

    cleanerExe.execute(new Cleaner.CleanerTask());
    log.info("cleaner task running");

    ShutdownHookManager.addHook(() -> {
      try{
        disconnect();
      }catch (IOException e){
        log.error("failed to disconnect FS client", e);
      }
    });

    inited = true;
    log.info("DaosFsClient for {}, {} inited", poolId, contId);
  }

  public long getDfsPtr() {
    return dfsPtr;
  }

  public static String createPool(DaosFsClientBuilder builder)throws IOException {
    String poolInfo = daosCreatePool(builder.serverGroup,
                          builder.poolSvcReplics,
                          builder.poolMode,
                          builder.poolScmSize,
                          builder.poolNvmeSize);
    log.info("opened pool: {}", poolInfo);
    //TODO: parse poolInfo to set poolId and svc , poolId svc1:svc2...
    return poolInfo;
  }

  public static synchronized long mountFileSystem(long poolPtr, long contPtr, boolean readOnly) throws IOException{
    if(contPtr == -1){
      return dfsMountFs(poolPtr, contPtr, readOnly);
    }
    return dfsMountFsOnRoot(poolPtr);
  }

  public void disconnect() throws IOException{
    if (inited && dfsPtr != 0) {
      if(contPtr == -1){
        dfsUnmountFsOnRoot(dfsPtr);
        log.info("FS unmounted {} from root container", dfsPtr);
      }else {
        dfsUnmountFs(dfsPtr);
        log.info("FS unmounted {}", dfsPtr);
        daosCloseContainer(contPtr);
        log.info("closed container {}", contPtr);
      }
      daosClosePool(poolPtr);
      log.info("closed pool {}", poolPtr);
      cleanerExe.shutdown();
      try {
        if (!cleanerExe.awaitTermination(200, TimeUnit.MILLISECONDS)) {
          cleanerExe.shutdownNow();
        }
      }catch (InterruptedException e){
        log.error("interrupted when wait for termination");
      }
      log.info("cleaner task stopped");
      log.info("DaosFsClient for {}, {} disconnected", poolId, contId);
    }
    inited = false;
    pcFsMap.remove(poolId+contId);
  }

  public DaosFile getFile(String path) {
    return getFile(path, builder.defaultFileAccessFlag);
  }

  public DaosFile getFile(String path, int accessFlags) {
    DaosFile daosFile = new DaosFile(path, this);
    daosFile.setAccessFlags(accessFlags);
    daosFile.setMode(builder.defaultFileMode);
    daosFile.setObjectType(builder.defaultFileObjType);
    daosFile.setChunkSize(builder.defaultFileChunkSize);
    return daosFile;
  }

  public DaosFile getFile(String parent, String path){
    return getFile(parent, path, builder.defaultFileAccessFlag);
  }

  public DaosFile getFile(String parent, String path, int accessFlags) {
    DaosFile daosFile = new DaosFile(parent, path, this);
    daosFile.setAccessFlags(accessFlags);
    daosFile.setMode(builder.defaultFileMode);
    daosFile.setObjectType(builder.defaultFileObjType);
    daosFile.setChunkSize(builder.defaultFileChunkSize);
    return daosFile;
  }

  public DaosFile getFile(DaosFile parent, String path){
    return getFile(parent, path, builder.defaultFileAccessFlag);
  }

  public DaosFile getFile(DaosFile parent, String path, int accessFlags) {
    DaosFile daosFile = new DaosFile(parent, path, this);
    daosFile.setAccessFlags(accessFlags);
    daosFile.setMode(builder.defaultFileMode);
    daosFile.setObjectType(builder.defaultFileObjType);
    daosFile.setChunkSize(builder.defaultFileChunkSize);
    return daosFile;
  }

  //non-native methods
  public void move(String srcPath, String destPath)throws IOException {
    move(dfsPtr, srcPath, destPath);
  }

  public void delete(String path)throws IOException{
    path = DaosUtils.normalize(path);
    String[] pc = DaosUtils.parsePath(path);
    delete(dfsPtr, pc.length==2 ? pc[0]:null, pc[1], false);
  }

  public void mkdir(String path, int mode, boolean recursive)throws IOException{
    mkdir(dfsPtr, path, mode, recursive);
  }

  /**
   * move file object denoted by <code>srcPath</code> to new path denoted by <code>destPath</code>
   *
   * @param dfsPtr
   * @param srcPath full path of source
   * @param destPath full path of destination
   * @throws IOException
   */
  native void move(long dfsPtr, String srcPath, String destPath)throws IOException;

  /**
   * make directory denoted by <code>path</code>
   *
   * @param dfsPtr
   * @param path full path
   * @param mode
   * @param recursive true to create all ancestors, false to create just itself
   * @throws IOException
   */
  native void mkdir(long dfsPtr, String path, int mode, boolean recursive)throws IOException;

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
   * @param chunkSize
   * @throws IOException
   *
   * @return DAOS FS object id
   */
  native long createNewFile(long dfsPtr, String parentPath, String name, int mode, int accessFlags,
                                      int objectId, int chunkSize)throws IOException;

  /**
   * delete file with <code>name</code> from <code>parentPath</code>
   *
   * @param dfsPtr
   * @param parentPath null for root
   * @param name
   * @param force true to delete directory if it's not empty
   * @throws IOException
   *
   * @return true for deleted successfully, false for other cases, like not existed or failed to delete
   */
  native boolean delete(long dfsPtr, String parentPath, String name, boolean force)throws IOException;


  //DAOS corresponding methods

  /**
   * create DAOS pool
   *
   * @param serverGroup
   * @param poolSvcReplics
   * @param mode
   * @param scmSize
   * @param nvmeSize
   * @throws IOException
   *
   * @return poold id
   */
  static native String daosCreatePool(String serverGroup, int poolSvcReplics, int mode, long scmSize,
                                      long nvmeSize)throws IOException;

  /**
   * open pool
   *
   * @param poolId
   * @param serverGroup
   * @param ranks
   * @param flags
   * @throws IOException
   *
   * @return pool pointer or pool handle
   */
  static native long daosOpenPool(String poolId, String serverGroup, String ranks, int flags)throws IOException;

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
  static native long daosOpenCont(long poolPtr, String contId, int mode)throws IOException;

  /**
   * close container
   *
   * @param contPtr
   * @throws IOException
   */
  static native void daosCloseContainer(long contPtr)throws IOException;

  /**
   * close pool
   * @param poolPtr
   * @throws IOException
   */
  static native void daosClosePool(long poolPtr)throws IOException;


  //DAOS FS corresponding methods

  /**
   * set prefix
   * @param dfsPtr
   * @param prefix
   * @throws IOException
   *
   * @return 0 for success, others for failure
   */
  native int dfsSetPrefix(long dfsPtr, String prefix)throws IOException;

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
  native long dfsLookup(long dfsPtr, long parentObjId, String name, int flags, long bufferAddress)throws IOException;

  /**
   * Same as {@link #dfsLookup(long, long, String, int, long)} except parent file is not opened.
   *
   * @param dfsPtr
   * @param path
   * @param flags
   * @param bufferAddress address of direct {@link java.nio.ByteBuffer} for holding all information of
   *                      {@link StatAttributes}
   * @throws IOException
   *
   * @return DAOS FS object id
   */
  native long dfsLookup(long dfsPtr, String path, int flags, long bufferAddress)throws IOException;

  /**
   * get file length for opened FS object
   *
   * @param dfsPtr
   * @param objId
   * @throws IOException
   *
   * @return file length
   */
  native long dfsGetSize(long dfsPtr, long objId)throws IOException;

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
  native long dfsDup(long dfsPtr, long objId, int flags)throws IOException;

  /**
   * release opened FS object
   *
   * @param objId
   * @throws IOException
   */
  native void dfsRelease(long objId)throws IOException;

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
  native long dfsRead(long dfsPtr, long objId, long bufferAddress, long offset, long len, int eventNo)throws IOException;

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
  native long dfsWrite(long dfsPtr, long objId, long bufferAddress, long offset, long len,
                              int eventNo)throws IOException;

  /**
   * read children
   *
   * @param dfsPtr
   * @param objId
   * @param maxEntries, -1 for no limit
   * @return file names separated by ','
   */
  native String dfsReadDir(long dfsPtr, long objId, int maxEntries)throws IOException;

  /**
   * get FS object status attribute into direct {@link java.nio.ByteBuffer}
   * Order of fields to be read.
   *     objId = buffer.getLong();
   *     mode = buffer.getInt();
   *     uid = buffer.getLong();
   *     gid = buffer.getLong();
   *     blockCnt = buffer.getLong();
   *     length = buffer.getLong();
   *     accessTime = buffer.getLong();
   *     modifyTime = buffer.getLong();
   *     createTime = buffer.getLong();
   *     file = buffer.get() > 0;
   * @param dfsPtr
   * @param objId
   * @param bufferAddress
   * @throws IOException
   */
  native void dfsOpenedObjStat(long dfsPtr, long objId, long bufferAddress)throws IOException;

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
  native void dfsSetExtAttr(long dfsPtr, long objId, String name, String value, int flags)throws IOException;

  /**
   * get extended attribute
   *
   * @param dfsPtr
   * @param objId
   * @param name
   * @param expectedValueLen
   * @return
   * @throws IOException
   */
  native String dfsGetExtAttr(long dfsPtr, long objId, String name, int expectedValueLen)throws IOException;

  /**
   * remove extended attribute
   *
   * @param dfsPtr
   * @param objId
   * @param name
   *
   * @throws IOException
   */
  native void dfsRemoveExtAttr(long dfsPtr, long objId, String name)throws IOException;

  /**
   * get chunk size
   *
   * @param objId
   * @return
   * @throws IOException
   */
  static native long dfsGetChunkSize(long objId)throws IOException;

  /**
   * get mode
   *
   * @param objId
   * @return
   * @throws IOException
   */
   static native int dfsGetMode(long objId)throws IOException;

  /**
   * check if it's directory by providing mode
   * @param mode
   * @return
   * @throws IOException
   */
  static native boolean dfsIsDirectory(int mode)throws IOException;

  /**
   * mount FS on container
   *
   * @param poolPtr
   * @param contPtr
   * @param readOnly
   * @return FS client pointer
   */
  static native long dfsMountFs(long poolPtr, long contPtr, boolean readOnly)throws IOException;

  /**
   * mount FS on non-readonly root container
   * @param poolPtr
   * @return
   * @throws IOException
   */
  static native long dfsMountFsOnRoot(long poolPtr)throws IOException;

  /**
   * unmount FS from root container
   * @param poolPtr
   * @throws IOException
   */
  static native void dfsUnmountFsOnRoot(long poolPtr)throws IOException;

  /**
   * unmount FS
   * @param dfsPtr
   * @throws IOException
   */
  static native void dfsUnmountFs(long dfsPtr)throws IOException;

  /**
   * finalize DAOS client
   * @throws IOException
   */
  static native void daosFinalize()throws IOException;


  /**
   * A builder for constructing Java DAOS FS Client. All parameters should be specified here. This builder
   * makes sure single instance of {@link DaosFsClient} per pool and container.
   *
   * Please note that new pool and new container will be created if their ids (poolId and containerId) are {@code null}.
   * //TODO: set default value
   */
  public static class DaosFsClientBuilder implements Cloneable{
    private String poolId;
    private String contId;
    private String ranks = "0";
    private String serverGroup = "daos_server";
    private int poolSvcReplics = 1;
    private int containerFlags = Constants.ACCESS_FLAG_CONTAINER_READWRITE;
    private int poolFlags = Constants.ACCESS_FLAG_POOL_READWRITE;
    private int poolMode = Constants.MODE_POOL_GROUP_READWRITE | Constants.MODE_POOL_OTHER_READWRITE
            | Constants.MODE_POOL_USER_READWRITE;
    private long poolScmSize;
    private long poolNvmeSize;
    private int defaultFileChunkSize = 8192; //8k
    private int defaultFileAccessFlag = Constants.ACCESS_FLAG_FILE_READWRITE;
    private int defaultFileMode = 0755;
    private DaosObjectType defaultFileObjType = DaosObjectType.OC_SX;
    private boolean readOnlyFs = false;
    private boolean shareFsClient = true;

    public DaosFsClientBuilder poolId(String poolId){
      this.poolId = poolId;
      return this;
    }

    public DaosFsClientBuilder containerId(String contId){
      this.contId = contId;
      return this;
    }

    /**
     * one or more ranks separated by ":"
     * @param ranks, default is "0"
     * @return
     */
    public DaosFsClientBuilder ranks(String ranks){
      this.ranks = ranks;
      return this;
    }

    /**
     * set group name of server.
     * @param serverGroup, default is 'daos_server'
     * @return
     */
    public DaosFsClientBuilder serverGroup(String serverGroup){
      this.serverGroup = serverGroup;
      return this;
    }

    /**
     * number of service replics when create pool
     * @param poolSvcReplics, default is 1
     * @return
     */
    public DaosFsClientBuilder poolSvcReplics(int poolSvcReplics){
      this.poolSvcReplics = poolSvcReplics;
      return this;
    }

    /**
     * set container mode when open container.
     * @param containerFlags, should be one of {@link Constants#ACCESS_FLAG_CONTAINER_READONLY},
     *                        {@link Constants#ACCESS_FLAG_CONTAINER_READWRITE} and
     *                        {@link Constants#ACCESS_FLAG_CONTAINER_NOSLIP}
     *        Default value is {@link Constants#ACCESS_FLAG_CONTAINER_READWRITE}
     * @return
     */
    public DaosFsClientBuilder containerFlags(int containerFlags){
      this.containerFlags = containerFlags;
      return this;
    }

    /**
     * set pool mode for creating pool
     * @param poolMode, should be one or combination of below three groups.
     *        <li>
     *                   user:
     *                   {@link Constants#MODE_POOL_USER_READONLY}
     *                   {@link Constants#MODE_POOL_USER_READWRITE}
     *                   {@link Constants#MODE_POOL_USER_EXECUTE}
     *        </li>
     *        <li>
     *                   group:
     *                   {@link Constants#MODE_POOL_GROUP_READONLY}
     *                   {@link Constants#MODE_POOL_GROUP_READWRITE}
     *                   {@link Constants#MODE_POOL_GROUP_EXECUTE}
     *        </li>
     *        <li>
     *                   other:
     *                   {@link Constants#MODE_POOL_OTHER_READONLY}
     *                   {@link Constants#MODE_POOL_OTHER_READWRITE}
     *                   {@link Constants#MODE_POOL_OTHER_EXECUTE}
     *        </li>
     *
     * @return
     */
    public DaosFsClientBuilder poolMode(int poolMode){
      this.poolMode = poolMode;
      return this;
    }

    /**
     * set pool flags for opening pool
     * @param poolFlags
     * should be one of
     * {@link Constants#ACCESS_FLAG_POOL_READONLY}
     * {@link Constants#ACCESS_FLAG_POOL_READWRITE}
     * {@link Constants#ACCESS_FLAG_POOL_EXECUTE}
     *
     * Default is {@link Constants#ACCESS_FLAG_POOL_READWRITE}
     * @return
     */
    public DaosFsClientBuilder poolFlags(int poolFlags){
      this.poolFlags = poolFlags;
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

    /**
     * set default file access flag.
     * @param defaultFileAccessFlag
     * should be one of
     * {@link Constants#ACCESS_FLAG_FILE_CREATE}
     * {@link Constants#ACCESS_FLAG_FILE_READONLY}
     * {@link Constants#ACCESS_FLAG_FILE_READWRITE}
     * {@link Constants#ACCESS_FLAG_FILE_EXCL}
     *
     * default is {@link Constants#ACCESS_FLAG_FILE_READWRITE}
     * @return
     */
    public DaosFsClientBuilder defaultFileAccessFlag(int defaultFileAccessFlag){
      this.defaultFileAccessFlag = defaultFileAccessFlag;
      return this;
    }

    /**
     * set default file mode. You can override this value when create new file by
     * calling {@link DaosFile#createNewFile(int, DaosObjectType, int)}
     * @param defaultFileMode, should be octal value. Default is 0755
     * @return
     */
    public DaosFsClientBuilder defaultFileMode(int defaultFileMode){
      this.defaultFileMode = defaultFileMode;
      return this;
    }

    /**
     * set default file type. You can override this value when create new file by
     * calling {@link DaosFile#createNewFile(int, DaosObjectType, int)}
     * @param defaultFileObjType, default is {@link DaosObjectType#OC_SX}
     * @return
     */
    public DaosFsClientBuilder defaultFileType(DaosObjectType defaultFileObjType){
      this.defaultFileObjType = defaultFileObjType;
      return this;
    }

    /**
     * set default file chunk size. You can override this value when create new file by
     * calling {@link DaosFile#createNewFile(int, DaosObjectType, int)}
     * @param defaultFileChunkSize, default is 8k
     * @return
     */
    public DaosFsClientBuilder defaultFileChunkSize(int defaultFileChunkSize){
      this.defaultFileChunkSize = defaultFileChunkSize;
      return this;
    }

    /**
     * set FS readonly
     * @param readOnlyFs, default is false
     * @return
     */
    public DaosFsClientBuilder readOnlyFs(boolean readOnlyFs){
      this.readOnlyFs = readOnlyFs;
      return this;
    }

    /**
     * share {@link DaosFsClient} instance or not
     * @param shareFsClient, default is true
     * @return
     */
    public DaosFsClientBuilder shareFsClient(boolean shareFsClient){
      this.shareFsClient = shareFsClient;
      return this;
    }

    @Override
    public DaosFsClientBuilder clone()throws CloneNotSupportedException{
      return (DaosFsClientBuilder)super.clone();
    }

    /**
     * Either return existing {@link DaosFsClient} instance or create new instance.
     * @return
     * @throws IOException
     */
    public DaosFsClient build()throws IOException{
      DaosFsClientBuilder copied = (DaosFsClientBuilder)ObjectUtils.clone(this);
      DaosFsClient client;
      if(poolId != null){
        client = getClientForCont(copied);
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
      if(contId == null){
        contId = ROOT_CONT_UUID;
      }
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
