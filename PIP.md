# Performance Improvement Plan（PIP）

本文件记录当前代码审计和 NDS 真机路径诊断得到的性能改进顺序。它是
实施计划，不是已经完成的性能承诺；每项改动都必须先有可重复的基线和
真实游戏循环验收。

本轮已实现 PIP 中的第一批公共热路径：SDL 1:1 indexed blit、完整表面复制
命中该路径、`SDL_FillRect` 行填充、整数递增缩放、FBP/RLE 的半像素映射，
以及 ESP32 indexed 到 RGB565 的 palette LUT。极小屏幕的显式 rect present
已接入 Cardputer/Xiaomiao 的 LCD 窗口；palette 变化会强制整屏 present，避免
旧 palette 残留。NDS 仍保留双页整屏 flip；RIX 已停止在 ARM9 逐样本合成，
改由固定 PCM8 波表驱动 DS 硬件声道。

## 结论先行

改动前最明确的公共热点不是“两个 framebuffer”本身，而是索引表面之间的
通用 blit：`SDL_UpperBlit()` 对每个像素做一次 64 位除法，并在 ARM9 上
落成 `__aeabi_ldivmod`。NDS 标题画面中，禁用音频后的更新间隔仍约为 21 个
VBlank（约 350 ms）；只绕过
`VIDEO_CopyEntireSurface(gpScreenBak, gpScreen)` 后，更新间隔降到约 5 个
VBlank（约 83 ms）。这说明完整表面复制是标题卡顿的主要剩余来源，两个显示
页不是首要原因。启用 OPL2 后又增加约 27 个 VBlank，音频合成是第二个独立
热点。

这里的除法量是“一次 y 除法/行 + 一次 x 除法/像素”，不是每个像素两次；
在 256x192 全屏复制中约为 192 + 49,152 次，且 ARM9 没有便宜的硬件 64 位
除法。1:1 分支应把这两类除法都变成零次。

第一步应放在公共 shim 中：为相同格式、相同尺寸、1:1 的 8-bit indexed
blit 增加逐行复制快路径，并让该路径完全绕开除法；通用缩放路径保留为
正确性兜底。`int64_t` 不应在快路径中使用，也不应为了快路径而删除通用
路径的溢出保护。

## 约束

- ESP32 三条硬件线和 NDS 分开评估；不要把 CoreS3 的总线假设套到
  Cardputer ADV 或 Xiaomiao。
- 嵌入式游戏代码不引入 `malloc`/容器隐式分配、运行时解压、通用 cache
  框架或第三块 framebuffer。
- Cardputer ADV 继续使用两个命名的 240x135x8-bit screen 和 4 KB RGB565
  DMA strip；Xiaomiao 为 160x128；NDS 为原生 256x192。
- 不用合成截图证明收益；基线和验收均来自真实 PAL 数据、真实 gameplay
  loop。host WebSocket 只用于可重复地准备状态和取样。
- 先测量再改变调度、dirty region 或资源所有权；不要以“看起来少拷贝”
  代替端到端帧时间和音频 underrun 数据。

## 优先级路线图

### P0：先消除可证明的除法和逐像素通用开销

#### 1. SDL 1:1 8-bit blit 快路径

位置：`esp32s3/native_engine_shim/sdl_shim.c` 的 `SDL_UpperBlit()`。

第一阶段只接受最安全、覆盖率最高的情况：源和目标均为 8-bit、格式和
palette 语义与现有 `convert_pixel()` 一致、尺寸相同、无缩放；源/目标
rect 可先限定为完整表面，随后再扩展到同尺寸 rect。每一行直接按 pitch
复制 `width` 个字节（对可能重叠的表面使用有明确定义的 `memmove` 语义，
对目标中的两个独立 screen 可使用 `memcpy`）。快路径放在 `SDL_UpperBlit`
中，`SDL_BlitSurface` 和 `SDL_BlitScaled` 自动共享它。

验收条件：

- 快路径每行只有地址递增和复制，不执行像素级 `read_pixel`、
  `write_pixel`、palette 转换或 `__aeabi_ldivmod`。
- pitch 大于 width、负/越界 rect、空表面和重叠表面的行为与原路径一致。
- NDS 标题诊断中，绕过 full-surface copy 的约 5-VBlank 基线应可复现；
  不以删除 framebuffer 或改变游戏逻辑换取结果。
- `make -C unix USE_SDL3=0 ws-check`、NDS native/DeSmuME 录制、
  `make -C esp32s3 cardputer-adv-music-check` 及相关 native view gate
  均通过。

#### 2. SDL_FillRect 的按行填充

位置：同一 shim 的 `SDL_FillRect()`。

8-bit surface 使用每行 `memset`；RGB565/ARGB 等常用格式使用已经编码好的
像素值按字或按行写入。保留边界裁剪和现有颜色转换语义。该项与 1:1 blit
一起实施，避免大量 UI 清屏继续走逐像素函数调用。

### P1：消除剩余的重复除法、全屏转换和音频调度抖动

#### 3. 通用缩放使用整数递增映射

对仍需缩放的 `SDL_UpperBlit()`，先计算 `step = source_extent /
destination_extent` 和余数，再用误差累加（Bresenham 风格）推进源坐标；
每行只计算一次 y 映射，x 循环不再做 64 位除法。任意尺寸、裁剪和溢出
仍走原有通用兜底。不要用生成的每分辨率常量表；如需要 scratch，使用
已有目标容量内的命名固定存储。

同样的策略用于 `embedded/pal_fullscreen_stretch.h`：当前
`PalFullScreenStretch_SourceCoordinate()` 在每个目的像素计算 64 位分子并
除法，`PalFullScreenStretch_BlitIndexedRow()` 和外层 y 循环会放大该成本。
应先证明映射序列与现有半像素取样完全相同，再替换热循环。

