/** Server-rendered HTML for the config portal (mobile-first, no framework). */

export function esc(s: unknown): string {
  return String(s ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

export type PortalTab = "badge" | "accounts" | "prefs" | "devices" | "assets";

const TABS: { id: PortalTab; label: string; href: string }[] = [
  { id: "badge", label: "名牌", href: "/portal/badge" },
  { id: "accounts", label: "账户", href: "/portal" },
  { id: "prefs", label: "偏好", href: "/portal/prefs" },
  { id: "devices", label: "设备", href: "/portal/devices" },
  { id: "assets", label: "素材", href: "/portal/assets" },
];

const CSS = `
:root { color-scheme: light; }
* { box-sizing: border-box; }
body { margin: 0; font-family: -apple-system, "PingFang SC", "Noto Sans SC", system-ui, sans-serif;
  background: #f2f4f8; color: #1c2333; }
.wrap { max-width: 560px; margin: 0 auto; padding: 12px 14px 48px; }
h1 { font-size: 20px; margin: 10px 0 4px; }
.sub { color: #6b7385; font-size: 13px; margin-bottom: 14px; }
nav.tabs { display: flex; gap: 6px; margin: 10px 0 14px; }
nav.tabs a { flex: 1; text-align: center; padding: 9px 0; border-radius: 10px; text-decoration: none;
  font-size: 14px; background: #e6e9f0; color: #46506a; }
nav.tabs a.active { background: #2f5cff; color: #fff; font-weight: 600; }
.card { background: #fff; border-radius: 14px; padding: 14px 16px; margin-bottom: 12px;
  box-shadow: 0 1px 2px rgba(20,30,60,.06); }
.card h2 { font-size: 16px; margin: 0 0 8px; }
.card .hint, .muted { color: #7b8398; font-size: 12.5px; line-height: 1.5; }
.badge { display: inline-block; padding: 2px 8px; border-radius: 999px; font-size: 12px; }
.badge.ok { background: #e2f6e8; color: #1c7c3c; }
.badge.off { background: #fdeaea; color: #b03434; }
.badge.p2 { background: #eef0f6; color: #6b7385; }
input[type=text], input[type=password], input[type=number], select {
  width: 100%; padding: 10px 12px; border: 1px solid #d6dbe7; border-radius: 10px;
  font-size: 15px; background: #fff; }
button, .btn { display: inline-block; border: 0; border-radius: 10px; padding: 10px 16px;
  font-size: 15px; background: #2f5cff; color: #fff; cursor: pointer; text-decoration: none; }
button.gray, .btn.gray { background: #8a92a8; }
button.red, .btn.red { background: #d64545; }
button.small { padding: 6px 10px; font-size: 13px; }
label { display: block; font-size: 13px; color: #46506a; margin: 10px 0 4px; }
.flash { padding: 10px 12px; border-radius: 10px; font-size: 13.5px; margin-bottom: 12px; }
.flash.ok { background: #e2f6e8; color: #14532d; }
.flash.err { background: #fdeaea; color: #7f1d1d; }
table { width: 100%; border-collapse: collapse; font-size: 13.5px; }
td, th { padding: 7px 6px; border-bottom: 1px solid #eef0f5; text-align: left; vertical-align: top; }
code { background: #eef0f6; padding: 1px 5px; border-radius: 5px; font-size: 12.5px; }
.row { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
.mono { font-family: ui-monospace, Menlo, monospace; }
.pairecode { font-size: 34px; letter-spacing: 10px; font-weight: 700; text-align: center;
  padding: 14px 0 4px; color: #2f5cff; font-family: ui-monospace, Menlo, monospace; }
form.inline { display: inline; }
`;

export function layout(opts: {
  title: string;
  activeTab?: PortalTab;
  flash?: { kind: "ok" | "err"; text: string } | null;
  body: string;
  loggedIn?: boolean;
}): string {
  const tabs = opts.loggedIn === false
    ? ""
    : `<nav class="tabs">${TABS.map(
        (t) =>
          `<a href="${t.href}"${t.id === opts.activeTab ? ' class="active"' : ""}>${t.label}</a>`,
      ).join("")}</nav>`;
  const flash = opts.flash
    ? `<div class="flash ${opts.flash.kind}">${esc(opts.flash.text)}</div>`
    : "";
  return `<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>${esc(opts.title)} · AI Passport 配置中心</title>
<style>${CSS}</style>
</head>
<body>
<div class="wrap">
<h1>AI Passport 配置中心</h1>
<div class="sub">多合一挂坠 · 本地服务</div>
${tabs}
${flash}
${opts.body}
</div>
</body>
</html>`;
}

export function flashFromQuery(query: Record<string, unknown>): { kind: "ok" | "err"; text: string } | null {
  const msg = typeof query.msg === "string" ? query.msg : "";
  const err = typeof query.err === "string" ? query.err : "";
  if (msg) return { kind: "ok", text: msg };
  if (err) return { kind: "err", text: err };
  return null;
}
