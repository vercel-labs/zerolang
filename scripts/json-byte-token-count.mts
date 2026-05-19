export function jsonByteTokenCount(bytes) {
  if (!bytes || bytes.length === 0) return -1n;
  const scanner = { bytes, pos: 0, tokens: 0 };
  const len = () => scanner.bytes.length;
  const skipWs = () => {
    while (scanner.pos < len()) {
      const ch = scanner.bytes[scanner.pos];
      if (ch !== 0x20 && ch !== 0x0a && ch !== 0x0d && ch !== 0x09) return;
      scanner.pos += 1;
    }
  };
  const parseString = () => {
    if (scanner.pos >= len() || scanner.bytes[scanner.pos] !== 0x22) return false;
    scanner.pos += 1;
    while (scanner.pos < len()) {
      const ch = scanner.bytes[scanner.pos++];
      if (ch === 0x22) return true;
      if (ch < 0x20) return false;
      if (ch !== 0x5c) continue;
      if (scanner.pos >= len()) return false;
      const esc = scanner.bytes[scanner.pos++];
      if (esc === 0x22 || esc === 0x5c || esc === 0x2f || esc === 0x62 || esc === 0x66 || esc === 0x6e || esc === 0x72 || esc === 0x74) continue;
      if (esc !== 0x75 || scanner.pos + 4 > len()) return false;
      for (let i = 0; i < 4; i++) {
        const hex = scanner.bytes[scanner.pos++];
        const ok = (hex >= 0x30 && hex <= 0x39) || (hex >= 0x61 && hex <= 0x66) || (hex >= 0x41 && hex <= 0x46);
        if (!ok) return false;
      }
    }
    return false;
  };
  const matchLiteral = (literal) => {
    const start = scanner.pos;
    for (let i = 0; i < literal.length; i++) {
      if (scanner.pos >= len() || scanner.bytes[scanner.pos] !== literal.charCodeAt(i)) {
        scanner.pos = start;
        return false;
      }
      scanner.pos += 1;
    }
    return true;
  };
  const parseNumber = () => {
    const start = scanner.pos;
    if (scanner.pos < len() && scanner.bytes[scanner.pos] === 0x2d) scanner.pos += 1;
    if (scanner.pos >= len()) {
      scanner.pos = start;
      return false;
    }
    if (scanner.bytes[scanner.pos] === 0x30) {
      scanner.pos += 1;
    } else if (scanner.bytes[scanner.pos] >= 0x31 && scanner.bytes[scanner.pos] <= 0x39) {
      while (scanner.pos < len() && scanner.bytes[scanner.pos] >= 0x30 && scanner.bytes[scanner.pos] <= 0x39) scanner.pos += 1;
    } else {
      scanner.pos = start;
      return false;
    }
    if (scanner.pos < len() && scanner.bytes[scanner.pos] === 0x2e) {
      scanner.pos += 1;
      const digits = scanner.pos;
      while (scanner.pos < len() && scanner.bytes[scanner.pos] >= 0x30 && scanner.bytes[scanner.pos] <= 0x39) scanner.pos += 1;
      if (scanner.pos === digits) {
        scanner.pos = start;
        return false;
      }
    }
    if (scanner.pos < len() && (scanner.bytes[scanner.pos] === 0x65 || scanner.bytes[scanner.pos] === 0x45)) {
      scanner.pos += 1;
      if (scanner.pos < len() && (scanner.bytes[scanner.pos] === 0x2b || scanner.bytes[scanner.pos] === 0x2d)) scanner.pos += 1;
      const digits = scanner.pos;
      while (scanner.pos < len() && scanner.bytes[scanner.pos] >= 0x30 && scanner.bytes[scanner.pos] <= 0x39) scanner.pos += 1;
      if (scanner.pos === digits) {
        scanner.pos = start;
        return false;
      }
    }
    return true;
  };
  const parseValue = (depth) => {
    if (depth > 64) return false;
    skipWs();
    if (scanner.pos >= len()) return false;
    const ch = scanner.bytes[scanner.pos];
    if (ch === 0x7b) return parseObject(depth);
    if (ch === 0x5b) return parseArray(depth);
    if (ch === 0x22) {
      scanner.tokens += 1;
      return parseString();
    }
    if (ch === 0x74) {
      scanner.tokens += 1;
      return matchLiteral("true");
    }
    if (ch === 0x66) {
      scanner.tokens += 1;
      return matchLiteral("false");
    }
    if (ch === 0x6e) {
      scanner.tokens += 1;
      return matchLiteral("null");
    }
    if (ch === 0x2d || (ch >= 0x30 && ch <= 0x39)) {
      scanner.tokens += 1;
      return parseNumber();
    }
    return false;
  };
  const parseArray = (depth) => {
    if (scanner.pos >= len() || scanner.bytes[scanner.pos] !== 0x5b) return false;
    scanner.tokens += 1;
    scanner.pos += 1;
    skipWs();
    if (scanner.pos < len() && scanner.bytes[scanner.pos] === 0x5d) {
      scanner.pos += 1;
      return true;
    }
    for (;;) {
      if (!parseValue(depth + 1)) return false;
      skipWs();
      if (scanner.pos < len() && scanner.bytes[scanner.pos] === 0x5d) {
        scanner.pos += 1;
        return true;
      }
      if (scanner.pos >= len() || scanner.bytes[scanner.pos] !== 0x2c) return false;
      scanner.pos += 1;
      skipWs();
    }
  };
  const parseObject = (depth) => {
    if (scanner.pos >= len() || scanner.bytes[scanner.pos] !== 0x7b) return false;
    scanner.tokens += 1;
    scanner.pos += 1;
    skipWs();
    if (scanner.pos < len() && scanner.bytes[scanner.pos] === 0x7d) {
      scanner.pos += 1;
      return true;
    }
    for (;;) {
      if (!parseString()) return false;
      scanner.tokens += 1;
      skipWs();
      if (scanner.pos >= len() || scanner.bytes[scanner.pos] !== 0x3a) return false;
      scanner.pos += 1;
      if (!parseValue(depth + 1)) return false;
      skipWs();
      if (scanner.pos < len() && scanner.bytes[scanner.pos] === 0x7d) {
        scanner.pos += 1;
        return true;
      }
      if (scanner.pos >= len() || scanner.bytes[scanner.pos] !== 0x2c) return false;
      scanner.pos += 1;
      skipWs();
    }
  };
  if (!parseValue(0)) return -1n;
  skipWs();
  return scanner.pos === len() ? BigInt(scanner.tokens) : -1n;
}
