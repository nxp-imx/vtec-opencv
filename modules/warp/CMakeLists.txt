# i.MX 8M Plus warper
set(the_description "Hardware accelerated Warper")
if (WITH_DW100)
  message("DW100 enabled")
  ocv_define_module(warp opencv_core WRAP python)
  ocv_module_include_directories(${KERNEL_HEADER_INCLUDE_DIR})
else()
  message("DW100 disabled")
endif()
