package com.intel.daos;

import org.apache.hadoop.util.ShutdownHookManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;

/**
 * This class represents a daos object. Asynchronous API are also supported.
 */
public class DaosObject {

  private long oh;

  private String objectID;

  private final DaosContainer parent;

  private final static Logger LOG = LoggerFactory.getLogger(DaosObject.class);

  private final Runnable hook = () -> {
    int rc = DaosJNI.daosObjectClose(oh);
    if (rc != DaosBaseErr.SUCCESS.getValue()) {
      LOG.error("Failed to close object with rc = " + rc);
    }
    LOG.debug("Closed object " + toString());
  };

  protected DaosObject(
      final DaosContainer parent,
      final long poh,
      final long coh,
      final long id,
      final int mode,
      final int ofeat,
      final int cid) throws IOException {
    objectID = id + "_" + mode + "_" + ofeat + "_" + cid;
    oh = DaosJNI.daosObjectOpen(poh, coh, id, mode, ofeat, cid);
    this.parent = parent;
    ShutdownHookManager.get().addShutdownHook(
        hook,
        DaosJNI.DFS_OBJ_SHUTDOWN_PRIO
    );
  }

  public long getHandle() {
    return oh;
  }

  public void close() throws IOException {
    hook.run();
    parent.deleteObject(this);
    ShutdownHookManager instance = ShutdownHookManager.get();
    if (!instance.isShutdownInProgress()) {
      instance.removeShutdownHook(hook);
    }
  }

  public void punch() throws IOException {
    int rc = DaosJNI.daosObjectPunch(oh);
    if (rc != DaosBaseErr.SUCCESS.getValue()) {
      throw new DaosNativeException(
        "daos native error: failed to punch with " + rc
      );
    }
  }

  public void punchDkeys(String[] dkeys) throws IOException {
    int rc = DaosJNI.daosObjectPunchDkeys(oh, dkeys);
    if (rc != DaosBaseErr.SUCCESS.getValue()) {
      throw new DaosNativeException(
        "daos native error: failed to punch with " + rc
      );
    }
  }

  public void punchAkeys(String dkey, String[] akeys)
      throws IOException {
    int rc = DaosJNI.daosObjectPunchAkeys(oh, dkey, akeys);
    if (rc != DaosBaseErr.SUCCESS.getValue()) {
      throw new DaosNativeException(
        "daos native error: failed to punch with " + rc
      );
    }
  }

  public String[] listDkey()
      throws IOException {
    return DaosJNI.daosObjectListDkey(oh).split(",");
  }

  public String[] listAkey(final String dkey)
      throws IOException {
    return DaosJNI.daosObjectListAkey(oh, dkey).split(",");
  }

  public String toString() {
    return objectID;
  }

}
