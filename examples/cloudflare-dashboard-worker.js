// Rendering is synchronous, so these dimensions can be switched safely for
// one card and restored by the next render without request interleaving.
let WIDTH = 528;
let HEIGHT = 792;
let PIXEL_ROW_BYTES = Math.ceil(WIDTH / 8);

function setCanvasDimensions(device, orientation) {
  const landscape = orientation === "landscape";
  const portraitWidth = device === "x4" ? 480 : 528;
  const portraitHeight = device === "x4" ? 800 : 792;
  WIDTH = landscape ? portraitHeight : portraitWidth;
  HEIGHT = landscape ? portraitWidth : portraitHeight;
  PIXEL_ROW_BYTES = Math.ceil(WIDTH / 8);
}

function resolveDevice(url) {
  const requested = (url.searchParams.get("device") || "x3").trim().toLowerCase();
  if (requested === "x3" || requested === "x4") return requested;
  return null;
}

function resolveOrientation(url) {
  const requested = (url.searchParams.get("orientation") || "portrait").trim().toLowerCase();
  if (requested === "portrait" || requested === "p") return "portrait";
  if (requested === "landscape" || requested === "l") return "landscape";
  return null;
}

// Compact 5x7 bitmap font. Each value is one five-pixel-wide row.
const FONT = {
  " ": [0, 0, 0, 0, 0, 0, 0],
  "0": [14, 17, 19, 21, 25, 17, 14],
  "1": [4, 12, 4, 4, 4, 4, 14],
  "2": [14, 17, 1, 2, 4, 8, 31],
  "3": [30, 1, 1, 14, 1, 1, 30],
  "4": [2, 6, 10, 18, 31, 2, 2],
  "5": [31, 16, 16, 30, 1, 1, 30],
  "6": [14, 16, 16, 30, 17, 17, 14],
  "7": [31, 1, 2, 4, 8, 8, 8],
  "8": [14, 17, 17, 14, 17, 17, 14],
  "9": [14, 17, 17, 15, 1, 1, 14],
  "A": [14, 17, 17, 31, 17, 17, 17],
  "B": [30, 17, 17, 30, 17, 17, 30],
  "C": [14, 17, 16, 16, 16, 17, 14],
  "D": [30, 17, 17, 17, 17, 17, 30],
  "E": [31, 16, 16, 30, 16, 16, 31],
  "F": [31, 16, 16, 30, 16, 16, 16],
  "G": [14, 17, 16, 23, 17, 17, 15],
  "H": [17, 17, 17, 31, 17, 17, 17],
  "I": [14, 4, 4, 4, 4, 4, 14],
  "J": [7, 2, 2, 2, 2, 18, 12],
  "K": [17, 18, 20, 24, 20, 18, 17],
  "L": [16, 16, 16, 16, 16, 16, 31],
  "M": [17, 27, 21, 21, 17, 17, 17],
  "N": [17, 25, 21, 19, 17, 17, 17],
  "O": [14, 17, 17, 17, 17, 17, 14],
  "P": [30, 17, 17, 30, 16, 16, 16],
  "Q": [14, 17, 17, 17, 21, 18, 13],
  "R": [30, 17, 17, 30, 20, 18, 17],
  "S": [15, 16, 16, 14, 1, 1, 30],
  "T": [31, 4, 4, 4, 4, 4, 4],
  "U": [17, 17, 17, 17, 17, 17, 14],
  "V": [17, 17, 17, 17, 17, 10, 4],
  "W": [17, 17, 17, 21, 21, 21, 10],
  "X": [17, 17, 10, 4, 10, 17, 17],
  "Y": [17, 17, 10, 4, 4, 4, 4],
  "Z": [31, 1, 2, 4, 8, 16, 31],
  ":": [0, 4, 4, 0, 4, 4, 0],
  "-": [0, 0, 0, 31, 0, 0, 0],
  "/": [1, 2, 2, 4, 8, 8, 16],
  ".": [0, 0, 0, 0, 0, 12, 12],
  "%": [17, 2, 4, 8, 17, 0, 0],
  "'": [4, 4, 8, 0, 0, 0, 0],
  "&": [12, 18, 20, 8, 21, 18, 13],
  "+": [0, 4, 4, 31, 4, 4, 0],
  "=": [0, 0, 31, 0, 31, 0, 0],
  "?": [14, 17, 1, 2, 4, 0, 4],
};

function setPixel(canvas, x, y, black = true) {
  x = Math.round(x);
  y = Math.round(y);
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
  const index = y * PIXEL_ROW_BYTES + (x >> 3);
  const mask = 1 << (7 - (x & 7));
  if (black) canvas[index] |= mask;
  else canvas[index] &= ~mask;
}

function fillRect(canvas, x, y, w, h, black = true) {
  for (let py = y; py < y + h; py++) {
    for (let px = x; px < x + w; px++) setPixel(canvas, px, py, black);
  }
}

function drawLine(canvas, x0, y0, x1, y1, thickness = 1) {
  x0 = Math.round(x0);
  y0 = Math.round(y0);
  x1 = Math.round(x1);
  y1 = Math.round(y1);
  const dx = Math.abs(x1 - x0);
  const sx = x0 < x1 ? 1 : -1;
  const dy = -Math.abs(y1 - y0);
  const sy = y0 < y1 ? 1 : -1;
  let error = dx + dy;
  while (true) {
    fillRect(canvas, x0 - Math.floor(thickness / 2), y0 - Math.floor(thickness / 2), thickness, thickness);
    if (x0 === x1 && y0 === y1) break;
    const twice = 2 * error;
    if (twice >= dy) {
      error += dy;
      x0 += sx;
    }
    if (twice <= dx) {
      error += dx;
      y0 += sy;
    }
  }
}

function drawRect(canvas, x, y, w, h, thickness = 1) {
  fillRect(canvas, x, y, w, thickness);
  fillRect(canvas, x, y + h - thickness, w, thickness);
  fillRect(canvas, x, y, thickness, h);
  fillRect(canvas, x + w - thickness, y, thickness, h);
}

function fillCircle(canvas, cx, cy, radius, black = true) {
  for (let y = -radius; y <= radius; y++) {
    const halfWidth = Math.floor(Math.sqrt(radius * radius - y * y));
    for (let x = -halfWidth; x <= halfWidth; x++) setPixel(canvas, cx + x, cy + y, black);
  }
}

function drawCircle(canvas, cx, cy, radius, thickness = 1) {
  fillCircle(canvas, cx, cy, radius, true);
  if (radius > thickness) fillCircle(canvas, cx, cy, radius - thickness, false);
}

