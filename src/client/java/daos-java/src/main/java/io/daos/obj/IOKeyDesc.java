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
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;

/**
 * A class to describe key listing, including approximate key length, number of keys to retrieve and batch size
 */
public class IOKeyDesc {

  private String dkey;

  private byte[] dkeyBytes;

  private final int nbrOfKeys;

  private final int batchSize;

  private final int akeyLen;

  private final ByteBuffer anchorBuffer;

  private final ByteBuffer descBuffer;

  private ByteBuffer keyBuffer;

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
      dkeyBytes = dkey.getBytes(Constants.KEY_CHARSET);
      if (dkeyBytes.length > Short.MAX_VALUE) {
        throw new IllegalArgumentException("dkey length in " + Constants.KEY_CHARSET + " should not exceed "
          + Short.MAX_VALUE);
      }
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
    anchorBuffer = BufferAllocator.directBuffer(1 + IOKeyDesc.getAnchorTypeLen());
    anchorBuffer.order(Constants.DEFAULT_ORDER);
    anchorBuffer.put((byte) 0);
    // 4 for actual number of keys returned, (2 + dkeyBytes.length) for dkeys
    int descLen = 4 + ((dkeyBytes == null) ? 0 : (Constants.ENCODED_LENGTH_KEY + dkeyBytes.length))
                  + IOKeyDesc.getKeyDescLen() * this.batchSize;
    if (descLen < 0) {
      throw new IllegalArgumentException("too big batchSize. " + this.batchSize);
    }
    descBuffer = BufferAllocator.directBuffer(descLen);
    keyBuffer = BufferAllocator.directBuffer(akeyLen * this.batchSize);
    descBuffer.order(Constants.DEFAULT_ORDER);
    keyBuffer.order(Constants.DEFAULT_ORDER);
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

  public ByteBuffer getDescBuffer() {
    if (!encoded) {
      throw new IllegalStateException("not encoded yet");
    }
    return descBuffer;
  }

  public ByteBuffer getAnchorBuffer() {
    return anchorBuffer;
  }

  public ByteBuffer getKeyBuffer() {
    return keyBuffer;
  }

  public String getDkey() {
    return dkey;
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
    if (resultParsed) {
      throw new IllegalStateException("result is parsed. cannot encode again");
    }
    if (!encoded) {
      descBuffer.position(0);
      descBuffer.putInt(0); // reserve for actual number of keys returned
      if ((!continued) && dkeyBytes != null) {
        descBuffer.putShort((short) dkeyBytes.length);
        descBuffer.put(dkeyBytes);
      }
      encoded = true;
    }
  }

  private void resizeKeyBuffer() {
    if (log.isDebugEnabled()) {
      log.debug("resize key buffer size to " + getSuggestedKeyLen() * batchSize);
    }
    keyBuffer = BufferAllocator.directBuffer(getSuggestedKeyLen() * batchSize);
  }

  /**
   * continue to list more keys with existing anchor which is updated in JNI call.
   * user should call this method before reusing this object to query more keys.
   */
  public void continueList() {
    anchorBuffer.position(0);
    byte stat = anchorBuffer.get();
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
    anchorBuffer.position(0);
    return anchorBuffer.get() == Constants.KEY_LIST_CODE_ANCHOR_END;
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
      descBuffer.position(0);
      int retNbr = descBuffer.getInt();
      if (retNbr == 0) { // check no result
        resultParsed = true;
        return resultKeys;
      }

      if (dkeyBytes != null) {
        descBuffer.position(descBuffer.position() + Constants.ENCODED_LENGTH_KEY + dkeyBytes.length);
      }
      anchorBuffer.position(0);
      if (anchorBuffer.get() == Constants.KEY_LIST_CODE_KEY2BIG) { // check key2big
        resultParsed = true;
        suggestedKeyLen = (int)descBuffer.getLong() + 1;
        if (log.isDebugEnabled()) {
          log.debug("key2big. suggested length is " + suggestedKeyLen);
        }
        return resultKeys;
      }

      long keyLen;
      short csumLen;
      int idx = 0;
      for (int i = 0; i < retNbr; i++) {
        keyBuffer.position(idx);
        keyLen = descBuffer.getLong();
        byte bytes[] = new byte[(int)keyLen];
        keyBuffer.get(bytes);
        resultKeys.add(new String(bytes, Constants.KEY_CHARSET));
        descBuffer.position(descBuffer.position() + 6); // uint32_t kd_val_type(4) + uint16_t kd_csum_type(2)
        csumLen = descBuffer.getShort();
        idx += keyLen + csumLen;
      }
      resultParsed = true;
      return resultKeys;
    }
    return resultKeys;
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
      + 4   // uint32_t kd_val_type
      + 2   // uint16_t kd_csum_type
      + 2;  // uint16_t kd_csum_len
  }

  public static int getAnchorTypeLen() {
    return 2      // uint16_t da_type
      + 2     // uint16_t	da_shard
      + 4     // uint32_t	da_flags
      + 120;  // uint8_t		da_buf[DAOS_ANCHOR_BUF_MAX]
  }
}
