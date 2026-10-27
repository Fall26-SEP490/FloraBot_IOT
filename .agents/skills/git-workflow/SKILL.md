---
name: git-workflow
description: >-
  Use this skill whenever working with Git in FloraBot_IOT: creating feature branches from main,
  inspecting prior commit message conventions, committing changes without co-author tags,
  syncing the latest changes from main into the feature branch, resolving conflicts,
  and cleaning up/deleting merged branches after pull requests.
---

# Quy trình Git Chuẩn (Git Workflow) cho FloraBot_IOT

Tài liệu hướng dẫn quy trình làm việc chuẩn mực với Git và GitHub dành cho AI và lập trình viên trong dự án FloraBot_IOT.

---

## 1. Nguyên Tắc Cốt Lõi (Core Rules)

1. **Nhánh `main` là nhánh chính duy nhất (Source of Truth):**
   - Tuyệt đối không code trực tiếp trên nhánh `main`.
   - Mọi chức năng/sửa lỗi mới đều phải được phân nhánh từ `main`.

2. **Commit Message tuân thủ phong cách dự án & Không có Co-Author:**
   - **BƯỚC BẮT BUỘC:** Trước khi commit, AI phải đọc 5 - 10 commit gần nhất bằng lệnh:
     ```bash
     git log -n 10 --oneline
     ```
   - Nhận diện cách đặt tên của nhóm (ví dụ: `Conventional Commits` như `feat(...)`, `fix(...)`, `docs(...)`) để viết commit message đồng nhất.
   - **TUYỆT ĐỐI KHÔNG thêm thông tin Co-author** (Không chèn `Co-authored-by: ...`, không thêm chữ ký AI bot vào commit message).

3. **Luôn đồng bộ code mới nhất từ `main` trước khi tạo PR:**
   - Sau khi hoàn thành chức năng trên nhánh phụ, phải kéo code mới nhất từ `main` đắp vào nhánh hiện tại: *"Lấy code mới từ main bỏ vào nhánh tôi, giải quyết conflict nếu có"*.
   - Đảm bảo kiểm tra, build và test không còn lỗi xung đột trước khi đẩy lên PR.

4. **Xóa nhánh phụ sau khi Pull Request hoàn tất:**
   - Khi PR đã được merge vào `main`, bắt buộc phải xóa nhánh phụ ở cả Local và Remote để giữ kho mã nguồn sạch sẽ.

---

## 2. Quy Trình Chi Tiết Từng Bước (Step-by-Step Runbook)

### Bước 1: Tách nhánh phụ từ `main`
Trước khi bắt đầu làm bất kỳ chức năng nào:
```bash
# 1. Chuyển về nhánh main
git checkout main

# 2. Cập nhật code mới nhất từ GitHub về máy
git pull origin main

# 3. Tạo và chuyển sang nhánh phụ mới (đặt tên theo convention: feature/... hoặc fix/...)
git checkout -b feature/<ten-chuc-nang>
# Ví dụ: git checkout -b feature/esp32-solenoid-lock
```

---

### Bước 2: Commit Code (Đọc lịch sử trước + Không gắn Co-Author)
Khi hoàn thành một phần việc và chuẩn bị commit:
```bash
# 1. AI đọc 10 commit gần nhất để học phong cách đặt tên của dự án
git log -n 10 --oneline

# 2. Kiểm tra các file thay đổi
git status

# 3. Thêm các file cần commit (chú ý tránh commit nhầm file rác / file nhạy cảm)
git add <cac-file-thay-doi>

# 4. Thực hiện commit theo đúng format đã học, TUYỆT ĐỐI KHÔNG để Co-authored-by
git commit -m "<type>(<scope>): <mo ta ngan gon ve chuc nang>"
# Ví dụ: git commit -m "feat(iot): add solenoid lock relay pulse actuation"
```

---

### Bước 3: Lấy code mới từ `main` bỏ vào nhánh của tôi & Xử lý Conflict
Khi code xong hoàn chỉnh 1 chức năng, trước khi đẩy lên tạo PR:
```bash
# 1. Kéo dữ liệu mới nhất từ remote
git fetch origin main

# 2. Hợp nhất code mới nhất từ main vào nhánh phụ hiện tại
git merge origin/main

# 3. NẾU CÓ CONFLICT (XUNG ĐỘT):
# - Mở các file bị conflict, trao đổi và giữ lại code chính xác.
# - Sau khi sửa xong conflict:
git add <cac-file-da-giai-quyet-conflict>
git commit -m "merge: sync latest changes from main and resolve conflicts"

# 4. Kiểm tra lại việc build và chạy thử xem có lỗi phát sinh không
```

---

### Bước 4: Đẩy nhánh lên GitHub & Tạo Pull Request
```bash
# 1. Push nhánh phụ lên GitHub
git push -u origin feature/<ten-chuc-nang>

# 2. Lập trình viên vào GitHub tạo Pull Request từ feature/<ten-chuc-nang> vào main
```

---

### Bước 5: Xóa nhánh phụ sau khi PR đã được Merge
Sau khi Pull Request đã được kiểm tra (Review) và gộp (Merged) vào nhánh `main`:
```bash
# 1. Chuyển về lại nhánh main
git checkout main

# 2. Kéo code main mới nhất (đã chứa tính năng vừa merge) về máy
git pull origin main

# 3. Xóa nhánh phụ ở Local (máy cá nhân)
git branch -d feature/<ten-chuc-nang>

# 4. Xóa nhánh phụ trên Remote (GitHub) nếu GitHub chưa tự động xóa
git push origin --delete feature/<ten-chuc-nang>
```

---

## 3. Bảng Tra Cứu Lệnh Nhanh (Cheatsheet)

| Yêu cầu của bạn | Thao tác lệnh Git tương ứng |
| :--- | :--- |
| **"Tách nhánh mới từ main"** | `git checkout main && git pull origin main && git checkout -b <nhanh-moi>` |
| **"Commit code"** | `git log -n 5` $\rightarrow$ `git add ...` $\rightarrow$ `git commit -m "..."` *(Không có Co-Author)* |
| **"Lấy code mới từ main bỏ vào nhánh tôi"** | `git fetch origin main && git merge origin/main` |
| **"Giải quyết conflict"** | Sửa file $\rightarrow$ `git add <file>` $\rightarrow$ `git commit -m "resolve conflicts"` |
| **"Xóa nhánh sau khi PR"** | `git checkout main && git pull && git branch -d <nhanh-phu> && git push origin --delete <nhanh-phu>` |