function cleanText(text) {
  return String(text || "")
    .replace(/[‘’]/g, "'")
    .replace(/[“”]/g, " ")
    .replace(/[–—]/g, "-")
    .normalize("NFD")
    .replace(/[\u0300-\u036f]/g, "")
    .toUpperCase()
    .replace(/[^A-Z0-9 :\-/.%'&]/g, " ")
    .replace(/\s+/g, " ")
    .trim();
}

function textWidth(text, scale) {
  return text.length ? text.length * 5 * scale + (text.length - 1) * scale : 0;
}

function drawChar(canvas, ch, x, y, scale) {
  const glyph = FONT[ch] || FONT["?"];
  for (let row = 0; row < 7; row++) {
    for (let col = 0; col < 5; col++) {
      if (!(glyph[row] & (1 << (4 - col)))) continue;
      fillRect(canvas, x + col * scale, y + row * scale, scale, scale);
    }
  }
}

function drawText(canvas, text, x, y, scale) {
  const clean = cleanText(text);
  let cursor = x;
  for (const ch of clean) {
    drawChar(canvas, ch, cursor, y, scale);
    cursor += 6 * scale;
  }
}

function drawTextCentered(canvas, text, y, scale, maxWidth = WIDTH - 24) {
  let clean = cleanText(text);
  while (scale > 1 && textWidth(clean, scale) > maxWidth) scale--;
  while (clean && textWidth(clean, scale) > maxWidth) clean = clean.slice(0, -1).trim();
  drawText(canvas, clean, Math.floor((WIDTH - textWidth(clean, scale)) / 2), y, scale);
}

function wrapText(text, scale, maxWidth, maxLines) {
  const words = cleanText(text).split(" ").filter(Boolean);
  const lines = [];
  let line = "";

  for (const word of words) {
    const candidate = line ? `${line} ${word}` : word;
    if (textWidth(candidate, scale) <= maxWidth) {
      line = candidate;
      continue;
    }
    if (line) lines.push(line);
    line = word;
    while (line && textWidth(line, scale) > maxWidth) line = line.slice(0, -1).trim();
    if (lines.length === maxLines) break;
  }
  if (line && lines.length < maxLines) lines.push(line);

  if (lines.length === maxLines) {
    const consumed = lines.join(" ").split(" ").length;
    if (consumed < words.length) {
      let last = lines[maxLines - 1];
      while (last && textWidth(`${last}.`, scale) > maxWidth) last = last.slice(0, -1).trim();
      lines[maxLines - 1] = `${last}.`;
    }
  }
  return lines;
}

function drawWrappedText(canvas, text, x, y, scale, maxWidth, maxLines, lineHeight = 9 * scale) {
  const lines = wrapText(text, scale, maxWidth, maxLines);
  lines.forEach((line, index) => drawText(canvas, line, x, y + index * lineHeight, scale));
  return lines.length;
}

function makeBmp(canvas) {
  const rowStride = (PIXEL_ROW_BYTES + 3) & ~3;
  const pixelBytes = rowStride * HEIGHT;
  const pixelOffset = 14 + 40 + 8;
  const fileSize = pixelOffset + pixelBytes;
  const out = new Uint8Array(fileSize);
  const view = new DataView(out.buffer);

  out[0] = 0x42;
  out[1] = 0x4d;
  view.setUint32(2, fileSize, true);
  view.setUint32(10, pixelOffset, true);
  view.setUint32(14, 40, true);
  view.setInt32(18, WIDTH, true);
  view.setInt32(22, HEIGHT, true);
  view.setUint16(26, 1, true);
  view.setUint16(28, 1, true);
  view.setUint32(30, 0, true);
  view.setUint32(34, pixelBytes, true);
  view.setInt32(38, 2835, true);
  view.setInt32(42, 2835, true);
  view.setUint32(46, 2, true);
  view.setUint32(50, 2, true);
  out.set([255, 255, 255, 0, 0, 0, 0, 0], 54);

  for (let y = 0; y < HEIGHT; y++) {
    const source = y * PIXEL_ROW_BYTES;
    const destination = pixelOffset + (HEIGHT - 1 - y) * rowStride;
    out.set(canvas.subarray(source, source + PIXEL_ROW_BYTES), destination);
  }
  return out;
}

function getClockParts(now, timeZone) {
  let formatter;
  try {
    formatter = new Intl.DateTimeFormat("en-GB", {
      timeZone,
      weekday: "long",
      day: "2-digit",
      month: "short",
      year: "numeric",
      hour: "2-digit",
      minute: "2-digit",
      hourCycle: "h23",
      timeZoneName: "short",
    });
  } catch {
    return null;
  }

  const parts = {};
  for (const part of formatter.formatToParts(now)) {
    if (part.type !== "literal") parts[part.type] = part.value;
  }

  return {
    time: `${parts.hour}:${parts.minute}`,
    weekday: parts.weekday.toUpperCase(),
    date: `${parts.day} ${parts.month.toUpperCase()} ${parts.year}`,
    zoneName: (parts.timeZoneName || "").toUpperCase(),
  };
}

function shortLocationLabel(timeZone) {
  const tail = timeZone.split("/").pop() || timeZone;
  return tail.replace(/_/g, " ").toUpperCase();
}

function renderClockBmp(timeZone, orientation, device) {
  setCanvasDimensions(device, orientation);
  const clock = getClockParts(new Date(), timeZone);
  if (!clock) return null;

  const canvas = new Uint8Array(PIXEL_ROW_BYTES * HEIGHT);
  if (orientation === "landscape") {
    const compact = HEIGHT < 500;
    drawTextCentered(canvas, clock.time, compact ? 76 : 105, 18);
    drawTextCentered(canvas, clock.weekday, compact ? 245 : 280, 7);
    drawTextCentered(canvas, clock.date, compact ? 315 : 355, 6);
    drawTextCentered(canvas, shortLocationLabel(timeZone), compact ? 385 : 435, 4);
    if (clock.zoneName) drawTextCentered(canvas, clock.zoneName, compact ? 432 : 480, 3);
  } else {
    drawTextCentered(canvas, clock.time, 220, 16);
    drawTextCentered(canvas, clock.weekday, 390, 7);
    drawTextCentered(canvas, clock.date, 470, 6);
    drawTextCentered(canvas, shortLocationLabel(timeZone), 585, 4);
    if (clock.zoneName) drawTextCentered(canvas, clock.zoneName, 635, 4);
  }
  return makeBmp(canvas);
}

function resolveClockTimeZone(url, request) {
  const requested = url.searchParams.get("tz")?.trim();
  if (!requested || requested.toLowerCase() === "auto") {
    return request.cf?.timezone || "Europe/London";
  }
  return requested;
}

function weatherCategory(code) {
  if (code === 0) return "clear";
  if (code === 1 || code === 2) return "partly";
  if (code === 3) return "cloud";
  if (code === 45 || code === 48) return "fog";
  if (code >= 51 && code <= 57) return "drizzle";
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return "rain";
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return "snow";
  if (code >= 95) return "storm";
  return "cloud";
}

function conditionLabel(code) {
  const labels = {
    0: "CLEAR",
    1: "MAINLY CLEAR",
    2: "PARTLY CLOUDY",
    3: "OVERCAST",
    45: "FOG",
    48: "RIME FOG",
    51: "LIGHT DRIZZLE",
    53: "DRIZZLE",
    55: "HEAVY DRIZZLE",
    56: "FREEZING DRIZZLE",
    57: "FREEZING DRIZZLE",
    61: "LIGHT RAIN",
    63: "RAIN",
    65: "HEAVY RAIN",
    66: "FREEZING RAIN",
    67: "FREEZING RAIN",
    71: "LIGHT SNOW",
    73: "SNOW",
    75: "HEAVY SNOW",
    77: "SNOW GRAINS",
    80: "RAIN SHOWERS",
    81: "RAIN SHOWERS",
    82: "HEAVY SHOWERS",
    85: "SNOW SHOWERS",
    86: "HEAVY SNOW",
    95: "THUNDERSTORM",
    96: "STORM WITH HAIL",
    99: "STORM WITH HAIL",
  };
  return labels[code] || "CLOUDY";
}

function currentConditionLabel(code, isDay) {
  if (code === 0) return isDay === 0 ? "CLEAR" : "SUNNY";
  if (code === 1) return isDay === 0 ? "MOSTLY CLEAR" : "MOSTLY SUNNY";
  return conditionLabel(code);
}

function drawCloud(canvas, cx, cy, size) {
  const s = size / 100;
  fillCircle(canvas, cx - 24 * s, cy + 2 * s, Math.round(19 * s));
  fillCircle(canvas, cx, cy - 13 * s, Math.round(28 * s));
  fillCircle(canvas, cx + 29 * s, cy + 3 * s, Math.round(22 * s));
  fillRect(canvas, cx - 43 * s, cy, 88 * s, 25 * s);
}

function drawSun(canvas, cx, cy, size) {
  const radius = Math.max(5, Math.round(size * 0.2));
  drawCircle(canvas, cx, cy, radius, Math.max(2, Math.round(size / 30)));
  for (let i = 0; i < 8; i++) {
    const angle = (Math.PI * i) / 4;
    const inner = radius + size * 0.1;
    const outer = radius + size * 0.25;
    drawLine(
      canvas,
      cx + Math.cos(angle) * inner,
      cy + Math.sin(angle) * inner,
      cx + Math.cos(angle) * outer,
      cy + Math.sin(angle) * outer,
      Math.max(1, Math.round(size / 35))
    );
  }
}

function drawSunEventIcon(canvas, cx, cy, size, rising) {
  const radius = Math.max(5, Math.round(size * 0.18));
  const horizonY = cy + Math.round(size * 0.11);
  drawCircle(canvas, cx, horizonY, radius, Math.max(2, Math.round(size / 20)));
  fillRect(canvas, cx - radius - 3, horizonY, radius * 2 + 7, radius + 4, false);
  drawLine(canvas, cx - size * 0.42, horizonY, cx + size * 0.42, horizonY, Math.max(2, Math.round(size / 18)));
  drawLine(canvas, cx, cy - size * 0.34, cx, cy - size * 0.18, 2);
  drawLine(canvas, cx - size * 0.29, cy - size * 0.2, cx - size * 0.18, cy - size * 0.1, 2);
  drawLine(canvas, cx + size * 0.29, cy - size * 0.2, cx + size * 0.18, cy - size * 0.1, 2);
  const arrowX = cx + size * 0.31;
  const arrowTipY = rising ? cy - size * 0.12 : cy + size * 0.34;
  const arrowTailY = rising ? cy + size * 0.34 : cy - size * 0.12;
  const headY = arrowTipY + (rising ? size * 0.09 : -size * 0.09);
  drawLine(canvas, arrowX, arrowTailY, arrowX, arrowTipY, 2);
  drawLine(canvas, arrowX, arrowTipY, arrowX - size * 0.08, headY, 2);
  drawLine(canvas, arrowX, arrowTipY, arrowX + size * 0.08, headY, 2);
}

function drawSunEventGroup(canvas, centerX, cy, label, time, size, rising, timeScale) {
  const labelScale = 2;
  const iconWidth = Math.round(size * 0.84);
  const gap = Math.max(10, Math.round(size * 0.22));
  const textBlockWidth = Math.max(textWidth(label, labelScale), textWidth(time, timeScale));
  const groupWidth = iconWidth + gap + textBlockWidth;
  const startX = Math.round(centerX - groupWidth / 2);
  const textX = startX + iconWidth + gap;
  drawSunEventIcon(canvas, startX + iconWidth / 2, cy, size, rising);
  drawText(canvas, label, textX + Math.floor((textBlockWidth - textWidth(label, labelScale)) / 2), cy - 25, labelScale);
  drawText(canvas, time, textX + Math.floor((textBlockWidth - textWidth(time, timeScale)) / 2), cy + 5, timeScale);
}

function drawWeatherIcon(canvas, code, cx, cy, size) {
  const category = weatherCategory(code);
  const scale = size / 100;

  if (category === "clear") {
    drawSun(canvas, cx, cy, size);
    return;
  }

  if (category === "partly") {
    drawSun(canvas, cx - 22 * scale, cy - 22 * scale, size * 0.65);
    drawCloud(canvas, cx + 8 * scale, cy + 10 * scale, size * 0.78);
    return;
  }

  if (category === "fog") {
    for (let i = -2; i <= 2; i++) {
      drawLine(canvas, cx - size * 0.38, cy + i * size * 0.14, cx + size * 0.38, cy + i * size * 0.14, 3);
    }
    return;
  }

  drawCloud(canvas, cx, cy - 10 * scale, size * 0.78);

  if (category === "rain" || category === "drizzle") {
    const count = category === "drizzle" ? 3 : 4;
    for (let i = 0; i < count; i++) {
      const x = cx + (i - (count - 1) / 2) * 20 * scale;
      drawLine(canvas, x + 5 * scale, cy + 24 * scale, x - 5 * scale, cy + 44 * scale, Math.max(2, Math.round(3 * scale)));
    }
  } else if (category === "snow") {
    for (const offset of [-25, 0, 25]) {
      const x = cx + offset * scale;
      const y = cy + 35 * scale;
      drawLine(canvas, x - 7 * scale, y, x + 7 * scale, y, 2);
      drawLine(canvas, x, y - 7 * scale, x, y + 7 * scale, 2);
      drawLine(canvas, x - 5 * scale, y - 5 * scale, x + 5 * scale, y + 5 * scale, 2);
      drawLine(canvas, x + 5 * scale, y - 5 * scale, x - 5 * scale, y + 5 * scale, 2);
    }
  } else if (category === "storm") {
    drawLine(canvas, cx + 5 * scale, cy + 18 * scale, cx - 8 * scale, cy + 39 * scale, Math.max(3, Math.round(5 * scale)));
    drawLine(canvas, cx - 8 * scale, cy + 39 * scale, cx + 7 * scale, cy + 36 * scale, Math.max(3, Math.round(5 * scale)));
    drawLine(canvas, cx + 7 * scale, cy + 36 * scale, cx - 8 * scale, cy + 58 * scale, Math.max(3, Math.round(5 * scale)));
  }
}

function dayLabel(isoDate) {
  const date = new Date(`${isoDate}T12:00:00Z`);
  return ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"][date.getUTCDay()];
}

function updatedLabel(localIsoTime) {
  const match = String(localIsoTime || "").match(/T(\d{2}:\d{2})/);
  return match ? match[1] : "--:--";
}

function renderWeatherBmp(place, weather, imperial, orientation, device) {
  setCanvasDimensions(device, orientation);
  const canvas = new Uint8Array(PIXEL_ROW_BYTES * HEIGHT);
  const current = weather.current;
  const daily = weather.daily;
  const tempUnit = imperial ? "F" : "C";
  const windUnit = imperial ? "MPH" : "KM/H";
  const sunrise = updatedLabel(daily.sunrise?.[0]);
  const sunset = updatedLabel(daily.sunset?.[0]);
  const condition = currentConditionLabel(current.weather_code, current.is_day);

  if (orientation === "landscape") {
    const compact = HEIGHT < 500;
    drawTextCentered(canvas, place.label, compact ? 10 : 14, 5, WIDTH - 30);
    drawTextCentered(canvas, condition, compact ? 50 : 54, 3, WIDTH - 36);
    fillRect(canvas, 20, compact ? 78 : 84, WIDTH - 40, 3);

    drawWeatherIcon(canvas, current.weather_code, 118, compact ? 157 : 170, 108);
    drawTextCenteredInBox(canvas, `${Math.round(current.temperature_2m)}${tempUnit}`, 205, 280, compact ? 109 : 118, 13);

    const sunY = compact ? 231 : 246;
    drawSunEventGroup(canvas, 137, sunY, "SUNRISE", sunrise, 42, true, 3);
    drawSunEventGroup(canvas, 368, sunY, "SUNSET", sunset, 42, false, 3);

    const detailsX = 505;
    const detailsWidth = WIDTH - detailsX - 20;
    const detailsY = compact ? 101 : 107;
    drawRect(canvas, detailsX, detailsY, detailsWidth, compact ? 166 : 174, 2);
    drawTextCenteredInBox(
      canvas,
      `FEELS ${Math.round(current.apparent_temperature)}${tempUnit}`,
      detailsX,
      detailsWidth,
      compact ? 120 : 128,
      3
    );
    drawTextCenteredInBox(
      canvas,
      `HUM ${Math.round(current.relative_humidity_2m)}%`,
      detailsX,
      detailsWidth,
      compact ? 157 : 167,
      3
    );
    drawTextCenteredInBox(
      canvas,
      `WIND ${Math.round(current.wind_speed_10m)} ${windUnit}`,
      detailsX,
      detailsWidth,
      compact ? 194 : 206,
      3
    );
    drawTextCenteredInBox(
      canvas,
      `RAIN ${Math.round(daily.precipitation_probability_max[0] || 0)}%`,
      detailsX,
      detailsWidth,
      compact ? 231 : 245,
      3
    );

    const days = Math.min(5, daily.time.length);
    const gap = 12;
    const cardWidth = Math.floor((WIDTH - 36 - gap * (days - 1)) / days);
    const startX = Math.floor((WIDTH - (cardWidth * days + gap * (days - 1))) / 2);
    const cardsY = compact ? 285 : 301;
    const cardsHeight = compact ? 145 : 176;
    for (let i = 0; i < days; i++) {
      const x = startX + i * (cardWidth + gap);
      drawRect(canvas, x, cardsY, cardWidth, cardsHeight, 2);
      drawTextCenteredInBox(canvas, dayLabel(daily.time[i]), x, cardWidth, cardsY + 12, 3);
      drawWeatherIcon(canvas, daily.weather_code[i], x + cardWidth / 2, cardsY + (compact ? 67 : 74), 48);
      drawTextCenteredInBox(
        canvas,
        `${Math.round(daily.temperature_2m_max[i])}${tempUnit}`,
        x,
        cardWidth,
        cardsY + (compact ? 101 : 115),
        3
      );
      drawTextCenteredInBox(
        canvas,
        `${Math.round(daily.temperature_2m_min[i])}${tempUnit}`,
        x,
        cardWidth,
        cardsY + (compact ? 124 : 144),
        2
      );
    }

    drawTextCentered(canvas, `UPDATED ${updatedLabel(current.time)} LOCAL`, HEIGHT - 31, 2);
    drawTextCentered(canvas, "DATA OPEN-METEO", HEIGHT - 14, 1);
    return makeBmp(canvas);
  }

  drawTextCentered(canvas, place.label, 24, 6, WIDTH - 30);
  drawTextCentered(canvas, condition, 78, 4, WIDTH - 30);
  fillRect(canvas, 24, 114, WIDTH - 48, 3);

  const iconX = Math.round(WIDTH * 0.269);
  const temperatureX = Math.round(WIDTH * 0.483);
  drawWeatherIcon(canvas, current.weather_code, iconX, 216, 128);
  drawTextCenteredInBox(canvas, `${Math.round(current.temperature_2m)}${tempUnit}`, temperatureX, WIDTH - temperatureX - 18, 166, 13);

  drawSunEventGroup(canvas, WIDTH / 4, 327, "SUNRISE", sunrise, 48, true, 4);
  drawSunEventGroup(canvas, (WIDTH * 3) / 4, 327, "SUNSET", sunset, 48, false, 4);

  drawRect(canvas, 25, 394, WIDTH - 50, 72, 2);
  drawTextCentered(
    canvas,
    `FEELS ${Math.round(current.apparent_temperature)}${tempUnit}   HUM ${Math.round(current.relative_humidity_2m)}%`,
    408,
    3,
    WIDTH - 70
  );
  drawTextCentered(
    canvas,
    `WIND ${Math.round(current.wind_speed_10m)} ${windUnit}   RAIN ${Math.round(daily.precipitation_probability_max[0] || 0)}%`,
    438,
    3,
    WIDTH - 70
  );

  const days = Math.min(5, daily.time.length);
  const gap = 10;
  const cardWidth = Math.floor((WIDTH - 36 - gap * (days - 1)) / days);
  const startX = Math.floor((WIDTH - (cardWidth * days + gap * (days - 1))) / 2);
  for (let i = 0; i < days; i++) {
    const x = startX + i * (cardWidth + gap);
    drawRect(canvas, x, 491, cardWidth, 184, 2);
    drawTextCenteredInBox(canvas, dayLabel(daily.time[i]), x, cardWidth, 506, 3);
    drawWeatherIcon(canvas, daily.weather_code[i], x + cardWidth / 2, 571, 53);
    drawTextCenteredInBox(
      canvas,
      `${Math.round(daily.temperature_2m_max[i])}${tempUnit}`,
      x,
      cardWidth,
      616,
      3
    );
    drawTextCenteredInBox(
      canvas,
      `${Math.round(daily.temperature_2m_min[i])}${tempUnit}`,
      x,
      cardWidth,
      646,
      2
    );
  }

  drawTextCentered(canvas, `UPDATED ${updatedLabel(current.time)} LOCAL`, HEIGHT - 49, 2);
  drawTextCentered(canvas, "DATA OPEN-METEO", HEIGHT - 22, 1);
  return makeBmp(canvas);
}

function drawTextCenteredInBox(canvas, text, x, width, y, scale) {
  let clean = cleanText(text);
  while (scale > 1 && textWidth(clean, scale) > width - 8) scale--;
  while (clean && textWidth(clean, scale) > width - 8) clean = clean.slice(0, -1).trim();
  drawText(canvas, clean, x + Math.floor((width - textWidth(clean, scale)) / 2), y, scale);
}

function validCoordinate(value, minimum, maximum) {
  if (value === null || value === undefined || String(value).trim() === "") return null;
  const number = Number(value);
  return Number.isFinite(number) && number >= minimum && number <= maximum ? number : null;
}

async function resolvePlace(url, request) {
  const latitude = validCoordinate(url.searchParams.get("lat"), -90, 90);
  const longitude = validCoordinate(url.searchParams.get("lon"), -180, 180);
  const requestedLabel = cleanText(url.searchParams.get("label") || "");

  if (latitude !== null && longitude !== null) {
    return { latitude, longitude, label: requestedLabel || "SELECTED LOCATION" };
  }

  const requestedLocation = (url.searchParams.get("location") || "").trim();
  const automatic = !requestedLocation || requestedLocation.toLowerCase() === "auto";
  if (!automatic) {
    const geocodeUrl = new URL("https://geocoding-api.open-meteo.com/v1/search");
    geocodeUrl.searchParams.set("name", requestedLocation);
    geocodeUrl.searchParams.set("count", "1");
    geocodeUrl.searchParams.set("language", "en");
    geocodeUrl.searchParams.set("format", "json");
    const country = (url.searchParams.get("country") || "").trim().toUpperCase();
    if (/^[A-Z]{2}$/.test(country)) geocodeUrl.searchParams.set("countryCode", country);

    const response = await fetch(geocodeUrl, { headers: { accept: "application/json" } });
    if (!response.ok) throw new Error("Location search failed");
    const data = await response.json();
    const result = data.results?.[0];
    if (!result) throw new Error("Location not found");
    const suffix = result.admin1 && result.admin1 !== result.name ? result.admin1 : result.country_code;
    return {
      latitude: result.latitude,
      longitude: result.longitude,
      label: cleanText(`${result.name}${suffix ? ` ${suffix}` : ""}`),
    };
  }

  const cfLatitude = validCoordinate(request.cf?.latitude, -90, 90);
  const cfLongitude = validCoordinate(request.cf?.longitude, -180, 180);
  if (cfLatitude !== null && cfLongitude !== null) {
    return {
      latitude: cfLatitude,
      longitude: cfLongitude,
      label: cleanText(request.cf?.city || request.cf?.region || request.cf?.country || "LOCAL WEATHER"),
    };
  }

  return { latitude: 51.5072, longitude: -0.1276, label: "LONDON" };
}

async function fetchWeather(place, imperial) {
  const apiUrl = new URL("https://api.open-meteo.com/v1/forecast");
  apiUrl.searchParams.set("latitude", String(place.latitude));
  apiUrl.searchParams.set("longitude", String(place.longitude));
  apiUrl.searchParams.set(
    "current",
    "temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
  );
  apiUrl.searchParams.set(
    "daily",
    "weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"
  );
  apiUrl.searchParams.set("temperature_unit", imperial ? "fahrenheit" : "celsius");
  apiUrl.searchParams.set("wind_speed_unit", imperial ? "mph" : "kmh");
  apiUrl.searchParams.set("timezone", "auto");
  apiUrl.searchParams.set("forecast_days", "5");

  const response = await fetch(apiUrl, { headers: { accept: "application/json" } });
  if (!response.ok) throw new Error("Weather service failed");
  const data = await response.json();
  if (!data.current || !data.daily?.time?.length) throw new Error("Incomplete weather response");
  return data;
}

const SYNODIC_MONTH_MS = 29.530588853 * 86400000;
const NEW_MOON_EPOCH_MS = Date.UTC(2000, 0, 6, 18, 14);

function moonState(now) {
  const cycles = (now.getTime() - NEW_MOON_EPOCH_MS) / SYNODIC_MONTH_MS;
  const phase = ((cycles % 1) + 1) % 1;
  const illumination = (1 - Math.cos(phase * Math.PI * 2)) / 2;
  const names = [
    "NEW MOON",
    "WAXING CRESCENT",
    "FIRST QUARTER",
    "WAXING GIBBOUS",
    "FULL MOON",
    "WANING GIBBOUS",
    "LAST QUARTER",
    "WANING CRESCENT",
  ];
  return { phase, illumination, label: names[Math.round(phase * 8) % 8] };
}

function drawMoonDisk(canvas, cx, cy, radius, phase) {
  fillCircle(canvas, cx, cy, radius, true);
  const cosine = Math.cos(phase * Math.PI * 2);
  for (let y = -radius + 3; y <= radius - 3; y++) {
    const halfWidth = Math.floor(Math.sqrt((radius - 3) ** 2 - y * y));
    if (phase <= 0.5) {
      const start = Math.ceil(cosine * halfWidth);
      for (let x = start; x <= halfWidth; x++) setPixel(canvas, cx + x, cy + y, false);
    } else {
      const end = Math.floor(-cosine * halfWidth);
      for (let x = -halfWidth; x <= end; x++) setPixel(canvas, cx + x, cy + y, false);
    }
  }
  const innerRadiusSquared = (radius - 3) ** 2;
  const outerRadiusSquared = radius ** 2;
  for (let y = -radius; y <= radius; y++) {
    for (let x = -radius; x <= radius; x++) {
      const distanceSquared = x * x + y * y;
      if (distanceSquared >= innerRadiusSquared && distanceSquared <= outerRadiusSquared) {
        setPixel(canvas, cx + x, cy + y, true);
      }
    }
  }
}

function datePartsInZone(date, timeZone) {
  try {
    const formatter = new Intl.DateTimeFormat("en-GB", {
      timeZone,
      day: "2-digit",
      month: "short",
      year: "numeric",
      hour: "2-digit",
      minute: "2-digit",
      hourCycle: "h23",
    });
    const parts = {};
    for (const part of formatter.formatToParts(date)) {
      if (part.type !== "literal") parts[part.type] = part.value;
    }
    return {
      date: `${parts.day} ${parts.month.toUpperCase()} ${parts.year}`,
      short: `${parts.day} ${parts.month.toUpperCase()} ${parts.hour}:${parts.minute}`,
      time: `${parts.hour}:${parts.minute}`,
    };
  } catch {
    return null;
  }
}

function approximateMoonPhases(now) {
  const phaseNames = ["NEW MOON", "FIRST QUARTER", "FULL MOON", "LAST QUARTER"];
  const cycle = (now.getTime() - NEW_MOON_EPOCH_MS) / SYNODIC_MONTH_MS;
  const currentCycle = Math.floor(cycle);
  const moments = [];
  for (let offset = 0; offset < 2; offset++) {
    for (let quarter = 0; quarter < 4; quarter++) {
      const time = NEW_MOON_EPOCH_MS + (currentCycle + offset + quarter / 4) * SYNODIC_MONTH_MS;
      if (time > now.getTime()) moments.push({ phase: phaseNames[quarter], date: new Date(time) });
    }
  }
  return moments.sort((a, b) => a.date - b.date).slice(0, 4);
}

async function fetchMoonPhases(now) {
  const date = now.toISOString().slice(0, 10);
  try {
    const response = await fetch(`https://aa.usno.navy.mil/api/moon/phases/date?date=${date}&nump=4`, {
      headers: { accept: "application/json", "user-agent": "CrossPointDashboard/1.0" },
    });
    if (!response.ok) throw new Error("Moon service failed");
    const data = await response.json();
    if (!Array.isArray(data.phasedata) || data.phasedata.length < 4) throw new Error("Incomplete moon response");
    const phases = data.phasedata.slice(0, 4).map((entry) => {
      const [hour, minute] = String(entry.time || "00:00").split(":").map(Number);
      return {
        phase: cleanText(entry.phase),
        date: new Date(Date.UTC(Number(entry.year), Number(entry.month) - 1, Number(entry.day), hour, minute)),
      };
    });
    return { phases, source: "PHASE DATA USNO" };
  } catch {
    return { phases: approximateMoonPhases(now), source: "PHASES APPROXIMATE" };
  }
}

function renderMoonBmp(now, timeZone, phaseData, orientation, device) {
  setCanvasDimensions(device, orientation);
  const canvas = new Uint8Array(PIXEL_ROW_BYTES * HEIGHT);
  const state = moonState(now);
  const local = datePartsInZone(now, timeZone);
  if (!local) return null;

  if (orientation === "landscape") {
    const compact = HEIGHT < 500;
    drawTextCentered(canvas, "MOON", compact ? 12 : 16, 6);
    drawTextCentered(canvas, local.date, compact ? 60 : 68, 3);
    fillRect(canvas, 20, compact ? 88 : 98, WIDTH - 40, 3);
    drawMoonDisk(canvas, 145, compact ? 202 : 218, compact ? 92 : 100, state.phase);
    drawTextCenteredInBox(canvas, state.label, 285, WIDTH - 305, compact ? 137 : 151, 5);
    drawTextCenteredInBox(
      canvas,
      `${Math.round(state.illumination * 100)}% ILLUMINATED`,
      285,
      WIDTH - 305,
      compact ? 198 : 216,
      3
    );
    drawTextCenteredInBox(canvas, shortLocationLabel(timeZone), 285, WIDTH - 305, compact ? 244 : 267, 2);

    const startY = compact ? 307 : 335;
    const gap = 10;
    const width = Math.floor((WIDTH - 36 - gap * 3) / 4);
    phaseData.phases.forEach((item, index) => {
      const x = 18 + index * (width + gap);
      drawRect(canvas, x, startY, width, compact ? 116 : 132, 2);
      drawTextCenteredInBox(canvas, item.phase, x, width, startY + 14, 2);
      const parts = datePartsInZone(item.date, timeZone);
      drawTextCenteredInBox(canvas, parts?.short || "--", x, width, startY + 55, 2);
    });
    drawTextCentered(canvas, phaseData.source, HEIGHT - 17, 1);
    return makeBmp(canvas);
  }

  drawTextCentered(canvas, "MOON", 24, 7);
  drawTextCentered(canvas, local.date, 86, 3);
  fillRect(canvas, 24, 120, WIDTH - 48, 3);
  drawMoonDisk(canvas, Math.floor(WIDTH / 2), 275, 112, state.phase);
  drawTextCentered(canvas, state.label, 412, 5, WIDTH - 24);
  drawTextCentered(canvas, `${Math.round(state.illumination * 100)}% ILLUMINATED`, 463, 3);
  drawTextCentered(canvas, "NEXT PHASES", 516, 3);
  phaseData.phases.forEach((item, index) => {
    const y = 555 + index * 42;
    drawText(canvas, item.phase, 28, y, 2);
    const parts = datePartsInZone(item.date, timeZone);
    const value = parts?.short || "--";
    drawText(canvas, value, WIDTH - 28 - textWidth(value, 2), y, 2);
    if (index < 3) fillRect(canvas, 28, y + 27, WIDTH - 56, 1);
  });
  drawTextCentered(canvas, phaseData.source, HEIGHT - 19, 1);
  return makeBmp(canvas);
}

function decodeXmlText(value) {
  return String(value || "")
    .replace(/<!\[CDATA\[([\s\S]*?)\]\]>/gi, "$1")
    .replace(/<[^>]+>/g, " ")
    .replace(/&#(\d+);/g, (_, number) => String.fromCodePoint(Number(number)))
    .replace(/&#x([0-9a-f]+);/gi, (_, number) => String.fromCodePoint(parseInt(number, 16)))
    .replace(/&amp;/gi, "&")
    .replace(/&quot;/gi, '"')
    .replace(/&apos;|&#39;/gi, "'")
    .replace(/&lt;/gi, "<")
    .replace(/&gt;/gi, ">")
    .replace(/\s+/g, " ")
    .trim();
}

function extractXmlTag(xml, tag) {
  const match = String(xml).match(new RegExp(`<${tag}(?:\\s[^>]*)?>([\\s\\S]*?)<\\/${tag}>`, "i"));
  return match ? decodeXmlText(match[1]) : "";
}

function validateFeedUrl(value) {
  let feedUrl;
  try {
    feedUrl = new URL(value);
  } catch {
    return null;
  }
  if (feedUrl.protocol !== "https:") return null;
  const host = feedUrl.hostname.toLowerCase().replace(/^\[|\]$/g, "");
  if (host === "localhost" || host.endsWith(".local") || host === "::1" || host === "0.0.0.0") return null;
  const octets = host.split(".").map(Number);
  if (
    octets.length === 4 &&
    octets.every((part) => Number.isInteger(part) && part >= 0 && part <= 255) &&
    (octets[0] === 10 ||
      octets[0] === 127 ||
      (octets[0] === 169 && octets[1] === 254) ||
      (octets[0] === 172 && octets[1] >= 16 && octets[1] <= 31) ||
      (octets[0] === 192 && octets[1] === 168))
  ) {
    return null;
  }
  return feedUrl;
}

async function fetchRss(feedUrl) {
  const response = await fetch(feedUrl, {
    redirect: "follow",
    headers: {
      accept: "application/rss+xml, application/atom+xml, application/xml, text/xml;q=0.9, */*;q=0.5",
      "user-agent": "CrossPointDashboard/1.0",
    },
  });
  if (!response.ok) throw new Error("RSS feed download failed");
  const contentLength = Number(response.headers.get("content-length") || 0);
  if (contentLength > 524288) throw new Error("RSS feed is too large");
  const xml = (await response.text()).slice(0, 524289);
  if (xml.length > 524288) throw new Error("RSS feed is too large");

  const itemMatches = [...xml.matchAll(/<item\b[\s\S]*?<\/item>/gi)];
  const entryMatches = itemMatches.length ? [] : [...xml.matchAll(/<entry\b[\s\S]*?<\/entry>/gi)];
  const blocks = (itemMatches.length ? itemMatches : entryMatches).map((match) => match[0]);
  const items = blocks.map((block) => extractXmlTag(block, "title")).filter(Boolean).slice(0, 6);
  if (!items.length) throw new Error("No RSS headlines found");

  const beforeFirstItem = xml.slice(0, Math.max(0, xml.search(/<(?:item|entry)\b/i)) || xml.length);
  const title = extractXmlTag(beforeFirstItem, "title") || feedUrl.hostname;
  return { title, items };
}

function renderListBmp(kind, title, items, footer, orientation, device) {
  setCanvasDimensions(device, orientation);
  const canvas = new Uint8Array(PIXEL_ROW_BYTES * HEIGHT);

  if (orientation === "landscape") {
    const compact = HEIGHT < 500;
    drawTextCentered(canvas, title, compact ? 10 : 14, 5, WIDTH - 30);
    drawTextCentered(canvas, kind, compact ? 53 : 58, 2);
    fillRect(canvas, 20, compact ? 79 : 86, WIDTH - 40, 3);
    const count = Math.min(items.length, 4);
    const columns = 2;
    const rows = Math.ceil(count / columns);
    const areaTop = compact ? 100 : 108;
    const areaBottom = HEIGHT - 38;
    const cellWidth = Math.floor((WIDTH - 50) / 2);
    const cellHeight = Math.floor((areaBottom - areaTop - 10) / rows);
    for (let index = 0; index < count; index++) {
      const column = index % 2;
      const row = Math.floor(index / 2);
      const x = 20 + column * (cellWidth + 10);
      const y = areaTop + row * cellHeight;
      drawRect(canvas, x, y, cellWidth, cellHeight - 10, 2);
      const kicker = cleanText(items[index].kicker || String(index + 1));
      drawText(canvas, kicker, x + 13, y + 13, 3);
      drawWrappedText(canvas, items[index].text, x + 13, y + 53, 2, cellWidth - 26, compact ? 3 : 4, 22);
    }
    drawTextCentered(canvas, footer, HEIGHT - 17, 1);
    return makeBmp(canvas);
  }

  drawTextCentered(canvas, title, 24, 5, WIDTH - 30);
  drawTextCentered(canvas, kind, 75, 2);
  fillRect(canvas, 24, 108, WIDTH - 48, 3);
  const count = Math.min(items.length, kind === "RSS HEADLINES" ? 5 : 4);
  const areaTop = 126;
  const areaBottom = HEIGHT - 42;
  const cellHeight = Math.floor((areaBottom - areaTop) / count);
  for (let index = 0; index < count; index++) {
    const y = areaTop + index * cellHeight;
    const kicker = cleanText(items[index].kicker || String(index + 1));
    drawText(canvas, kicker, 24, y + 14, kicker.length > 3 ? 3 : 4);
    const textX = kind === "TODAY IN HISTORY" ? 112 : 72;
    drawWrappedText(canvas, items[index].text, textX, y + 12, 2, WIDTH - textX - 22, 3, 23);
    if (index < count - 1) fillRect(canvas, 24, y + cellHeight - 2, WIDTH - 48, 1);
  }
  drawTextCentered(canvas, footer, HEIGHT - 19, 1);
  return makeBmp(canvas);
}

async function fetchWikipediaToday(language, now, timeZone) {
  const parts = new Intl.DateTimeFormat("en-GB", {
    timeZone,
    month: "2-digit",
    day: "2-digit",
  }).formatToParts(now);
  const month = parts.find((part) => part.type === "month")?.value || String(now.getUTCMonth() + 1).padStart(2, "0");
  const day = parts.find((part) => part.type === "day")?.value || String(now.getUTCDate()).padStart(2, "0");
  const endpoint = `https://api.wikimedia.org/feed/v1/wikipedia/${language}/onthisday/events/${month}/${day}`;
  const response = await fetch(endpoint, {
    headers: {
      accept: "application/json",
      "user-agent": "CrossPointDashboard/1.0 (https://github.com/petereading/crosspoint-dashboard-experiments)",
    },
  });
  if (!response.ok) throw new Error("Wikipedia Today service failed");
  const data = await response.json();
  const events = (data.events || [])
    .filter((event) => event?.text && Number.isFinite(Number(event.year)))
    .slice(0, 6)
    .map((event) => ({ kicker: String(event.year), text: event.text }));
  if (!events.length) throw new Error("No Wikipedia events found");
  return events;
}

function textResponse(message, status = 200) {
  return new Response(message, {
    status,
    headers: { "content-type": "text/plain; charset=utf-8" },
  });
}

function bmpResponse(request, bmp, filename) {
  return new Response(request.method === "HEAD" ? null : bmp, {
    headers: {
      "content-type": "image/bmp",
      "content-disposition": `inline; filename="${filename}"`,
      "cache-control": "no-store, no-cache, must-revalidate, max-age=0",
      "cloudflare-cdn-cache-control": "no-store",
      pragma: "no-cache",
      expires: "0",
    },
  });
}

export default {
  async fetch(request) {
    const url = new URL(request.url);

    if (url.pathname === "/") {
      return textResponse(
        "CrossPoint Dashboard Worker\n\n" +
          "Clock:\n/clock.bmp\n/clock.bmp?tz=Europe/London\n" +
          "/clock.bmp?tz=Australia/Sydney&device=x4&orientation=landscape\n\n" +
          "Weather, automatic location:\n/weather.bmp\n/weather.bmp?location=auto\n\n" +
          "Weather, place name:\n/weather.bmp?location=London,GB\n" +
          "/weather.bmp?location=Sydney,AU&orientation=landscape\n\n" +
          "Weather, exact coordinates:\n/weather.bmp?lat=51.5072&lon=-0.1276&label=London\n\n" +
          "Weather, imperial units:\n/weather.bmp?location=New York,US&units=imperial\n\n" +
          "Moon phases:\n/moon.bmp\n/moon.bmp?tz=Australia/Sydney&orientation=landscape\n\n" +
          "RSS headlines (URL encode the feed value):\n/rss.bmp?feed=https%3A%2F%2Fexample.com%2Ffeed.xml\n\n" +
          "Wikipedia Today:\n/today.bmp\n/today.bmp?lang=en&orientation=landscape\n\n" +
          "Device defaults to X3. Use device=x3 or device=x4.\n" +
          "Orientation defaults to portrait. Use orientation=portrait or orientation=landscape.\n"
      );
    }

    if (request.method !== "GET" && request.method !== "HEAD") {
      return new Response("Method not allowed", { status: 405, headers: { Allow: "GET, HEAD" } });
    }

    const orientation = resolveOrientation(url);
    if (!orientation) return textResponse("orientation must be portrait or landscape", 400);
    const device = resolveDevice(url);
    if (!device) return textResponse("device must be x3 or x4", 400);

    if (url.pathname === "/clock.bmp") {
      const timeZone = resolveClockTimeZone(url, request);
      const bmp = renderClockBmp(timeZone, orientation, device);
      if (!bmp) return textResponse("Invalid IANA time zone. Example: Europe/London", 400);
      return bmpResponse(request, bmp, "clock.bmp");
    }

    if (url.pathname === "/moon.bmp") {
      const timeZone = resolveClockTimeZone(url, request);
      if (!getClockParts(new Date(), timeZone)) {
        return textResponse("Invalid IANA time zone. Example: Europe/London", 400);
      }
      const now = new Date();
      const phaseData = await fetchMoonPhases(now);
      const bmp = renderMoonBmp(now, timeZone, phaseData, orientation, device);
      return bmpResponse(request, bmp, "moon.bmp");
    }

    if (url.pathname === "/rss.bmp") {
      const feedUrl = validateFeedUrl(url.searchParams.get("feed") || "");
      if (!feedUrl) return textResponse("feed must be a public HTTPS RSS or Atom URL", 400);
      try {
        const feed = await fetchRss(feedUrl);
        const override = cleanText(url.searchParams.get("title") || "");
        const timeZone = resolveClockTimeZone(url, request);
        const fetched = datePartsInZone(new Date(), timeZone);
        if (!fetched) return textResponse("Invalid IANA time zone. Example: Europe/London", 400);
        const items = feed.items.map((text, index) => ({ kicker: String(index + 1), text }));
        const bmp = renderListBmp(
          "RSS HEADLINES",
          override || feed.title,
          items,
          `FETCHED ${fetched.time}  SOURCE ${feedUrl.hostname}`,
          orientation,
          device
        );
        return bmpResponse(request, bmp, "rss.bmp");
      } catch (error) {
        return textResponse(error instanceof Error ? error.message : "RSS generation failed", 502);
      }
    }

    if (url.pathname === "/today.bmp") {
      const language = (url.searchParams.get("lang") || "en").trim().toLowerCase();
      if (!/^[a-z][a-z0-9-]{1,11}$/.test(language)) return textResponse("lang must be a Wikipedia language code", 400);
      const timeZone = resolveClockTimeZone(url, request);
      const now = new Date();
      const local = datePartsInZone(now, timeZone);
      if (!local) return textResponse("Invalid IANA time zone. Example: Europe/London", 400);
      try {
        const events = await fetchWikipediaToday(language, now, timeZone);
        const title = local.date.replace(/ \d{4}$/, "");
        const bmp = renderListBmp(
          "TODAY IN HISTORY",
          title,
          events,
          `DATA WIKIPEDIA ${language.toUpperCase()}  CC BY-SA`,
          orientation,
          device
        );
        return bmpResponse(request, bmp, "today.bmp");
      } catch (error) {
        return textResponse(error instanceof Error ? error.message : "Wikipedia Today generation failed", 502);
      }
    }

    if (url.pathname !== "/weather.bmp") return textResponse("Not found", 404);

    try {
      const units = (url.searchParams.get("units") || "metric").toLowerCase();
      if (units !== "metric" && units !== "imperial") {
        return textResponse("units must be metric or imperial", 400);
      }

      const imperial = units === "imperial";
      const place = await resolvePlace(url, request);
      const weather = await fetchWeather(place, imperial);
      const bmp = renderWeatherBmp(place, weather, imperial, orientation, device);
      return bmpResponse(request, bmp, "weather.bmp");
    } catch (error) {
      return textResponse(error instanceof Error ? error.message : "Weather generation failed", 502);
    }
  },
};
