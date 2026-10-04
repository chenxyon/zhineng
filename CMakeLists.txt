cmake_minimum_required(VERSION 3.16)

# 挂载共享自定义组件库（含 wifi_manager、oled、font_service 等 20 个组件）
# 注意：这些组件按需编译，未被 main/CMakeLists.txt 的 REQUIRES 引用则不参与构建，
# 从而避免引入 LVGL 等重型依赖。索引见 d:\components\COMPONENTS.md
set(EXTRA_COMPONENT_DIRS "D:/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(zhineng)