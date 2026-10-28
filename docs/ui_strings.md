# UI 中文文案维护

主界面文案集中维护在 `Core/Src/app/ui_strings_zh.txt`，使用 UTF-8 编码，可直接阅读和编辑中文。不要直接修改生成文件 `Core/Src/app/ui_strings_generated.h`。

在仓库根目录运行：

```powershell
python tools/generate_ui_strings.py Core/Src/app/ui_strings_zh.txt Core/Src/app/ui_strings_generated.h
```

生成头文件只包含 ASCII 字符，并把中文转换为独立 UTF-8 `\xNN` 字节，供 ARMCC 5 编译。每个十六进制字节使用独立转义，不会吞掉紧随其后的 A–F 或数字。格式占位符如 `%ld`、`%lu` 会原样保留。

文案表每行使用 `#define S_NAME "文字" /* 注释 */` 格式。宏名必须唯一且以 `S_` 开头。修改、增加或删除文案后重新运行脚本，再编译 Keil 工程。
