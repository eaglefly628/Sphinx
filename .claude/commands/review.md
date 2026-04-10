# 代码审查指令

审查当前分支的最新提交，检查以下要点：

1. **编译安全**：是否有 API 不兼容（参考 docs/AngelScript_Guide.md 踩坑记录）
2. **规范遵守**：是否符合 CLAUDE.md 代码约定（前缀、日志类别、目录结构）
3. **AngelScript 优先**：新功能是否应该用 AS 而非 C++
4. **跨 agent 影响**：是否修改了其他 agent 的文件，是否通知了对方 SHARED.md
5. **版本标签**：SHARED.md 更新是否带版本标签
6. **测试覆盖**：改动是否需要新增测试

发现问题写入对应 agent 的 SHARED.md TODO 区域。
