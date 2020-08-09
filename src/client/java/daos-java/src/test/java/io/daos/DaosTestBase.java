package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "f0bc1688-23f9-4c17-bba3-2f31613ac990";
  public static final String DEFAULT_CONT_ID = "f7b421e5-63bd-428d-96ec-922b9032a3b2";

  public static final String DEFAULT_OBJECT_CONT_ID = "f7b421e5-63bd-428d-96ec-922b9032a3b2";

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
