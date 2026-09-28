/**
 * Config portal (mobile web UI, same port as the device API).
 * P1 pages: login, accounts (keys + primary), preferences (city / budget),
 * devices (pairing code / revoke), assets (upload -> publish -> rollback).
 */

import type { FastifyInstance, FastifyReply, FastifyRequest } from "fastify";
import type { SettingsStore } from "../store/settings.js";
import { maskKey, type Provider } from "../store/settings.js";
import type { DeviceRegistry } from "../store/devices.js";
import type { DraftStore } from "../assets/draftStore.js";
import type { AssetPublisher } from "../assets/publisher.js";
import type { Collector } from "../collectors/types.js";
import type { Snapshot } from "../snapshot.js";
import { CITY_TABLE, findCity } from "../cityTable.js";
import { ACTION_SPECS, type ActionSlot } from "../assets/placeholder.js";
import {
  ACTION_RULES,
  DECO_H,
  DECO_W,
  MAP_H,
  MAP_W,
  MAX_DECORATIONS,
  MAX_UPLOAD_FILES,
  AssetValidationError,
  convertWithSize,
  validateActionFrameCount,
  validateFps,
} from "../assets/pipeline.js";
import { BundleFormatError } from "../assets/bundleFormat.js";
import { esc, flashFromQuery, layout, type PortalTab } from "./html.js";
import { validateBadgeName, validateBadgeRole } from "./badgeName.js";
import { FailureLimiter } from "../util/rateLimit.js";
import {
  SESSION_COOKIE,
  SessionStore,
  checkPassword,
  readSessionCookie,
} from "./session.js";

export interface PortalDeps {
  auth: { password: string | null; secret: string };
  settings: SettingsStore;
  devices: DeviceRegistry;
  collectors: Collector[];
  draft: DraftStore;
  publisher: AssetPublisher;
  refreshSnapshot: () => Promise<unknown>;
  getSnapshot?: () => Promise<Snapshot>;
}

const ACTION_LABELS: Record<ActionSlot, string> = {
  run: "跑步",
  fight: "打怪",
  sleep: "睡觉",
  victory: "胜利",
};

function redirect(reply: FastifyReply, url: string, msg?: string, err?: string): FastifyReply {
  const sep = (u: string) => (u.includes("?") ? "&" : "?");
  let target = url;
  if (msg) target += sep(target) + "msg=" + encodeURIComponent(msg);
  else if (err) target += sep(target) + "err=" + encodeURIComponent(err);
  return reply.redirect(target);
}

function fmtBytes(n: number): string {
  return n >= 1024 ? `${(n / 1024).toFixed(1)} KB` : `${n} B`;
}

function fmtTime(iso: string | null): string {
  return iso ? iso.replace("T", " ").slice(0, 19) : "—";
}

