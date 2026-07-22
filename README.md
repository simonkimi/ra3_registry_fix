# ra3-registry-helper

用于修复《命令与征服：红色警戒 3》Windows 注册表项的命令行工具。会清理并重建 HKLM / HKCU 下的游戏注册表（32 位视图）。

## 构建

```bash
cmake -S . -B build
cmake --build build --config Release
```

产物：`build/Release/ra3-registry-helper.exe`（路径可能随生成器略有不同）。

写入 `HKLM` 通常需要管理员权限。

## 用法

```text
ra3-registry-helper.exe <action> --path <install_dir> --result <result_file> [--key <cd_key>]
```

### 参数

| 参数 | 必填 | 说明 |
|------|------|------|
| `action` | 是 | 目前仅支持 `fix` |
| `--path <install_dir>` | 是 | 游戏安装目录，会规范化后写入 `Install Dir` 等注册表值 |
| `--result <result_file>` | 是 | 结果文件路径；程序结束时写入执行结果 |
| `--key <cd_key>` | 否 | CD Key；省略时自动生成 UUID 格式密钥 |

### 示例

```bash
ra3-registry-helper.exe fix --path "D:\Games\Red Alert 3" --result "C:\Temp\ra3_fix_result.txt"
```

指定 CD Key：

```bash
ra3-registry-helper.exe fix --path "D:\Games\Red Alert 3" --key "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX" --result "C:\Temp\ra3_fix_result.txt"
```

## 结果文件

`--result` 指向的文件会被覆盖写入，格式为：

```text
OK
注册表修复成功
```

或：

```text
ERR
<错误信息>
```

## 退出码

| 退出码 | 含义 |
|--------|------|
| `0` | 成功 |
| `1` | 参数错误或注册表修复失败 |
