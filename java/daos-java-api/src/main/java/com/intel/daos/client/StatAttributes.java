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

package com.intel.daos.client;

import java.nio.ByteBuffer;

/**
 * Java attributes representing DAOS <code>struct stat</code> which has below information
 *  * mode_t    st_mode;
 *  * uid_t     st_uid;
 *  * gid_t     st_gid;
 *  * off_t     st_size;
 *  * blkcnt_t  st_blocks
 *  * struct timespec st_atim;
 *  * struct timespec st_mtim;
 *  * struct timespec st_ctim;
 *
 * This Java representative adds two more fields, object id and file (is file).
 */
public class StatAttributes {

  private final long objId;

  private final int mode;

  private final int uid;

  private final int gid;

  private final long blockCnt;

  private final long length;

  private final TimeSpec accessTime;

  private final TimeSpec modifyTime;

  private final TimeSpec createTime;

  private final boolean file;

  protected StatAttributes(ByteBuffer buffer){
    objId = buffer.getLong();
    mode = buffer.getInt();
    uid = buffer.getInt();
    gid = buffer.getInt();
    blockCnt = buffer.getLong();
    length = buffer.getLong();
    accessTime = new TimeSpec(buffer.getLong(), buffer.getLong());
    modifyTime = new TimeSpec(buffer.getLong(), buffer.getLong());
    createTime = new TimeSpec(buffer.getLong(), buffer.getLong());
    file = buffer.get() > 0;
  }

  public int getMode() {
    return mode;
  }

  public long getObjId() {
    return objId;
  }

  public TimeSpec getAccessTime() {
    return accessTime;
  }

  public TimeSpec getCreateTime() {
    return createTime;
  }

  public int getGid() {
    return gid;
  }

  public long getBlockCnt() {
    return blockCnt;
  }

  public long getLength() {
    return length;
  }

  public TimeSpec getModifyTime() {
    return modifyTime;
  }

  public int getUid() {
    return uid;
  }

  public boolean isFile() {
    return file;
  }

  public static int objectSize(){
    return 3*8 + 3*4 + 3*16 + 1; //85
  }

  public static class TimeSpec{
    private final long seconds;
    private final long nano;

    public TimeSpec(long seconds, long nano){
      this.seconds = seconds;
      this.nano = nano;
    }

    public long getNano() {
      return nano;
    }

    public long getSeconds() {
      return seconds;
    }
  }
}
