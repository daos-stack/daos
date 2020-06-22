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
import io.daos.obj.attr.DaosObjectAttribute;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import sun.nio.ch.DirectBuffer;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * //TODO: buffer management
 */
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
   * is object open
   *
   * @return true for object open; false for not open or open failed
   */
  public boolean isOpen() {
    return objectPtr != -1;
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

  public void punch() throws IOException {
    if (objectPtr != -1) {
      client.punchObject(objectPtr, 0);
    }
  }

  private ByteBuffer encodeKeys(List<String> keys) throws IOException {
    int bufferLen = 0;
    for (String key : keys) {
      bufferLen += (key.length() + Constants.ENCODED_LENGTH_KEY);
    }
    if (bufferLen == 0) {
      return null;
    }
    // encode keys to buffer
    ByteBuffer buffer = BufferAllocator.directBuffer(bufferLen);
    buffer.order(Constants.DEFAULT_ORDER);
    int capacity = buffer.capacity();
    int keyLen;
    for (String key : keys) {
      byte[] bytes = key.getBytes(Constants.KEY_CHARSET);
      if (bytes.length > Short.MAX_VALUE) {
        throw new IllegalArgumentException("key length in " + Constants.KEY_CHARSET +
                      " should not exceed " + Short.MAX_VALUE);
      }
      keyLen = (Constants.ENCODED_LENGTH_KEY + bytes.length);
      if ((buffer.position() + keyLen) > capacity) { // in case there is non ascii char
        capacity *= 2;
        ByteBuffer newBuffer = BufferAllocator.directBuffer(capacity);
        newBuffer.order(Constants.DEFAULT_ORDER);
        buffer.flip();
        newBuffer.put(buffer);
        buffer = newBuffer;
      }
      buffer.putShort((short)bytes.length).put(bytes);
    }
    buffer.flip();
    return buffer;
  }

  public void punchDkeys(List<String> dkeys) throws IOException {
    checkOpen();
    ByteBuffer buffer = encodeKeys(dkeys);
    if (buffer == null) {
      log.warn("no dkeys specified when punch object dkeys");
      return;
    }
    client.punchObjectDkeys(objectPtr, 0, dkeys.size(), ((DirectBuffer)buffer).address(), buffer.limit());
  }

  public void punchAkeys(String dkey, List<String> akeys) throws IOException {
    checkOpen();
    if (akeys.isEmpty()) {
      log.warn("no akeys specified when punch object akeys");
      return;
    }
    int nbrOfAkyes = akeys.size();
    List<String> allKeys = new ArrayList<>(nbrOfAkyes + 1);
    allKeys.add(dkey);
    allKeys.addAll(akeys);
    ByteBuffer buffer = encodeKeys(allKeys);
    client.punchObjectAkeys(objectPtr, 0, nbrOfAkyes, ((DirectBuffer)buffer).address(), buffer.limit());
  }

  public DaosObjectAttribute queryAttribute() throws IOException {
    checkOpen();
    byte[] bytes = client.queryObjectAttribute(objectPtr);
    return DaosObjectAttribute.parseFrom(bytes);
  }

  /**
   * fetch object with given <code>desc</code>. User should get result from each entry, like below code snippet.
   * <code>
   *   for (Entry e : desc.getAkeyEntries()) {
   *     int actualSize = e.getActualSize();
   *     if (actualSize > 0 ) {
   *       e.read(bytes) // or e.read(byteBuffer)
   *     }
   *   }
   * </code>
   *
   * @param desc
   * {@link IODataDesc} describes list of {@link io.daos.obj.IODataDesc.Entry} to fetch akeyes' data under dkey.
   * @throws IOException
   */
  public void fetch(IODataDesc desc) throws IOException {
    checkOpen();
    desc.encode();

    ByteBuffer descBuffer = desc.getDescBuffer();
    client.fetchObject(objectPtr, 0, desc.getNbrOfEntries(), ((DirectBuffer)descBuffer).address(),
      ((DirectBuffer)desc.getDataBuffer()).address());
    desc.parseResult();
  }

  /**
   * update object with given <code>desc</code>.
   *
   * @param desc
   * {@link IODataDesc} describes list of {@link io.daos.obj.IODataDesc.Entry} to update on dkey.
   * @throws IOException
   */
  public void update(IODataDesc desc) throws IOException {
    checkOpen();
    desc.encode();

    client.updateObject(objectPtr, 0, desc.getNbrOfEntries(), ((DirectBuffer)desc.getDescBuffer()).address(),
      ((DirectBuffer)desc.getDataBuffer()).address());
  }

  /**
   * list object dkeys.
   *
   * @param desc
   * @return
   * @throws IOException
   */
  public List<String> listDkeys(IOKeyDesc desc) throws IOException {
    checkOpen();
    desc.encode();
    // TODO: check completion of list
    // TODO: key2big, double size
    client.listObjectDkeys(objectPtr, ((DirectBuffer)desc.getDescBuffer()).address(),
      ((DirectBuffer)desc.getKeyBuffer()).address(), desc.getKeyBuffer().capacity(),
      ((DirectBuffer)desc.getAnchorBuffer()).address(),
      desc.getBatchSize());
    ByteBuffer anchorBuffer = desc.getAnchorBuffer();
    anchorBuffer.position(0);
    System.out.println(anchorBuffer.get());
    return desc.parseResult();
  }

  public List<String> listAkeys(IOKeyDesc desc) throws IOException {
    checkOpen();
    if (desc.getDkey() == null) {
      throw new IllegalArgumentException("dkey is needed when list akeys");
    }
    desc.encode();

    // TODO: check completion of list
    client.listObjectAkeys(objectPtr, ((DirectBuffer)desc.getDescBuffer()).address(),
      ((DirectBuffer)desc.getKeyBuffer()).address(), desc.getKeyBuffer().capacity(),
      ((DirectBuffer)desc.getAnchorBuffer()).address(), desc.getBatchSize());
    return null;
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

  /**
   * create a new instance of {@link IODataDesc} for update
   *
   * @param dkey
   * distribution key
   * @param entries
   * list of entries describing records update
   * @return {@link IODataDesc}
   * @throws IOException
   */
  public IODataDesc createDataDescForUpdate(String dkey, List<IODataDesc.Entry> entries) throws IOException {
    IODataDesc desc = new IODataDesc(dkey, entries, true);
    return desc;
  }

  /**
   * create a new instance of {@link IODataDesc} for fetch
   *
   * @param dkey
   * distribution key
   * @param entries
   * list of entries describing records fetch
   * @return {@link IODataDesc}
   * @throws IOException
   */
  public IODataDesc createDataDescForFetch(String dkey, List<IODataDesc.Entry> entries) throws IOException {
    IODataDesc desc = new IODataDesc(dkey, entries, false);
    return desc;
  }

  /**
   * create new instance of {@link IOKeyDesc} with all parameters provided.
   *
   * @param dkey
   * distribution key for listing akeys. null for listing dkeys
   * @param nbrOfKeys
   * number of keys to list. The listing could stop if <code>nbrOfKeys</code> exceeds actual number of keys
   * @param keyLen
   * approximate key length, so that buffer size can be well-calculated
   * @param batchSize
   * how many keys to list per native method call
   * @return new instance of IOKeyDesc
   * @throws IOException
   */
  public IOKeyDesc createKDWithAllParams(String dkey, int nbrOfKeys, int keyLen, int batchSize)
                  throws IOException {
    return new IOKeyDesc(dkey, nbrOfKeys, keyLen, batchSize);
  }

  /**
   * create new instance of {@link IOKeyDesc} with default batch size,
   * {@linkplain Constants#KEY_LIST_BATCH_SIZE_DEFAULT}.
   *
   * @param dkey
   * distribution key for listing akeys. null for listing dkeys
   * @param nbrOfKeys
   * number of keys to list. The listing could stop if <code>nbrOfKeys</code> exceeds actual number of keys
   * @param keyLen
   * approximate key length, so that buffer size can be well-calculated
   * @return new instance of IOKeyDesc
   * @throws IOException
   */
  public IOKeyDesc createKDWithDefaultBs(String dkey, int nbrOfKeys, int keyLen) throws IOException {
    return new IOKeyDesc(dkey, nbrOfKeys, keyLen);
  }

  /**
   * create new instance {@link IOKeyDesc} with number of key specified and with default key length,
   * {@linkplain Constants#KEY_LIST_LEN_DEFAULT} and batch size, {@linkplain Constants#KEY_LIST_BATCH_SIZE_DEFAULT}.
   *
   * @param dkey
   * distribution key for listing akeys. null for listing dkeys
   * @param nbrOfKeys
   * number of keys to list. The listing could stop if <code>nbrOfKeys</code> exceeds actual number of keys
   * @return new instance of IOKeyDesc
   * @throws IOException
   */
  public IOKeyDesc createKDWithNbrOfKeys(String dkey, int nbrOfKeys) throws IOException {
    return new IOKeyDesc(dkey, nbrOfKeys);
  }

  /**
   * create new instance {@link IOKeyDesc} with all defaults to list all keys (Integer.MAX_VALUE) with default key
   * length, {@linkplain Constants#KEY_LIST_LEN_DEFAULT} and batch size,
   * {@linkplain Constants#KEY_LIST_BATCH_SIZE_DEFAULT}.
   *
   * @param dkey
   * distribution key for listing akeys. null for listing dkeys
   * @return new instance of IOKeyDesc
   * @throws IOException
   */
  public IOKeyDesc createKD(String dkey) throws IOException {
    return new IOKeyDesc(dkey);
  }

  /**
   * create new instance {@link IOKeyDesc} with key length and batch size specified.
   *
   * @param dkey
   * distribution key for listing akeys. null for listing dkeys
   * @param keyLen
   * approximate key length, so that buffer size can be well-calculated
   * @param batchSize
   * how many keys to list per native method call
   * @return new instance of IOKeyDesc
   * @throws IOException
   */
  public IOKeyDesc createKDWithKlAndBs(String dkey, int keyLen, int batchSize) throws IOException {
    return new IOKeyDesc(dkey, Integer.MAX_VALUE, keyLen, batchSize);
  }
}
