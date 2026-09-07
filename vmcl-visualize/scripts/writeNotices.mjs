import { execFileSync } from "node:child_process";
import { readFileSync, readdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = dirname(dirname(fileURLToPath(import.meta.url)));
const folders = execFileSync(
  "npm",
  ["ls", "--omit=dev", "--all", "--parseable"],
  {
    cwd: root,
    encoding: "utf8",
  },
)
  .trim()
  .split("\n")
  .filter((folder) => folder !== root);
const notices = [
  "VMCL Visualize — third-party notices",
  "ELK.js upstream source: https://github.com/kieler/elkjs",
];
for (const folder of [...new Set(folders)].sort()) {
  const metadata = JSON.parse(
    readFileSync(join(folder, "package.json"), "utf8"),
  );
  const licenses = readdirSync(folder).filter((name) =>
    /^(license|copying|notice)(\.|$)/i.test(name),
  );
  if (!licenses.length) throw new Error(`Missing license: ${metadata.name}`);
  notices.push(`\n${metadata.name} ${metadata.version}\n${"=".repeat(60)}`);
  for (const license of licenses)
    notices.push(
      readFileSync(join(folder, license), "utf8")
        .replace(/\r\n/g, "\n")
        .trimEnd(),
    );
}
writeFileSync(
  join(root, "public", "thirdPartyNotices.txt"),
  notices.join("\n\n").trimEnd() + "\n",
);
