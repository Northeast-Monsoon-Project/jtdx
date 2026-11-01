# 许可与第三方组件

JTDX-F 遵循项目原有 GNU GPLv3 许可。原版的 `COPYING`、`AUTHORS`、`THANKS` 和源代码版权声明均保留。Fluxmancer 的修改以同一项目许可提供，修改记录见审查文档和 Git 历史。Logo 由项目团队提供。

Windows 包动态链接若干开源组件，并提供其原有许可文本。组件不归 Fluxmancer 所有；商标和项目名称属于各自权利人。

| 组件 | 用途 | 源码与构建资料 |
|---|---|---|
| JTDX / WSJT-X | 通联、协议、GUI 和解码基础 | 本次交付完整修改源码及原有版权声明；上游 https://github.com/jtdx-project/jtdx |
| Qt 5 / ActiveQt | GUI、多媒体、网络和 COM | https://code.qt.io/cgit/qt/ ；MSYS2 配方提供所使用的补丁 |
| Hamlib 4.7.2 | CAT/PTT 控制 | https://github.com/Hamlib/Hamlib/tree/4.7.2 |
| libusb 1.0.30 | USB 支持 | https://github.com/libusb/libusb/tree/v1.0.30 |
| FFTW 3 | FFT 算法 | https://www.fftw.org/download.html |
| GCC / MinGW-w64 运行库 | C++、Fortran、OpenMP 和线程支持 | https://gcc.gnu.org/ ；https://www.mingw-w64.org/ ；保留 GCC Runtime Library Exception |
| Boost | C++ 头文件与支持代码 | https://www.boost.org/ |
| OpenSSL | Qt 网络 TLS | https://github.com/openssl/openssl |
| ICU / HarfBuzz / FreeType / PCRE2 等 | 文本、字体及底层依赖 | 具体包版本、来源 URL、SHA-256 见 `manifest-ucrt64.json`；各包许可位于安装目录 `licenses/` |
| NSIS | Windows 安装程序 | https://nsis.sourceforge.io/ |

MSYS2 的依赖源码、补丁和 PKGBUILD 配方： https://github.com/msys2/MINGW-packages 。本次 Windows 构建使用 UCRT64 包，清单记录了每个包的版本与下载哈希；安装树的 `runtime-manifest.json` 记录实际分发的 DLL/EXE。

发布二进制时应将本次对应的完整源码包、构建脚本和许可一起提供，依赖组件的源码获取方式和适用许可也应继续保留。这里的清单是发布资料的一部分，不替代各组件自身的许可正文。


## SuperFox 接收代码（V3.1.0）

来自 [CE3TSK JTDX_CONTEST](https://github.com/ce3tsk/jtdx_contest)，保留其头部声明、测量程序和记录。该实现基于 WSJT-X 的 GPL 接收代码及 Nico Palermo 的 GPL polar codec。JTDX-F 未包含上游带额外限制的 `qpc_n127k50q128.c`；独立测量得到的定义、来源提交和相关许可说明见 `lib/superfox/PROVENANCE.txt` 与 `lib/superfox/qpc/measured/README.md`。当前集成用于接收，不包含远征身份在线认证。


V3.4.0 新增 Decodium FT2 与 WSJT-X v3.3.0-beta4 JTTY 的 GPL 编解码源码；具体提交、移植范围、适配及测试来源分别记录于 `lib/ft2/PROVENANCE.txt` 和 `lib/jtty/PROVENANCE.txt`。运行包新增本地 JTTY 编解码助手 `jtdx-f-jtty.exe`；助手与主程序一起按 GPL 提供完整源码。


V3.5.0 的可选现代解码扩展继续采用项目 GPLv3 许可；参考 WSJT-X 的后验辅助 OSD 与短码可靠度译码研究，并单独比较 layered SPA、normalized min-sum 和加权后验方案。来源固定提交、论文链接、移植边界与未采用的实验见 `lib/MODERN_DECODER_PROVENANCE.txt`。


V3.6.0 固定正式 WSJT-X 3.0.2 的 Q65/MSK144/Echo/天文及 WSPR 组件，保留原 GPL、作者和源码头。具体修改边界见 `helpers/upstream/MIGRATION.txt`、`docs/WSPR升级与审计_V3.6.0.txt`。`upstream/wsjtx-3.0.2/SOURCE.json` 记录每个原样文件的 SHA-256 和明确排除项；受限制的 SuperFox 码表没有进入本项目发行源码或二进制。SLALIB 原文件的 GPLv2-or-later 声明保留，项目按兼容的 GPLv3 条款提供。`grid.dat` 来源和用途另记于 `docs/grid-data-provenance.txt`。
