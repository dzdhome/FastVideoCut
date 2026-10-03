# FastVideoCut

Win32 / C++17 桌面工具：用 `ffmpeg` 做**黑屏自动检测**、**帧流缩略图时间线**，并进行**无损裁剪 / 合并**。

## 功能

- **黑屏检测** — `blackdetect` 滤镜（`d` / `pix_th` / `pic_th` 可调），把黑屏区间切成独立分段
  · **只扫片头/片尾**：默认只解码**前 180 秒和后 180 秒**（片头结束、片尾开始基本都在这里），
    视频短于 6 分钟时自动整段扫描。设置里「只扫片头/片尾(秒)」可改，`0` = 整段扫描，
    命令行 `--scan-window <秒>`。实测 16 分钟 1080p 素材 **14.1s → 6.2s**，且片头/片尾边界一处不漏
  · **检测结果逐条写进日志**（`· 黑屏位置：00:02:06.240-00:02:07.200, …`），方便直接核对
  · 逐帧精确扫描，不会漏掉不含关键帧的短黑屏
- **分段选择** — 时间线 + 列表双视图，支持保留 / 丢弃 / 反选 / 只留选中
  · **文件列表默认隐藏**：工具栏「列表」按钮（或 `Ctrl+L`、菜单「视图」）在
    **文件列表**和**视频列表（帧流）**之间切换，选择会记在设置里
  · 列表里多出 **起始时间 / 结束时间** 两列，直接显示当前选择的区间端点（`00:00:12.480`）
  · **视频很多也能翻**：右侧垂直滚动条 + **滚轮上下翻视频**（`Ctrl+滚轮` 仍是缩放，
    `Shift+滚轮` / 中键拖动是横向平移），`↑` `↓` `PgUp` `PgDn` 也能翻行
- **增量黑屏检测** — `F6` / 「检测黑屏」**只检测还没检测过的视频**，新增或移除文件后再点
  不会把已经算好的结果重跑一遍（日志会写 `N 个待检测，跳过 M 个已检测`）。
  需要重算全部时用 `Shift+F6` 或菜单「视频 → 重新检测全部黑屏」
- **右侧预览窗** — 单击时间线上的任一分段即自动开始播放该段（视频 + 音频），
  面板自带「暂停/继续、停止」按钮与进度信息，切换分段会无缝切换
- **无损导出** — `-c copy` 流复制裁剪（输出端精确裁切），`concat` 封装合并；可切换为 `libx264` 重编码
  · **连续选中的片段只裁一次**：选中的相邻片段会被合并成一次 `Trim`（从第一个片段开头到最后一个片段结尾），
    而不是切成很多段再 concat —— 后者会在每个关键帧重新起播，播放时出现闪烁
  · **裁切起点自动对齐关键帧**：流复制只能从关键帧开始，否则导出的文件开头会出现
    「只有声音、画面要过几秒才出来」。所以起点会先回退到**前一个黑屏段的开头**，
    再对齐到前一个关键帧（`Project::BuildCopyRuns`），调整量会写在日志里
- **多视频合并** — 按列表顺序拼接为一个文件
- **缩略图时间线** — `fps` 抽帧 + `tile` 拼图生成马赛克，磁盘缓存，块式按需展开
- **纯 Win32** — 无 MFC / Qt / WTL，设置对话框由内存 `DLGTEMPLATE` 构建，无 `.rc` 对话框资源

## 构建

需要 MinGW-w64（带 `g++`、`windres`）和一份 ffmpeg。

```powershell
.\build.ps1                 # 编译 GUI + SelfTest
.\build.ps1 -Test           # 编译并跑自测
.\build.ps1 -Mingw D:\mingw64\bin -Ffmpeg D:\ffmpeg\bin
```

产物在 `_test\` 下：`FastVideoCut.exe`（静态链接 GUI）与 `SelfTest.exe`（控制台自测）。

## 命令行

```
FastVideoCut.exe [options] <video files...>

  --ffmpeg <dir>      ffmpeg bin 目录（含 ffmpeg.exe / ffprobe.exe）
  --output <dir>      输出目录
  --thumb <px>        时间线缩略图高度（24-240，默认 64）
  --black-min <sec>   blackdetect d=        （默认 0.10）
  --black-pix <val>   blackdetect pix_th=   （默认 0.10）
  --black-pic <val>   blackdetect pic_th=   （默认 0.98）
  --scan-window <sec> 只扫片头/片尾各 N 秒（默认 180，0 = 整段扫描）
  --reencode          导出时重编码（默认无损流复制）
  --full-detect       黑屏检测：全文逐帧精确扫描（默认，不漏检）
  --fast-detect       黑屏检测：关键帧粗扫（快约 6 倍，可能漏检短黑屏）
  --merge-all         合并为一个视频（默认每个输入各出一个）
  --auto-detect       启动后立刻开始黑屏检测
  --auto-export       检测完自动导出
  --nogui             不显示窗口（无对话框阻塞，适合自动化）
  --log <file>        把每条日志与 ffmpeg 命令追加写入 <file>（UTF-8）
  --quit              任务结束后退出（退出码 0 = 成功，1 = 失败）
