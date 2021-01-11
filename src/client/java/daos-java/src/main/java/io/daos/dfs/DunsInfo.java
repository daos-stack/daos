/*
 * (C) Copyright 2018-2020 Intel Corporation.
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

package io.daos.dfs;

/**
 * DAOS UNS information to be accessed outside of this module.
 */
public class DunsInfo {

  private String poolId;
  private String contId;
  private String layout;
  private String appInfo;
  private String prefix;

  /**
   * constructor with pool UUID, container UUID, layout and more application specific info.
   *
   * @param poolId
   * pool UUID
   * @param contId
   * container UUID
   * @param layout
   * layout
   * @param appInfo
   * application specific info.
   * @param prefix
   * UNS OS path or UUID prefix in format /pooluuid/containeruuid
   */
  public DunsInfo(String poolId, String contId, String layout, String appInfo, String prefix) {
    this.poolId = poolId;
    this.contId = contId;
    this.layout = layout;
    this.appInfo = appInfo;
    this.prefix = prefix;
  }

  public String getAppInfo() {
    return appInfo;
  }

  public String getContId() {
    return contId;
  }

  public String getLayout() {
    return layout;
  }

  public String getPoolId() {
    return poolId;
  }

  public String getPrefix() {
    return prefix;
  }
}
