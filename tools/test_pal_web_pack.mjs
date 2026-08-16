#!/usr/bin/env node

import assert from "node:assert/strict";
import { createHash, webcrypto } from "node:crypto";
import { createRequire } from "node:module";
import { readFile, readdir } from "node:fs/promises";
import path from "node:path";

import {
  FONT10_ARCHIVE_FILENAME,
  browserDeflateRaw,
  browserInflateRaw,
  buildCardputerPackSet,
  buildXiaomiaoFullPack,
  crc32,
  makeDeflatedZipEntries,
  validatePalDosStructure,
  validateFont10Archive,
} from "./pal_web_pack_core.mjs";

if (!globalThis.crypto) {
  globalThis.crypto = webcrypto;
}

class NodePalDirectory {
  static async open(root) {
    const entries = await readdir(root, { withFileTypes: true });
    const files = new Map();
    for (const entry of entries) {
      if (!entry.isFile()) {
        continue;
      }
      const key = entry.name.toUpperCase();
      if (files.has(key)) {
        throw new Error(`case-insensitive duplicate input file: ${entry.name}`);
      }
      files.set(key, path.join(root, entry.name));
    }
    return new NodePalDirectory(files);
  }

  constructor(files) {
    this.files = files;
    this.cache = new Map();
  }

  has(name) {
    return this.files.has(name.toUpperCase());
  }

  async read(name) {
    const key = name.toUpperCase();
    const filename = this.files.get(key);
    if (!filename) {
      throw new Error(`missing PAL input file: ${name}`);
    }
    if (!this.cache.has(key)) {
      this.cache.set(key, readFile(filename).then((buffer) => new Uint8Array(
        buffer.buffer,
        buffer.byteOffset,
        buffer.byteLength,
      )));
    }
    return this.cache.get(key);
  }
}

function firstDifference(left, right) {
  const length = Math.min(left.length, right.length);
  for (let index = 0; index < length; index += 1) {
    if (left[index] !== right[index]) {
      return index;
    }
  }
  return left.length === right.length ? -1 : length;
}

const repository = path.resolve(path.dirname(new URL(import.meta.url).pathname), "..");
const pakoPath = path.join(repository, "docs/pako.min.js");
const pakoBytes = await readFile(pakoPath);
assert.equal(
  createHash("sha256").update(pakoBytes).digest("hex"),
  "ede2693a4a6a5126b9d35669062b358ecab6ae7b9b86a1cf302feb45a8514907",
  "vendored pako 2.1.0 digest differs",
);
const require = createRequire(import.meta.url);
globalThis.pako = require(pakoPath);
assert.equal(typeof globalThis.pako.inflateRaw, "function");
assert.equal(typeof globalThis.pako.deflateRaw, "function");

const dataDirectory = path.resolve(
  process.argv[2] ?? "/mnt/hgfs/deb13/PALSteam/PAL_DOS",
);
const expectedPackPath = path.resolve(
  process.argv[3] ?? path.join(repository, "esp32s3/TF_datapak/pal_full.pak"),
);
const font10ArchivePath = path.resolve(
  process.argv[4]
    ?? path.join(repository, "docs", FONT10_ARCHIVE_FILENAME),
);
const cardputerPlanPath = path.resolve(
  process.argv[5] ?? path.join(repository, "tools/pal_web_cardputer_plan.json"),
);
const cardputerPlan = JSON.parse(await readFile(cardputerPlanPath, "utf8"));

const source = await NodePalDirectory.open(dataDirectory);
const font10Archive = await readFile(font10ArchivePath);
await assert.rejects(
  validatePalDosStructure({
    source: {
      has: (name) => source.has(name),
      read: async (name) => {
        const bytes = await source.read(name);
        if (name.toUpperCase() !== "ABC.MKF") {
          return bytes;
        }
        const damaged = new Uint8Array(bytes);
        const view = new DataView(damaged.buffer, damaged.byteOffset, damaged.byteLength);
        const chunkCount = view.getUint32(0, true) / 4 - 1;
        for (let chunkId = 0; chunkId < chunkCount; chunkId += 1) {
          const start = view.getUint32(chunkId * 4, true);
          const end = view.getUint32((chunkId + 1) * 4, true);
          if (end > start) {
            damaged[start] ^= 0xff;
            break;
          }
        }
        return damaged;
      },
    },
    plan: cardputerPlan,
  }),
  /ABC\.MKF#\d+ 不是 DOS 版 YJ1/,
);
await validatePalDosStructure({
  source: {
    has: (name) => source.has(name),
    read: async (name) => {
      const bytes = await source.read(name);
      if (name.toUpperCase() !== "GOP.MKF") {
        return bytes;
      }
      const structurallyCompatible = new Uint8Array(bytes);
      structurallyCompatible[structurallyCompatible.length - 1] ^= 1;
      return structurallyCompatible;
    },
  },
  plan: cardputerPlan,
});
await validateFont10Archive(font10Archive, browserInflateRaw);
const result = await buildXiaomiaoFullPack({
  source,
  font10Archive,
  inflateRaw: browserInflateRaw,
  onProgress: async (_phase, current, total, message) => {
    process.stdout.write(`[${current}/${total}] ${message}\n`);
  },
});

