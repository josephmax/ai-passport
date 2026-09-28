**English** · [简体中文](usage-collection.zh_CN.md)

# Local service usage collection plan

## What ccusage covers

The pinned ccusage CLI is a reusable **local agent log** reader. It supplies observed Tokens for supported clients on the service host. Its price calculations, if enabled, are estimates. It cannot by itself measure an account-wide subscription reset window, an official invoice, or calls made on another host. Model name alone does not identify the paying account or whether a call used a subscription or an API key. Keep ccusage as an adapter; extend the local service rather than forking ccusage for provider quota and billing features.

## Reading contract

Store a reading as `{accountId, agentId?, metric, value, unit, intervalStart?, intervalEnd?, observedAt, source, authority, coverage, requestId?, resetAt?}`. `metric` distinguishes `tokens`, `subscription_quota`, `api_charge`, and `prepaid_balance`; `authority` distinguishes `official`, `observed`, and `estimated`; `coverage` states which agents or requests were seen. A quota reading also records whether `value` means used or remaining. Invalid or stale readings carry an unavailable state and a reason. Never sum different currencies, units, accounts, or overlapping sources. Keep provider raw responses out of device payloads and diagnostic logs.

## Adapters and scheduling

| Adapter | Input | Output | Limit |
| --- | --- | --- | --- |
| ccusage | Local Agent records | Observed Tokens, optional estimated price | Host and supported log coverage only |
| Quota sampler | A verified provider subscription usage endpoint or supported local client status | Window use/remaining, cap, reset time | Poll periodically; retain timestamp and expire stale samples |
| Provider finance | Documented provider billing or balance API | Actual charge or prepaid balance, separately | Balance changes alone do not prove spend |
| API receipt | Usage fields returned for requests made through an instrumented SDK/proxy | Per-request Tokens and, with official price or bill, charge | Does not see calls outside that path |

Run each adapter independently with a timeout, rate limit, backoff, and last-good sample. A provider's failure cannot erase another reading. Deduplicate local logs and API receipts by stable request ID when possible; if identity is missing, show separate source totals and do not add them. The hourly device fetch reads the local normalized cache. The portal can show source, freshness, and coverage; the device gets only supported, unit-safe fields.

## Provider fit and current gaps

| Provider/use | Available approach | Current service state |
| --- | --- | --- |
| Claude Code, Codex, OpenCode, Gemini CLI and other ccusage-supported agents | Local log Tokens | Implemented on service host; no subscription window |
| Claude/ChatGPT subscription plans | Periodic quota sampling only where a verified supported source is available | Codex local `rate_limits` observations implemented with one-hour/reset expiry; Claude and ChatGPT web quotas remain unavailable |
| Kimi CLI and other Chinese agents | ccusage log coverage where supported; separate plan quota sampler | Local Tokens may be covered; account quota unimplemented |
| GLM Coding Plan | Verified plan quota source, if available to the account | Existing heuristic endpoint is experimental; response shape and units need live verification |
| DeepSeek API | Official balance endpoint plus response `usage` receipts for Tokens | Balance collector now returns a separate `balance` reading; real-account query remains unverified |

The service now separates DeepSeek balance, labels GLM requests correctly and requires an explicit window, and displays local Token totals and fresh Codex quota on the portal. The complete reading contract and durable sample storage remain to be implemented. Keep the existing device snapshot schema until firmware supports the typed fields; unsupported quota fields must be null. Then add verified provider samplers and instrumented API receipts one at a time with account-backed acceptance tests. A single key or local log cannot guarantee complete coverage of every Agent and every account; the UI must report the covered scope.

## References

- [ccusage supported sources](https://github.com/ccusage/ccusage#supported-sources)
- [DeepSeek balance API](https://api-docs.deepseek.com/api/get-user-balance/)
- [DeepSeek chat completion usage](https://api-docs.deepseek.com/api/create-chat-completion/)
- [Kimi CLI FAQ](https://github.com/MoonshotAI/kimi-cli/blob/main/docs/en/faq.md)
