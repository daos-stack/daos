package com.intel.daos;

import org.apache.hadoop.util.ShutdownHookManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;

/**
 * Abstract parent class of DaosFile and DaosDirectory.
 */
public abstract class DaosFSEntry {

  private long handle;

  private String path;

  private final static Logger LOG = LoggerFactory.getLogger(DaosFSEntry.class);

  private final Runnable hook = () -> {
    LOG.debug("closing " + path);
    int rc = DaosJNI.daosFSClose(handle);
    if (rc !=DaosBaseErr.SUCCESS.getValue()) {
      LOG.error("Failed to close " + path + " with " + rc);
    }
  };

  public long getHandle() {
    return handle;
  }

  protected void setHandle(long handle) {
    this.handle = handle;
  }

  public String getPath() {
    return path;
  }

  protected void setPath(String path) {
    this.path = path;
  }

  protected void registerHook() {
    ShutdownHookManager.get().addShutdownHook(
        hook,
        DaosJNI.ENTRY_SHUTDOWN_PRIO
    );
  }

  public void close() throws IOException {
    hook.run();
    ShutdownHookManager instance = ShutdownHookManager.get();
    if (!instance.isShutdownInProgress()) {
      instance.removeShutdownHook(hook);
    }
  }
}
