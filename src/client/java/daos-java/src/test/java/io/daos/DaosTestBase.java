package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "3e252768-8e19-4526-9fae-8610357bcf68";
  public static final String DEFAULT_CONT_ID = "82cce997-a506-480b-a4fb-954f2e8d11cd";

  public static final String DEFAULT_OBJECT_CONT_ID = "82cce997-a506-480b-a4fb-954f2e8d11cd";

  public static String getPoolId() {
    return System.getProperty("pool_id", DaosTestBase.DEFAULT_POOL_ID);
  }

  public static String getContId() {
    return System.getProperty("cont_id", DaosTestBase.DEFAULT_CONT_ID);
  }

  public static String getObjectContId() {
    return System.getProperty("object_cont_id", DaosTestBase.DEFAULT_OBJECT_CONT_ID);
  }
}
