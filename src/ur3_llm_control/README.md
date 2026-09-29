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

File `.env` ở gốc `~/thuc_hanh_tuan2_ws/src` chứa `NINEROUTER_API_KEY`, `ROBOT_LLM_MODEL` và `ROBOT_LLM_BASE_URL`. Task manager tự đọc file này khi cần gọi LLM, kể cả khi chạy trực tiếp bằng `ros2 run`; không cần `export` key trong mỗi terminal. `.env` và `.venv/` đã được thêm vào `.gitignore`, và `.env` chỉ cho chủ tài khoản đọc/ghi. Không đưa key vào README hoặc mã nguồn.

Giá trị model Antigravity đang đặt trong `.env` là `ag/gemini-3.8-flash`,Hãy thay bằng **ID model đúng trong dashboard** nếu khác. Có thể xem danh sách qua `curl http://127.0.0.1:20128/v1/models`; model xuất hiện trong danh sách vẫn cần provider hoạt động. Nếu đổi cổng 9Router, sửa `ROBOT_LLM_BASE_URL` trong `.env`. Các giá trị trong `.env` được dùng ngay cả khi terminal trước đó đã `export` giá trị cũ.

Nếu nhận `401 Invalid API key`, kiểm tra API key do 9Router tạo trong `.env`. Nếu nhận lỗi model/provider, kiểm tra `ROBOT_LLM_MODEL` trong cùng file.

## Chạy nhanh Terminal 3 ở những lần sau

Script `scripts/run_task_manager.sh` tự source ROS và workspace. Khi có `.env`, script để task manager đọc các giá trị trong file và không hỏi key; chỉ cần:

```bash
~/pick_and_place_ur3e/src/ur3_llm_control/scripts/run_task_manager.sh
```

Nếu không dùng `.env`, script vẫn hỗ trợ cách lưu key cũ trong tệp riêng ngoài workspace. Lệnh dưới đây nhận key kín từ bàn phím và ghi tệp chỉ tài khoản của bạn đọc được:

Chạy **một lệnh** sau, sau đó dán key ở lời nhắc và nhấn Enter. Khi dán, key sẽ không hiện trên màn hình:

```bash
~/pick_and_place_ur3e/src/ur3_llm_control/scripts/save_9router_key.sh
```

Tệp này chứa key dạng văn bản; giữ nó ngoài Git và không gửi cho người khác. Nếu đang dùng model Antigravity khác, thêm `export ROBOT_LLM_MODEL='ag/ID_MODEL'` vào tệp bằng trình soạn thảo. Script vẫn hỏi key nếu chưa có biến hoặc tệp riêng.

## Chạy demo


Nhập ví dụ `Put the red cube in zone B.` hoặc `Đưa vật màu đỏ sang vùng B.`. Terminal hiển thị `USER COMMAND`, `LLM PLAN`, `EXECUTION` và `TASK SUCCESS` nếu cả ba skill thành công. Câu lệnh tiếng Anh hoặc tiếng Việt được gửi nguyên văn tới LLM; chương trình không liệt kê cứng mọi câu lệnh.

Để chạy nhiệm vụ theo hai chữ số cuối MSSV, nhập đúng hai chữ số tại dấu nhắc. Với MSSV `23020760` đang ghi trong `config/student_config.yaml`, nhập `60` (Zone A: Red, Zone B: Yellow, Zone C: Blue). Chương trình dùng chính hai chữ số vừa nhập để tính `P = XX % 6`; lệnh có chữ `mssv` hoặc `student id` vẫn dùng ID trong file cấu hình. Sau khi sửa mã nguồn, chạy lại `colcon build --packages-select ur3_llm_control` và `source install/setup.bash` trước khi khởi động task manager.

Nếu cả ba zone đã có vật, chương trình dùng ô `buffer` ở giữa bàn để tạm đặt một khối, sắp xếp các khối còn lại rồi chuyển khối từ `buffer` vào zone đích. Ô `buffer` phải trống khi nhiệm vụ MSSV hoàn tất. Sau khi build lại, khởi động lại Gazebo và task manager để nạp mô hình và vị trí mới.
