/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos;

import io.netty.buffer.ByteBuf;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.Closeable;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

public class DaosContainer extends Shareable implements Closeable {

  private long contPtr;

  private String id;

  // keyed by container UUID
  private static final Map<String, DaosContainer> containerMap = new ConcurrentHashMap<>();

  private static final Logger log = LoggerFactory.getLogger(DaosContainer.class);

  private DaosContainer(String contUuid) {
    if (contUuid.length() > Constants.ID_LENGTH) {
      throw new IllegalArgumentException("container UUID length should be " + Constants.ID_LENGTH);
    }
    this.id = contUuid;
  }

  protected static DaosContainer getInstance(String contId, long poolPtr, int containerFlags) throws IOException {
    DaosContainer dc = containerMap.get(contId);
    if (dc == null) {
      dc = new DaosContainer(contId);
      containerMap.putIfAbsent(contId, dc);
      dc = containerMap.get(contId);
    }
    synchronized (dc) {
      dc.init(poolPtr, containerFlags);
      dc.incrementRef();
    }
    return dc;
  }

  private void init(long poolPtr, int containerFlags) throws IOException {
    if (isInited()) {
      return;
    }
    contPtr = DaosClient.daosOpenCont(poolPtr, id, containerFlags);
    setInited(true);
    if (log.isDebugEnabled()) {
      log.debug("opened container {} with ptr {}", id, contPtr);
    }
  }

  public void setAttributes(Map<String, String> attrMap) throws IOException {
    if (attrMap.isEmpty()) {
      return;
    }
    int totalSize = 0;
    int totalAttrSize = 0;
    Map<byte[], byte[]> byteMap = new HashMap<>(attrMap.size());
    try {
      for (Map.Entry<String, String> entry : attrMap.entrySet()) {
        byte[] keyBytes = entry.getKey().getBytes(Constants.KEY_CHARSET);
        byte[] valueBytes = entry.getValue().getBytes(Constants.KEY_CHARSET);
        byteMap.put(keyBytes, valueBytes);
        totalAttrSize += keyBytes.length + valueBytes.length;
        totalSize += (1 + 4 + 4 + totalAttrSize); // 1 for null-terminator
      }
    } catch (UnsupportedEncodingException e) {
      throw new RuntimeException("failed to encode attributes", e);
    }
    totalSize += (4 + 4); // number of attributes + total size
    ByteBuf buffer = BufferAllocator.objBufWithNativeOrder(totalSize);
    try {
      buffer.writeInt(byteMap.size());
      buffer.writeInt(totalAttrSize);
      for (Map.Entry<byte[], byte[]> entry : byteMap.entrySet()) {
        buffer.writeInt(entry.getKey().length).writeBytes(entry.getKey()).writeByte('\0');
        buffer.writeInt(entry.getValue().length).writeBytes(entry.getValue());
      }
      DaosClient.daosSetContAttrs(contPtr, buffer.memoryAddress());
    } finally {
      buffer.release();
    }
  }

  public Map<String, String> getAttributes(List<String> attrNameList) throws IOException {
    if (attrNameList.isEmpty()) {
      return Collections.emptyMap();
    }
    int maxValueSize = 64;
    Map<String, String> attrMap = new HashMap<>();
    List<String> reqList = new ArrayList<>(attrNameList.size());
    reqList.addAll(attrNameList);
    while (!reqList.isEmpty()) {
      getAttributes(attrMap, reqList, maxValueSize);
      Iterator<String> reqIt = reqList.iterator();
      while (reqIt.hasNext()) {
        if (attrMap.containsKey(reqIt.next())) {
          reqIt.remove();
        }
      }
      if (reqList.isEmpty()) {
        break;
      }
      maxValueSize *= 2; // try with doubled max value size
      if (maxValueSize > 1024) {
        throw new IllegalArgumentException("attribute value should be no more than 1024. " + maxValueSize);
      }
    }
    return attrMap;
  }

