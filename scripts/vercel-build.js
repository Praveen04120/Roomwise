#!/usr/bin/env node
/**
 * Roomwise Production Build Script (Vercel & Cross-Platform)
 * 1. Copies static frontend assets + sample CSV into public/
 * 2. Generates public/config.js from VITE_API_URL (if configured)
 * 3. Compiles the native C++17 scheduling engine (backend/cli_handler.cpp + storage.cpp + scheduler.cpp)
 *    into api/bin/roomwise_cli for serverless execution.
 */

const fs = require('fs');
const path = require('path');
const { execFileSync, spawnSync } = require('child_process');

const rootDir = path.resolve(__dirname, '..');
const frontendDir = path.join(rootDir, 'frontend');
const samplesDir = path.join(rootDir, 'samples');
const publicDir = path.join(rootDir, 'public');
const apiBinDir = path.join(rootDir, 'api', 'bin');

function ensureDir(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function copyDirFiles(srcDir, dstDir) {
  ensureDir(dstDir);
  for (const entry of fs.readdirSync(srcDir, { withFileTypes: true })) {
    const src = path.join(srcDir, entry.name);
    const dst = path.join(dstDir, entry.name);
    if (entry.isDirectory()) {
      copyDirFiles(src, dst);
    } else {
      fs.copyFileSync(src, dst);
    }
  }
}

console.log('[Roomwise Build] Preparing public frontend assets...');
fs.rmSync(publicDir, { recursive: true, force: true });
copyDirFiles(frontendDir, publicDir);

const sampleCsvSrc = path.join(samplesDir, 'sample_timetable.csv');
if (fs.existsSync(sampleCsvSrc)) {
  fs.copyFileSync(sampleCsvSrc, path.join(publicDir, 'sample_timetable.csv'));
}

// Generate runtime config.js from VITE_API_URL (empty string => same-origin Vercel C++ API)
const apiUrl = (process.env.VITE_API_URL || '').trim().replace(/\/+$/, '');
const configContent = `// Generated at build time by scripts/vercel-build.js\nwindow.ROOMWISE_API_URL = ${JSON.stringify(apiUrl)};\n`;
fs.writeFileSync(path.join(publicDir, 'config.js'), configContent, 'utf8');
console.log(`[Roomwise Build] Generated public/config.js (API URL: ${apiUrl || '<same-origin>'})`);

// Compile native C++17 engine on Linux/macOS (e.g., Vercel build container)
if (process.platform !== 'win32') {
  ensureDir(apiBinDir);
  const outBin = path.join(apiBinDir, 'roomwise_cli');
  const sources = [
    path.join(rootDir, 'backend', 'cli_handler.cpp'),
    path.join(rootDir, 'backend', 'storage.cpp'),
    path.join(rootDir, 'backend', 'scheduler.cpp')
  ];
  const args = [
    '-std=c++17',
    '-O2',
    `-I${path.join(rootDir, 'include')}`,
    `-I${path.join(rootDir, 'backend')}`,
    ...sources,
    '-o',
    outBin,
    '-pthread'
  ];

  let compiler = null;
  for (const cand of ['g++', 'clang++', 'c++']) {
    const check = spawnSync(cand, ['--version'], { stdio: 'pipe' });
    if (check.status === 0) {
      compiler = cand;
      break;
    }
  }

  if (!compiler) {
    console.error('[Roomwise Build] ERROR: No C++17 compiler (g++/clang++) found in build environment.');
    process.exit(1);
  }

  console.log(`[Roomwise Build] Compiling C++17 scheduling engine with ${compiler}...`);
  execFileSync(compiler, args, { stdio: 'inherit' });
  fs.chmodSync(outBin, 0o755);
  console.log(`[Roomwise Build] Compiled native C++17 binary: ${outBin}`);
} else {
  console.log('[Roomwise Build] Windows host detected; skipping Linux binary step (use build.bat for Windows binaries).');
}

console.log('[Roomwise Build] Done.');
