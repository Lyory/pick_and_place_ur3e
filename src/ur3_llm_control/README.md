# UR3e LLM control — mức cơ bản

Package ROS 2 Humble này chạy UR3e trong Gazebo Classic và dùng MoveIt 2 cho ba skill `pick(object)`, `place(object, zone)` và `home()`. LLM qua 9Router chỉ tạo JSON plan. `task_validator.py` kiểm tra skill, object, zone và thứ tự trước khi executor gửi yêu cầu tới robot. LLM không tạo joint trajectory.

## Môi trường

- Ubuntu 22.04, ROS 2 Humble, Gazebo Classic, MoveIt 2 và các package UR trong workspace hiện tại.
- Python package `openai` cho client API tương thích OpenAI của 9Router.
- UR3e chưa có gripper vật lý trong mô hình này. Plugin Gazebo mô phỏng thao tác giữ và thả vật; chuyển động robot vẫn do MoveIt 2 lập kế hoạch và controller Gazebo thực thi.
- Bàn và vật có collision object trong MoveIt Planning Scene. Mặt bàn Gazebo chỉ hiển thị để tránh va chạm vật lý trùng lặp làm mất ổn định mô phỏng; MoveIt kiểm tra va chạm với bàn, các khối và vật đang giữ.

## Build

```bash
cd ~/thuc_hanh_tuan2_ws/src
source /opt/ros/humble/setup.bash
python3 -m pip install --user openai
colcon build --packages-select ur3_llm_control
source install/setup.bash
```

Workspace được cung cấp có thư mục `install` ở `~/thuc_hanh_tuan2_ws/src`. Mỗi terminal mới cần `source /opt/ros/humble/setup.bash` và `source ~/thuc_hanh_tuan2_ws/src/install/setup.bash`.

## Cấu hình 9Router trên máy của bạn

9Router cần chạy trên máy này và lắng nghe ở `127.0.0.1:20128`. Kiểm tra bằng `ss -ltnp | grep ':20128'`. Nếu đang lắng nghe ở `0.0.0.0`, hãy dừng instance hiện tại bằng **Quit** ở tray hoặc `Ctrl+C` trong terminal đã mở nó rồi khởi động lại trên loopback.

Mở lại trong một terminal riêng, giữ terminal này đang chạy:

```bash
9router --host 127.0.0.1 --port 20128 --no-browser
```

Kiểm tra `ss -ltnp | grep ':20128'`: địa chỉ lắng nghe phải là `127.0.0.1:20128`, không phải `0.0.0.0:20128`. Mở `http://127.0.0.1:20128/dashboard` trong trình duyệt, kết nối provider của bạn rồi sao chép **API key do 9Router tạo**. Khóa/provider ở trong dashboard, không cần ghi vào mã nguồn hoặc gửi qua chat. Địa chỉ `127.0.0.1` là loopback cục bộ; package không cần biết địa chỉ API của provider phía sau 9Router.

Trong **chính terminal sẽ chạy task manager**, nhập khóa mà không hiện ra màn hình hoặc lưu vào lịch sử lệnh:

```bash
read -rsp '9Router API key: ' NINEROUTER_API_KEY; echo
export NINEROUTER_API_KEY
export ROBOT_LLM_MODEL='ag/gemini-3.8-flash'  # Ví dụ model Antigravity
export ROBOT_LLM_BASE_URL='http://127.0.0.1:20128/v1'
```

Nếu bạn dùng model Antigravity khác, thay `ag/gemini-3.8-flash` bằng ID model đó trong dashboard. Có thể xem danh sách qua `curl http://127.0.0.1:20128/v1/models`; model xuất hiện trong danh sách vẫn cần provider hoạt động. Nếu bạn đổi cổng, cập nhật `ROBOT_LLM_BASE_URL` trong terminal đó.

Nếu nhận `401 Invalid API key`, kiểm tra khóa 9Router và biến `NINEROUTER_API_KEY` ở **chính terminal** chạy task manager. Nếu nhận lỗi model/provider, chọn model đã kết nối và cập nhật `ROBOT_LLM_MODEL`.

## Chạy nhanh Terminal 3 ở những lần sau

Script `scripts/run_task_manager.sh` tự source ROS và workspace, dùng model Antigravity `ag/gemini-3.8-flash` và 9Router ở `127.0.0.1:20128`. Nếu đã lưu `NINEROUTER_API_KEY` trong Bash, chỉ cần:

```bash
~/thuc_hanh_tuan2_ws/src/ur3_llm_control/scripts/run_task_manager.sh
```

