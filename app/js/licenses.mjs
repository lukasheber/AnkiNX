// The licence texts of the packages in the bundle, from esbuild's metafile:
// node licenses.mjs meta.json > runtime-licenses.txt
import { readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";

const meta = JSON.parse(readFileSync(process.argv[2], "utf8"));
const packages = new Set();
for (const input of Object.keys(meta.inputs)) {
    const m = input.match(/node_modules\/((?:@[^/]+\/)?[^/]+)\//);
    if (m) packages.add(m[1]);
}
const out = ["Third-party code in runtime.js (the card page runtime of AnkiNX), with its licences.", ""];
for (const name of [...packages].sort()) {
    const dir = join("node_modules", name);
    const info = JSON.parse(readFileSync(join(dir, "package.json"), "utf8"));
    const file = readdirSync(dir).find(f => /^(licen[cs]e|copying)(\.|$)/i.test(f));
    out.push("=".repeat(78), `${name} ${info.version} (${info.license})`, "=".repeat(78), "");
    out.push(file ? readFileSync(join(dir, file), "utf8").trim() : `Licence: ${info.license} (no licence file in the package)`, "");
}
process.stdout.write(out.join("\n") + "\n");
