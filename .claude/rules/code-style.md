# 代码风格规则

- C++ 前缀：E（枚举）、F（结构体）、U（UObject）、A（Actor）
- 日志统一用 `LogGIS` 类别
- 关键流程用 `===== START =====` / `===== END =====` 标记
- AngelScript 文件放 `Script/` 目录，写前必读 `docs/AngelScript_Guide.md`
- 新功能优先 AngelScript，仅性能敏感/底层需求用 C++
