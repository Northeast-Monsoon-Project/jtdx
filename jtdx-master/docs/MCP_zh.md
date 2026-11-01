# JTDX-F V3.2.0 MCP 使用指南

Fluxmancer 在 JTDX 中加入本机 MCP 服务。服务直接调用主程序的状态和操作接口，不需要通过屏幕坐标控制。接收、解码、通联时序、日志和监管计时仍由 JTDX-F 管理。

## 快速连接

1. 启动 JTDX-F，在“文件 → 设置”中配置呼号、网格、声卡和电台。
2. 打开设置的 **MCP** 页，勾选“启用 MCP 服务”。默认地址为 `http://127.0.0.1:22380/mcp`。
3. 点击“复制令牌”，将令牌填入客户端。点击“确定”保存并启动服务；状态栏显示端口或错误。
4. 读取功能只需要令牌。需要修改模式、频率、通联目标和自动程序时勾选“允许控制”。发射相关操作还须勾选“允许开启发射和调谐”，但此权限不能代替人工在主界面启用发射。
5. 客户端调用 `initialize`、发送 `notifications/initialized`，随后可以 `tools/list`、`tools/call`、`resources/read`。

默认关闭服务及控制权限。设置更改在“确定”后生效；取消设置不会应用修改。生成新令牌并保存后，原令牌失效。令牌保存在本机应用设置中，不通过 MCP 返回；持有令牌的本机程序能读取呼号、解码及通联日志。

### 支持 Streamable HTTP 的客户端

- URL：`http://127.0.0.1:22380/mcp`
- Header：`Authorization: Bearer 你复制的令牌`
- `Content-Type: application/json`
- `Accept: application/json, text/event-stream`
- 初始化后带 `MCP-Protocol-Version: 2025-11-25`（或初始化协商的版本）。

实现为无会话的 JSON 响应型 Streamable HTTP；支持协议版本 `2025-11-25`、`2025-06-18`、`2025-03-26`。不提供 SSE 推送和旧式 HTTP+SSE 端点，不要求 Session ID；GET/DELETE 返回 405。通知返回 202，通知不能执行工具操作。请通过游标轮询解码和事件。

PowerShell 连接示例：

```powershell
$env:JTDX_MCP_TOKEN = '从设置复制的令牌'
$headers = @{ Authorization = "Bearer $env:JTDX_MCP_TOKEN"; Accept = 'application/json, text/event-stream' }
$body = @{ jsonrpc='2.0'; id=1; method='initialize'; params=@{
  protocolVersion='2025-11-25'; capabilities=@{}; clientInfo=@{name='my-client';version='1.0'}
}} | ConvertTo-Json -Depth 8
Invoke-RestMethod -Uri 'http://127.0.0.1:22380/mcp' -Method Post -Headers $headers -ContentType 'application/json' -Body $body
```

### 只支持 stdio 的客户端

附带的 `tools/jtdx_mcp_bridge.py` 使用 Python 3.9 或以上版本，仅需标准库。安装包中的位置是 `share/jtdx/tools/jtdx_mcp_bridge.py`。Windows 的 JTDX-F 程序本身不需要 Python；只有此桥接脚本需要。

客户端启动命令为 `python`（也可指定 Python 的完整路径），参数为脚本完整路径。设置环境变量 `JTDX_MCP_TOKEN`，可选 `JTDX_MCP_URL`。例如，采用 `mcpServers` 格式的客户端可按以下形式配置，实际字段以该客户端为准：

```json
{
  "mcpServers": {
    "jtdx-f": {
      "command": "python",
      "args": ["C:/Users/YourName/AppData/Local/Programs/JTDX-F/share/jtdx/tools/jtdx_mcp_bridge.py"],
      "env": {
        "JTDX_MCP_URL": "http://127.0.0.1:22380/mcp",
        "JTDX_MCP_TOKEN": "替换为实际令牌"
      }
    }
  }
}
```

脚本不启动 JTDX-F，请先运行软件并启用服务。标准输出只发送 JSON-RPC，诊断输出到标准错误。桥接禁止代理和重定向，也不接受外部主机地址。

## 工具列表

所有工具接受对象参数，未列出的字段会被拒绝。调用示例：

```json
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"get_status","arguments":{}}}
```

