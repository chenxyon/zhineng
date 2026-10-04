cmake_minimum_required(VERSION 3.16)

# 挂载共享自定义组件库
#
# 注意：EXTRA_COMPONENT_DIRS 必须逐个列出**组件目录**，不能指向 d:\components 父目录，
# 因为 ESP-IDF 会把每个条目当作单个组件处理，指向父目录会导致
# "Failed to resolve component 'xxx' required by component 'main'"。
#
# 未被 main/CMakeLists.txt 的 REQUIRES 引用则不参与构建，
# 从而避免引入 LVGL 等重型依赖。索引见 d:\components\COMPONENTS.md
set(EXTRA_COMPONENT_DIRS
    "D:/components/wifi_manager"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(zhineng)