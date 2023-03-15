set(the_description "i.MX 2D primitives acceleration")

set(IMX2D_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(IMX2D_COMMON_LIB_NAME "imx2d_common")

if(WITH_IMX2D)
  # i.MX 2D acceleration extra module library
  # OCV modules have link dependency with HAL libs
  # By transitivity, modules will have dependency on imx2d_common lib
  message("i.MX 2D enabled")

  ocv_define_module(imx2d opencv_core opencv_imgproc
    WRAP python)

  # HAL counters and preallocated buffers cache snoop...
  target_link_libraries(opencv_perf_imx2d
                        PRIVATE ${IMX2D_COMMON_LIB_NAME})
  target_link_libraries(opencv_test_imx2d
                        PRIVATE ${IMX2D_COMMON_LIB_NAME})

else()
  message("i.MX 2D disabled")
endif()
