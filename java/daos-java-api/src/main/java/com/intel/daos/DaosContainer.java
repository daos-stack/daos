package com.intel.daos;

import org.apache.hadoop.util.ShutdownHookManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.util.concurrent.ConcurrentHashMap;

/**
 * This class represents a daos container, inside which users can open objects.
 * Currently snapshots are not supported yet.
 */
public class DaosContainer {
  private boolean isClose=false;

  private String uuid;

  private long poh;

  private long coh;

  private DaosContainerMode mode;

  private final DaosPool parent;

  private final ConcurrentHashMap<String, DaosKeyValue> kvMap;

  private final ConcurrentHashMap<String, DaosArray> arrayMap;

  private final static Logger LOG = LoggerFactory.getLogger(DaosContainer.class);

  private final Runnable hook = () -> {
    if (!closeCont()) {
      LOG.error("Daos close container failed ");
    }
    LOG.debug("Closed daos container ");
  };

  public long getPoolHandle() {
    return poh;
  }

  public long getHandle() {
    return coh;
  }

  public DaosContainerMode getMode() {
    return mode;
  }

  protected DaosContainer(
      final DaosPool parent,
      final String uuid,
      final DaosContainerMode mode,
      final boolean isCreate) throws DaosNativeException {
    this.uuid = uuid;
    this.mode = mode;
    this.parent = parent;
    poh = parent.getHandle();
    try {
      if(isCreate){
        LOG.info("daos container create ");
        int rc = DaosJNI.daosContCreate(poh, uuid);
        if (rc == DaosBaseErr.DER_NONEXIST.getValue()) {
          throw new DaosNativeException(
                  "Daos create container failed with " + rc);
        }
        coh = DaosJNI.daosContOpen(poh, uuid, mode.getValue());
        if(coh < 0){
          LOG.info("daos_cont_open() failed ; rc = " +coh);
        }
      }else{
        coh = DaosJNI.daosContOpen(poh, uuid, mode.getValue());
      }
    } catch (DaosNativeException e) {
      throw e;
    }
    ShutdownHookManager.get().addShutdownHook(
        hook,
        DaosJNI.CONT_SHUTDOWN_PRIO
    );
    kvMap = new ConcurrentHashMap<>();
    arrayMap = new ConcurrentHashMap<>();
  }

  public DaosKeyValue getKV(
      final long id,
      final int objMode,
      final int ofeat,
      final int cid) throws IOException {
    String oid = id + "_" + objMode + "_" + ofeat + "_" + cid;
    DaosKeyValue kv = kvMap.computeIfAbsent(oid, (String k) -> {
      try {
        return new DaosKeyValue(
          this,
          poh,
          coh,
          id,
          objMode,
          ofeat,
          cid
        );
      } catch (IOException e) {
        return null;
      }
    });
    if (kv == null) {
      throw new DaosNativeException("Failed to get kv object.");
    }
    return kv;
  }

  public DaosArray getArray(
      final long id,
      final int objMode,
      final int ofeat,
      final int cid) throws IOException {
    String oid = id + "_" + objMode + "_" + ofeat + "_" + cid;
    DaosArray array = arrayMap.computeIfAbsent(oid, (String k) -> {
      try {
        return new DaosArray(
          this,
          poh,
          coh,
          id,
          objMode,
          ofeat,
          cid
        );
      } catch (IOException e) {
        return null;
      }
    });
    if (array == null) {
      throw new DaosNativeException("Failed to get kv object.");
    }
    return array;
  }

  protected void deleteObject(final DaosObject object) {
    kvMap.remove(object.toString());
    arrayMap.remove(object.toString());
  }

  public void close() throws IOException {
    kvMap.forEachValue(1, ((DaosKeyValue kv) ->{
      try {
        kv.close();
      } catch (IOException e) {
        LOG.error(e.getMessage());
      }
    }));
    arrayMap.forEachValue(1, ((DaosArray array) ->{
      try {
        array.close();
      } catch (IOException e) {
        LOG.error(e.getMessage());
      }
    }));
    hook.run();
    ShutdownHookManager instance = ShutdownHookManager.get();
    if (!instance.isShutdownInProgress()) {
      instance.removeShutdownHook(hook);
    }
    parent.deleteContainer(this);
  }

  public String toString() {
    return uuid + "_" + mode;
  }

  public String getUuid() {
    return uuid;
  }

  public DaosPool getPool(){
    return parent;
  }

  public boolean closeCont(){
    if(isClose){
      LOG.info("Daos close container already close  ");
      return true;
    }
    int rc = DaosJNI.daosContClose(coh);
    if(rc!=0){
      LOG.error("Daos close container " + uuid + " failed with " + rc);
      return false;
    }
    isClose=true;
    return true;
  }
}