# 构建 JTDX-F V3.6.0

## 源码基线和工具链

用户提供的 `jtdx-master.zip` 保留不变；其中 `Versions.cmake` 为 JTDX 2.2.159。本分支使用 C、C++11（JTTY 助手使用 C++17）和 Fortran，CMake 构建。

本次验证环境：Linux x86_64，GCC/G++/GFortran 16.2.1；Windows 交叉编译为 x86_64 MinGW-w64 GCC/GFortran 16.2.0、UCRT，配合 MSYS2 UCRT64 的 Qt 5.15.19、FFTW 3、Boost 和 Hamlib 4.7.2。完整包版本及 SHA-256 见发行源码附带的依赖清单。

Windows 运行库使用同一种 UCRT 体系，避免混用 MSVCRT 与 UCRT 的 C/C++ 运行库。未使用 `-march=native`，不要求构建机器相同的 CPU。优化器升级本身不保证解码速度提高；应使用相同音频、设置和硬件进行实测比较。

## 当前工作区构建

源码包保留 `jtdx-master/`、`scripts/` 两个同级目录。当前工作区下载的依赖在 `.deps/`，不更改系统编译器。

Linux：

```bash
source scripts/env.sh
cmake -S jtdx-master -B build -G Ninja \
  -DCMAKE_Fortran_COMPILER="$PWD/scripts/gfortran-local" \
  -DCMAKE_BUILD_TYPE=Release \
  -DWSJT_GENERATE_DOCS=OFF -DWSJT_SKIP_MANPAGES=ON \
  -DCMAKE_INSTALL_PREFIX="$PWD/install"
cmake --build build -j6
cmake --install build
ctest --test-dir build --output-on-failure
```

运行本机版本：`scripts/run-linux.sh`。这是针对当前 Linux 环境构建的程序，不是跨发行版的通用 Linux 安装包。

Windows：

```bash
scripts/build-windows.sh
```

脚本使用 `scripts/windows-toolchain.cmake`，输出在 `build-windows-ucrt/`，安装树在 `install-windows/`。`scripts/package_windows.py` 递归检查 PE 导入表、复制所需 DLL 和 Qt 插件，任何未解析的非系统 DLL 会使打包失败。`runtime-manifest.json` 记录所含 PE 文件的 SHA-256 和导入表。

生成安装程序：`scripts/make-windows-installer.sh`，采用 NSIS，默认输出 `release/JTDX-F-V3.6.0-Windows-x64-Setup.exe`。Wine 只在 Linux 构建安装器和兼容性测试时使用，Windows 用户不需要 Wine。所有 Wine 操作放在工作区的独立前缀和 Xvfb 显示中。

## 在其他环境准备依赖

常规原生构建需要 Qt 5 的 Core、Gui、Widgets、Network、Multimedia、SerialPort、WebSockets、LinguistTools；测试还需要 Test。Windows 另外需要 ActiveQt。还需要 GCC/GFortran、FFTW 的单精度及线程库、Boost 头文件、Hamlib、libusb、CMake 和 Ninja。可关闭 `BUILD_TESTING` 以跳过测试构建。

下载脚本 `scripts/download_windows_ucrt_deps.py` 优先读取源码包中的 `dependencies/manifest-ucrt64.json` 并验证 SHA-256；没有该清单时才读取已转换为 JSON 的 MSYS2 数据库 `.deps/msys2/db-ucrt64.json`。清单记录本次实际使用的版本。它不是保证永久可用的二进制镜像；包仓库更新后应使用相匹配的完整依赖集合。

libusb 1.0.30 和 Hamlib 4.7.2 使用官方源代码交叉构建，关键命令如下（从各自独立构建目录执行，`FM_PREFIX` 指向 UCRT64 前缀）：

