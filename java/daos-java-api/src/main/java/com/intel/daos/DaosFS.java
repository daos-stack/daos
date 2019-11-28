package com.intel.daos;

import org.apache.hadoop.util.ShutdownHookManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.util.UUID;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantReadWriteLock;

/**
 * This singleton is a wrapper for daos file system.
 */
public final class DaosFS {

  private DaosContainer mycontainer;

  private final DaosPool mypool;

  private final long scm;
  private final long nvme;
  private final String pooluuid;
  private final String containeruuid;
  private final boolean isCreatePool;
  private final String containerpath;
  private final boolean isCreateCont;
  private final String svc;
  private DaosPoolMode daospoolmode;
  private DaosContainerMode daosContainerMode;

  private static DaosFS instance = null;

  private final static ReentrantReadWriteLock RWLOCK = new ReentrantReadWriteLock();
  private final boolean isOnlyread;


  private final static Lock RL = RWLOCK.readLock();

  private final static Lock WL = RWLOCK.writeLock();

  private final static Logger LOG = LoggerFactory.getLogger(DaosFS.class);

  /**
   */
  public static class Builder {
    private long scm;
    private long nvme;
    private String pooluuid;
    private String containeruuid;
    private boolean isOnlyread;
    private boolean isCreatePool;
    private String containerpath;
    private boolean isCreateCont;
    private String svc;
    private DaosPoolMode daospoolmode;
    private DaosContainerMode daosContainerMode;



    public Builder(boolean isCreatePool, boolean isOnlyread, boolean isCreateCont) {
      this.isCreatePool = isCreatePool;
      this.isOnlyread = isOnlyread;
      this.isCreateCont = isCreateCont;
    }


    public Builder setScm(long scmsize) {
      this.scm = scmsize;
      return this;
    }

    public Builder setNvme(long nvmesize) {
      this.nvme = nvmesize;
      return this;
    }

    public Builder setPooluuid(String pool) {
      this.pooluuid = pool;
      return this;
    }

    public Builder setContaineruuid(String container) {
      this.containeruuid = container;
      return this;
    }

    public Builder setContainerpath(String path) {
      this.containerpath = path;
      return this;
    }

    public Builder setSvc(String svcRank) {
      this.svc = svcRank;
      return this;
    }

    public Builder setDaosPoolMode(DaosPoolMode daospoolmode){
      this.daospoolmode = daospoolmode;
      return this;
    }

    public Builder setDaosContainerMode(DaosContainerMode daosContainerMode){
      this.daosContainerMode = daosContainerMode;
      return  this;
    }


    public DaosFS build() throws IOException {
      WL.lock();
      try {
        if (instance == null) {
          instance = new DaosFS(this);
        }
      } finally {
        WL.unlock();
      }
      return instance;
    }
  }

  private final Runnable hook = () -> {
    LOG.info("Umount daos fs!");
    int rc = DaosJNI.daosFSUmount();
    if (rc < 0) {
      LOG.error("Failed to umount daos fs with {}", rc);
    }
  };

  private DaosFS(Builder builder)
          throws IOException {
    this.scm = builder.scm;
    this.nvme = builder.nvme;
    this.pooluuid = builder.pooluuid;
    this.containeruuid = builder.containeruuid;
    this.isCreatePool = builder.isCreatePool;
    this.isOnlyread = builder.isOnlyread;
    this.containerpath = builder.containerpath;
    this.isCreateCont = builder.isCreateCont;
    this.daosContainerMode = builder.daosContainerMode;
    this.daospoolmode = builder.daospoolmode;
    this.svc = builder.svc;
    int rc = -1;
    DaosSession session = DaosSession.getInstance();
    if(this.isCreatePool){
      if(this.scm != 0L || this.nvme != 0L){
        this.mypool = session.createAndGetPool(
                this.scm,
                this.nvme,
                DaosPoolMode.DAOS_PC_RW);
        this.mycontainer = this.mypool.getContainer(
                this.mypool.getUuid(),
                DaosContainerMode.DAOS_COO_RW,
                this.isCreateCont);
        rc = DaosJNI.daosFSMount(
                this.mycontainer.getPoolHandle(),
                this.mycontainer.getHandle(),
                this.isOnlyread
        );
      }else{
        this.mypool = session.createAndGetPool(
                DaosUtils.DEFAULT_DAOS_POOL_SCM,
                DaosUtils.DEFAULT_DAOS_POOL_NVME,
                DaosPoolMode.DAOS_PC_RW);
        this.mycontainer = this.mypool.getContainer(
                this.mypool.getUuid(),
                DaosContainerMode.DAOS_COO_RW,
                this.isCreateCont);
        rc = DaosJNI.daosFSMount(
                this.mycontainer.getPoolHandle(),
                this.mycontainer.getHandle(),
                this.isOnlyread
        );
      }
    }else{
      if(this.pooluuid == null ||
              (this.containeruuid == null &&
                      this.containerpath ==null)){
        throw new DaosNativeException(
                "pool_uuid and container_uuid can't empty when connecting to DAOS");
      }
      this.mypool = session.getPool(this.pooluuid, DaosPoolMode.DAOS_PC_RW, this.svc);
      if(this.containeruuid != null){
        this.mycontainer = this.mypool.getContainer(
                this.containeruuid,
                DaosContainerMode.DAOS_COO_RW,
                this.isCreateCont);
      }else {
        String uuid = UUID.nameUUIDFromBytes(
                this.containerpath.getBytes()).toString();
        this.mycontainer = this.mypool.getContainer(
                uuid,
                DaosContainerMode.DAOS_COO_RW,
                this.isCreateCont);
      }
      rc = DaosJNI.daosFSMount(
              this.mycontainer.getPoolHandle(),
              this.mycontainer.getHandle(),
              this.isOnlyread);
    }
    if (rc < 0) {
      throw new DaosNativeException("failed to mount dfs with " + rc);
    }
    ShutdownHookManager.get().addShutdownHook(
            hook,
            DaosJNI.DFS_OBJ_SHUTDOWN_PRIO
    );
  }

