# UR3e ROS 2 workspace

Workspace ROS 2 cho mô phỏng Universal Robots trong Gazebo Classic và package `ur3_llm_control`.

## Yêu cầu

- Ubuntu 22.04 và ROS 2 Humble
- `colcon`, `rosdep`, Gazebo Classic và MoveIt 2
- Các phụ thuộc ROS được khai báo trong các file `package.xml`

## Clone và build

```bash
git clone https://github.com/Lyory/pick_and_place_ur3e.git

cd pick_and_place_ur3e/src

source /opt/ros/humble/setup.bash
rosdep update
rosdep install --from-paths src --ignore-src -r -y
python3 -m pip install --user openai
colcon build --symlink-install
source install/setup.bash
```

## Cấu hình LLM sau khi clone (một lần)

Mỗi người dùng cấu hình provider và API key của mình. Repository cung cấp file mẫu [`src/.env.example`](src/.env.example); file `src/.env` chứa cấu hình thật được Git bỏ qua.

1. Cài và khởi động 9Router trong một terminal riêng:

   ```bash
   npm install -g 9router
   9router --host 127.0.0.1 --port 20128 --no-browser
   ```

2. Mở [dashboard 9Router](http://127.0.0.1:20128/dashboard). Kết nối provider bạn muốn dùng và nhập API key hoặc đăng nhập tài khoản provider tại dashboard theo phương thức provider hỗ trợ. Sao chép **ID model đầy đủ** và **API key do 9Router tạo**.

3. Trong thư mục `pick_and_place_ur3e/src`, tạo cấu hình cá nhân:

   ```bash
   cp .env.example .env
   chmod 600 .env
   nano .env
   ```

   Điền theo bảng sau, lưu file rồi thoát trình soạn thảo:

   | Biến trong `src/.env` | Giá trị cần điền |
   | --- | --- |
   | `ROBOT_LLM_MODEL` | ID model đầy đủ lấy từ 9Router, gồm tiền tố provider nếu ID có tiền tố. Không điền riêng tên provider. |
   | `NINEROUTER_API_KEY` | API key do **9Router** tạo; API key của provider được điền trong dashboard ở bước 2. |
   | `ROBOT_LLM_BASE_URL` | Giữ `http://127.0.0.1:20128/v1` nếu 9Router chạy trên máy này ở cổng 20128. |

   Không cần thêm biến `PROVIDER`: 9Router định tuyến theo ID trong `ROBOT_LLM_MODEL`.

Task manager tự đọc `src/.env`. Những lần chạy sau không cần nhập lại hoặc export API key; giữ 9Router hoạt động và chạy các terminal bên dưới từ đúng bản clone. Sau khi clone trên máy khác, thực hiện lại bước cấu hình này bằng tài khoản của máy đó.

Nếu gặp `Set ROBOT_LLM_MODEL...`, kiểm tra đã tạo `src/.env` và điền model chưa. Nếu gặp `401 Invalid API key`, kiểm tra `NINEROUTER_API_KEY`. Nếu báo lỗi model/provider, kiểm tra ID model và kết nối provider trong dashboard. Sửa `.env` rồi khởi động lại task manager; không cần build lại chỉ vì đổi cấu hình.

## Chạy

Sử dụng đồng thời 3 terminal:
Terminal 1: (9router)
```bash
9router --host 127.0.0.1 --port 20128 --no-browser
```
Terminal 2: robot và gazebo

```bash
cd pick_and_place_ur3e/src
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch ur3_llm_control llm_robot.launch.py
```

Terminal 3: Nhập lệnh cho robot
```bash
cd pick_and_place_ur3e/src
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 run ur3_llm_control task_manager.py
```
