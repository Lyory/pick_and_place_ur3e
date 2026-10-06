## Build

```bash
cd pick_and_place_ur3e/src
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

Sau khi clone, làm theo [hướng dẫn cấu hình LLM trong README chính](../../README.md#cấu-hình-llm-sau-khi-clone-một-lần). Từ thư mục `pick_and_place_ur3e/src`:

```bash
cp .env.example .env
chmod 600 .env
nano .env
```

- Chọn/kết nối **provider** và điền thông tin đăng nhập hoặc API key của provider trong dashboard 9Router.
- Điền **ID model đầy đủ** của provider vào `ROBOT_LLM_MODEL` trong `.env`.
- Điền **API key do 9Router tạo** vào `NINEROUTER_API_KEY` trong `.env`.
- Giữ `ROBOT_LLM_BASE_URL=http://127.0.0.1:20128/v1` nếu dùng cổng mặc định của dự án.

Task manager tự đọc `.env` khi chạy bằng `ros2 run`; không cần export key trong mỗi terminal. Chạy từ thư mục `src` của bản clone và source đúng `install/setup.bash`. `.env` và `.venv/` được Git bỏ qua; chỉ `.env.example` được chia sẻ. Các giá trị có trong `.env` thay thế giá trị đã export trong terminal. Nếu dùng nhiều workspace, có thể chỉ định file bằng `export ROBOT_LLM_ENV_FILE="/duong/dan/ban-clone/src/.env"`.

Nếu nhận `Set ROBOT_LLM_MODEL...`, kiểm tra file `.env` và điền ID model. Nếu nhận `401 Invalid API key`, kiểm tra key do 9Router tạo. Nếu nhận lỗi model/provider, kiểm tra ID model và provider đã kết nối trong dashboard. Sau khi sửa cấu hình, khởi động lại task manager.

## Chạy nhanh Terminal 3 ở những lần sau

Script `scripts/run_task_manager.sh` tự source ROS và workspace. Khi có `.env`, script để task manager đọc các giá trị trong file và không hỏi key; chỉ cần:

```bash
./ur3_llm_control/scripts/run_task_manager.sh
```

Nếu không dùng `.env`, script vẫn hỗ trợ cách lưu key cũ trong tệp riêng ngoài workspace. Lệnh dưới đây nhận key kín từ bàn phím và ghi tệp chỉ tài khoản của bạn đọc được:

Chạy **một lệnh** sau, sau đó dán key ở lời nhắc và nhấn Enter. Khi dán, key sẽ không hiện trên màn hình:

```bash
./ur3_llm_control/scripts/save_9router_key.sh
```

Tệp này chứa key dạng văn bản; giữ nó ngoài Git và không gửi cho người khác. Thêm `export ROBOT_LLM_MODEL='ID_MODEL_DAY_DU_TU_9ROUTER'` vào tệp bằng trình soạn thảo. Script hỏi model và key nếu cấu hình còn thiếu; không chọn sẵn provider cho người dùng.

## Chạy demo


Nhập ví dụ `Put the red cube in zone B.` hoặc `Đưa vật màu đỏ sang vùng B.`. Terminal hiển thị `USER COMMAND`, `LLM PLAN`, `EXECUTION` và `TASK SUCCESS` nếu cả ba skill thành công. Câu lệnh tiếng Anh hoặc tiếng Việt được gửi nguyên văn tới LLM; chương trình không liệt kê cứng mọi câu lệnh.

Để chạy nhiệm vụ theo hai chữ số cuối MSSV, nhập đúng hai chữ số tại dấu nhắc. Với MSSV `23020760` đang ghi trong `config/student_config.yaml`, nhập `60` (Zone A: Red, Zone B: Yellow, Zone C: Blue). Chương trình dùng chính hai chữ số vừa nhập để tính `P = XX % 6`; lệnh có chữ `mssv` hoặc `student id` vẫn dùng ID trong file cấu hình. Sau khi sửa mã nguồn, chạy lại `colcon build --packages-select ur3_llm_control` và `source install/setup.bash` trước khi khởi động task manager.

Nếu cả ba zone đã có vật, chương trình dùng ô `buffer` ở giữa bàn để tạm đặt một khối, sắp xếp các khối còn lại rồi chuyển khối từ `buffer` vào zone đích. Ô `buffer` phải trống khi nhiệm vụ MSSV hoàn tất. Sau khi build lại, khởi động lại Gazebo và task manager để nạp mô hình và vị trí mới.