  private boolean getAttributes(Map<String, String> attrMap,
                                List<String> reqList, int maxValueSize) throws IOException {
    int totalSize = 0;
    int totalNameSize = 0;
    List<byte[]> byteList = new ArrayList<>(reqList.size());
    Map<byte[], String> byteStrMap = new HashMap<>(reqList.size());
    try {
      for (String entry : reqList) {
        byte[] keyBytes = entry.getBytes(Constants.KEY_CHARSET);
        byteList.add(keyBytes);
        byteStrMap.put(keyBytes, entry);
        totalNameSize += keyBytes.length;
        totalSize += (1 + 1 + 4 + 4 + totalNameSize + maxValueSize); // 1 for null-terminator, 1 for truncated
      }
    } catch (UnsupportedEncodingException e) {
      throw new RuntimeException("failed to encode attributes", e);
    }
    totalSize += (4 + 4 + 4); // number of attributes + total name size + max value size
    ByteBuf buffer = BufferAllocator.objBufWithNativeOrder(totalSize);
    boolean truncated = false;
    try {
      buffer.writeInt(byteList.size());
      buffer.writeInt(totalNameSize);
      buffer.writeInt(maxValueSize);
      for (byte[] entry : byteList) {
        buffer.writeInt(entry.length).writeBytes(entry).writeByte('\0');
        buffer.writerIndex(buffer.writerIndex() + 1 + 4 + maxValueSize);
      }
      buffer.readerIndex(4 + 4 + 4);
      DaosClient.daosGetContAttrs(contPtr, buffer.memoryAddress());

      for (byte[] entry : byteList) {
        int len = buffer.readInt();
        buffer.readerIndex(buffer.readerIndex() + len + 1);
        int b = buffer.readByte();
        if (b == 1) { // truncated?
          truncated = true;
          buffer.readerIndex(buffer.readerIndex() + 4 + maxValueSize);
        } else {
          int size = buffer.readInt();
          if (size > 0) {
            byte[] vb = new byte[size];
            buffer.readBytes(vb);
            attrMap.put(byteStrMap.get(entry), new String(vb, Constants.KEY_CHARSET));
          } else if (size < 0) {
            throw new IllegalStateException("size should be no less than 0. " + size);
          }
          buffer.readerIndex(buffer.readerIndex() + maxValueSize - size);
        }
      }
    } finally {
      buffer.release();
    }
    return truncated;
  }

  public Set<String> listAttributes() throws IOException {
    int totalSize = 1024;
    Set<String> nameSet = new HashSet<>();
    boolean truncated = true;
    while (truncated) {
      ByteBuf buffer = BufferAllocator.objBufWithNativeOrder(totalSize + 8);
      try {
        buffer.writeLong(totalSize);
        buffer.writerIndex(buffer.capacity());
        DaosClient.daosListContAttrs(contPtr, buffer.memoryAddress());
        buffer.readerIndex(0);
        long actualSize = buffer.readLong();
        if (actualSize <= totalSize) {
          parseNameBuffer(buffer, actualSize, nameSet);
          truncated = false;
        }
      } finally {
        buffer.release();
      }
      totalSize *= 2;
      if (totalSize > 1024 * 1024) {
        throw new IllegalArgumentException("total size of all attribute names should not exceed " + 1024 * 1024);
      }
    }
    return nameSet;
  }

  private void parseNameBuffer(ByteBuf buffer, long actualSize, Set<String> nameSet)
      throws UnsupportedEncodingException {
    byte terminator = '\0';
    int count = 0;
    while (count < actualSize) {
      int startIndex = buffer.readerIndex();
      int nameLen = 0;
      while (count < actualSize && (buffer.readByte()) != terminator) {
        nameLen++;
        count++;
      }
      count++; // for null terminator
      byte[] bytes = new byte[nameLen];
      buffer.readerIndex(startIndex);
      buffer.readBytes(bytes);
      buffer.readerIndex(buffer.readerIndex() + 1); // skip terminator
      nameSet.add(new String(bytes, Constants.KEY_CHARSET));
    }
  }

  @Override
  public synchronized void close() throws IOException {
    decrementRef();
    if (isInited() && contPtr != 0 && getRefCnt() <= 0) {
      DaosClient.daosCloseContainer(contPtr);
      if (log.isDebugEnabled()) {
        log.debug("container {} with ptr {} closed", id, contPtr);
      }
      setInited(false);
      containerMap.remove(id);
    }
  }

  public String getContainerId() {
    return id;
  }

  public long getContPtr() {
    return contPtr;
  }
}
