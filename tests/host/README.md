# AnxSync 主机侧测试

这些测试在**开发机上真编译、真运行**，不需要 PlatformIO，也不需要 OnePage 硬件。

## 为什么这样组织

`lib/AnxSync/` 里的五个模块被刻意写成**不依赖 Arduino / ESP-IDF 的纯 C++**：

| 模块 | 职责 |
|---|---|
| `AnxHttpDate` | WebDAV 的 RFC 1123 时间解析 / 格式化 |
| `AnxPropfindParser` | `207 Multi-Status` 响应解析 |
| `AnxPathMap` | 远端 href → SD 卡路径映射与安全校验 |
| `AnxSyncPlan` | 远端清单 × 本地清单 → 下载/替换/跳过决策 |
| `AnxProgressJson` | `anx/onepage/progress.json` 的读写与合并 |

这样最容易出错的部分（XML、时间、路径穿越、差量判断、JSON 往返）能在本机被真跑测试覆盖，
而不是靠「烧进设备看看」。网络与存储的最后一层（`AnxWebDavClient`、SD 写入）仍需真机验证。

## 运行

```powershell
# 单元测试（201 项检查）
powershell -ExecutionPolicy Bypass -File firmware\tests\host\run_tests.ps1
```

需要 MSVC（VS 2022 Build Tools 的 C++ 工作负载）。脚本会把 `TMP`/`TEMP` 指到
`tests\host\build\`：在某些沙箱环境下默认 `%TEMP%` 不存在，MSVC 链接器会因此报
`LNK1104`（找不到自己的临时文件）。

## 端到端对拍（真实 HTTP）

单元测试用的是手写响应；端到端对拍用的是**从真实 HTTP 服务器抓下来**的响应，
覆盖 `PROPFIND → 解析 → 决策` 整条链路。

```powershell
# 1. 起 mock WebDAV（模仿安读的 anx/ 目录）
python tools\mock_webdav.py --port 18080 --root tools\_fixtures --user reader --password secret

# 2. 抓一份真实 PROPFIND 响应
curl.exe -sS -o .probe\propfind_real.xml -X PROPFIND -H "Depth: 1" `
  -u reader:secret http://127.0.0.1:18080/anx/data/file/

# 3. 编译并运行端到端驱动
$lib = 'firmware\lib\AnxSync'
cl /nologo /utf-8 /std:c++20 /EHsc /I"$lib" `
   /Fo:build\ /Fe:build\anx_e2e.exe `
   firmware\tests\host\anx_e2e.cpp `
   $lib\AnxHttpDate.cpp $lib\AnxPropfindParser.cpp $lib\AnxPathMap.cpp $lib\AnxSyncPlan.cpp

build\anx_e2e.exe .probe\propfind_real.xml --expect-download 2 --expect-ignored 2
```

实测输出（fixtures 里 2 本 EPUB + 1 个 PDF + 1 个 TXT + 目录自身）：

```
parsed 5 entries
  [dir]    file                                     size=0   mtime=yes
  [file]   9f2c1ab34d5e6f708192a3b4c5d6e7f8.epub    size=19  mtime=yes
  [file]   a1b2c3d4e5f60718293a4b5c6d7e8f90.epub    size=36  mtime=yes
  [file]   book.pdf                                 size=17  mtime=yes
  [file]   notes.txt                                size=10  mtime=yes

plan: 2 to download, 0 to replace, 0 to skip, 2 ignored, 55 bytes
E2E OK
```

## 测试替身说明

`tools/mock_webdav.py` 只用 Python 标准库，实现了 `OPTIONS / PROPFIND / GET / HEAD /
PUT / DELETE / MKCOL`，带 Basic Auth。它返回的 `href` 是**绝对路径**形式
（`/anx/data/file/x.epub`）；Nextcloud 等真实服务器可能返回绝对 URL，`AnxPathMap::stripBaseUrl`
覆盖了这种情况并有单元测试。
