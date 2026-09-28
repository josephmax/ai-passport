<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# AI Passport 本地服务

用量来源边界及下一步采集架构见[用量采集方案](../docs/development/engineering/usage-collection.zh_CN.md)
和 [ADR 0005](../docs/adr/0005-separate-usage-quota-and-billing.zh_CN.md)。

多合一挂坠的自托管后端（见
[`docs/specs/2026-09-22-multi-pendant-app.md`](../docs/specs/2026-09-22-multi-pendant-app.md) 与
[`docs/specs/2026-09-22-config-portal.md`](../docs/specs/2026-09-22-config-portal.md)）：
采集各家 Coding 账户用量、聚合成设备用量快照、把宠物素材 PNG 转码为 RGB565 皮肤包、
转发天气、托管手机配置中心。设备只与本地服务通信（ADR-0001）；PNG→RGB565 转码在
服务端完成（ADR-0004）。

- 运行环境：Node.js ≥ 22，TypeScript（strict），单进程 Fastify + sharp。
- 范围：P1（ccusage 本地 Agent 报告、GLM/DeepSeek API 采集器；订阅额度另行处理）。

## 安装

```bash
cd service
npm install            # 默认源慢可加 --registry=https://registry.npmmirror.com
cp .env.example .env   # 然后修改 PORTAL_PASSWORD / SESSION_SECRET
npm run build          # tsc -> dist/
```

## 启动

```bash
npm start              # node dist/index.js
npm run start:local    # 构建并后台运行，仅在不存在时创建私有 .env
npm run dev            # tsx watch src/index.ts（热重载）
npm test               # node:test 单元测试（仅纯逻辑）
npm run smoke          # 短启停冒烟：配对/快照/皮肤包全链路
npm run gen:placeholder # 生成并检查默认占位皮肤包 v1
```

首次启动时，若从未发布过任何皮肤包，服务会自动发布一份纯代码生成的
**占位皮肤包 v1**（跑步小人 / 剑斗 / 月亮睡觉 / 奖杯胜利四动作 + 渐变地图条带 +
灌木装扮 + 雨雪小精灵），保证新设备开箱即可拉到 v1。

## 配置

环境变量（`.env`，见 `.env.example`）：