```bash
../libusb-1.0.30/configure --host=x86_64-w64-mingw32 \
  --prefix="$FM_PREFIX" --disable-static
make -j6
make install

PKG_CONFIG_LIBDIR="$FM_PREFIX/lib/pkgconfig" \
../hamlib-4.7.2/configure --host=x86_64-w64-mingw32 \
  --prefix="$FM_PREFIX" --disable-static --without-cxx-binding \
  --without-readline --disable-winradio --without-lua-binding \
  --without-python-binding --without-tcl-binding --without-perl-binding
make -j6
make install
```

原生 Windows 可在 MSYS2 UCRT64 环境构建同一源码，并指定这些依赖的安装前缀。不要混用 `mingw64` 和 `ucrt64` 包。

`windows/omnirig` 提供生成的 ActiveQt 包装文件；`JTDX_OMNIRIG_GENERATED_DIR` 默认为该目录。它不包含 OmniRig 服务本身。若要使用本机注册的类型库重新生成，可将此 CMake 变量设为空；重新生成时需留意 Qt 5 `dumpcpp` 对失败的 COM 文档查询缺少检查的问题。

## 验证

`fm_regression` 使用真实主界面逻辑、临时配置和虚拟电台，不进行真实发射；包括监管边界、连续/单次通联、下一站时隙、MCP 控制、WAV 损坏与中文路径、HRD 分段/错误报文、解码进程故障恢复、人工发射授权、呼号管理、季度导入失败与重试，以及真实 FT8/SuperFox 模拟音频解码。

`fm_mcp_http` 使用标准库测试 HTTP、权限和 stdio 桥接；`tests/test_mcp_sdk.py` 可使用官方 `mcp` Python 包进一步验证。`fm_codec_memory` 检查 WSPR 堆栈译码器边界，另可用 AddressSanitizer 和 UndefinedBehaviorSanitizer 编译 `tests/CodecMemory.c` 运行。

`fm_decoder_buffers` 在启用 Fortran bounds 检查的情况下，以多线程验证解码缓冲区容量边界。SuperFox 接收代码及编码表来源见 `lib/superfox/PROVENANCE.txt`；模拟 WAV 是公开测试素材，用户日志不是测试包的一部分。可选的私有参考日志仅通过 `JTDXF_PRIVATE_LOG=/path/to/log.adi` 环境变量传入，测试在临时目录进行，原件保持不变。

Windows 回归在 Wine 11.20 中执行；真实 Windows 电台、声卡、驱动和长时间通联仍需目标硬件验收。这一限制不应被描述为所有 Windows 环境均已验证。


## V3.2.0 校时辅助程序

`jtdx-f-clock`（Windows 为 `.exe`）与主程序一起安装，仅负责 NTP 查询和设置系统 UTC 时钟。Windows 链接系统 Shell/权限 API；Linux 运行时需要 polkit 的 `pkexec`。主界面不需要整体以管理员身份运行。

`fm_ntp_tests` 验证协议、服务器列表、UTC/时区和授权生命周期。它只启动编译时启用假时钟的 `fm_ntp_helper`，不会设置宿主系统时间。两个测试程序均不安装到发行目录；生产辅助程序没有假时钟运行开关。

只读网络诊断：`jtdx-f-clock --query ntp1.aliyun.com`。这条命令仅打印 UTC、时间偏差及往返延迟，不申请权限、不校时。

