# CMakeList.txt: Coroutine 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#

# 将源代码添加到此项目的可执行文件。
#add_executable (step1_demo "step1_demo.cpp")
#add_executable (step2_demo "step2_demo.cpp")
#add_executable (step3_demo "step3_demo.cpp")
#add_executable (step4_demo "step4_demo.cpp")
#add_executable (step5_demo "step5_demo.cpp")
#add_executable (step6_demo "step6_demo.cpp")
add_executable (step7_demo "step7_demo.cpp")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  # set_property(TARGET step1_demo PROPERTY CXX_STANDARD 20)
  # set_property(TARGET step2_demo PROPERTY CXX_STANDARD 20)
  # set_property(TARGET step3_demo PROPERTY CXX_STANDARD 20)
  # set_property(TARGET step4_demo PROPERTY CXX_STANDARD 20)
  # set_property(TARGET step5_demo PROPERTY CXX_STANDARD 20)
  # set_property(TARGET step6_demo PROPERTY CXX_STANDARD 20)
  set_property(TARGET step7_demo PROPERTY CXX_STANDARD 20)
endif()

# TODO: 如有需要，请添加测试并安装目标。