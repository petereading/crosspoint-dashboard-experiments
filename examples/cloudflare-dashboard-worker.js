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
    .normalize("NFD")
    .replace(/[\u0300-\u036f]/g, "")
    .toUpperCase()
    .replace(/[^A-Z0-9 :\-/.%]/g, " ")
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
  const clean = cleanText(text);
  while (scale > 1 && textWidth(clean, scale) > maxWidth) scale--;
  drawText(canvas, clean, Math.floor((WIDTH - textWidth(clean, scale)) / 2), y, scale);
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

  if (orientation === "landscape") {
    const compact = HEIGHT < 500;
    drawTextCentered(canvas, place.label, compact ? 14 : 18, 5, WIDTH - 30);
    drawTextCentered(canvas, "WEATHER", compact ? 55 : 63, 2);
    fillRect(canvas, 20, compact ? 82 : 91, WIDTH - 40, 3);

    drawWeatherIcon(canvas, current.weather_code, 122, compact ? 165 : 181, 115);
    drawTextCenteredInBox(canvas, `${Math.round(current.temperature_2m)}${tempUnit}`, 215, 260, compact ? 110 : 122, 13);
    drawTextCenteredInBox(canvas, conditionLabel(current.weather_code), 20, 465, compact ? 230 : 251, 4);

    drawRect(canvas, 505, compact ? 96 : 108, 265, compact ? 156 : 166, 2);
    drawTextCenteredInBox(
      canvas,
      `FEELS ${Math.round(current.apparent_temperature)}${tempUnit}`,
      505,
      265,
      compact ? 113 : 126,
      3
    );
    drawTextCenteredInBox(
      canvas,
      `HUM ${Math.round(current.relative_humidity_2m)}%`,
      505,
      265,
      compact ? 145 : 161,
      3
    );
    drawTextCenteredInBox(
      canvas,
      `WIND ${Math.round(current.wind_speed_10m)} ${windUnit}`,
      505,
      265,
      compact ? 177 : 196,
      3
    );
    drawTextCenteredInBox(
      canvas,
      `RAIN ${Math.round(daily.precipitation_probability_max[0] || 0)}%`,
      505,
      265,
      compact ? 209 : 231,
      3
    );

    const days = Math.min(5, daily.time.length);
    const gap = 8;
    const cardWidth = Math.floor((WIDTH - 30 - gap * (days - 1)) / days);
    const startX = Math.floor((WIDTH - (cardWidth * days + gap * (days - 1))) / 2);
    for (let i = 0; i < days; i++) {
      const x = startX + i * (cardWidth + gap);
      drawRect(canvas, x, compact ? 275 : 305, cardWidth, compact ? 145 : 162, 2);
      drawTextCenteredInBox(canvas, dayLabel(daily.time[i]), x, cardWidth, compact ? 286 : 316, 3);
      drawWeatherIcon(canvas, daily.weather_code[i], x + cardWidth / 2, compact ? 342 : 376, 48);
      drawTextCenteredInBox(
        canvas,
        `${Math.round(daily.temperature_2m_max[i])}${tempUnit}`,
        x,
        cardWidth,
        compact ? 376 : 413,
        3
      );
      drawTextCenteredInBox(
        canvas,
        `${Math.round(daily.temperature_2m_min[i])}${tempUnit}`,
        x,
        cardWidth,
        compact ? 401 : 442,
        2
      );
    }

    drawTextCentered(canvas, `UPDATED ${updatedLabel(current.time)} LOCAL`, compact ? 441 : 483, 2);
    drawTextCentered(canvas, "DATA OPEN-METEO", compact ? 463 : 506, 2);
    return makeBmp(canvas);
  }

  drawTextCentered(canvas, place.label, 28, 6, WIDTH - 30);
  drawTextCentered(canvas, "WEATHER", 84, 3);
  fillRect(canvas, 24, 120, WIDTH - 48, 3);

  const iconX = Math.round(WIDTH * 0.269);
  const temperatureX = Math.round(WIDTH * 0.483);
  drawWeatherIcon(canvas, current.weather_code, iconX, 229, 135);
  drawTextCenteredInBox(canvas, `${Math.round(current.temperature_2m)}${tempUnit}`, temperatureX, WIDTH - temperatureX - 18, 178, 13);
  drawTextCentered(canvas, conditionLabel(current.weather_code), 320, 5, WIDTH - 30);

  drawRect(canvas, 25, 374, WIDTH - 50, 72, 2);
  drawTextCentered(
    canvas,
    `FEELS ${Math.round(current.apparent_temperature)}${tempUnit}   HUM ${Math.round(current.relative_humidity_2m)}%`,
    389,
    3,
    WIDTH - 70
  );
  drawTextCentered(
    canvas,
    `WIND ${Math.round(current.wind_speed_10m)} ${windUnit}   RAIN ${Math.round(daily.precipitation_probability_max[0] || 0)}%`,
    417,
    3,
    WIDTH - 70
  );

  const days = Math.min(5, daily.time.length);
  const gap = 7;
  const cardWidth = Math.floor((WIDTH - 30 - gap * (days - 1)) / days);
  const startX = Math.floor((WIDTH - (cardWidth * days + gap * (days - 1))) / 2);
  for (let i = 0; i < days; i++) {
    const x = startX + i * (cardWidth + gap);
    drawRect(canvas, x, 475, cardWidth, 188, 2);
    drawTextCenteredInBox(canvas, dayLabel(daily.time[i]), x, cardWidth, 490, 3);
    drawWeatherIcon(canvas, daily.weather_code[i], x + cardWidth / 2, 555, 55);
    drawTextCenteredInBox(
      canvas,
      `${Math.round(daily.temperature_2m_max[i])}${tempUnit}`,
      x,
      cardWidth,
      600,
      3
    );
    drawTextCenteredInBox(
      canvas,
      `${Math.round(daily.temperature_2m_min[i])}${tempUnit}`,
      x,
      cardWidth,
      630,
      2
    );
  }

  drawTextCentered(canvas, `UPDATED ${updatedLabel(current.time)} LOCAL`, 704, 3);
  drawTextCentered(canvas, "DATA OPEN-METEO", 744, 2);
  return makeBmp(canvas);
}

function drawTextCenteredInBox(canvas, text, x, width, y, scale) {
  const clean = cleanText(text);
  while (scale > 1 && textWidth(clean, scale) > width - 8) scale--;
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
    "temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m"
  );
  apiUrl.searchParams.set(
    "daily",
    "weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
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