安装脚本保存为 UTF-8 BOM，并在 `makensis` 命令中指定 `-INPUTCHARSET UTF8`，确保降级确认等中文字符串不会按系统 ANSI 代码页误读。`Unicode True` 仅控制输出程序，不能替代输入脚本编码声明。参考 [NSIS 命令行说明](https://nsis.sourceforge.io/Docs/Chapter3.html)。

## V3.4.0 新模式及审计检查

`jtdx-f-jtty` 与主程序一起安装，使用 Qt Core 和既有 Fortran 库，单独子进程串行处理键盘模式编解码。四组 `fm_jtty_*` Fortran 测试验证上游协议、纠错和连续接收；`fm_jtty_helper` 验证实际 PCM 编解码往返。`fm_wspr_codec` 检查畸形消息与 10000 个固定种子输入。

V3.4.0 的 CTest 为 13 组，V3.5.0 新增日志缓存、黑名单、现代解码、线程一致性和波形验证。审计构建可额外传 `-DCMAKE_Fortran_FLAGS="-fcheck=bounds -fbacktrace"`。多个主程序回归会使用同一共享内存测试键，应串行运行；测试不接电台、不改变宿主时钟。


## V3.5.0 新增验证

`fm_operator_log_cache` 检查日志缓存命中、内容变更、双日志同步及数据库失效；`fm_blacklist_tests` 检查按操作员持久化、精确匹配、原子保存、文件监测、损坏保护和编辑冲突。主界面回归覆盖“选中但不确认不切换”、发射与日志切换门控、黑名单显示和控制入口。

`fm_modern_decoder` 检查 CRC、重编码、有限输入和软比特恢复，`fm_modern_decoder_threads` 检查多个工作线程及 OSD 同时首次初始化；`fm_modern_waveform` 使用完整生成波形、同步和生产译码链进行传统/现代对照。正常 CTest 仅运行小规模波形回归，完整实验可执行：

```bash
python3 jtdx-master/tests/test_modern_waveform.py \
  build/fm_modern_waveform build/test-data --benchmark --messages mixed \
  --seed 20261031 --trials 30 --scenarios awgn impulse crowded \
  --snr8 -21 -22 -23 --snr4 -17 -18 -19 \
  --output logs/modern-waveform-reproduction.json
```

请先看脚本 `--help` 核对可用参数；各组种子、线程、SNR 和场景会写入结果。定时比较应停止并行编译和其他高负载任务。所有音频均在测试进程内生成，不使用声卡或电台，也不读取用户日志。候选算法研究代码保留在 `tests/ModernDecoderCandidates.f90` 和 `tests/ModernCandidateBench.f90`，未选中的算法不进入生产译码路径。来源、参数取舍和失败实验见 `lib/MODERN_DECODER_PROVENANCE.txt`。

从旧版构建目录迁移时，建议使用新的构建目录。V3.5.0 将串行与 OpenMP 的 Fortran 模块分别放入 `fortran_modules_serial/` 和 `fortran_modules_omp/`；旧构建根目录中遗留的 `.mod` 应先移走，避免编译器优先读取过期接口。


## V3.6.0：独立上游后端与审计

固定上游为 WSJT-X 正式 `v3.0.2`，commit `ccdfaf3c1c109010d15399674ce278167cfde848`。源码、GPL、作者及每文件 SHA-256 位于 `upstream/wsjtx-3.0.2/`；`SOURCE.json.excluded_files` 明确排除了禁止本分支再分发的 SuperFox 表。本地 SuperFox 继续使用已有独立测量表。

`jtdx-f-upstream` 在独立进程使用上游 Q65/MSK144/Echo/天文实现，避免其 Fortran 全局变量和较大缓冲 ABI 与 JTDX 冲突。主程序负责 CAT、音频和发射许可，后端不接触设备。构建同时安装该助手、正式 JPLEPH 和 grid.dat。接口/迁移记录见 `helpers/upstream/PROTOCOL.txt` 和 `MIGRATION.txt`，运行包也提供这两份文档。

`UpstreamWaveform` 执行真实 PCM 往返、平均、EME 时延、天文及畸形输入检查；`UpstreamController/UpstreamOperating` 检查协议、超时、迟到回包、取消、UTC 倒跳和频率反馈；协议/波形兼容测试与正式上游固定向量比较。`ReedSolomonSafety` 检查旧 JT65 纠错接口、并发、擦除边界和重复分配路径。Windows 同样编译这些测试；helper 的 Python 测试支持 `--wine`。

`jtdx-hamlib-update` 为事务更新助手，Windows 安装后才运行更新/回退，不写用户日志。主程序继续以兼容 4.7.2 头文件编译；不能用 Hamlib 5 DLL 直接替代。