| 变量 | 默认值 | 说明 |
| --- | --- | --- |
| `PORT` | `3000` | HTTP 端口（设备 API 与配置中心共用） |
| `HOST` | `0.0.0.0` | 监听地址；需保证设备与手机可达 |
| `PORTAL_PASSWORD` | — | 配置中心登录口令（必须设置；未设置时配置中心禁用，设备 API 不受影响） |
| `SESSION_SECRET` | 临时随机 | Cookie 签名密钥（设置后重启不掉登录态） |
| `CLAUDE_CONFIG_DIR`、`CODEX_HOME` 等 | Agent 默认路径 | 可选的 ccusage 数据目录，位于服务主机；见[支持来源](https://github.com/ccusage/ccusage#supported-sources) |
| `SNAPSHOT_REFRESH_MINUTES` | `10` | 后台快照刷新间隔（分钟），整数 1–1440 |
| `DATA_DIR` | `service/data` | 运行时数据目录 |

运行时设置保存在 `data/config.json`（首次运行自动生成默认值）：

```jsonc
{
  "primaryAccount": "local",         // 本地 Agent Token 报告排第一
  "city": "shanghai",                // 内置城市表（33 个中文城市）
  "customLat": null, "customLon": null, // 两者都填写时覆盖城市
  "weeklyTokenBudget": null,         // null = 设备显示纯数值
  "glm":     { "label": "GLM",      "codingPlanUrl": "https://open.bigmodel.cn/api/paas/openapi/resource/coding-plan" },
  "deepseek":{ "label": "DeepSeek", "balanceUrl": "https://api.deepseek.com/user/balance" }
}
```

多数生效设置也可在配置中心修改。旧配置文件中的 `claude` 设置继续保留以兼容，
但本地 Agent 采集器不再使用它们。

### API Key（P1 风险说明）

在配置中心填写的 GLM/DeepSeek API Key 以**明文**保存在 `data/keys.json`
（文件权限 0600）。这是 P1 的已知取舍：本服务只应跑在用户自己的机器上、
仅局域网可访问。任何能读取主机文件（或拿到 `data/` 备份）的人都能读到
Key；配置中心回显仅显示尾 4 位。若要把主机暴露给不完全可信的用户，
请先改造为加密存储。

## 设备 API

| 端点 | 鉴权 | 说明 |
| --- | --- | --- |
| `POST /api/pair` | 配对码 | 请求体 `{"code":"123456","deviceName":"..."}`。6 位一次性配对码（10 分钟有效，在配置中心生成）。成功：`200 {"token":"<32位hex>"}`；错误/过期/已用：`403`；缺 `deviceName`：`400`。 |
| `GET /api/snapshot` | 请求头 `X-Device-Token` | 用量快照（见下）。令牌无效/已吊销返回 `401`。请求成功会更新设备最后同步时间。 |
| `GET /assets/bundle_v{N}.bin` | 无（局域网、按版本） | 已发布的 APB1 皮肤包。版本未发布过返回 `404`。 |
| `GET /api/health` | 无 | 存活探测。 |

### 快照 schema（主规格 §4.3）

```jsonc
{
  "schema": 1,
  "generatedAt": "2026-09-22T06:00:00+08:00",
  "servedAt": "2026-09-22T06:02:00+08:00",
  "accounts": [{
    "provider": "local",
    "label": "Local Agents",
    "quotas": {
      "weekly": null,
      "rolling5h": null,
      "weeklyTokens": { "used": 412000, "cap": null, "unit": "tokens", "resetAt": "...", "percent": null }
    }
  }],
  "dailyTokens": { "used": 32000, "coverage": "local-agent-logs" },
  "agents": [
    { "agent": "codex", "dailyTokens": 30000, "weeklyTokens": 410000,
      "rolling5h": null, "weekly": { "used": 3, "cap": 100, "unit": "%", "resetAt": "...", "percent": 3 } },
    { "agent": "pi", "dailyTokens": 2000, "weeklyTokens": 2000,
      "rolling5h": null, "weekly": null }
  ],
  "badgeName": "Example",
  "badgeRole": "Role",
  "weather": { "code": 61, "kind": "rain", "sunrise": "06:12", "sunset": "18:05", "city": "上海" },
  "assetBundle": { "version": 3 }
}
```

规格之外的附加字段：`percent`（服务端算好，保留 1 位小数，无 cap 时为
null——设备只渲染）、`kind`（归一化后的天气）、降级口径的 `basis` 与
账户级 `error`。`generatedAt` 表示缓存读数生成时间；`servedAt` 每次响应重新生成，
供设备校时使用。规则：

- 只输出**已连接**的账户；主力账户固定排在 `accounts[0]`。
- 拿不到的额度维度为 `null`，绝不编造：
  - **DeepSeek** 余额通过账户新增的 `balance` 读数 `{ remaining, currency, basis }` 返回；Token 额度保持 null。
  - **GLM** 为实验性接口；仅明确的小时／请求次数且包含 300／10080 分钟窗口时映射额度，含糊读数保持 null。
  - **Codex 额度** 读取近期本地 `rate_limits` 样本，按窗口时长而非主／次位置映射。采样超过一小时或额度重置后失效；单位 `%`，不读取厂商凭证。来源是本机 Codex 会话，未核实登录账户绑定；不是 ChatGPT 网页额度或账单。
- 单个采集器失败不影响快照：账户仍在列表中，带 `error` 且配额为 null。
- 本地 Agent 采集器调用固定版本 ccusage 的离线 JSON 报告，从本地周一零点
  到今天累加每日记录。它在**服务主机**检测 Claude Code、Codex、OpenCode、
  Gemini CLI 等受支持来源；日志与 API Key 不上传。这些 Token 计数不代表
  官方订阅额度或使用百分比。
- 接入本地采集器时，`dailyTokens.used` 为观测到的本地 Agent 总量，`coverage` 为 `local-agent-logs`。`agents` 最多保留八个来源的今日／本周 Token 分项（含缓存读取）；5 小时／周额度另行采样，无可靠读数时为 null。仅提供余额的账户不使总量失效，也不重复叠加供应商计数；本地采集失败时保持 null。除默认每 10 分钟的周期采集外，服务在主机本地零点固定刷新；新读数完成前今日用量为 null，周一换周时也屏蔽上周本地 Token。Codex 来源使用 `codex` 标识，旧设置中的 `chatgpt` 主力项在加载时迁移。名牌字段来自鉴权的名牌页；设备将其保存到独立 NVS 键，仅内容变化时重写。
  设备离线时沿用上次快照。

## 皮肤包（APB1）

与设备固件逐字节一致的容器格式（全部小端）：

```
0x00  4  magic "APB1"
0x04  2  version（u16 LE）
0x06  2  file_count（u16 LE）
随后 file_count 个条目：
  u16 name_len（LE）+ name_len 字节 UTF-8 相对路径
  u32 data_len（LE）+ data_len 字节原始内容
```

必含 `manifest.json` 与帧数据文件。帧数据 = 逐帧 RGB565 像素顺序拼接，
每像素一个 u16 **小端**（LVGL 原生字节序），单帧大小 = w×h×2 字节。
单包发布上限 4 MB。

上传时服务端校验：动作 64×64（run/fight 4–8 帧、sleep 2–4 帧、
victory ≤4 帧）、地图 240×160、装扮 24×24（≤8 个）、帧率 1–10 fps。
雨/雪天气小精灵（16×16、2 帧）P1 固定使用内置生成素材。

## 配置中心

`http://<局域网IP>:<PORT>/portal` —— 口令登录（session cookie，8 小时）。页签：

- **名牌**：编辑主屏中与今日 Token 并排的姓名和下方身份。固件字库支持
  `main/fonts/badge_name_glyphs.txt` 中的汉字，以及英文字母、数字、空格、
  句点、下划线和连字符；不支持的姓名在同步前拒绝。身份最多 10 个受支持的
  ASCII 字符；姓名留空显示设备占位文案。
- **账户**：GLM/DeepSeek Key 表单（回显仅尾 4 位）、本地 Agent 采集器状态与
  最近采集时间、主力账户单选（默认 local）、Codex 额度观测。
- **偏好**：城市选择（内置城市表 + 自定义经纬度）、周 Token 预算
  （null = 设备显示纯数值）。
- **设备**：设备列表（名称/令牌状态/最后同步）、生成一次性配对码、
  吊销令牌（立即生效）。
- **素材**：按槽位上传 PNG（multipart）、服务端尺寸/帧数校验并转码
  RGB565、草稿→发布（版本号自增）→历史列表→回滚（旧版本内容以新
  版本号重新发布）。发布时未上传的槽位回退为占位素材。

配置中心安全边界（P1）：局域网 + 口令，无 CSRF token（仅表单），
不暴露公网。请勿做端口转发。

## 目录结构

```
service/
├── src/
│   ├── index.ts               入口：环境变量、存储、占位包发布、监听
│   ├── app.ts                 Fastify 工厂（cookie/formbody/multipart）
│   ├── deviceApi.ts           /api/pair、/api/snapshot、/assets/*
│   ├── snapshot.ts            §4.3 快照构建（纯函数）
│   ├── snapshotService.ts     缓存 + 定时刷新
│   ├── weather.ts             open-meteo + 30 分钟缓存（映射纯函数）
│   ├── wmo.ts                 WMO 归一化（固件镜像）
│   ├── xpTable.ts             经验分段表镜像（校验合计 3145）
│   ├── cityTable.ts           33 城市表 + 定位解析
│   ├── collectors/            localAgents.ts（ccusage）、
│   │                          glm.ts / deepseek.ts（映射 + 客户端）、registry.ts
│   ├── assets/                bundleFormat.ts、rgb565.ts、pipeline.ts（sharp）、
│   │                          placeholder.ts、draftStore.ts、publisher.ts
│   ├── store/                 settings.ts（配置 + Key）、devices.ts、jsonStore.ts
│   ├── portal/                routes.ts（页面 + 表单）、session.ts、html.ts
│   └── util/time.ts           本地时区 ISO、ISO 周窗口
├── test/                      node:test 测试（纯逻辑 + 临时目录）
├── tools/                     gen-placeholder-bundle.ts、smoke.sh
├── data/                      运行时数据（已 gitignore）：config.json、keys.json、
│                              devices.json、assets/（皮肤包 + 草稿）
└── dist/                      构建产物（已 gitignore）
```

## 测试

`npm test` 运行纯逻辑测试：WMO 归一化全 0–99 范围（镜像
`main/app/app_weather.c`）、经验分段表（满级合计 3145 番茄）、配对码生命
周期、APB1 打包/解析（含 4 MB 上限）、RGB565 字节序、本地 Agent 报告解析
（活动块/窗口/Token）、GLM/DeepSeek 响应映射、快照字段/排序/百分比规则、
城市表、发布器草稿→发布→回滚、sharp PNG 转码管线。
`npm run smoke` 额外把真实服务短启停做端到端验证。

## P1 未实现

- NFC 轻碰配网／打开入口及连贯的两阶段手机流程；目前需从设备设置手动进入
  SoftAP 表单。见[接入设计与交接](../docs/specs/2026-09-24-badge-onboarding.zh_CN.md)。
- Claude 订阅额度轮询、ChatGPT 网页额度及 API 实际账单未实现；已提供 Codex 本地额度观测。
- 配置中心素材动画预览与快照预览渲染（P2）。
- 天气精灵上传（仅内置）、按设备强制重下发皮肤包、多设备分组（P3+）、
  Key 加密存储、HTTPS（P1 设备在局域网内走明文 HTTP；需要 TLS 请加反向代理）。


## 本地验收交接（2026-09-26）

服务构建、77 项单元测试和真实日志冒烟验收通过，涵盖登录、手动刷新、一次性配对、鉴权快照、拒绝未授权访问、素材下载及设备快照 4096 字节限制。账户页显示观测到的今日／本周 Token 和有效的 Codex 额度。运行中的本地实例也与独立执行的 ccusage 当日报告核对一致。没有真实 Key 的 GLM／DeepSeek 账户查询尚未验证，不声称账户全量或实际账单完整。

打开 `http://localhost:3000/portal/accounts`。后台进程使用忽略的 `service/.env` 和 `service/data/`；登录密码查看 `.env` 的 `PORTAL_PASSWORD`。`npm run start:local` 会复用可访问的实例，不会在修改源码后自动重启。重启时先确认配置端口上的服务进程并停止它，再运行此命令。电脑休眠／重启会中断服务；该辅助命令不安装系统自启动项。局域网设备使用电脑的局域网地址，不能用 localhost，并通过配置中心配对。

语音输入调研见[可行性报告](../docs/specs/2026-09-26-voice-input-feasibility.zh_CN.md)。
