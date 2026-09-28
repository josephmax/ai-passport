[English](usage-collection.md) · **简体中文**

# 本地服务用量采集方案

## ccusage 的覆盖范围

固定版本的 ccusage CLI 是可复用的**本地 Agent 日志**读取器，提供运行本地服务的主机上受支持客户端的观测 Token 数。启用价格计算时，结果也是估算值。它不能独自取得账户全量的订阅重置窗口、官方账单或其他主机上的调用。仅凭模型名无法确定付款账户，也无法判断调用走订阅还是 API Key。继续把 ccusage 当作一个适配器；供应商额度与账单能力在本地服务扩展，无须为此 fork ccusage。

## 读数契约

读数保存为 `{accountId, agentId?, metric, value, unit, intervalStart?, intervalEnd?, observedAt, source, authority, coverage, requestId?, resetAt?}`。`metric` 区分 `tokens`、`subscription_quota`、`api_charge` 和 `prepaid_balance`；`authority` 区分 `official`、`observed` 和 `estimated`；`coverage` 说明观测到的 Agent 或请求范围。额度读数还须标明 `value` 是已用还是剩余。无效或过期读数带不可用状态及原因。不同货币、单位、账户或重叠来源不得相加。供应商原始响应不得进入设备报文或诊断日志。

## 适配器与调度

| 适配器 | 输入 | 输出 | 限制 |
| --- | --- | --- | --- |
| ccusage | 本地 Agent 记录 | 观测 Token，可选估算价格 | 只覆盖本机受支持日志 |
| 额度采样器 | 经核实的供应商订阅用量接口或受支持的本地客户端状态 | 窗口已用／剩余、上限、重置时间 | 定期采样，保留时间戳并让过期读数失效 |
| 供应商财务接口 | 有文档的账单或余额 API | 分开的实际收费或预付余额 | 单凭余额变化不能证明支出 |
| API 回执 | 经埋点 SDK／代理所发请求的响应 `usage` 字段 | 逐请求 Token；有官方价格或账单时提供费用 | 无法看到埋点路径以外的调用 |

各适配器独立运行，设置超时、限速、退避和最后一次成功样本。单个供应商失败不清除其他读数。能取得稳定请求 ID 时，对本地日志与 API 回执去重；缺乏身份信息时分别展示来源合计，不相加。服务默认每 10 分钟及主机本地零点刷新缓存；新一天采集完成前屏蔽昨日 Token。设备清醒且联网时每小时从缓存拉取，零点没有服务端推送。配置中心可显示来源、新鲜度及覆盖范围；设备只收到可支持且单位正确的字段。

## 供应商适配与当前缺口

| 供应商／用途 | 可用办法 | 当前服务状态 |
| --- | --- | --- |
| Claude Code、Codex、OpenCode、Gemini CLI 等 ccusage 支持的 Agent | 本地日志 Token | 服务主机已接入；没有订阅窗口 |
| Claude／ChatGPT 订阅计划 | 仅在存在经核实且受支持的来源时定期采样额度 | 已接入 Codex 本地 `rate_limits` 观测，超过一小时或重置后失效；Claude 和 ChatGPT 网页额度仍不可用 |
| Kimi CLI 等国产 Agent | ccusage 支持时读取日志；计划额度另设采样器 | 本地 Token 可能覆盖；账户额度未实现 |
| GLM Coding Plan | 若账户可访问，经核实的计划额度来源 | 现有启发式接口属于实验性；响应结构和单位须实测 |
| DeepSeek API | 官方余额接口加响应 `usage` 回执计量 Token | 余额采集器已单独返回 `balance` 读数；真实账户查询未验证 |

服务已分开 DeepSeek 余额、正确标注 GLM 请求次数并要求明确窗口，配置中心与设备快照显示本地 Token 总量、各 Agent 的今日／本周分项及有效的 Codex 额度。Pi、ZCode、Claude Code 的额度没有可靠来源时保持 null。完整读数契约及持久化样本存储仍待实现。之后逐个加入经核实的供应商采样器和 API 回执，并用真实账户做验收。单个 Key 或本地日志无法保证覆盖所有 Agent 和账户；界面必须报告实际覆盖范围。

## 参考资料

- [ccusage 支持的来源](https://github.com/ccusage/ccusage#supported-sources)
- [DeepSeek 余额 API](https://api-docs.deepseek.com/api/get-user-balance/)
- [DeepSeek 对话响应中的用量](https://api-docs.deepseek.com/api/create-chat-completion/)
- [Kimi CLI 常见问题](https://github.com/MoonshotAI/kimi-cli/blob/main/docs/en/faq.md)
