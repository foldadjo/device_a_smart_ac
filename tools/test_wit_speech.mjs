import { execFileSync } from "node:child_process";
import { readFileSync, rmSync } from "node:fs";
import { request } from "node:https";
import { tmpdir } from "node:os";
import { join } from "node:path";

const secrets = readFileSync("include/WitAiSecrets.h", "utf8");
const token = secrets.match(/WITAI_SERVER_TOKEN\s+"([^"]+)"/)?.[1];
if (!token || token.includes("YOUR_")) throw new Error("Wit.ai token belum dikonfigurasi");

const caf = join(tmpdir(), "device-a-wit-test.caf");
const raw = join(tmpdir(), "device-a-wit-test.raw");
const phrase = process.argv.slice(2).join(" ") || "Halo Stroomer, berapa tegangan";
execFileSync("say", ["-v", "Damayanti", "-r", "170", "--data-format=LEI16@16000", "-o", caf, phrase]);
execFileSync("ffmpeg", ["-loglevel", "error", "-y", "-i", caf, "-ac", "1", "-ar", "8000", "-f", "s16le", raw]);
const audio = readFileSync(raw);

const req = request({
  method: "POST",
  hostname: "api.wit.ai",
  path: "/speech?v=20240919",
  headers: {
    Authorization: `Bearer ${token}`,
    "Content-Type": "audio/raw;encoding=signed-integer;bits=16;rate=8000;endian=little",
    "Content-Length": audio.length
  }
}, res => {
  let body = "";
  res.on("data", chunk => body += chunk);
  res.on("end", () => {
    rmSync(caf, { force: true });
    rmSync(raw, { force: true });
    const matches = [...body.matchAll(/"text"\s*:\s*"([^"]*)"/g)];
    const text = matches.at(-1)?.[1] ?? "";
    const variants = [...new Set(matches.map(match => match[1]).filter(Boolean))];
    console.log(`HTTP ${res.statusCode}; transcript: ${text || "(kosong)"}; variants: ${variants.join(" | ") || "(kosong)"}`);
    if (!text) console.log(body.slice(0, 2000));
    if (res.statusCode < 200 || res.statusCode >= 300 || !text) process.exitCode = 1;
  });
});
req.on("error", error => { console.error(error.message); process.exitCode = 1; });
req.end(audio);