```

无头批处理示例：

```powershell
.\_test\FastVideoCut.exe --nogui --auto-export --quit --merge-all `
    --ffmpeg C:\ffmpeg\bin --output D:\out\ --log D:\out\run.log a.mp4 b.mp4
```

## 自测

```powershell
$env:FASTVIDEOCUT_FFMPEG='C:\ffmpeg\bin'
.\_test\SelfTest.exe .\_test\media\v1.mp4 .\_test\media\v2.mp4
```

覆盖：参数转义 / 时间码解析、ffmpeg 定位与 probe、黑屏检测、分段模型与选区统计、马赛克尺寸、无损裁剪+合并导出时长校验、多视频合并。

## 实现要点 / 注意

- **检测提速的取舍**（实测，同一个 997s / 1080p 素材）：
  `scale` 滤镜（20.4s）、`fps=10`（27.6s）、`-hwaccel auto`（38.3s）、`-skip_frame nokey`（1.1s，
  但会漏检不含关键帧的短黑屏）、`-skip_frame noref`（14.2s，仍漏 1 段）、
  分块并行（1.24x）—— 逐帧精确解码无法在不丢结果的前提下提速。
  最终采用**按业务范围扫描**：只解码片头/片尾各 180 秒（`Ffmpeg::ScanWindow` + `-ss`/`-copyts`
  保留绝对时间戳），边界结果与整段扫描完全一致，耗时降到 1/2~1/3。
- **预览窗（`src/Preview.cpp`）**：两条 `ffmpeg` 管道线程 —— 视频解码为
  `rawvideo bgr24`（按帧间隔定时、GDI 绘制），音频解码为 `s16le` 交给 `waveOut`
  （无音频设备时自动降级为静音）。
  为避免闪烁：帧先缩放进**离屏位图**（只在真正有新帧时重绘），窗口用一次 `BitBlt`
  更新；每帧只重绘视频区，底栏不动；帧在 worker 与 UI 之间用指针交换传递，不做拷贝
  （注意交换后读取缓冲会变成「上一帧」，必须重新 `resize`，否则越界写）；
  缩放模式用 `COLORONCOLOR`（`HALFTONE` 每帧都要重采样，既慢又会看出闪动）；
  解码跟不上时直接丢帧并重新对时，避免追赶式卡顿。
  线程只通过 `PostMessage` / `PostUiMessage` 回到 UI 线程更新界面，停止时先
  `TerminateProcess` 再 join，因此切换分段、启动导出、关闭窗口都不会被卡住。
- **连续裁切**：`VideoItem::selectedRuns()` 把相邻的选中片段合并成一个区间，
  导出时每个区间只调用一次 `Ffmpeg::Trim`；
  `Project::BuildCopyRuns()` 在此基础上再把起点回退到前一个黑屏段并对齐关键帧
  （`Ffmpeg::KeyframeTimeBefore()` 用 ffprobe 查关键帧）。
- **无损裁切必须用输出端 `-ss`**：输入端 `-ss` 会退回到前一个关键帧，导致每段多出最多一个 GOP（选区越切越长、黑屏切不干净）。重编码模式则用输入端快速定位（精确且更快）。
- 裁切点落在非关键帧时，播放器在该点后最多一个 GOP 内可能出现短暂花屏；如无法接受请在设置里改用“重编码导出”。
- 所有 `ffmpeg` 调用都走 `CreateProcess` + 管道捕获（`src/Process.cpp`），可随时取消（`CancelToken`）。
- 编码统一 UTF-8：宽字符 ↔ UTF-8 转换见 `src/Utf.cpp`，宽字符格式化用 `FormatString`（`%s` = `wchar_t*`，MSVC / MinGW 一致）。

## 目录

```
src/        主程序（Utf / Process / Ffmpeg / Project / Settings / Timeline /
            MainWindow / SettingsDialog / main + app.rc + resource.h）
tests/      SelfTest 控制台自测
_test/      构建产物、obj、测试媒体与导出样例（不入库）
```

## 开源授权

本项目采用 **GNU General Public License v3.0 或更高版本**（`GPL-3.0-or-later`），
完整条款见 [`LICENSE`](LICENSE)。

```
Copyright (C) 2026 dzdhome

FastVideoCut - ffmpeg based black frame detector and lossless video trimmer

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program.  If not, see <https://www.gnu.org/licenses/>.
```

**GPL-3.0 是强 copyleft**：你可以自由使用本软件（包括闭源商用），
但**对外分发**二进制或源码时必须：

1. 附带本许可证全文与上述版权声明；
2. 提供**完整对应源码**（GitHub 上的 tag `v1.0.0` 即满足此要求）；
3. 若修改过软件，需注明修改内容（GPL 第 5 条）。
   本项目未启用「安装提示」条款，因此衍生作品无需与本项目采用同一许可证。

仅自己使用、不对外分发时不受上述条款约束。
程序**不含与 GPL 冲突的第三方代码**；运行时依赖 `ffmpeg` / `ffprobe`，
它们是独立进程、通过命令行调用，未与本项目静态链接，因此不构成分发的一部分
（本项目未捆绑 ffmpeg，需自行安装并指定路径）。
