#!/usr/bin/env node

import { readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const toolsDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryDirectory = path.dirname(toolsDirectory);
const corePath = path.join(toolsDirectory, "pal_web_pack_core.mjs");
const cardputerPlanPath = path.join(toolsDirectory, "pal_web_cardputer_plan.json");
const templatePath = path.join(toolsDirectory, "xiaomiao_resource_builder.html.in");
const outputPath = path.join(repositoryDirectory, "docs", "index.html");
const coreMarker = "/*__PAL_WEB_PACK_CORE__*/";
const cardputerPlanMarker = "/*__PAL_WEB_CARDPUTER_PLAN__*/";

const [core, cardputerPlanText, template] = await Promise.all([
  readFile(corePath, "utf8"),
  readFile(cardputerPlanPath, "utf8"),
  readFile(templatePath, "utf8"),
]);
for (const marker of [coreMarker, cardputerPlanMarker]) {
  const firstMarker = template.indexOf(marker);
  if (firstMarker < 0 || template.indexOf(marker, firstMarker + marker.length) >= 0) {
    throw new Error(`HTML template must contain exactly one ${marker}`);
  }
}
const cardputerPlan = JSON.parse(cardputerPlanText);
const generated = template
  .replace(coreMarker, core.trim())
  .replace(cardputerPlanMarker, JSON.stringify(cardputerPlan));

if (process.argv.includes("--check")) {
  const current = await readFile(outputPath, "utf8").catch(() => "");
  if (current !== generated) {
    throw new Error("docs/index.html is stale; run its build script");
  }
  process.stdout.write("docs/index.html: generated file is current\n");
} else {
  await writeFile(outputPath, generated);
  process.stdout.write(`${outputPath}: generated\n`);
}
