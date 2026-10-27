# Quy Tắc Bắt Buộc Khi Thao Tác Git (Git Rules) - FloraBot_IOT

Bất cứ khi nào Agent làm việc với Git trong repository này, BẮT BUỘC phải tuân thủ nghiêm ngặt các quy tắc sau:

1. **Commit Message:**
   - **Đọc lịch sử trước:** Luôn chạy `git log -n 5 --oneline` để xem định dạng commit của dự án trước khi tạo commit mới.
   - **Viết commit đúng phong cách:** Đặt tên commit ngắn gọn, rõ ràng theo đúng format hiện hữu (Conventional Commits `feat(...)`, `fix(...)`, `docs(...)`).
   - **KHÔNG chèn Co-Author:** Tuyệt đối KHÔNG thêm bất kỳ dòng `Co-authored-by: ...` nào vào commit message.

2. **Quy trình Phân nhánh (Branching):**
   - Nhánh `main` là nhánh chính duy nhất.
   - Luôn bắt đầu làm tính năng hoặc sửa lỗi bằng cách tạo nhánh phụ từ `main`:
     `git checkout main` -> `git pull origin main` -> `git checkout -b <branch_name>`
   - Tuyệt đối không commit hoặc code trực tiếp trên `main`.

3. **Đồng bộ code từ main (Syncing):**
   - Khi hoàn thành 1 chức năng trên nhánh phụ, luôn cập nhật code mới nhất từ `main` vào nhánh hiện tại trước khi tạo Pull Request:
     `git fetch origin main` -> `git merge origin/main`
   - Giải quyết toàn bộ conflict nếu có, sau đó kiểm tra và đảm bảo dự án build thành công.

4. **Xóa nhánh sau khi PR:**
   - Khi Pull Request đã được merge vào `main`, phải xóa nhánh phụ đó ở cả local (`git branch -d <branch_name>`) và remote (`git push origin --delete <branch_name>`).
