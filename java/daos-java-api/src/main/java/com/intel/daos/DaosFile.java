package com.intel.daos;

import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * This class represents a file in a daos file system.
 * Currently supported operations are read, write, and getSize;
 */
public class DaosFile extends DaosFSEntry {

  protected DaosFile(
      String path,
      boolean readOnly) throws IOException {
    setPath(path);
    long handle = DaosJNI.daosFSOpenFile(path, readOnly);
    if(handle < 0){
      throw DaosUtils.translateException((int)handle, "open file failed , path = " + path);
    }else {
      setHandle(handle);
    }
    registerHook();
  }

  protected DaosFile(
      String path,
      int mode,
      long chunkSize,
      DaosObjClass cid) throws IOException {
    setPath(path);
    long handle = DaosJNI.daosFSCreateFile(
            path,
            mode,
            chunkSize,
            cid.getValue());
    if(handle<0){
      throw DaosUtils.translateException((int)handle, "create file failed , path = " + path);
    }else{
      setHandle(handle);
    }
    registerHook();
  }

  public int read(
      long index,
      ByteBuffer buffer) throws IOException {
    return DaosJNI.daosFSRead(getHandle(), index, buffer);
  }

  public int write(
      long index,
      ByteBuffer buffer,
      int length) throws IOException {
    return DaosJNI.daosFSWrite(
      getHandle(),
      index,
      buffer,
      0,
      length
    );
  }

  public long size() throws IOException {
    return DaosJNI.daosFSGetSize(getHandle());
  }
}
