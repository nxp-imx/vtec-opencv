set(IMX2D_HAL_DIR "${CMAKE_CURRENT_LIST_DIR}")
cmake_path(GET IMX2D_HAL_DIR PARENT_PATH IMX2D_DIR)

set(G2D_LIB_NAME "g2d")
set(IMX2D_HAL_LIB_NAME "imx2d_hal")
set(IMX2D_COMMON_LIB_NAME "imx2d_common")


# IMX2D HAL library
file(GLOB_RECURSE IMX2D_HAL_SOURCES "${IMX2D_HAL_DIR}/src/*.cpp")
add_library(${IMX2D_HAL_LIB_NAME} STATIC ${IMX2D_HAL_SOURCES})
target_include_directories(${IMX2D_HAL_LIB_NAME} PRIVATE
  ${CMAKE_SOURCE_DIR}/modules/core/include
  ${CMAKE_SOURCE_DIR}/modules/imgproc/include
  ${IMX2D_DIR}/hal/include
  ${IMX2D_DIR}/include)
target_link_libraries(${IMX2D_HAL_LIB_NAME}
  PRIVATE ${G2D_LIB_NAME}
  PUBLIC ${IMX2D_COMMON_LIB_NAME})


# IMX2D COMMON library
file(GLOB_RECURSE IMX2D_COMMON_SOURCES "${IMX2D_DIR}/common/src/*.cpp")
add_library(${IMX2D_COMMON_LIB_NAME} SHARED ${IMX2D_COMMON_SOURCES})
target_include_directories(${IMX2D_COMMON_LIB_NAME}
  PUBLIC ${IMX2D_DIR}/common/include)
target_link_libraries(${IMX2D_COMMON_LIB_NAME}
                      PRIVATE ${G2D_LIB_NAME})

set_target_properties(${IMX2D_COMMON_LIB_NAME} PROPERTIES
  VERSION ${OPENCV_LIBVERSION}
  SOVERSION ${OPENCV_SOVERSION}
)

install(TARGETS ${IMX2D_COMMON_LIB_NAME}
  RUNTIME DESTINATION ${OPENCV_BIN_INSTALL_PATH} COMPONENT libs
  LIBRARY DESTINATION ${OPENCV_LIB_INSTALL_PATH} COMPONENT libs
  ARCHIVE DESTINATION ${OPENCV_LIB_ARCHIVE_INSTALL_PATH} COMPONENT dev
)
