/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.obj;

import io.daos.BufferAllocator;
import io.daos.Constants;
import io.daos.DaosUtils;
import io.netty.buffer.ByteBuf;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.util.ArrayList;
import java.util.List;

/**
 * A class to describe key listing, including approximate key length, number of keys to retrieve and batch size.
 * User should call
 */
public class IOKeyDesc {

  private String dkey;

  private byte[] dkeyBytes;

  private final int nbrOfKeys;

  private final int batchSize;

  private final int akeyLen;

  private final ByteBuf anchorBuffer;

  private final ByteBuf descBuffer;

  private ByteBuf keyBuffer;

  private List<String> resultKeys;

  private int suggestedKeyLen;

  private boolean encoded;

  private boolean resultParsed;

  private boolean continued;

  private static final Logger log = LoggerFactory.getLogger(IOKeyDesc.class);

  /**
   * constructor to set all parameters.
   *
   * @param dkey
   * distribution key for listing akeys. null for listing dkeys
   * @param nbrOfKeys
   * number of keys to list. The listing could stop if <code>nbrOfKeys</code> exceeds actual number of keys
   * @param akeyLen
   * approximate key length, so that buffer size can be well-calculated. It may be increased after key2big error when
   * list keys.
   * @param batchSize
   * how many keys to list per native method call
   * @throws IOException
   */
  protected IOKeyDesc(String dkey, int nbrOfKeys, int akeyLen, int batchSize) throws IOException {
    this.dkey = dkey;
    if (dkey != null) {
      dkeyBytes = DaosUtils.keyToBytes(dkey);
    }
    if (nbrOfKeys < 1) {
      throw new IllegalArgumentException("nbrOfKeys should be at least 1, " + nbrOfKeys);
    }
    this.nbrOfKeys = nbrOfKeys;
    this.akeyLen = akeyLen;
    if (nbrOfKeys < batchSize) {
      this.batchSize = nbrOfKeys;
    } else {
      this.batchSize = batchSize;
    }
    // 1 byte for anchor status
    anchorBuffer = BufferAllocator.objBufWithNativeOrder(1 + IOKeyDesc.getAnchorTypeLen());
    anchorBuffer.writeByte((byte) 0);
    anchorBuffer.writerIndex(anchorBuffer.capacity());
    // 4 for actual number of keys returned, (2 + dkeyBytes.length) for dkeys
    int descLen = 4 + ((dkeyBytes == null) ? 0 : (Constants.ENCODED_LENGTH_KEY + dkeyBytes.length))
                  + IOKeyDesc.getKeyDescLen() * this.batchSize;
    descBuffer = BufferAllocator.objBufWithNativeOrder(descLen);
    keyBuffer = BufferAllocator.objBufWithNativeOrder(akeyLen * this.batchSize);
    keyBuffer.writerIndex(keyBuffer.capacity());
  }

  /**
   * constructor with default batch size, {@linkplain Constants#KEY_LIST_BATCH_SIZE_DEFAULT}.
   *
   * @param dkey
   * distribution key for listing akeys. null for listing dkeys
   * @param nbrOfKeys
   * number of keys to list. The listing could stop if <code>nbrOfKeys</code> exceeds actual number of keys
   * @param keyLen
   * approximate key length, so that buffer size can be well-calculated
   * @throws IOException
   */
  protected IOKeyDesc(String dkey, int nbrOfKeys, int keyLen) throws IOException {
    this(dkey, nbrOfKeys, keyLen, Constants.KEY_LIST_BATCH_SIZE_DEFAULT);
  }

  /**
   * constructor with default key length, {@linkplain Constants#KEY_LIST_LEN_DEFAULT} and batch size,
   * {@linkplain Constants#KEY_LIST_BATCH_SIZE_DEFAULT}.
   *
   * @param dkey
   * @param nbrOfKeys
   * @throws IOException
   */
  protected IOKeyDesc(String dkey, int nbrOfKeys) throws IOException {
    this(dkey, nbrOfKeys, Constants.KEY_LIST_LEN_DEFAULT, Constants.KEY_LIST_BATCH_SIZE_DEFAULT);
  }

  /**
   * constructor to list all keys (Integer.MAX_VALUE) with default key length,
   * {@linkplain Constants#KEY_LIST_LEN_DEFAULT} and batch size, {@linkplain Constants#KEY_LIST_BATCH_SIZE_DEFAULT}.
   *
   * @param dkey
   * @throws IOException
   */
  protected IOKeyDesc(String dkey) throws IOException {
    this(dkey, Integer.MAX_VALUE, Constants.KEY_LIST_LEN_DEFAULT, Constants.KEY_LIST_BATCH_SIZE_DEFAULT);
  }

  public int getBatchSize() {
    return batchSize;
  }

  /**
   * get description buffer. The buffer's reader index and write index should be restored if user
   * changed them.
   *
   * @return bytebuf
   */
  protected ByteBuf getDescBuffer() {
    if (encoded) {
      return descBuffer;
    }
    throw new IllegalStateException("not encoded yet");
  }

  /**
   * get anchor buffer. The buffer's reader index and write index should be restored if user
   * changed them.
   *
   * @return bytebuf
   */
  protected ByteBuf getAnchorBuffer() {
    if (encoded) {
      return anchorBuffer;
    }
    throw new IllegalStateException("not encoded yet");
  }

  /**
   * get key buffer. The buffer's reader index and write index should be restored if user
   * changed them.
   *
   * @return
   */
  protected ByteBuf getKeyBuffer() {
    if (encoded) {
      return keyBuffer;
    }
    throw new IllegalStateException("not encoded yet");
  }