#### 4. 索引 palette 到 RGB565 的查表

位置：`esp32s3/main/cardputer_extreme_native_view.c` 的
`CardputerExtreme_CopyIndexedNativeStrip()` 及 Xiaomiao 对应路径。

palette 变化时预计算 256 项 RGB565 big-endian 表；每个像素只做一次索引
读取和 16 位存储，不再重复读取 RGBA 和 mask/shift。表是固定的 256 项，
不改变两个 screen/4 KB DMA strip 的所有权。记录 palette 版本，避免每次
present 重建。

#### 5. `VIDEO_UpdateScreen()` 的 present 合并和 dirty 区域调查

Cardputer/Xiaomiao 现在保留 `lpRect` 的有效边界并按 LCD 窗口发送，复用同一
个 4KB DMA strip；palette 变化会把本次更新升级为整屏。native title loop
使用 deferred palette API，把“改 palette 后立刻 present”合并到随后真正的
整帧 present，避免每帧重复一次 LCD 转换/传输。NDS 仍必须整屏更新：双页
flip 的隐藏页没有可安全复用的 dirty-region 所有权，不能只拷贝一个矩形而
不引入第三块 framebuffer。接下来仍应在 NDS 和 ESP32 分别测量：像素转换、
DMA/SPI、VBlank、palette 更新、未变化帧各占多少时间。按目标选择：

- 连续 palette/fade 操作合并到下一次真正的像素 present；
- 只有 framebuffer 变化时才发送；
- 若硬件和 UI 语义允许，再增加受边界约束的 dirty region。

不能假设 palette 变化可以“只传 palette”：indexed LCD 输出的每个像素都
可能随 palette 改变，必须以 LUT/端到端显示时间验证。

#### 6. NDS 音频泵和 OPL2 预算

位置：`nds/source/nds_music.cpp`、`nds/source/nds_board.c`。

已移除 ARM9 的 22.05kHz OPL2 逐样本合成、315-sample tick buffer 和 4096
sample ring。ARM9 现在只以 70Hz 解码 RIX 寄存器流，把音高、按键边沿、
音色和音量映射到九个固定 PCM8 波表；节奏模式复用三个声道。实际声道提交
由 Calico ARM7 音频服务完成。波表、正弦表、timer 表和当前曲目均为命名
固定 owner。

DeSmuME SDL disk-audio 验证已捕获到连续非零 PCM；音色属于硬件近似，仍需
真机听感验收。不要再恢复逐样本 PCM ring，也不需要为此引入自定义 ARM7
内核。

#### 7. 资源读取批量化

`palcommon.c` 的 FBP/资源路径存在逐行 `ReadNativeChunkRange` 的调用形态。
在不引入通用 cache 或改变 pack 所有权的前提下，测量 TF/NOR/SPI 上的
调用次数、seek、等待和实际字节数；对连续行合并成一次固定缓冲读取，或
复用已有生命周期内的命名缓冲。只在 trace 证明存储等待占到帧预算时实施。

### P2：算法和低频循环

#### 8. 场景 sprite 排序

`scene.c` 用 bubble sort 按 y 排序；Cardputer 的上限为 512 个 sprite，
最坏约 130k 次比较/交换。先记录每帧 sprite 数量、逆序度和排序时间，再
选择稳定的插入排序（近乎有序时更快）或使用固定容量的稳定临时数组。
不得使用 heap，且必须保持相同 y 的绘制顺序。

#### 9. fade/palette 计算缓存

`palette.c` 和 title 路径反复遍历 256 色并调用 `VIDEO_SetPalette()`。缓存
未变化的源 palette、预计算整数 fade step，并把 palette 提交合并到帧边界；
浮点循环不是当前 NDS 标题残余卡顿的首因，因此排在 blit、present 和 OPL2
之后。

#### 10. 低收益逐行操作

审查 `PAL_ApplyWave()` 等每行 `memmove`/`memcpy` 路径，在 profile 显示其
进入主帧预算后再做行宽、方向和临时缓冲优化；不要为几十次固定宽度运算
引入复杂抽象。

## 测量和验收协议

每个 P0/P1 改动都要同时保存“改动前”和“改动后”的：

- blit、像素转换、present、VBlank、audio render/pump 的 p50/p95/p99；
- NDS title/map/menu 的真实 gameplay capture，以及音频 underrun 计数；
- Cardputer 240x135、Xiaomiao 160x128 的 native host/目标数据；
- ELF/map/反汇编证据，确认快路径没有把 64 位除法带入热循环。

推荐顺序：先跑现有 WebSocket/DeSmuME 可重复 capture，再做单项 A/B；
不要用手工跳帧、调试器注入状态或合成截图作为性能证据。所有视觉验收
仍遵循 `embedded/UI_REVIEW_SOP.md`。

## 明确不做

- 不因为抖动而减少或重定义现有两个 framebuffer；先消除 full-surface
  copy 和 present 阻塞。
- 不把 `int64_t` 从通用边界检查中全局删除；只在已证明有界的热路径采用
  32 位递增/逐行复制。
- 不引入运行时资源解压、通用分配器、隐式容器 cache 或第三块 framebuffer。
- 不把 host WebSocket、网络调试或 NDS 专用调度编进 ESP32 固件。

当前状态：第一批实现已通过 shim、fullscreen-stretch、Cardputer native
view、native engine、NDS native build/check；仍需在真实 NDS/ESP32 gameplay
loop 上采集
端到端帧时间、present 时间和音频 underrun。诊断 ROM 和捕获文件位于被忽略
的 `tmp_ui/nds/perf-diagnosis-20260806/`，不属于发布资源。