const expected = await readFile(expectedPackPath);
const difference = firstDifference(result.pack, expected);
assert.equal(
  difference,
  -1,
  difference < 0
    ? ""
    : `web pack differs at 0x${difference.toString(16)}: `
      + `web=${result.pack[difference]} python=${expected[difference]}`,
);

const expectedPackId = expected.readUInt32LE(20);
assert.equal(result.packSetId, expectedPackId);
const cardputer = await buildCardputerPackSet({
  archives: result.archives,
  fullPack: result.pack,
  fullPackSha256: result.packSha256,
  packSetId: result.packSetId,
  plan: cardputerPlan,
});
assert.equal(cardputer.files.length, 19);
const expectedPackDirectory = path.dirname(expectedPackPath);
for (const file of cardputer.files) {
  if (file.name === "chapter_manifest.json") {
    const manifest = JSON.parse(new TextDecoder().decode(file.data));
    assert.equal(manifest.builder.kind, "browser");
    assert.equal(manifest.pack_set.id, result.packSetId);
    continue;
  }
  const expectedFile = await readFile(path.join(expectedPackDirectory, file.name));
  assert.equal(
    firstDifference(file.data, expectedFile),
    -1,
    `${file.name} differs from the Python Cardputer pack builder`,
  );
}

const zip = await makeDeflatedZipEntries(cardputer.files, browserDeflateRaw);
const rawOutputBytes = cardputer.files.reduce((total, file) => total + file.data.length, 0);
assert.ok(zip.size < rawOutputBytes);
const zipBytes = new Uint8Array(await zip.arrayBuffer());
const zipView = new DataView(zipBytes.buffer, zipBytes.byteOffset, zipBytes.byteLength);
let zipOffset = 0;
const localOffsets = [];
for (const file of cardputer.files) {
  localOffsets.push(zipOffset);
  assert.equal(zipView.getUint32(zipOffset, true), 0x04034b50);
  assert.equal(zipView.getUint16(zipOffset + 8, true), 8);
  assert.equal(zipView.getUint32(zipOffset + 14, true), crc32(file.data));
  const compressedBytes = zipView.getUint32(zipOffset + 18, true);
  assert.equal(zipView.getUint32(zipOffset + 22, true), file.data.length);
  const nameBytes = zipView.getUint16(zipOffset + 26, true);
  const nameStart = zipOffset + 30;
  const payloadStart = nameStart + nameBytes;
  const filename = new TextDecoder().decode(zipBytes.subarray(nameStart, payloadStart));
  assert.equal(filename, file.name);
  const member = await browserInflateRaw(
    zipBytes.subarray(payloadStart, payloadStart + compressedBytes),
  );
  assert.equal(firstDifference(member, file.data), -1);
  zipOffset = payloadStart + compressedBytes;
}
const centralOffset = zipOffset;
for (let fileIndex = 0; fileIndex < cardputer.files.length; fileIndex += 1) {
  const file = cardputer.files[fileIndex];
  assert.equal(zipView.getUint32(zipOffset, true), 0x02014b50);
  assert.equal(zipView.getUint16(zipOffset + 10, true), 8);
  assert.equal(zipView.getUint32(zipOffset + 16, true), crc32(file.data));
  assert.equal(zipView.getUint32(zipOffset + 42, true), localOffsets[fileIndex]);
  const nameBytes = zipView.getUint16(zipOffset + 28, true);
  const extraBytes = zipView.getUint16(zipOffset + 30, true);
  const commentBytes = zipView.getUint16(zipOffset + 32, true);
  const filename = new TextDecoder().decode(
    zipBytes.subarray(zipOffset + 46, zipOffset + 46 + nameBytes),
  );
  assert.equal(filename, file.name);
  zipOffset += 46 + nameBytes + extraBytes + commentBytes;
}
const centralBytes = zipOffset - centralOffset;
assert.equal(zipView.getUint32(zipOffset, true), 0x06054b50);
assert.equal(zipView.getUint16(zipOffset + 8, true), cardputer.files.length);
assert.equal(zipView.getUint16(zipOffset + 10, true), cardputer.files.length);
assert.equal(zipView.getUint32(zipOffset + 12, true), centralBytes);
assert.equal(zipView.getUint32(zipOffset + 16, true), centralOffset);
assert.equal(zipOffset + 22, zipBytes.length);

process.stdout.write(
  `pal_web_pack: exact full/Cardputer binaries; files=${cardputer.files.length} `
  + `full_bytes=${result.pack.length} `
  + `pack_set_id=0x${result.packSetId.toString(16).padStart(8, "0")} `
  + `sha256=${result.packSha256} zip_bytes=${zip.size}\n`,
);
