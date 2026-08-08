# --------------------------------------------------------------------------
#  Add module for log
# --------------------------------------------------------------------------
rmvl_add_module(log
  DEPENDS core
  EXTERNAL fmt
)

# --------------------------------------------------------------------------
#  Build the test program
# --------------------------------------------------------------------------
if(BUILD_TESTS)
  rmvl_add_test(
    log Unit
    DEPENDS log
    EXTERNAL GTest::gtest_main
  )
endif(BUILD_TESTS)
