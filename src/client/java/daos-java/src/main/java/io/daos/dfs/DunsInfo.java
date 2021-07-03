/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
