/**
 * Roomwise Serverless API Bridge (Vercel)
 *
 * Thin transport + persistent cloud storage adapter that delegates 100% of
 * scheduling, validation, authentication, and CRUD execution to the compiled
 * C++17 Roomwise engine binary (backend/cli_handler.cpp + storage.cpp + scheduler.cpp).
 *
 * Persistent storage: Vercel Blob (private store, single atomic state snapshot)
 * Fallback (local dev without BLOB_READ_WRITE_TOKEN): local data/ directory
 */

const fs = require('fs');
const os = require('os');
const path = require('path');
const crypto = require('crypto');
const { execFileSync } = require('child_process');
const { get, put } = require('@vercel/blob');

const BLOB_PATHNAME = 'roomwise/state.json';
const DATA_FILES = [
  'floors.json',
  'rooms.json',
  'timetable.json',
  'settings.json',
  'active_sessions.json'
];

const ROOT_DIR = path.resolve(__dirname, '..');
const SEED_DATA_DIR = path.join(ROOT_DIR, 'data');

function resolveCppBinary() {
  const candidates = [
    path.join(__dirname, 'bin', 'roomwise_cli'),
    path.join(ROOT_DIR, 'api', 'bin', 'roomwise_cli'),
    path.join(ROOT_DIR, 'roomwise_cli.exe'),
    path.join(ROOT_DIR, 'roomwise_cli')
  ];
  for (const cand of candidates) {
    if (fs.existsSync(cand)) return cand;
  }
  return null;
}

function setCorsHeaders(res) {
  const allowedOrigin = process.env.ALLOWED_ORIGINS || '*';
  res.setHeader('Access-Control-Allow-Origin', allowedOrigin);
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type, Authorization, X-Admin-Token');
  res.setHeader('Cache-Control', 'no-store');
}

async function readBlobState() {
  if (!process.env.BLOB_READ_WRITE_TOKEN) {
    return null;
  }
  try {
    const result = await get(BLOB_PATHNAME, {
      access: 'private',
      useCache: false
    });
    if (!result || result.statusCode !== 200 || !result.stream) {
      return null;
    }
    const text = await new Response(result.stream).text();
    return JSON.parse(text);
  } catch (err) {
    if (err && (err.name === 'BlobNotFoundError' || /not found/i.test(err.message || ''))) {
      return null;
    }
    throw err;
  }
}

async function writeBlobState(dir) {
  if (!process.env.BLOB_READ_WRITE_TOKEN) {
    return;
  }
  const bundle = {};
  for (const fname of DATA_FILES) {
    const fpath = path.join(dir, fname);
    if (fs.existsSync(fpath)) {
      bundle[fname] = fs.readFileSync(fpath, 'utf8');
    }
  }
  await put(BLOB_PATHNAME, JSON.stringify(bundle), {
    access: 'private',
    addRandomSuffix: false,
    allowOverwrite: true,
    contentType: 'application/json'
  });
}

function hydrateWorkingDir(workDir, blobState) {
  fs.mkdirSync(workDir, { recursive: true });
  for (const fname of DATA_FILES) {
    const dst = path.join(workDir, fname);
    if (blobState && typeof blobState[fname] === 'string') {
      fs.writeFileSync(dst, blobState[fname], 'utf8');
    } else {
      const seedPath = path.join(SEED_DATA_DIR, fname);
      if (fs.existsSync(seedPath)) {
        fs.copyFileSync(seedPath, dst);
      }
    }
  }
}

async function readRawBody(req) {
  if (typeof req.body === 'string') return req.body;
  if (Buffer.isBuffer(req.body)) return req.body.toString('utf8');
  if (req.body && typeof req.body === 'object') return JSON.stringify(req.body);

  return await new Promise((resolve, reject) => {
    const chunks = [];
    let total = 0;
    const MAX_BYTES = 5 * 1024 * 1024;
    req.on('data', (chunk) => {
      total += chunk.length;
      if (total > MAX_BYTES) {
        reject(new Error('Payload too large'));
        return;
      }
      chunks.push(chunk);
    });
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
    req.on('error', reject);
  });
}

module.exports = async function handler(req, res) {
  setCorsHeaders(res);

  if (req.method === 'OPTIONS') {
    res.status(204).end();
    return;
  }

  const cppBin = resolveCppBinary();
  if (!cppBin) {
    res.status(500).json({
      error: 'C++17 scheduling engine binary (roomwise_cli) not found.'
    });
    return;
  }

  const useBlob = Boolean(process.env.BLOB_READ_WRITE_TOKEN);
  let workDir = SEED_DATA_DIR;
  let tempDirCreated = false;

  try {
    if (useBlob) {
      workDir = path.join(os.tmpdir(), `rw_${Date.now()}_${crypto.randomBytes(6).toString('hex')}`);
      tempDirCreated = true;
      const blobState = await readBlobState();
      hydrateWorkingDir(workDir, blobState);
    }

    const parsedUrl = new URL(req.url, 'http://localhost');
    const queryObj = {};
    for (const [k, v] of parsedUrl.searchParams.entries()) {
      queryObj[k] = v;
    }

    const rawBody = (req.method === 'POST' || req.method === 'PUT' || req.method === 'PATCH')
      ? await readRawBody(req)
      : '';

    const envelope = {
      method: req.method || 'GET',
      path: parsedUrl.pathname,
      query: queryObj,
      headers: req.headers || {},
      body: rawBody
    };

    const stdout = execFileSync(cppBin, ['--data', workDir], {
      input: JSON.stringify(envelope),
      encoding: 'utf8',
      maxBuffer: 10 * 1024 * 1024,
      env: process.env
    });

    const engineResp = JSON.parse(stdout.trim());

    if (useBlob && engineResp.mutated) {
      await writeBlobState(workDir);
    }

    res.status(engineResp.status || 200);
    res.setHeader('Content-Type', engineResp.content_type || 'application/json');
    res.send(JSON.stringify(engineResp.body));
  } catch (err) {
    console.error('[Roomwise API Bridge Error]', err);
    res.status(500).json({
      error: 'Internal server error invoking C++ scheduling engine.'
    });
  } finally {
    if (tempDirCreated) {
      fs.rmSync(workDir, { recursive: true, force: true });
    }
  }
};
