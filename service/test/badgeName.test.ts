import { test } from "node:test";
import assert from "node:assert/strict";
import { validateBadgeName, validateBadgeRole } from "../src/portal/badgeName.js";

test("badge name accepts only names the generated device font and header can show", () => {
  assert.equal(validateBadgeName(" 你的名字 "), "你的名字");
  assert.equal(validateBadgeName("Ada_2026"), "Ada_2026");
  assert.equal(validateBadgeName(""), "");
  assert.equal(validateBadgeName("你的名字字"), null); // > 8 display cells
  assert.equal(validateBadgeName("未收录"), null);
  assert.equal(validateBadgeName("<script>"), null);
});

test("badge role fits the secondary line and excludes unsupported characters", () => {
  assert.equal(validateBadgeRole(" Developer "), "Developer");
  assert.equal(validateBadgeRole(""), "");
  assert.equal(validateBadgeRole("Developer!!"), null);
  assert.equal(validateBadgeRole("开发者"), null);
});
