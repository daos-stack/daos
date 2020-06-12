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

package io.daos.obj;

import io.daos.BufferAllocator;
import io.daos.Constants;
import io.daos.DaosIOException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import sun.nio.ch.DirectBuffer;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.List;

public class DaosObject {

  private DaosObjClient client;

  private long contPtr;

  private DaosObjectId oid;

  private long objectPtr = -1;

  private static final Logger log = LoggerFactory.getLogger(DaosObject.class);

  /**
   * construct new instance of DaosObject with given <code>oid</code>.
   * <code>oid</code> must be encoded.
   *
   * @param client
   * initialized DAOS object client
   * @param oid
   * encoded DAOS object id
   */
  protected DaosObject(DaosObjClient client, DaosObjectId oid) {
    this.oid = oid;
    this.client = client;
    this.contPtr = client.getContPtr();
    if (!oid.isEncoded()) {
      throw new IllegalArgumentException("DAOS object ID should be encoded.");
    }
  }

  /**
   * open object with default mode, {@linkplain OpenMode#UNKNOWN}.
   *
   * @throws IOException
   */
  public void open() throws IOException {
    open(OpenMode.UNKNOWN);
  }

  /**
   * open object with given <code>mode</code> if it hasn't been opened yet.
   *
   * @param mode
   * open mode, see {@link OpenMode}
   * @throws IOException
   */
  public void open(OpenMode mode) throws IOException {
    if (objectPtr == -1) {
      DirectBuffer buffer = (DirectBuffer) oid.getBuffer();
      objectPtr = client.openObject(contPtr, buffer.address(), mode.getValue());
    }
  }

  public void punchObject() throws IOException {
    checkOpen();
    client.punchObject(objectPtr, 0);
  }

  public void punchObjectDkeys(List<String> dkeys) throws IOException {
    checkOpen();
    if (dkeys.isEmpty()) {
      return;
    }
    int bufferLen = 0;
    for (String key : dkeys) {
      bufferLen += (key.length() + 4);
    }
    if (bufferLen == 0) {
      log.warn("no dkeys specified when punch object dkeys");
      return;
    }
    ByteBuffer buffer = BufferAllocator.directBuffer(bufferLen);
    buffer.order(Constants.DEFAULT_ORDER);
    for (String key : dkeys) {
      buffer.putInt(key.length()).put(key.getBytes());
    }
    client.punchObjectDkeys(objectPtr, 0, dkeys.size(), ((DirectBuffer)buffer).address(), bufferLen);
  }

  private void checkOpen() throws IOException {
    if (objectPtr == -1) {
      throw new DaosIOException("object is not open");
    }
  }

  /**
   * close object if it's open.
   *
   * @throws IOException
   */
  public void close() throws IOException {
    if (objectPtr != -1) {
      client.closeObject(objectPtr);
    }
  }
}
