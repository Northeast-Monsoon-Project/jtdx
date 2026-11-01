# JTDX-F

**JTDX-F V3.7.1** 是 Fluxmancer 基于 JTDX 2.2.159 维护的独立分支，用于业余无线电弱信号数字模式通联。保留原作者署名和 GNU GPLv3 许可；本社区项目未获原 JTDX 作者认可或背书。

本仓库的 `master` 保留原版历史，**`JTDX-F` 分支提供本版本源码**。JTDX-F 使用独立的应用名称、设置和日志目录，可与原版共存。

## V3.7.1 更新

修复托管模式发送最后一条 73 后，DX 呼号与网格未清除、无法接续下一台的问题。收尾依据同一周期的实际音频覆盖和实时解码完成事件确认，避免上一周期提示及音频分块时间偏差造成持续等待；重复 RR73、音频缺失和人工停止等保护仍保留。

具体原因、改动和原发行版回归记录见 [V3.7.1 修复说明](jtdx-master/docs/本轮修复_V3.7.1.txt)。该说明引用的外部验证目录和发行清单未包含在本源码包中，不应据此认为本仓库更新时重新完成了双平台验证。

## 主要功能

| 功能 | 说明 |
| --- | --- |
| 数字模式 | FT8、FT4、JT65、JT9、T10、WSPR，以及 FT2、JTTY、Q65、MSK144、Echo；各模式支持范围见使用说明 |
| 托管通联 | DXCC 猎人、QSO 爱好者、连续 QSO、来电优先、次数限制及发射监管 |
| 解码 | 可选现代 FT8/FT4 算法；SuperFox 接收及普通 FT8 Hound 应答 |
| 电台与音频 | Hamlib CAT、OmniRig、TCI，以及相关音频和设备配置 |
| 日志与呼号 | 多呼号资料、独立日志、UTC 季度归档、ADIF 导入、查重与永久黑名单 |
| 控制与辅助 | 本机 MCP 服务、NTP 校时辅助程序、JTAlert 兼容支持 |
| 构建与测试 | Linux 原生构建、Windows x64 UCRT 交叉编译、依赖校验清单及 CTest 回归 |

发射需要人工授权；托管、远程控制和自动程序不能代替授权。各模式的设备支持、时序和兼容边界请参阅下方文档。

## 获取源码

```bash
git clone --branch JTDX-F https://github.com/Northeast-Monsoon-Project/jtdx.git
cd jtdx
```

```text
jtdx-master/       主程序、编解码器、测试、文档及第三方源码
scripts/           构建、打包与依赖下载辅助脚本
dependencies/      依赖版本、下载地址与 SHA-256 清单
SOURCE_COMMIT.txt  原始 V3.7.1 源码包记录的提交标识
```

`SOURCE_COMMIT.txt` 用于保留源码包来源，其标识不等同于本仓库此次导入提交。依赖二进制、构建目录、安装程序及个人日志不纳入源码树。

## Windows 使用

从 [V3.7.1 Release](https://github.com/Northeast-Monsoon-Project/jtdx/releases/tag/v3.7.1) 下载 `JTDX-F-V3.7.1-Windows-x64-Setup.exe`，面向 Windows 10/11 64 位。源码 ZIP 不能直接运行。

安装后先配置呼号、网格、声卡、CAT/PTT、频率和系统时间。OmniRig 用户另需安装并注册 OmniRig 服务。详细步骤见 [使用说明](jtdx-master/docs/使用说明_zh.md)。

## 构建

### Linux 原生构建

需要 CMake 3.16 或更新版本、Ninja、C/C++ 和 Fortran 编译器，以及 Qt 5（Widgets、Network、Multimedia、SerialPort、WebSockets、LinguistTools；测试需要 Test）、Boost、FFTW3 单精度及线程库、Hamlib 和 libusb。请先按所用发行版安装开发包并确保 CMake 能找到依赖。

在仓库根目录使用系统工具链：

```bash
cmake -S jtdx-master -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DWSJT_GENERATE_DOCS=OFF -DWSJT_SKIP_MANPAGES=ON \
  -DCMAKE_INSTALL_PREFIX="$PWD/install"
cmake --build build -j 6
cmake --install build
./install/bin/jtdx-f
```

若需要指定 Fortran 编译器，可在配置时添加 `-DCMAKE_Fortran_COMPILER=/path/to/gfortran`。源码包中的 `scripts/gfortran-local` 和 `scripts/env.sh` 面向预先准备的 `.deps/` 工作区，并不会自动安装编译器或依赖。

### Windows 交叉编译

辅助脚本面向 Linux 上准备好的 MinGW-w64/UCRT、Qt 5、Fortran 和相关依赖环境。工具链路径固定在 `.deps/root/usr` 和 `.deps/msys2/ucrt64`；不能在全新克隆后直接执行便完成构建。先参照 [构建说明](jtdx-master/docs/构建说明_zh.md) 准备完整依赖集合，再运行：

```bash
bash scripts/build-windows.sh
bash scripts/make-windows-installer.sh
```

构建、安装和发行输出分别在 `build-windows-ucrt/`、`install-windows/` 和 `release/`。安装器生成还需要 NSIS、Wine 与 Xvfb。依赖下载脚本和清单不等同于完整环境引导程序；不要混用 MSVCRT 与 UCRT 依赖。

## 验证

完成启用测试的构建后串行运行：

```bash
ctest --test-dir build --output-on-failure
```

多个 GUI 回归使用共享内存测试键，应避免并行运行。完整测试范围和专项命令见构建说明与审计记录。本次仓库同步只核对源码完整性、版本和文档链接，未重新编译或执行 CTest；发行源码记录的测试结果与本次验证应分别理解。

## 文档索引

- [使用说明](jtdx-master/docs/使用说明_zh.md) · [构建说明](jtdx-master/docs/构建说明_zh.md)
- [托管模式](jtdx-master/docs/托管模式_zh.md) · [MCP](jtdx-master/docs/MCP_zh.md) · [NTP 校时](jtdx-master/docs/NTP校时_zh.md)
- [JTAlert 兼容说明](jtdx-master/docs/JTAlert兼容说明_zh.md)
- [V3.7.0 架构与兼容边界](jtdx-master/docs/V3.7.0_架构与兼容边界.txt) · [FT8 验证记录](jtdx-master/docs/FT8_V370_VALIDATION.md)
- [审查记录](jtdx-master/docs/审查记录_zh.md) · [第三方组件](jtdx-master/docs/第三方组件_zh.md)

部分随包文档标题与示例仍标注 V3.6.x，作为历史背景保留；当前版本以 `jtdx-master/Versions.cmake` 和 V3.7.1 修复说明为准。

## 支持边界与许可

SuperFox 支持接收及普通 FT8 应答，不发送 SuperFox 波形，也不提供在线 OTP 身份认证。FT2/JTTY 的设备支持、Q65 的周期与 EME 接收边界见使用说明。Wine 回归不等同于真实 Windows 电台、声卡、驱动和长期现场运行验证；解码性能也取决于信号、硬件和参数。

项目按 [GNU GPLv3](jtdx-master/COPYING) 提供，原始作者信息见 [AUTHORS](jtdx-master/AUTHORS) 与 [原版 README](jtdx-master/README)。感谢 WSJT-X、JTDX 及各第三方组件作者；各移植模块的来源和修改记录保留在相应 `PROVENANCE`、`SOURCE.json` 和文档中。
