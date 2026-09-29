# UR3e ROS 2 workspace

Workspace ROS 2 cho mô phỏng Universal Robots trong Gazebo Classic và package `ur3_llm_control`.

## Yêu cầu

- Ubuntu 22.04 và ROS 2 Humble
- `colcon`, `rosdep`, Gazebo Classic và MoveIt 2
- Các phụ thuộc ROS được khai báo trong các file `package.xml`

## Clone và build

```bash
git clone https://github.com/Lyory/pick_and_place_ur3e.git

cd pick_and_place_ur3e/

source /opt/ros/humble/setup.bash
rosdep update
rosdep install --from-paths src --ignore-src -r -y
python3 -m pip install --user openai
colcon build --symlink-install
source install/setup.bash
```

## Cài đặt 9router

```bash
npm install -g 9router

9router
```

## Chạy

Sử dụng đồng thời 3 terminal:
Terminal 1: (9router)
```bash
9router --host 127.0.0.1 --port 20128 --no-browser
```
Terminal 2: robot và gazebo

```bash
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch ur3_llm_control llm_robot.launch.py
```

Terminal 3: Nhập lệnh cho robot
```bash
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 run ur3_llm_control task_manager.py
```

