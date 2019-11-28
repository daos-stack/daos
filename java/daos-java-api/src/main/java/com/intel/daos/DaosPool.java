package com.intel.daos;

import org.apache.hadoop.util.ShutdownHookManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.util.concurrent.ConcurrentHashMap;

/**
 * This class represents a daos pool. Pool is the highest level in native daos,
 * inside pool you can open containers.
 */
public class DaosPool {
  private boolean isClose =false;

  private String uuid;

  private String  svc;

  private long poh;

  private final ConcurrentHashMap<String, DaosContainer> containers;

  private DaosPoolMode mode;

  private final DaosSession parent;

  private final static Logger LOG =
          LoggerFactory.getLogger(DaosPool.class);

  private final Runnable hook = () -> {
    if (!disconPool()) {
      LOG.error("Daos disconnect pool failed ");
    }
    LOG.debug("Closed daos pool ");
  };

  protected DaosPool(
          final DaosSession parent,
          final String uuid,
          final DaosPoolMode mode,
          final String svc)
          throws DaosNativeException {
    this.uuid = uuid;
    this.mode = mode;
    this.parent = parent;
    this.svc = svc;
    poh = DaosJNI.daosPoolConnect(this.uuid, this.mode.getValue(), this.svc);
    ShutdownHookManager.get().addShutdownHook(
            hook,
            DaosJNI.POOL_SHUTDOWN_PRIO
    );
    containers = new ConcurrentHashMap<>();
  }

  public DaosPoolMode getMode() {
    return mode;
  }

  public long getHandle() {
    return poh;
  }

  public DaosContainer getContainer(
          final String contUUID,
          final DaosContainerMode contMode,
          final boolean isCreate) throws IOException {
    String key = contUUID + "_" + contMode.getValue();
    DaosContainer cont = containers.computeIfAbsent(key, (String k) -> {
      try {
        return new DaosContainer(this, contUUID, contMode, isCreate);
      } catch (IOException e) {
        return null;
      }
    });
    if (cont == null) {
      throw new DaosNativeException("Failed to get container.");
    }
    return cont;
  }

  protected void deleteContainer(final DaosContainer container) {
    containers.remove(container.toString());
  }

  public void disconnect() throws IOException {
    containers.forEachValue(1, (DaosContainer cont) -> {
      try{
        cont.close();
      } catch (IOException e) {
        LOG.error(e.getMessage());
      }
    });
    hook.run();
    ShutdownHookManager instance = ShutdownHookManager.get();
    if (!instance.isShutdownInProgress()) {
      instance.removeShutdownHook(hook);
    }
    parent.deletePool(this);
  }

  public boolean destroyPool(){
    int rc = -1;
    try {
      rc = DaosJNI.daosPoolDestroy(this.uuid);
    } catch (DaosNativeException e) {
      LOG.error(e.getMessage());
    }
    return rc == 0;
  }

  public String toString() {
    return uuid + mode;
  }

  public String getUuid() {
    return uuid;
  }

  public String getSvc() {
    return svc;
  }

  public boolean disconPool(){
    if(isClose){
      LOG.info("Daos close container already close  ");
      return true;
    }
    int rc = DaosJNI.daosPoolDisconnect(poh);
    if(rc!=0){
      LOG.error("Daos disconnect pool " + uuid + " failed with " + rc);
      return false;
    }
    isClose=true;
    return true;
  }

  public ConcurrentHashMap<String, DaosContainer> getContainers() {
    return containers;
  }
}