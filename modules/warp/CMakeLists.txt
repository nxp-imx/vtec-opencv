set(the_description "Hardware accelerated Warper")
ocv_define_module(warp opencv_core WRAP python)
ocv_module_include_directories(${KERNEL_HEADER_INCLUDE_DIR})