| 工具 | 参数 | 功能 / 权限 |
|---|---|---|
| `get_status` | 无 | 读取模式、频率、目标、收发、解码、QSO、计时器和权限状态 |
| `get_config` | 无 | 读取呼号、网格、自动程序、数据目录等；不返回令牌 |
| `get_decodes` | `after_id` 可选，默认 0；`limit` 1～500，默认 100 | 读取最近解码，保留 500 条 |
| `get_events` | 同上 | 读取日志、控制和解码故障事件，保留 500 条 |
| `get_frequencies` | 无 | 当前模式/区域的预设频率，单位 Hz |
| `get_messages` | 无 | 六条标准消息、当前选择及上次发送内容 |
| `get_log` | `max_bytes` 1～262144，默认 65536 | 读取 ADIF 日志尾部；截断时首条记录可能不完整 |
| `set_monitor` | `enabled` 布尔 | 开关接收，需要控制权限 |
| `set_frequency` | `hz` 100000～2147483647 的整数 | 设置工作频率，需要落在程序定义的业余波段内 |
| `set_mode` | `mode`: FT8 / FT4 / FT2 / JTTY / Q65 / MSK144 / Echo / JT65 / JT9 / T10 / WSPR-2 | 切换模式 |
| `set_audio_frequency` | 至少一个 `rx_hz`、`tx_hz`，200～4000 | 设置音频频率，受当前模式界面范围限制 |
| `set_tx_period` | `first` 布尔 | true 为偶数/第一时隙，false 为奇数/第二时隙 |
| `set_target` | `callsign`；可选 `grid`、`report`（−50～49，默认 −10） | 准备目标及标准消息，不开启发射 |
| `reply_to_decode` | `id` | 选择缓存中的实时解码，采用正常应答逻辑，不开启发射；拒绝过期、文件或不同波段/模式的解码 |
| `select_message` | `index` 1～6 | 选择标准消息，不开启发射 |
| `set_options` | 见下表 | 修改自动程序选项 |
| `clear_target` | 无 | 清除当前通联目标 |
| `start_tx` | 无 | 仅能继续已由人工启用的发射会话；需要控制及发射权限，不能解锁停止状态 |
| `stop_tx` | 无 | 停止发射和调谐，取消待执行的重新发射计时器；持有令牌即可使用 |
| `tune` | `enabled` 布尔 | 开启需要当前人工发射授权、控制和发射权限；停止只需要令牌 |
| `log_qso` | 无 | 打开正常日志窗口；遵循用户的自动记录设置，不伪造通联记录 |

`set_options` 至少包含一个字段：

| 字段 | 值 |
|---|---|
| `managed_mode` | `off` / `dxcc_hunter` / `qso_enthusiast`；不保存模式，不授予发射许可，模式条件在设置页配置 |
| `auto_sequence` | 布尔，AutoSeq |
| `auto_tx` | 布尔，AutoTX |
| `single_qso` | 布尔，单次通联 |
| `answer_worked` | 布尔，应答已通联电台 |
| `call_worked` | 布尔，呼叫已通联电台 |
| `search_cq` | 布尔，优先应答并搜索 CQ；开启时如有必要切换到中段自动选择 |
| `selection` | `manual` / `first` / `middle` / `end` |
| `watchdog_minutes` | 整数 0～999；0 为不限时 |

除接收、日志、开启发射和停止操作外，修改操作要求先关闭发射，包括待发射、正在发射及调谐状态。模态设置/日志窗口打开时，修改操作会拒绝执行；停止发射仍可调用。更改电台设备、声卡、呼号及 MCP 自身权限需要在设置界面完成，接口不提供任意文件、脚本或系统命令执行。

## 连续通联工作流

1. 读取 `get_status` 和 `get_config`，确认呼号、网格、FT8 模式、频率和收发状态。
2. 在停止发射时设置 `auto_sequence: true`、`single_qso: false`、合适的 `selection` 和 `watchdog_minutes`。
3. 希望无人呼叫时主动应答别人的 CQ，可启用 `call_worked` 或 `search_cq`；其他新电台/区域筛选仍遵循设置。
4. 选择实时解码或准备目标；希望先 CQ 时清空目标并 `select_message` 6。
5. 操作者在程序界面人工点击“启用发射”，或在开启“双击呼号启用发射”后亲自双击解码目标。`start_tx` 和远程选择目标不能替代这个人工动作。自动程序会优先处理符合条件的呼叫，再搜索允许的 CQ/73/RR73/RRR；没有候选时 CQ，不插入强制 CQ 轮次。
6. 轮询状态和解码；让 AutoSeq 完成交换。客户端不需要每个周期重新调用 `start_tx`，重复调用也不会重置监管计时。
7. 调用 `stop_tx` 结束，或等待监管时间到期。到期后不会被操作、解码或 QSO 自动重新开启；手动开启发射会建立新会话。

监管时间是从“开启发射”开始的单调时钟累计时间，包含收听和等待周期，跨多个 QSO；不是累计载波时间，也不是鼠标/键盘闲置时间。0 不限时，999 为 16 小时 39 分钟。单次通联开启时，在完成一轮后停止。自动运行仍需 AutoSeq 和自动候选选择，手动模式不会擅自选择电台。

## 数据与错误

工具成功结果包含 `content` 文本及同内容的 `structuredContent`；执行失败返回 `isError: true`。调用方必须检查，HTTP 200 本身不表示操作成功。协议错误返回 JSON-RPC `error`：未知方法 −32601，参数错误 −32602，解析错误 −32700。

