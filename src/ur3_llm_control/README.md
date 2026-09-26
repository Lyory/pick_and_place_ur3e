## Build

```bash
cd ~/thuc_hanh_tuan2_ws/src
source /opt/ros/humble/setup.bash
python3 -m pip install --user openai
colcon build --packages-select ur3_llm_control
source install/setup.bash
```


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

Terminal 2, sau khi đặt hai biến 9Router ở trên:

```bash
cd ~/thuc_hanh_tuan2_ws/src
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 run ur3_llm_control task_manager.py
```

Nhập ví dụ `Put the red cube in zone B.` hoặc `Đưa vật màu đỏ sang vùng B.`. Terminal hiển thị `USER COMMAND`, `LLM PLAN`, `EXECUTION` và `TASK SUCCESS` nếu cả ba skill thành công. Câu lệnh tiếng Anh hoặc tiếng Việt được gửi nguyên văn tới LLM; chương trình không liệt kê cứng mọi câu lệnh.

Để chạy nhiệm vụ theo hai chữ số cuối MSSV, nhập đúng hai chữ số tại dấu nhắc, ví dụ `23` (Zone A: Blue, Zone B: Yellow, Zone C: Red). Với MSSV `23020760` đang ghi trong `config/student_config.yaml`, nhập `60` (Zone A: Red, Zone B: Yellow, Zone C: Blue). Chương trình dùng chính hai chữ số vừa nhập để tính `P = XX % 6`; lệnh có chữ `mssv` hoặc `student id` vẫn dùng ID trong file cấu hình. Sau khi sửa mã nguồn, chạy lại `colcon build --packages-select ur3_llm_control` và `source install/setup.bash` trước khi khởi động task manager.

Nếu cả ba zone đã có vật, chương trình dùng ô `buffer` ở giữa bàn để tạm đặt một khối, sắp xếp các khối còn lại rồi chuyển khối từ `buffer` vào zone đích. Ô `buffer` phải trống khi nhiệm vụ MSSV hoàn tất. Sau khi build lại, khởi động lại Gazebo và task manager để nạp mô hình và vị trí mới.

## Các skill của robot

Ba skill mà planner được phép gọi, được nhóm cạnh nhau trong `src/robot_skills.cpp`:

| Skill | Tác dụng | Tham số hợp lệ |
| --- | --- | --- |
| `home()` | Đưa tay máy về tư thế Home sau khi đặt vật. | Không có |
| `pick(object)` | Gắp một khối từ vị trí hiện tại, kể cả `buffer`. | `red_cube`, `yellow_cube`, `blue_cube` |
| `place(object, zone)` | Đặt khối đang giữ vào ô còn trống. | Cùng ba tên vật; `zone_a`, `zone_b`, `zone_c` hoặc `buffer` |

`/execute_skill` nhận tên skill và tham số trong `srv/ExecuteSkill.srv`. `scripts/task_validator.py` kiểm tra tên, tham số và thứ tự trước khi `scripts/skill_executor.py` gửi từng bước cho robot. `moveToPose`, `moveLinearToPose` và thao tác gắn/thả trong Gazebo là hàm nội bộ của ba skill trên. Mô hình hiện tại dùng cơ chế gắn vật ảo, nên không có skill `open_gripper()` hoặc `close_gripper()` riêng.
