package com.intel.daos;

import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * Daos array.
 */
public class DaosArray extends DaosObject {

  protected DaosArray(
        final DaosContainer parent,
        final long poh,
        final long coh,
        final long id,
        final int mode,
        final int ofeat,
        final int cid) throws IOException {
    super(parent, poh, coh, id, mode, ofeat, cid);
  }

  public void write(
        final String dkey,
        final String akey,
        final int index,
        final int size,
        final ByteBuffer buffer) throws DaosNativeException {
    int rc = DaosJNI.daosObjectUpdateArray(
          getHandle(),
          dkey,
          akey,
          index,
          size,
          buffer
    );
    if (rc < 0) {
      throw new DaosNativeException("daos failed to update with " + rc);
    }
  }

  public void write(
        final String dkey,
        final String akey,
        final int index,
        final int size,
        final ByteBuffer buffer,
        DaosEventQueue eq) throws DaosNativeException {
    long ioreq = DaosJNI.allocateIOReq(
        dkey.length() + akey.length(),
        eq.getHandle()
    );
    eq.enqueue(ioreq);
    int rc = DaosJNI.daosObjectUpdateArrayAsync(
        getHandle(),
        dkey,
        akey,
        index,
        size,
        buffer,
        ioreq
    );
    if (rc < 0) {

      throw new DaosNativeException("daos failed to update with " + rc);
    }
  }


  public long read(
        final String dkey,
        final String akey,
        final int index,
        final int number,
        ByteBuffer buffer) throws DaosNativeException {
    return DaosJNI.daosObjectFetchArray(
        getHandle(),
        dkey,
        akey,
        index,
        number,
        buffer
    );
  }

  public void read(
        final String dkey,
        final String akey,
        final int index,
        final int number,
        ByteBuffer buffer,
        DaosEventQueue eq) throws DaosNativeException {
    long ioreq = DaosJNI.allocateIOReq(
        dkey.length() + akey.length(),
        eq.getHandle()
    );
    eq.enqueue(ioreq);
    DaosJNI.daosObjectFetchArrayAsync(
        getHandle(),
        dkey,
        akey,
        index,
        number,
        buffer,
        ioreq
    );
  }

  public void listRecx(
        final String dkey,
        final String akey,
        final boolean inIncreasingOrder) {
    DaosJNI.daosObjListRecx(getHandle(), dkey, akey, inIncreasingOrder);
  }
}
