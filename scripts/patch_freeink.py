"""Add the UC8253 X3 partial-window implementation to the pinned FreeInk SDK.

The SDK pin falls back to a full-frame FAST paint for displayWindow(). This
pre-build overlay adds the PTIN/PTL rectangular RAM/update sequence used by the
Remote Image hardware experiment. Exact anchors make the overlay idempotent and
fail the build if the pinned source drifts.
"""

Import("env")  # noqa: F821 (SCons-injected global)
from pathlib import Path


SDK_DIR = Path(env["PROJECT_DIR"]) / "freeink-sdk"  # noqa: F821


def insert_after(path, anchor, addition):
    source = path.read_text()
    if addition.strip() in source:
        return
    if source.count(anchor) != 1:
        raise RuntimeError("FreeInk partial-window anchor changed in %s" % path)
    path.write_text(source.replace(anchor, anchor + addition))


header = SDK_DIR / "libs/display/FreeInkDisplay/src/driver/Uc8253X3Driver.h"
insert_after(
    header,
    "  void display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) override;\n",
    "  void displayWindow(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, uint16_t x, uint16_t y, uint16_t w,\n"
    "                     uint16_t h, bool turnOff) override;\n",
)
insert_after(
    header,
    "  void loadBankCdi(EpdBus& bus, uint8_t cdi0, uint8_t cdi1, const Uc8253LutBank& bank);\n",
    "  void setPartialWindow(EpdBus& bus, uint16_t x, uint16_t y, uint16_t w, uint16_t h);\n"
    "  void writeWindowPlane(EpdBus& bus, uint8_t command, const uint8_t* fb, uint16_t x, uint16_t y, uint16_t w,\n"
    "                        uint16_t h);\n",
)


implementation = SDK_DIR / "libs/display/FreeInkDisplay/src/driver/Uc8253X3Driver.cpp"
insert_after(
    implementation,
    "  _forcedConditionPassesNext = 0;\n}\n\n",
    """void Uc8253X3Driver::setPartialWindow(EpdBus& bus, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  const uint16_t xEnd = static_cast<uint16_t>(x + w - 1);
  const uint16_t yEnd = static_cast<uint16_t>(y + h - 1);
  const uint16_t gateYStart = static_cast<uint16_t>((_h - 1) - yEnd);
  const uint16_t gateYEnd = static_cast<uint16_t>((_h - 1) - y);
  const uint8_t window[9] = {static_cast<uint8_t>(x >> 8),
                             static_cast<uint8_t>(x & 0xFF),
                             static_cast<uint8_t>(xEnd >> 8),
                             static_cast<uint8_t>(xEnd & 0xFF),
                             static_cast<uint8_t>(gateYStart >> 8),
                             static_cast<uint8_t>(gateYStart & 0xFF),
                             static_cast<uint8_t>(gateYEnd >> 8),
                             static_cast<uint8_t>(gateYEnd & 0xFF),
                             0x01};
  bus.cmdData(CMD_PARTIAL_WINDOW, window, sizeof(window));
}

void Uc8253X3Driver::writeWindowPlane(EpdBus& bus, uint8_t command, const uint8_t* fb, uint16_t x, uint16_t y,
                                      uint16_t w, uint16_t h) {
  const uint16_t firstByte = x / 8;
  const uint16_t windowWidthBytes = w / 8;
  bus.cmd(command);
  bus.beginTxn();
  for (int row = static_cast<int>(y + h) - 1; row >= static_cast<int>(y); row--) {
    bus.rawWriteBytes(fb + static_cast<uint32_t>(row) * _wb + firstByte, windowWidthBytes);
  }
  bus.endTxn();
  bus.cmd(CMD_DATA_STOP);
}

void Uc8253X3Driver::displayWindow(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, uint16_t x, uint16_t y,
                                   uint16_t w, uint16_t h, bool turnOff) {
  (void)prev;
  if (!fb || w == 0 || h == 0 || x >= _w || y >= _h || (x & 7u) != 0 || (w & 7u) != 0 || x + w > _w ||
      y + h > _h) {
    return;
  }

  // A rectangular differential update is only valid while DTM1 contains the
  // displayed B/W frame. The standalone caller establishes that baseline with
  // a normal full cached-dashboard paint immediately before this call.
  if (_inGrayscaleMode || !_redRamSynced || _grayState.lsbValid) return;

  bus.cmd(CMD_PARTIAL_IN);
  setPartialWindow(bus, x, y, w, h);
  writeWindowPlane(bus, CMD_DTM2, fb, x, y, w, h);
  loadBankCdi(bus, 0x29, 0x07, _cfg.fast);
  triggerRefresh(bus, turnOff);

  // Rebase only the same old-RAM rectangle. This makes a second partial call
  // a true inverse differential update and preserves the rest of both planes.
  writeWindowPlane(bus, CMD_DTM1, fb, x, y, w, h);
  bus.cmd(CMD_PARTIAL_OUT);
  _redRamSynced = true;
}

""",
)
