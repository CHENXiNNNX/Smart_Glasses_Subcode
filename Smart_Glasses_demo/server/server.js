#!/usr/bin/env node
const WebSocket = require('ws');
const fs = require('fs');
const path = require('path');
const { OggOpusWriter } = require('./ogg-opus-writer.js');

const PORT = process.env.PORT || 8000;
const IMAGES_DIR = path.join(__dirname, 'data', 'images');
const AUDIO_DIR = path.join(__dirname, 'data', 'audio');
const MSG_TYPE_JPEG = 0x01;
const MSG_TYPE_OPUS = 0x02;

function ensureDirs() {
  [IMAGES_DIR, AUDIO_DIR].forEach((dir) => {
    if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
  });
}

function parseMessage(data) {
  if (!Buffer.isBuffer(data) || data.length < 5) return null;
  const length = data.readUInt32BE(1);
  if (data.length < 5 + length) return null;
  return { type: data[0], payload: data.subarray(5, 5 + length) };
}

const wss = new WebSocket.Server({ port: PORT });

wss.on('listening', () => {
  ensureDirs();
  console.log(`ws://0.0.0.0:${PORT}`);
});

wss.on('connection', (ws, req) => {
  const sessionId = Date.now().toString(36) + Math.random().toString(36).slice(2, 6);
  let opusWriter = null;

  ws.on('message', (data) => {
    if (!Buffer.isBuffer(data)) return;
    const msg = parseMessage(data);
    if (!msg) return;

    if (msg.type === MSG_TYPE_JPEG) {
      const filepath = path.join(IMAGES_DIR, `capture_${sessionId}_${Date.now()}.jpg`);
      fs.writeFile(filepath, msg.payload, () => {});
    } else if (msg.type === MSG_TYPE_OPUS) {
      if (!opusWriter) {
        opusWriter = new OggOpusWriter(path.join(AUDIO_DIR, `audio_${sessionId}.ogg`));
        opusWriter.start();
      }
      opusWriter.writeOpusFrame(msg.payload);
    }
  });

  ws.on('close', () => {
    if (opusWriter) opusWriter.finish();
  });

  ws.on('error', (err) => {
    if (opusWriter) opusWriter.finish();
  });
});

wss.on('error', (err) => {
  console.error(err.message);
  process.exit(1);
});