状态增加 `manual_tx_authorized`（当前人工授权）及 `superfox`（接收模式）。`get_config` 的 `log_directory` 与 `log_files` 对应当前呼号全部季度日志。`get_log` 可以传入 `file` 文件名（不含目录）读取任一当前呼号日志，省略时读取排序后的最近文件；仍受 `max_bytes` 限制。

状态中的 `managed_mode` 表示当前托管模式，`managed_tx_ready` 表示本次是否已选定托管发射内容（仍需人工发射许可）；MCP 初始化版本号跟随当前程序版本。

状态包含 `tx_enabled`（已允许发射）、`transmitting`（正在发射）、`tuning`、`decoder_busy`、`rig_online`、`dx_call`、`qso_state`、`watchdog_minutes`、`watchdog_expired`、`watchdog_remaining_seconds`。不限时的剩余秒数为 −1。停止状态的剩余时间显示下一次会话的完整额度。频率使用 Hz，时间使用 UTC。

解码和事件返回 `items`、`oldest_id`、`latest_id`、`next_after_id`。用 `next_after_id` 继续翻页；ID 只在本次程序运行期间有效，重启后应从 0 开始。若游标早于 `oldest_id`，说明部分旧数据已被覆盖。建议每秒轮询一次，按需减少字段调用。

资源 URI：`jtdx://status`、`jtdx://decodes`、`jtdx://events`、`jtdx://config`、`jtdx://log`，均为 JSON。提示词 `operate_ft8` 提供基本操作流程。

| 现象 | 处理 |
|---|---|
| 连接被拒绝 | 确认软件运行、MCP 已保存启用、端口与状态栏一致 |
| HTTP 401 | 令牌缺失或错误；重新复制，勿把令牌发到公开日志 |
| HTTP 403 | Host/Origin 不属于本机端点；不要通过网页跨站请求 |
| HTTP 400 / 413 / 415 | 检查协议版本、请求格式、64 KiB 请求上限和 JSON 类型 |
| `Control is disabled` / `Transmit is disabled` | 在设置的 MCP 页启用对应权限并确定 |
| TX 操作成功却没有载波 | `start_tx` 是允许在正确时隙发射；检查时钟、当前时隙、声卡、CAT/PTT 和消息内容 |
| 解码器错误 | 先停止发射；程序尝试重启，随后点击“监听”；最多自动尝试 3 次，错误保存在数据目录 `decoder-errors.log` |

协议参考：[MCP Streamable HTTP 规范](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)。


## V3.4.0：双份日志、FT2 与 JTTY

本版提供 22 个工具。`set_mode` 新增 `FT2` 与 `JTTY`。`get_config.compatibility_log` 指向当前呼号目录的完整 `wsjtx_log.adi`，`log_files` 只列季度主日志。

`set_jtty_text {"text":"CQ N0CALL"}` 准备 JTTY 文本（1～80 字符），不会发射。操作员先在程序内手动开启发射并授予 MCP 发射权限后，`start_tx` 才会发送一条消息。每条消息需要单独发送；`stop_tx` 立即取消待编码、待 PTT 和当前发射。`get_messages` 返回 JTTY 文本及编码状态，`get_events` 的 `jtty_receive` 提供逐步拼接的接收内容，`get_decodes` 收录完整接收消息。JTTY 不支持 AutoSeq、托管、标准六消息选择、时隙设置或调谐。

`get_status.watchdog_finishing_qso` 为 true 表示监管已到期、正在完成当前 QSO；这时不能开始新 QSO。JTTY 则发完当前消息后停止。人工停止和故障停止始终立即执行。


## V3.5.0：永久黑名单

MCP 解码列表和事件缓存遵守当前操作员的永久黑名单，已屏蔽的完整呼号不会通过历史解码列表绕过界面过滤。`set_target` 与基于解码 ID 的回复同样拒绝屏蔽对象。黑名单损坏时保留原文件并阻止自动选择/发射，用户需修复文件；没有配置操作员时仍可纯监听。黑名单管理入口在主界面第三标签页，文件位于当前呼号日志目录 `blacklist.json`。

第三标签页的呼号选择需点击“确认切换”才生效，暂存选择不改变 MCP 报告的当前操作员。切换后发射授权清零，仍须人工在界面启用。现代 FT8/FT4 解码开关是独立的本地解码设置，不改变现有 MCP 消息格式或远程发射权限。


## V3.6.0 模式兼容

`set_mode` 新增 `Q65`、`MSK144` 和 `Echo`，工具总数仍为 22。新模式使用主界面相同的人工发射授权、监管和黑名单门禁；远程选择模式不会开启发射。Q65 周期/子模式、全双工和天文跟踪在本地界面配置。Echo 是测量模式，不接受普通通联文本、托管或调谐命令；错误会通过既有 `isError` 返回。

Q65/MSK144 的完整消息通过既有解码事件输出，Echo 测量通过 `echo` 事件提供信噪比、频移、质量和积分次数。JTDX-F 原有 FT8/FT4 解码事件字段保持兼容。
