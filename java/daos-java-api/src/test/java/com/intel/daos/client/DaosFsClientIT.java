package com.intel.daos.client;

import org.junit.BeforeClass;
import org.junit.Test;

public class DaosFsClientIT {

  private static String poolId;
  private static String contId;

  @BeforeClass
  public void setup(){
    poolId = "0eba76a4-5f9d-4c47-91c7-545b3677fb28";
    contId = "3f56f74f-dd21-49ec-899e-2b410543314b";
  }

  @Test
  public void testIt(){
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.
  }
}
