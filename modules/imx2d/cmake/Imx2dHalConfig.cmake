set(IMX2D_DIR "${CMAKE_CURRENT_LIST_DIR}/..")

get_filename_component(IMX2D_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Variables expected by top level CMakeLists.txt
if (WITH_IMX2D)
  add_subdirectory(${IMX2D_DIR}/hal ./imx2d_hal_build)

  set(OpenCV_HAL_LIBRARIES "imx2d_hal")
  set(OpenCV_HAL_HEADERS "imx2d_hal.hpp")
  set(OpenCV_HAL_INCLUDE_DIRS "${IMX2D_DIR}/hal/include")

  set(Imx2dHal_FOUND TRUE)
  set(Imx2dHal_VERSION "0.0.1")
endif()