  public String getDkey() {
    return dkey;
  }

  public byte[] getDkeyBytes() {
    return dkeyBytes;
  }

  public List<String> getResultKeys() {
    return resultKeys;
  }

  /**
   * get suggested key length after key2big error occurred.
   *
   * @return suggested key length
   */
  public int getSuggestedKeyLen() {
    return suggestedKeyLen;
  }

  /**
   * encode dkey, if any, to descBuffer and encode status to anchor buffer.
   */
  public void encode() {
    if (!resultParsed) {
      if (!encoded) {
        descBuffer.clear();
        descBuffer.writeInt(0); // reserve for actual number of keys returned
        if ((!continued) && dkeyBytes != null) {
          descBuffer.writeShort(dkeyBytes.length);
          descBuffer.writeBytes(dkeyBytes);
        }
        encoded = true;
      }
      return;
    }
    throw new IllegalStateException("result is parsed. cannot encode again");
  }

  private void resizeKeyBuffer() {
    if (log.isDebugEnabled()) {
      log.debug("resize key buffer size to " + getSuggestedKeyLen() * batchSize);
    }
    keyBuffer.release();
    keyBuffer = BufferAllocator.objBufWithNativeOrder(getSuggestedKeyLen() * batchSize);
    keyBuffer.writerIndex(keyBuffer.capacity());
  }

  /**
   * continue to list more keys with existing anchor which is updated in JNI call.
   * user should call this method before reusing this object to query more keys.
   */
  public void continueList() {
    anchorBuffer.readerIndex(0);
    byte stat = anchorBuffer.readByte();
    if (log.isDebugEnabled()) {
      log.debug("continue listing. state of anchor: " + stat);
    }
    switch (stat) {
      case Constants.KEY_LIST_CODE_NOT_STARTED: return;
      case Constants.KEY_LIST_CODE_REACH_LIMIT: break;
      case Constants.KEY_LIST_CODE_KEY2BIG: resizeKeyBuffer(); break;
      default: throw new IllegalStateException("cannot continue the key listing due" +
        " to incorrect anchor status " + stat);
    }
    encoded = false;
    resultKeys.clear();
    resultKeys = null;
    resultParsed = false;
    continued = true;
  }

  /**
   * to test if all keys are listed.
   *
   * @return true for end. no otherwise.
   */
  public boolean reachEnd() {
    anchorBuffer.readerIndex(0);
    return anchorBuffer.readByte() == Constants.KEY_LIST_CODE_ANCHOR_END;
  }

  /**
   * parse result and store it to resultList after JNI call.
   * When you get empty list, it could be one of two reasons.<br/>
   * 1. list ended. You should check {@link #reachEnd()}.
   * 2. key2big error. You should continue the listing by calling {@link #continueList()}
   * and {@link DaosObject#listDkeys(IOKeyDesc)} or {@link DaosObject#listAkeys(IOKeyDesc)}
   *
   * @return result key list
   * @throws UnsupportedEncodingException
   */
  protected List<String> parseResult() throws UnsupportedEncodingException {
    if (!resultParsed) {
      resultKeys = new ArrayList<>();
      // parse desc buffer and key buffer
      descBuffer.readerIndex(0);
      descBuffer.writerIndex(descBuffer.capacity());
      int retNbr = descBuffer.readInt();
      if (retNbr == 0) { // check no result
        resultParsed = true;
        return resultKeys;
      }

      if (dkeyBytes != null) {
        descBuffer.readerIndex(descBuffer.readerIndex() + Constants.ENCODED_LENGTH_KEY + dkeyBytes.length);
      }
      anchorBuffer.readerIndex(0);
      if (anchorBuffer.readByte() == Constants.KEY_LIST_CODE_KEY2BIG) { // check key2big
        resultParsed = true;
        suggestedKeyLen = (int)descBuffer.readLong() + 1;
        if (log.isDebugEnabled()) {
          log.debug("key2big. suggested length is " + suggestedKeyLen);
        }
        return resultKeys;
      }

      long keyLen;
      int idx = 0;
      for (int i = 0; i < retNbr; i++) {
        keyBuffer.readerIndex(idx);
        keyLen = descBuffer.readLong();
        byte bytes[] = new byte[(int)keyLen];
        keyBuffer.readBytes(bytes);
        resultKeys.add(new String(bytes, Constants.KEY_CHARSET));
        descBuffer.readerIndex(descBuffer.readerIndex() + 4); // uint32_t kd_val_type(4)
//        csumLen = descBuffer.readShort();
        idx += keyLen;
      }
      resultParsed = true;
      return resultKeys;
    }
    return resultKeys;
  }

  public void release() {
    if (this.descBuffer != null) {
      this.descBuffer.release();
    }
    if (this.anchorBuffer != null) {
      this.anchorBuffer.release();
    }
    if (this.keyBuffer != null) {
      this.keyBuffer.release();
    }
  }

  @Override
  public String toString() {
    return "IOKeyDesc{" +
      "dkey='" + dkey + '\'' +
      ", nbrOfKeys=" + nbrOfKeys +
      ", batchSize=" + batchSize +
      ", akeyLen=" + akeyLen +
      '}';
  }

  public static int getKeyDescLen() {
    return 8    // daos_size_t kd_key_len
      + 4;   // uint32_t kd_val_type
  }

  public static int getAnchorTypeLen() {
    return 2      // uint16_t da_type
      + 2     // uint16_t	da_shard
      + 4     // uint32_t	da_flags
      + 120;  // uint8_t		da_buf[DAOS_ANCHOR_BUF_MAX]
  }
}
