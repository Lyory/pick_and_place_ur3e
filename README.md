# UR3e ROS 2 workspace

Workspace ROS 2 cho mô phỏng Universal Robots trong Gazebo Classic và package `ur3_llm_control`.

## Yêu cầu

- Ubuntu 22.04 và ROS 2 Humble
- `colcon`, `rosdep`, Gazebo Classic và MoveIt 2
- Các phụ thuộc ROS được khai báo trong các file `package.xml`

## Clone và build

```bash
git clone <URL_REPOSITORY> thuc_hanh_tuan2_ws
cd thuc_hanh_tuan2_ws
source /opt/ros/humble/setup.bash
rosdep update
rosdep install --from-paths src --ignore-src -r -y
python3 -m pip install --user openai
colcon build --symlink-install
source install/setup.bash
```

Thay `<URL_REPOSITORY>` bằng URL GitHub của repo. Sau khi clone, các thư mục `build/`, `install/` và `log/` được `colcon` tạo lại trên máy của bạn; chúng không cần được đưa lên GitHub.

## Chạy

```bash
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch ur3_llm_control llm_robot.launch.py
```

Xem [hướng dẫn package UR3e](src/ur3_llm_control/README.md) để cấu hình 9Router và chạy task manager. Phần mô phỏng Universal Robots có [hướng dẫn riêng](src/Universal_Robots_ROS2_Gazebo_Simulation/README.md).
