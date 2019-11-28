package com.intel.daos;

import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * Daos key value.
 */
public class DaosKeyValue extends DaosObject {

  protected DaosKeyValue(
      final DaosContainer parent,
      final long poh,
      final long coh,
      final long id,
      final int mode,
      final int ofeat,
      final int cid) throws IOException {
    super(parent, poh, coh, id, mode, ofeat, cid);
  }

  public void put(
      final String dkey,
      final String akey,
      final ByteBuffer buffer) throws IOException {
    int rc = DaosJNI.daosObjectUpdateSingle(getHandle(), dkey, akey, buffer);
    if (rc != DaosBaseErr.SUCCESS.getValue()) {
      throw new DaosNativeException("daos failed to update with " + rc);
    }
  }

  public void put(
      final String dkey,
      final String akey,
      final ByteBuffer buffer,
      DaosEventQueue eq) throws IOException {
    long ioreq = DaosJNI.allocateIOReq(
        dkey.length() + akey.length(),
        eq.getHandle()
    );
    eq.enqueue(ioreq);
    int rc = DaosJNI.daosObjectUpdateSingleAsync(
        getHandle(),
        dkey,
        akey,
        buffer,
        ioreq
    );
    if (rc != DaosBaseErr.SUCCESS.getValue()) {
      throw new DaosNativeException("daos failed to update with " + rc);
    }
  }

  public long get(
      final String dkey,
      final String akey,
      ByteBuffer buffer) throws IOException {
    return DaosJNI.daosObjectFetchSingle(getHandle(), dkey, akey, buffer);
  }

  public void get(
      final String dkey,
      final String akey,
      ByteBuffer buffer,
      DaosEventQueue eq) throws IOException {
    long ioreq = DaosJNI.allocateIOReq(
        dkey.length() + akey.length(),
        eq.getHandle()
    );
    eq.enqueue(ioreq);
    DaosJNI.daosObjectFetchSingleAsync(getHandle(), dkey, akey, buffer, ioreq);
  }
}
