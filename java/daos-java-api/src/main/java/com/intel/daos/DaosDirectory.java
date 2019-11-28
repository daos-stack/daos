package com.intel.daos;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;

/**
 * This class represents a directory in daos file system. Users can use this to
 * alter contents under it, such as move and rename files. This way handles are
 * re-used, thus will be slightly faster.
 */
public class DaosDirectory extends DaosFSEntry {

  private final static Logger LOG = LoggerFactory.getLogger(DaosDirectory.class);

  protected DaosDirectory(
      String path,
      int mode) throws IOException {
    setPath(path);
    long handle = DaosJNI.daosFSCreateDir(path, mode);
    if(handle<0){
      throw DaosUtils.translateException((int)handle, "create  dir failed , path = " + path);
    }else{
      setHandle(handle);
    }
    registerHook();
  }

  protected DaosDirectory(
      String path,
      boolean readOnly) throws IOException {
    setPath(path);
    long handle = DaosJNI.daosFSOpenDir(path, readOnly);
    if(handle<0){
      throw DaosUtils.translateException((int)handle, "open dir failed , path = " + path);
    }else{
      setHandle(handle);
    }
    registerHook();
  }

  public void remove(String name) throws IOException {
    int rc = DaosJNI.daosFSRemove(getHandle(), name);
    if (rc !=DaosBaseErr.SUCCESS.getValue()) {
      throw new DaosNativeException("DFS failed to remove with " + rc);
    }
  }

  public void move(
      String name,
      DaosDirectory dest,
      String newName) throws IOException {
    int rc = DaosJNI.daosFSMove(
        getHandle(),
        name,
        dest.getHandle(),
        newName
    );
    if (rc !=DaosBaseErr.SUCCESS.getValue()) {
      throw new DaosNativeException("DFS failed to move with " + rc);
    }
  }

  public void rename(
      String name,
      String newName) throws IOException {
    int rc = DaosJNI.daosFSMove(
        getHandle(),
        name,
        getHandle(),
        newName
    );
    if (rc !=DaosBaseErr.SUCCESS.getValue()) {
      throw new DaosNativeException("DFS failed to remove with " + rc);
    }
  }

  public String[] list() throws IOException {
    String dirs = DaosJNI.daosFSListDir(getHandle());
    return dirs.isEmpty() ? new String[]{} : dirs.split(",");
  }
}
