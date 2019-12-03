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

import java.io.IOException;

/**
 * Exception class for remote DAOS operations. The <code>errorCode</code> is passed and defined by DAOS system.
 * The corresponding error message of <code>errorCode</code> is get when {@link #toString()} method is called.
 */
public class DaosIOException extends IOException {

  private int errorCode = Integer.MIN_VALUE;

  public DaosIOException(String msg){
    super(msg);
  }

  public DaosIOException(Throwable cause){
    super(cause);
  }

  public DaosIOException(String msg, int errorCode){
    super(msg);
    this.errorCode = errorCode;
  }

  public DaosIOException(String msg, int errorCode, Throwable cause){
    super(msg, cause);
    this.errorCode = errorCode;
  }

  public int getErrorCode() {
    return errorCode;
  }

  public String toString(){
    //TODO: parse error code if errorcode is set
    return null;
  }
}