  public boolean closeContainer(){
    RL.lock();
    try {
      return  this.mycontainer.closeCont();
    } finally {
      RL.unlock();
    }
  }

  public boolean disconnectPool(){
    RL.lock();
    try {
      return  this.mycontainer.getPool().disconPool();
    } finally {
      RL.unlock();
    }
  }


  public boolean destroyPool(){
    RL.lock();
    try {
      if(this.mypool==null) {
        return false;
      }
      return  this.mypool.destroyPool();
    } finally {
      RL.unlock();
    }

  }

  public void unmount() {
    WL.lock();
    try {
      hook.run();
      ShutdownHookManager manager = ShutdownHookManager.get();
      if (!manager.isShutdownInProgress()) {
        manager.removeShutdownHook(hook);
      }
      instance = null;
    } finally {
      WL.unlock();
    }
  }

  public boolean isDir(String path)
          throws DaosNativeException, DaosJavaException {
    RL.lock();
    try {
      if (instance == null) {
        throw new DaosJavaException("DFS is not mounted.");
      }
      return DaosJNI.daosFSIsDir(path);
    } finally {
      RL.unlock();
    }
  }

  public boolean ifExist(String path)
          throws DaosNativeException, DaosJavaException {
    RL.lock();
    try {
      if (instance == null) {
        throw new DaosJavaException("DFS is not mounted.");
      }
      return DaosJNI.daosFsIfExist(path);
    } finally {
      RL.unlock();
    }
  }

  public DaosFile getFile(String path, boolean readOnly)
      throws IOException {
    RL.lock();
    try {
      if (instance == null) {
        throw new DaosJavaException("DFS is not mounted.");
      }
      return new DaosFile(path, readOnly);
    } finally {
      RL.unlock();
    }
  }


  public DaosFile createFile(
      String path,
      int mode,
      long chunkSize,
      DaosObjClass cid)
      throws IOException {
    RL.lock();
    try {
      if (instance == null) {
        throw new DaosJavaException("DFS is not mounted.");
      }
      return new DaosFile(path, mode, chunkSize, cid);
    } finally {
      RL.unlock();
    }
  }

  public DaosDirectory getDir(String path, boolean readOnly)
      throws IOException {
    RL.lock();
    try {
      if (instance == null) {
        throw new DaosJavaException("DFS is not mounted.");
      }
      return new DaosDirectory(path, readOnly);
    } finally {
      RL.unlock();
    }
  }

  public DaosDirectory createDir(String path, int mode)
      throws IOException {
    RL.lock();
    try {
      if (instance == null) {
        throw new DaosJavaException("DFS is not mounted.");
      }
      return new DaosDirectory(path, mode);
    } finally {
      RL.unlock();
    }
  }

  public long getSize(String path)
      throws IOException {
    RL.lock();
    try {
      if (instance == null) {
        throw new DaosJavaException("DFS is not mounted.");
      }
      return DaosJNI.daosFSGetSize(path);
    } finally {
      RL.unlock();
    }
  }

  public String[] listDir(String path)
      throws IOException {
    RL.lock();
    try {
      if (instance == null) {
        throw new DaosJavaException("DFS is not mounted.");
      }
      String dirs = DaosJNI.daosFSListDir(path);
      return dirs.isEmpty() ? new String[]{} : dirs.split(",");
    } finally {
      RL.unlock();
    }
  }

  public int move(String path, String newPath)
      throws IOException {
    RL.lock();
    try {
      if (instance == null) {
        throw new DaosJavaException("DFS is not mounted.");
      }
      return DaosJNI.daosFSMove(path, newPath);
    } finally {
      RL.unlock();
    }
  }

  public int remove(String path)
      throws IOException {
    RL.lock();
    try {
      if (instance == null) {
        throw new DaosJavaException("DFS is not mounted.");
      }
      return DaosJNI.daosFSRemove(path);
    } finally {
      RL.unlock();
    }
  }

  private void remount(DaosContainer container) throws IOException {
    this.mycontainer = container;
    int rc = DaosJNI.daosFSMount(
            container.getPoolHandle(),
            container.getHandle(),
            false
    );
    if (rc !=DaosBaseErr.SUCCESS.getValue()) {
      throw new DaosNativeException("failed to mount dfs with " + rc);
    }
    ShutdownHookManager.get().addShutdownHook(
            hook,
            DaosJNI.DFS_OBJ_SHUTDOWN_PRIO
    );
  }

  public DaosContainer getMycontainer() {
    return mycontainer;
  }

  public DaosPool getMypool() {
    return mypool;
  }
}