Nếu muốn script không hỏi key ở mỗi terminal mới, lưu **API key của 9Router** một lần trong tệp riêng ngoài workspace. Lệnh dưới đây nhận key kín từ bàn phím và ghi tệp chỉ tài khoản của bạn đọc được:

Chạy **một lệnh** sau, sau đó dán key ở lời nhắc và nhấn Enter. Khi dán, key sẽ không hiện trên màn hình:

```bash
~/thuc_hanh_tuan2_ws/src/ur3_llm_control/scripts/save_9router_key.sh
```

Tệp này chứa key dạng văn bản; giữ nó ngoài Git và không gửi cho người khác. Nếu đang dùng model Antigravity khác, thêm `export ROBOT_LLM_MODEL='ag/ID_MODEL'` vào tệp bằng trình soạn thảo. Script vẫn hỏi key nếu chưa có biến hoặc tệp riêng.

## Chạy demo

Terminal 1:

```bash
cd ~/thuc_hanh_tuan2_ws/src
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch ur3_llm_control llm_robot.launch.py
```

Có thể thêm `gazebo_gui:=false launch_rviz:=false` khi chỉ kiểm tra bằng terminal. Đợi Gazebo, `move_group`, controller và `robot_skills` khởi động xong.

Terminal 2, sau khi đặt hai biến 9Router ở trên:

```bash
cd ~/thuc_hanh_tuan2_ws/src
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 run ur3_llm_control task_manager.py
```

Nhập ví dụ `Put the red cube in zone B.` hoặc `Đưa vật màu đỏ sang vùng B.`. Terminal hiển thị `USER COMMAND`, `LLM PLAN`, `EXECUTION` và `TASK SUCCESS` nếu cả ba skill thành công. Câu lệnh tiếng Anh hoặc tiếng Việt được gửi nguyên văn tới LLM; chương trình không liệt kê cứng mọi câu lệnh.

Nếu muốn gửi lệnh qua topic thay cho terminal tương tác, chạy launch với `start_task_manager:=true` rồi gửi:

```bash
ros2 topic pub --once /ur3_llm_control/command std_msgs/msg/String \
  "{data: 'Move the blue cube to zone C.'}"
```

Biến môi trường 9Router phải được đặt trong **Terminal 1** nếu dùng cách gửi qua topic.

## Kiểm tra và giới hạn

- Cho phép đúng `red_cube`, `yellow_cube`, `blue_cube` và `zone_a`, `zone_b`, `zone_c`.
- Plan phải có thao tác gắp, đặt vật đang giữ và kết thúc bằng `home`. Skill hoặc tên không hợp lệ bị từ chối trước khi robot di chuyển.
- Mỗi skill trả trạng thái như `SUCCESS`, `INVALID_OBJECT`, `INVALID_ZONE`, `PLANNING_FAILED` hoặc `EXECUTION_FAILED`. Executor dừng ở lỗi đầu tiên.
- Vị trí cố định của robot, bàn, vật và zone nằm trong `config/scene.yaml` và `worlds/assignment2.world`; mô phỏng chưa có camera hay nhận dạng vật.
- Bàn gỗ kích thước 0,64 × 0,84 m có ba khối đỏ, vàng, xanh. Ba ô A/B/C có đáy 0,14 × 0,14 m, bốn thành và nhãn chữ; phần trống bên trong rộng 0,12 m cho khối lập phương 0,04 m. Mô hình Gazebo dùng màu riêng, và MoveIt Planning Scene cũng có cùng màu.
- `config/home_joints.yaml` giữ dáng dựng đứng gần mặc định, với khuỷu và cổ tay lệch khỏi góc kỳ dị. `config/staging_joints.yaml` là tư thế trung gian hướng xuống bàn. Chuyển động đi qua MoveIt, kiểm tra va chạm, giới hạn khớp, chiều cao đầu công cụ và Jacobian trước khi thực thi.
- Gazebo dùng cơ chế gắn/thả vật ảo, còn MoveIt kiểm tra vật đang cầm ở đúng khoảng lệch so với `tool0`. Robot dừng cách đáy ô 5 mm trước khi plugin đặt vật vào tâm ô.
- Muốn chạy lại demo từ vị trí ban đầu, dừng và khởi động lại launch.
- Controller mô phỏng xử lý góc cổ tay tương đương nhau theo chu kỳ `2π`. Các trajectory gửi đi giữ nguyên từ MoveIt 2 và chịu giới hạn joint của robot; giá trị thô trong `/joint_states` có thể khác một vòng `2π`.
- `config/student_config.yaml` đang để placeholder theo yêu cầu. Bài demo mức cơ bản không cần MSSV; trước khi demo nhiệm vụ cá nhân hóa, điền họ tên và MSSV thật.