export function registerPortal(app: FastifyInstance, deps: PortalDeps): void {
  const sessions = new SessionStore();
  // Plaintext-HTTP password endpoint: cap failed logins per client IP so the
  // portal password cannot be brute-forced from the LAN.
  const loginFailures = new FailureLimiter(10, 10 * 60_000);

  // ---- auth guard ----------------------------------------------------------
  app.addHook("onRequest", async (request: FastifyRequest, reply: FastifyReply) => {
    const url = request.raw.url ?? "";
    if (!url.startsWith("/portal") || url.startsWith("/portal/login")) return;
    const sid = readSessionCookie(request);
    if (!sessions.isValid(sid, new Date())) {
      redirect(reply, "/portal/login");
    }
  });

  app.setNotFoundHandler((request, reply) => {
    if ((request.raw.url ?? "").startsWith("/portal")) {
      void reply.code(404).send(layout({ title: "未找到", loggedIn: true, body: '<div class="card">页面不存在</div>' }));
      return;
    }
    void reply.code(404).send({ error: "not found" });
  });

  // ---- login ---------------------------------------------------------------
  app.get("/portal/login", async (request, reply) => {
    if (!deps.auth.password) {
      return reply.type("text/html").send(
        layout({
          title: "登录",
          loggedIn: false,
          flash: { kind: "err", text: "未设置 PORTAL_PASSWORD：请在 service/.env 中配置后重启服务" },
          body: '<div class="card"><h2>配置中心未启用</h2><div class="hint">设备 API 不受影响。</div></div>',
        }),
      );
    }
    const flash = flashFromQuery(request.query as Record<string, unknown>);
    return reply.type("text/html").send(
      layout({
        title: "登录",
        loggedIn: false,
        flash,
        body: `<div class="card">
<h2>口令登录</h2>
<form method="post" action="/portal/login">
  <label>配置中心口令</label>
  <input type="password" name="password" autocomplete="current-password" required>
  <div style="margin-top:12px"><button type="submit">登录</button></div>
</form>
</div>`,
      }),
    );
  });

  app.post("/portal/login", async (request, reply) => {
    const body = (request.body ?? {}) as { password?: unknown };
    const pw = typeof body.password === "string" ? body.password : "";
    const now = new Date();
    if (loginFailures.isBlocked(request.ip, now)) {
      return redirect(reply, "/portal/login", undefined, "尝试过多，请稍后再试");
    }
    if (!checkPassword(pw, deps.auth.password)) {
      loginFailures.recordFailure(request.ip, now);
      return redirect(reply, "/portal/login", undefined, "口令错误");
    }
    loginFailures.reset(request.ip);
    const sid = sessions.create(now);
    reply.setCookie(SESSION_COOKIE, sid, {
      signed: true,
      httpOnly: true,
      sameSite: "lax",
      path: "/",
      maxAge: 8 * 3600,
    });
    return redirect(reply, "/portal");
  });

  app.get("/portal/logout", async (request, reply) => {
    sessions.destroy(readSessionCookie(request));
    reply.clearCookie(SESSION_COOKIE, { path: "/" });
    return redirect(reply, "/portal/login");
  });

  // ---- accounts ------------------------------------------------------------
  const renderAccounts = async (request: FastifyRequest, reply: FastifyReply) => {
    const settings = deps.settings.get();
    const keys = deps.settings.getKeys();
    const status = (p: string) => deps.collectors.find((c) => c.provider === p)?.status();
    const local = status("local");
    const glm = status("glm");
    const deepseek = status("deepseek");
    const codex = status("codex");
    const snapshot = await deps.getSnapshot?.();
    const count = snapshot?.dailyTokens.used;
    const week = snapshot?.accounts.find(a => a.provider === "local")?.quotas.weeklyTokens?.used;
    const number = (n: number | null | undefined) => n == null ? "暂无数据" : n.toLocaleString("zh-CN");

    const keyCard = (
      id: "glm" | "deepseek",
      title: string,
      hint: string,
      st: ReturnType<Collector["status"]> | undefined,
    ) => `<div class="card">
<h2>${title} <span class="badge ${st?.connected ? "ok" : "off"}">${st?.connected ? "已连接" : "未连接"}</span></h2>
<div class="hint">${hint}</div>
<div class="hint">当前 Key：<span class="mono">${esc(keys[id] ? maskKey(keys[id]) : "未设置")}</span>${st?.lastOkAt ? ` · 最近采集 ${esc(fmtTime(st.lastOkAt))}` : ""}${st?.connected && st?.detail ? ` · ${esc(st.detail)}` : ""}</div>
<form method="post" action="/portal/accounts/keys" style="margin-top:8px">
  <input type="hidden" name="provider" value="${id}">
  <label>API Key（留空保持不变，仅回显尾 4 位）</label>
  <input type="text" name="key" placeholder="sk-..." autocomplete="off">
  <div class="row" style="margin-top:10px">
    <button type="submit" name="op" value="save">保存</button>
    <button type="submit" name="op" value="clear" class="gray">清除</button>
  </div>
</form>
</div>`;

    const primaryOptions = (["local", "codex", "glm", "deepseek"] as Provider[])
      .map(
        (p) =>
          `<label style="margin:8px 0"><input type="radio" name="primary" value="${p}" ${settings.primaryAccount === p ? "checked" : ""}> ${p === "local" ? "本地 Agent" : p === "codex" ? "Codex 额度" : p === "glm" ? "GLM" : "DeepSeek"}</label>`,
      )
      .join("");

    const body = `
<div class="card">
  <h2>本地 Agent <span class="badge ${local?.connected ? "ok" : "off"}">${local?.connected ? "已安装" : "未安装"}</span></h2>
  <div class="hint">ccusage 汇总本机 Claude Code、Codex、OpenCode、Gemini CLI 等已检测到的 Agent；读取本地日志，不需要厂商 API Key。${esc(local?.detail ?? "")}</div>
  <div class="hint">最近采集：${esc(fmtTime(local?.lastRunAt ?? null))}${local?.lastOkAt && local.lastOkAt === local.lastRunAt ? "（成功）" : ""}</div>
  <p>今日 Token：<strong>${number(count)}</strong> · 本周 Token：<strong>${number(week)}</strong></p>
  <div class="hint">本机所有会话，包含缓存读取。分项仅来自已检测到的本地 Agent 日志；额度与 Token 数是不同指标。</div>
  <table><thead><tr><th>Agent</th><th>今日 Token</th><th>本周 Token</th><th>5 小时额度</th><th>周额度</th></tr></thead><tbody>
  ${(snapshot?.agents ?? []).map(a => `<tr><td>${esc(a.agent)}</td><td>${number(a.dailyTokens)}</td><td>${number(a.weeklyTokens)}</td><td>${a.rolling5h ? `${number(a.rolling5h.used)}%` : "暂无数据"}</td><td>${a.weekly ? `${number(a.weekly.used)}%` : "暂无数据"}</td></tr>`).join("") || `<tr><td colspan="5">暂无 Agent 分项</td></tr>`}
  </tbody></table>
  <div class="hint">覆盖本机已检测日志，不代表供应商账户全量或订阅额度。设备缓存最多约 10 分钟后更新。</div>
  <form method="post" action="/portal/accounts/refresh"><button type="submit">立即采集</button></form>
</div>
${keyCard("glm", "GLM", "实验性 Coding Plan 接口，尚待真实账户验证；未知单位不展示。", glm)}
${keyCard("deepseek", "DeepSeek", "官方余额接口 api.deepseek.com/user/balance。DeepSeek 无周额度口径，快照中显示为余额（CNY，无 cap）。", deepseek)}
${snapshot?.accounts.filter(a => a.balance).map(a => `<div class="card"><h2>${esc(a.label)} 余额</h2><p>${number(a.balance!.remaining)} ${esc(a.balance!.currency)}</p><div class="hint">预付余额，不是今日费用或 Token 数。</div></div>`).join("") ?? ""}
<div class="card">
  <h2>Codex 额度</h2>
  <div class="hint">${esc(codex?.detail ?? "未检测到 Codex")}</div>
  <div class="hint">客户端样本：${esc(fmtTime(codex?.lastOkAt ?? null))}。来自本机 Codex 会话，未绑定当前登录身份；不是 ChatGPT 网页额度。</div>
  <p>${snapshot?.accounts.find(a => a.provider === "codex")?.quotas.weekly ? `周额度已用：${number(snapshot.accounts.find(a => a.provider === "codex")!.quotas.weekly!.used)}%` : "周额度：暂无数据"}</p>
  <p>${snapshot?.accounts.find(a => a.provider === "codex")?.quotas.rolling5h ? `5 小时额度已用：${number(snapshot.accounts.find(a => a.provider === "codex")!.quotas.rolling5h!.used)}%` : "5 小时额度：暂无数据"}</p>
</div>
<div class="card">
  <h2>主力账户</h2>
  <div class="hint">设备仪表盘主界面渲染排在第一位的账户（快照 accounts[0]）。</div>
  <form method="post" action="/portal/accounts/primary">
    ${primaryOptions}
    <div style="margin-top:10px"><button type="submit">保存主力账户</button></div>
  </form>
</div>`;
    return reply.type("text/html").send(
      layout({ title: "账户", activeTab: "accounts", flash: flashFromQuery(request.query as Record<string, unknown>), body }),
    );
  };

  app.get("/portal", renderAccounts);
  app.get("/portal/accounts", renderAccounts);
  app.post("/portal/accounts/refresh", async (_request, reply) => {
    await deps.refreshSnapshot();
    return redirect(reply, "/portal/accounts", "采集已刷新，请查看读数与各来源状态");
  });

  app.post("/portal/accounts/keys", async (request, reply) => {
    const body = (request.body ?? {}) as { provider?: string; key?: string; op?: string };
    if (body.provider !== "glm" && body.provider !== "deepseek") {
      return redirect(reply, "/portal", undefined, "未知账户");
    }
    if (body.op === "clear") {
      await deps.settings.setKey(body.provider, null);
      await deps.refreshSnapshot();
      return redirect(reply, "/portal", `${body.provider.toUpperCase()} Key 已清除`);
    }
    const key = (body.key ?? "").trim();
    if (key === "") return redirect(reply, "/portal", undefined, "Key 不能为空（留空提交请用清除）");
    await deps.settings.setKey(body.provider, key);
    await deps.refreshSnapshot();
    return redirect(reply, "/portal", `${body.provider.toUpperCase()} Key 已保存（明文存储于 data/keys.json，注意风险）`);
  });

  app.post("/portal/accounts/primary", async (request, reply) => {
    const body = (request.body ?? {}) as { primary?: string };
    if (body.primary !== "local" && body.primary !== "codex" && body.primary !== "glm" && body.primary !== "deepseek") {
      return redirect(reply, "/portal", undefined, "无效的主力账户");
    }
    await deps.settings.update({ primaryAccount: body.primary });
    await deps.refreshSnapshot().catch(() => undefined);
    return redirect(reply, "/portal", `主力账户已设为 ${body.primary}`);
  });

  // ---- badge ---------------------------------------------------------------
  app.get("/portal/badge", async (request, reply) => {
    const { badgeName: name, badgeRole: role } = deps.settings.get();
    const body = `<div class="card">
  <h2>工牌名牌</h2>
  <div class="hint">姓名与今日 Token 用量同屏显示；在主屏选中顶部信息区按 OK，仍进入用量明细。</div>
  <form method="post" action="/portal/badge">
    <label>展示姓名</label>
    <input type="text" name="badgeName" maxlength="31" value="${esc(name)}" placeholder="你的名字" autocomplete="name">
    <label>身份</label>
    <input type="text" name="badgeRole" maxlength="10" value="${esc(role)}" placeholder="Developer">
    <div class="hint" style="margin-top:6px">当前字体支持英文字母、数字及已收录汉字；最多约 4 个汉字。留空显示“你的名字”。</div>
    <div style="margin-top:12px"><button type="submit">保存名牌</button></div>
  </form>
</div>`;
    return reply.type("text/html").send(
      layout({ title: "名牌", activeTab: "badge", flash: flashFromQuery(request.query as Record<string, unknown>), body }),
    );
  });

  app.post("/portal/badge", async (request, reply) => {
    const body = (request.body ?? {}) as Record<string, unknown>;
    const badgeName = validateBadgeName(body.badgeName);
    if (badgeName === null) return redirect(reply, "/portal/badge", undefined, "姓名过长或含当前工牌字体未收录的字，请联系维护者扩充字库");
    const badgeRole = validateBadgeRole(body.badgeRole);
    if (badgeRole === null) return redirect(reply, "/portal/badge", undefined, "身份最多 10 个英文字符或数字，可含空格、句点、下划线和连字符");
    await deps.settings.update({ badgeName, badgeRole });
    await deps.refreshSnapshot().catch(() => undefined);
    return redirect(reply, "/portal/badge", "名牌已保存，设备下次同步后显示");
  });

  // ---- preferences -----------------------------------------------------------
  app.get("/portal/prefs", async (request, reply) => {
    const s = deps.settings.get();
    const options = CITY_TABLE.map(
      (c) => `<option value="${c.id}" ${s.city === c.id ? "selected" : ""}>${c.name}</option>`,
    ).join("");
    const body = `
<div class="card">
  <h2>天气城市</h2>
  <div class="hint">open-meteo 免 key 拉取当前天气与日出日落，缓存 30 分钟。</div>
  <form method="post" action="/portal/prefs">
    <label>城市（内置 ${CITY_TABLE.length} 城）</label>
    <select name="city">${options}</select>
    <label>自定义纬度（可选，填写后覆盖城市）</label>
    <input type="text" name="customLat" inputmode="decimal" placeholder="如 30.5728" value="${s.customLat ?? ""}">
    <label>自定义经度（可选）</label>
    <input type="text" name="customLon" inputmode="decimal" placeholder="如 104.0668" value="${s.customLon ?? ""}">
    <label>周 Token 预算（留空 = 设备显示纯数值，不加百分比）</label>
    <input type="text" name="weeklyTokenBudget" inputmode="numeric" placeholder="如 5000000" value="${s.weeklyTokenBudget ?? ""}">
    <div style="margin-top:12px"><button type="submit">保存偏好</button></div>
  </form>
</div>`;
    return reply.type("text/html").send(
      layout({ title: "偏好", activeTab: "prefs", flash: flashFromQuery(request.query as Record<string, unknown>), body }),
    );
  });

  app.post("/portal/prefs", async (request, reply) => {
    const body = (request.body ?? {}) as Record<string, string>;
    const parseNum = (v: string | undefined): number | null => {
      if (v === undefined || v.trim() === "") return null;
      const n = Number(v.trim());
      return Number.isFinite(n) ? n : null;
    };
    const city = findCity(body.city ?? "") ? body.city : CITY_TABLE[0]!.id;
    const customLat = parseNum(body.customLat);
    const customLon = parseNum(body.customLon);
    let weeklyTokenBudget: number | null = parseNum(body.weeklyTokenBudget);
    if (weeklyTokenBudget !== null && (!Number.isInteger(weeklyTokenBudget) || weeklyTokenBudget <= 0)) {
      weeklyTokenBudget = null;
      return redirect(reply, "/portal/prefs", undefined, "周 Token 预算须为正整数（留空表示无预算）");
    }
    await deps.settings.update({ city, customLat, customLon, weeklyTokenBudget });
    await deps.refreshSnapshot().catch(() => undefined);
    return redirect(reply, "/portal/prefs", "偏好已保存（下次快照生效）");
  });

  // ---- devices ---------------------------------------------------------------
  app.get("/portal/devices", async (request, reply) => {
    const devices = deps.devices.list();
    const active = deps.devices.activePairingCode(new Date());
    const rows = devices.length === 0
      ? '<tr><td class="muted">暂无设备。生成配对码后，在设备 SoftAP 配网页填入服务地址与配对码。</td></tr>'
      : devices
          .map(
            (d) => `<tr>
<td><b>${esc(d.name)}</b><br><span class="muted mono">${esc(d.id)}</span></td>
<td><span class="badge ${d.status === "active" ? "ok" : "off"}">${d.status === "active" ? "令牌有效" : "已吊销"}</span></td>
<td>${esc(fmtTime(d.lastSyncAt))}</td>
<td>${
  d.status === "active"
    ? `<form class="inline" method="post" action="/portal/devices/revoke"><input type="hidden" name="deviceId" value="${esc(d.id)}"><button class="red small" type="submit">吊销</button></form>`
    : ""
}</td>
</tr>`,
          )
          .join("");
    const codeBlock = active
      ? `<div class="pairecode">${esc(active.code)}</div>
<div class="hint" style="text-align:center">${Math.max(1, Math.round((active.expiresAt - Date.now()) / 60000))} 分钟内有效 · 一次性使用</div>`
      : '<div class="hint" style="text-align:center;padding:10px 0">当前无有效配对码</div>';
    const body = `
<div class="card">
  <h2>配对码</h2>
  ${codeBlock}
  <form method="post" action="/portal/devices/pair-code" style="margin-top:8px;text-align:center">
    <button type="submit">${active ? "重新生成" : "生成配对码"}</button>
  </form>
</div>
<div class="card">
  <h2>设备列表</h2>
  <table><tr><th>设备</th><th>令牌</th><th>最后同步</th><th></th></tr>${rows}</table>
</div>`;
    return reply.type("text/html").send(
      layout({ title: "设备", activeTab: "devices", flash: flashFromQuery(request.query as Record<string, unknown>), body }),
    );
  });

  app.post("/portal/devices/pair-code", async (request, reply) => {
    const { code } = deps.devices.generatePairingCode(new Date());
    return redirect(reply, "/portal/devices", `配对码 ${code} 已生成（10 分钟内有效）`);
  });

  app.post("/portal/devices/revoke", async (request, reply) => {
    const body = (request.body ?? {}) as { deviceId?: string };
    const ok = await deps.devices.revoke(body.deviceId ?? "");
    return redirect(reply, "/portal/devices", ok ? "令牌已吊销，设备下次同步将被拒" : "设备不存在", ok ? undefined : "设备不存在");
  });

  // ---- assets ------------------------------------------------------------------
  app.get("/portal/assets", async (request, reply) => {
    const draft = deps.draft.get();
    const history = deps.publisher.history();
    const slotRow = (slot: ActionSlot) => {
      const d = draft.actions[slot];
      const spec = ACTION_SPECS[slot];
      const rule = ACTION_RULES[slot];
      const state = d
        ? `已上传 <b>${d.frames}</b> 帧 @ ${d.fps}fps（${d.w}×${d.h}）`
        : "未设置（发布时使用内置占位素材）";
      return `<div class="card">
<h2>${ACTION_LABELS[slot]} <span class="muted">（${rule.minFrames}–${rule.maxFrames} 帧 · 64×64 PNG）</span></h2>
<div class="hint">${state}</div>
<form method="post" action="/portal/assets/upload" enctype="multipart/form-data">
  <input type="hidden" name="slot" value="${slot}">
  <label>PNG 帧序列（按顺序多选，默认帧率 ${spec.fps}）</label>
  <input type="file" name="files" accept="image/png" multiple required>
  <label>帧率 1–10 fps</label>
  <input type="number" name="fps" min="1" max="10" value="${d?.fps ?? spec.fps}">
  <div style="margin-top:10px"><button type="submit">上传并转换</button></div>
</form>
</div>`;
    };
    const mapState = draft.map
      ? `已上传（${draft.map.w}×${draft.map.h}）`
      : "未设置（发布时使用内置渐变地图）";
    const decoState = draft.decorations.length > 0
      ? `已上传 ${draft.decorations.length} 个（24×24）`
      : "未设置（发布时使用内置灌木）";
    const historyRows = history.length === 0
      ? '<tr><td class="muted">尚无发布版本</td></tr>'
      : history
          .map(
            (h) => `<tr>
<td><b>v${h.version}</b>${h.version === deps.publisher.currentVersion() ? ' <span class="badge ok">当前</span>' : ""}</td>
<td>${esc(fmtTime(h.publishedAt))}</td>
<td>${fmtBytes(h.bytes)}</td>
<td class="muted">${esc(h.source)}</td>
<td><form class="inline" method="post" action="/portal/assets/rollback"><input type="hidden" name="version" value="${h.version}"><button class="gray small" type="submit">回滚</button></form></td>
</tr>`,
          )
          .join("");
    const body = `
${slotRow("run")}${slotRow("fight")}${slotRow("sleep")}${slotRow("victory")}
<div class="card">
  <h2>地图条带 <span class="muted">（240×160 PNG，1 张）</span></h2>
  <div class="hint">${mapState}。服务端转码 RGB565；请上传左右可无缝循环的条带。</div>
  <form method="post" action="/portal/assets/upload" enctype="multipart/form-data">
    <input type="hidden" name="slot" value="map">
    <input type="file" name="files" accept="image/png" required>
    <div style="margin-top:10px"><button type="submit">上传并转换</button></div>
  </form>
</div>
<div class="card">
  <h2>装扮元素 <span class="muted">（24×24 PNG，每个 1 帧，最多 ${MAX_DECORATIONS} 个）</span></h2>
  <div class="hint">${decoState}。上传会整体替换装扮列表。</div>
  <form method="post" action="/portal/assets/upload" enctype="multipart/form-data">
    <input type="hidden" name="slot" value="deco">
    <input type="file" name="files" accept="image/png" multiple required>
    <div style="margin-top:10px"><button type="submit">上传并转换</button></div>
  </form>
</div>
<div class="card">
  <h2>发布皮肤包</h2>
  <div class="hint">发布 = 草稿打包为新版本（v${deps.publisher.currentVersion() + 1}，自动递增），设备下次同步时发现 <code>assetBundle.version</code> 变化并下载。未上传的槽位使用内置占位素材；天气雨雪精灵 P1 使用内置素材。单包上限 4 MB。</div>
  <form method="post" action="/portal/assets/publish" style="margin-top:10px">
    <button type="submit">发布新版本</button>
  </form>
</div>
<div class="card">
  <h2>历史版本</h2>
  <table><tr><th>版本</th><th>发布时间</th><th>大小</th><th>来源</th><th></th></tr>${historyRows}</table>
  <div class="hint" style="margin-top:8px">回滚 = 把旧版本内容重新发布为新版本号（设备再次热更新）。</div>
</div>`;
    return reply.type("text/html").send(
      layout({ title: "素材", activeTab: "assets" as PortalTab, flash: flashFromQuery(request.query as Record<string, unknown>), body }),
    );
  });

  app.post("/portal/assets/upload", async (request, reply) => {
    try {
      const parts = request.parts();
      let slot = "";
      let fpsStr = "";
      const files: { filename: string; buffer: Buffer }[] = [];
      for await (const part of parts) {
        if (part.type === "file") {
          if (files.length >= MAX_UPLOAD_FILES) {
            throw new AssetValidationError(`单次上传最多 ${MAX_UPLOAD_FILES} 个文件`);
          }
          const buffer = await part.toBuffer();
          files.push({ filename: part.filename, buffer });
        } else if (part.type === "field") {
          if (part.fieldname === "slot") slot = String(part.value ?? "");
          if (part.fieldname === "fps") fpsStr = String(part.value ?? "");
        }
      }
      if (files.length === 0) throw new AssetValidationError("未收到任何文件");

      if (slot === "map") {
        if (files.length !== 1) throw new AssetValidationError("地图槽位只接受 1 张 PNG");
        const img = await convertWithSize(files[0]!.buffer, MAP_W, MAP_H, "地图条带", { alpha: false });
        await deps.draft.setMap(img.data, img.w, img.h);
        return redirect(reply, "/portal/assets", `地图已更新（${img.w}×${img.h}）`);
      }

      if (slot === "deco") {
        if (files.length > MAX_DECORATIONS) throw new AssetValidationError(`装扮元素最多 ${MAX_DECORATIONS} 个`);
        const decos = [] as { w: number; h: number; data: Buffer }[];
        for (let i = 0; i < files.length; i++) {
          const img = await convertWithSize(files[i]!.buffer, DECO_W, DECO_H, `装扮元素 #${i + 1}`);
          decos.push({ w: img.w, h: img.h, data: img.data });
        }
        await deps.draft.setDecorations(decos);
        return redirect(reply, "/portal/assets", `装扮元素已更新（${decos.length} 个）`);
      }

      if (slot === "run" || slot === "fight" || slot === "sleep" || slot === "victory") {
        validateActionFrameCount(slot, files.length);
        const fps = validateFps(Number(fpsStr || ACTION_SPECS[slot].fps));
        const frames: Buffer[] = [];
        for (let i = 0; i < files.length; i++) {
          const img = await convertWithSize(files[i]!.buffer, 64, 64, `${slot} 第 ${i + 1} 帧`);
          frames.push(img.data);
        }
        await deps.draft.setAction(slot, frames, fps, 64, 64);
        return redirect(reply, "/portal/assets", `${ACTION_LABELS[slot]} 已更新（${frames.length} 帧 @ ${fps}fps）`);
      }

      return redirect(reply, "/portal/assets", undefined, "未知素材槽位");
    } catch (err) {
      const msg =
        err instanceof AssetValidationError || err instanceof BundleFormatError
          ? err.message
          : err instanceof Error
            ? `上传失败: ${err.message}`
            : "上传失败";
      return redirect(reply, "/portal/assets", undefined, msg);
    }
  });

  app.post("/portal/assets/publish", async (request, reply) => {
    try {
      const entry = await deps.publisher.publishFromDraft(deps.draft.get());
      return redirect(
        reply,
        "/portal/assets",
        `已发布 v${entry.version}（${fmtBytes(entry.bytes)}），设备下次同步时更新`,
      );
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      return redirect(reply, "/portal/assets", undefined, `发布失败: ${msg}`);
    }
  });

  app.post("/portal/assets/rollback", async (request, reply) => {
    const body = (request.body ?? {}) as { version?: string };
    const version = Number(body.version);
    if (!Number.isInteger(version) || version < 1) {
      return redirect(reply, "/portal/assets", undefined, "无效版本号");
    }
    try {
      const entry = await deps.publisher.rollback(version);
      return redirect(reply, "/portal/assets", `已把 v${version} 回滚发布为 v${entry.version}`);
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      return redirect(reply, "/portal/assets", undefined, `回滚失败: ${msg}`);
    }
  });
}
