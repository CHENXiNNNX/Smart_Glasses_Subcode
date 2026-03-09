const fs = require('fs');

const SAMPLE_RATE = 48000;
const CHANNELS = 1;
const PRESKIP = 0;

function buildOggCrcTable() {
  const table = new Uint32Array(256);
  const poly = 0x04c11db7;
  for (let i = 0; i < 256; i++) {
    let r = (i << 24) >>> 0;
    for (let k = 0; k < 8; k++) {
      r = (r & 0x80000000) ? ((r << 1) ^ poly) >>> 0 : (r << 1) >>> 0;
    }
    table[i] = r;
  }
  return table;
}

const OGG_CRC_TABLE = buildOggCrcTable();

function oggCrc32(buffer) {
  let crc = 0;
  for (let i = 0; i < buffer.length; i++) {
    crc = (OGG_CRC_TABLE[((crc >>> 24) ^ buffer[i]) & 0xff] ^ (crc << 8)) >>> 0;
  }
  return crc;
}

function writeU32LE(buf, offset, value) {
  buf[offset] = value & 0xff;
  buf[offset + 1] = (value >> 8) & 0xff;
  buf[offset + 2] = (value >> 16) & 0xff;
  buf[offset + 3] = (value >> 24) & 0xff;
}

function writeU64LE(buf, offset, value) {
  writeU32LE(buf, offset, value >>> 0);
  writeU32LE(buf, offset + 4, Math.floor(value / 0x100000000) >>> 0);
}

class OggOpusWriter {
  constructor(filePath, sampleRate = SAMPLE_RATE, channels = CHANNELS) {
    this.filePath = filePath;
    this.sampleRate = sampleRate;
    this.channels = channels;
    this.serial = Math.floor(Math.random() * 0x7fffffff);
    this.sequence = 0;
    this.granule = 0;
    this.fd = null;
  }

  _writeOggPage(headerType, packets) {
    const segmentTable = [];
    let totalSize = 0;
    for (const pkt of packets) {
      let len = pkt.length;
      while (len > 0) {
        const seg = Math.min(len, 255);
        segmentTable.push(seg);
        totalSize += seg;
        len -= seg;
      }
    }

    const page = Buffer.alloc(27 + segmentTable.length + totalSize);
    let off = 0;
    page.write('OggS', off); off += 4;
    page[off++] = 0;
    page[off++] = headerType;
    writeU64LE(page, off, this.granule); off += 8;
    writeU32LE(page, off, this.serial); off += 4;
    writeU32LE(page, off, this.sequence++); off += 4;
    off += 4;
    page[off++] = segmentTable.length;
    for (const s of segmentTable) page[off++] = s;
    for (const pkt of packets) pkt.copy(page, off), off += pkt.length;

    writeU32LE(page, 22, oggCrc32(page));
    fs.writeSync(this.fd, page);
  }

  start() {
    this.fd = fs.openSync(this.filePath, 'w');
    const opusHead = Buffer.alloc(19);
    opusHead.write('OpusHead', 0);
    opusHead[8] = 1;
    opusHead[9] = this.channels;
    opusHead[10] = PRESKIP & 0xff;
    opusHead[11] = (PRESKIP >> 8) & 0xff;
    writeU32LE(opusHead, 12, this.sampleRate);
    opusHead[16] = opusHead[17] = opusHead[18] = 0;
    this._writeOggPage(2, [opusHead]);

    const vendor = Buffer.from('SmartGlasses', 'utf8');
    const opusTags = Buffer.alloc(16 + vendor.length);
    opusTags.write('OpusTags', 0);
    writeU32LE(opusTags, 8, vendor.length);
    vendor.copy(opusTags, 12);
    writeU32LE(opusTags, 12 + vendor.length, 0);
    this._writeOggPage(0, [opusTags]);
  }

  writeOpusFrame(frame) {
    if (!this.fd) return;
    this.granule += 960 * this.channels;
    this._writeOggPage(0, [frame]);
  }

  finish() {
    if (this.fd) fs.closeSync(this.fd), (this.fd = null);
  }
}

module.exports = { OggOpusWriter, SAMPLE_RATE, CHANNELS };
