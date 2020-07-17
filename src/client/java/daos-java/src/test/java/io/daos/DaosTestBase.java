package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "b5f48193-ca9c-431f-8328-ba1342c93cb1";
  public static final String DEFAULT_CONT_ID = "f393e87a-3e6d-4434-952a-3704ba12914f";

  public static final String DEFAULT_OBJECT_CONT_ID = "97eab4d6-3a7a-4a6c-ac61-1c363de9323c";

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
