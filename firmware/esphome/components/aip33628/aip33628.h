#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/time/real_time_clock.h"

#include <driver/gptimer.h>

namespace esphome {
namespace aip33628 {

// Two AiP33628 drivers behind the HU-058D panel, one per two-wire bus.
// Mapping, scan timing and the current budget are documented in
// docs/display-map.md.

static const uint8_t DRAM_ATTR COM_SEQ[4] = {0x30, 0x0C, 0x03, 0xC0};

// Each COM pair holds the bus for 600us, so a full four pair cycle is 2400us
// and the panel refreshes at 416.7Hz, within a hertz of what the stock
// firmware ran.
//
// That 600us is subdivided into binary weighted sub-frames, 40, 80, 160 and
// 320us. A channel wanting duty level L is lit in the sub-frames whose weight
// bits are set in L, so four sub-frames buy sixteen levels rather than the
// five that four equal ones would. The shortest sub-frame is 40us against a
// 6.4us frame send, so there is room to spare.
static const uint8_t COLOR_BITS = 4;
static const uint8_t COLOR_LEVELS = 1 << COLOR_BITS;  // 0 to 15 inclusive
static const uint32_t UNIT_US = 40;
static const uint8_t MAX_STEPS = 4 * COLOR_BITS;

// Current step to mA, from the datasheet. Tenths of a mA.
static const uint16_t IS_MA[16] = {25, 51, 76, 101, 126, 152, 177, 202,
                                   227, 253, 278, 303, 328, 354, 379, 404};

// Current step to the nibble that goes on the wire. The frame carries IS[0]
// at bit 27 and IS[3] at bit 24, so the field is bit reversed against the
// rest of the frame. Steps 0 and 15 are palindromes, which is why getting
// this wrong looks correct at both ends of the brightness range and scrambles
// the order everywhere in between. The table is its own inverse.
static const uint8_t DRAM_ATTR IS_WIRE[16] = {0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE,
                                              0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF};

enum Channel : uint8_t { CH_BLUE = 0, CH_GREEN = 1, CH_RED = 2 };

// Where each LED physically sits, from docs/led-layout.md. id is the number
// used by the board layout map, 1 to 33, and 0
// marks a position the panel can address but nothing is wired to. nx runs 0
// at the left edge of the first digit to 255 at the right edge of the last,
// ny 0 at the top of a digit to 255 at the bottom.
struct PosGeom {
  uint8_t id;
  uint8_t nx;
  uint8_t ny;
};
extern const PosGeom GEOM[4][9];

// What the panel is showing. Everything except TIME is temporary and expires
// on its own, so no caller can leave the clock stuck not being a clock.
enum class Mode : uint8_t { TIME, SECONDS, NUMBER, LAMP };

// One step of the scan schedule: latch this pattern on both drivers, then
// hold it for this long. Adjacent sub-frames with identical data collapse
// into a single longer step, so a saturated color costs one send per COM
// pair, exactly what the two level scan cost before.
struct ScanStep {
  uint8_t units;  // dwell, in UNIT_US ticks, 1 to 15
  uint8_t cs;
  uint16_t ss[2];
};

// One complete scan pattern. render_() fills the back buffer and then flips
// front_, so the scan callback can never read a half written pattern.
struct ScanBuf {
  ScanStep step[MAX_STEPS];
  uint8_t n;
  uint8_t is;
};

class Aip33628Panel : public Component {
 public:
  void set_pins(InternalGPIOPin *clk, InternalGPIOPin *data, InternalGPIOPin *clk2,
                InternalGPIOPin *data2) {
    clk_ = clk;
    data_ = data;
    clk2_ = clk2;
    data2_ = data2;
  }
  void set_time(time::RealTimeClock *rtc) { time_ = rtc; }
  void set_max_current(uint8_t is) { max_current_ = is; }
  // Both of these are live. Home Assistant drives them through template
  // switches, so each has to force a redraw rather than wait for the next
  // rollover.
  void set_twelve_hour(bool v) {
    twelve_hour_ = v;
    dirty_ = true;
  }
  void set_blink_colon(bool v) {
    blink_colon_ = v;
    dirty_ = true;
  }
  // Offline drops the upper colon dot, so a glance at the panel says whether
  // the time is still being kept honest. Driven from the wifi triggers in
  // clock.yaml rather than by including the wifi component here.
  void set_online(bool v) {
    online_ = v;
    dirty_ = true;
  }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  // Called by the light platform. Color components and brightness are 0 to 1.
  void set_light(bool on, float r, float g, float b, float brightness);

  // Temporary displays, driven from the api actions in clock.yaml. Each takes
  // a lifetime in milliseconds and falls back to the time when it runs out.
  void show_seconds(int ms);
  void show_number(int value, const std::string &unit, int ms);
  // Every populated position, white, at a fixed brightness. Ignores the
  // light's color and the light being off.
  void lamp_test(int ms);


 protected:
  void render_();
  void apply_colors_();
  void write_digit_(uint8_t block, char c);
  void write_pos_(uint8_t block, uint8_t seg, bool on);
  void draw_number_(int value, char unit);
  static bool scan_tick_(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata,
                         void *arg);
  void send_pair_(uint16_t ss1, uint16_t ss2, uint8_t cs, uint8_t is);

  InternalGPIOPin *clk_{nullptr};
  InternalGPIOPin *data_{nullptr};
  InternalGPIOPin *clk2_{nullptr};
  InternalGPIOPin *data2_{nullptr};
  time::RealTimeClock *time_{nullptr};

  // Port bit masks for the four pins. A frame goes out as direct register
  // stores with both buses clocked together, which takes 6.4us against 28.0us
  // for two passes through ISRInternalGPIOPin. All four pins have to live
  // below GPIO32 for this, which __init__.py enforces at config time.
  uint32_t clk_mask_{0}, data_mask_{0}, clk2_mask_{0}, data2_mask_{0};


  uint8_t max_current_{15};
  bool twelve_hour_{true};
  bool blink_colon_{true};
  bool online_{false};

  // Which positions are lit, before color is applied.
  bool on_[4][9]{};   // [block][segment], segments A..G then annunciator, annunciator 2
  bool enabled_{false};
  // [block][segment][channel], 0 to COLOR_LEVELS - 1, index by Channel.
  uint8_t level_[4][9][3]{};
  uint8_t requested_current_{15};

  // The master light's color before it is quantized.
  float base_rgb_[3]{1.0f, 1.0f, 1.0f};

  ScanBuf buf_[2]{};
  volatile uint8_t front_{0};
  volatile uint8_t step_{0};
  volatile uint8_t wait_{0};  // ticks left before the next step is latched

  Mode mode_{Mode::TIME};
  uint32_t mode_until_{0};
  uint8_t lamp_current_{0};
  int number_{0};
  char unit_{'\0'};

  int last_key_{-1};  // whatever the current mode reduces its content to
  bool last_colon_{false};
  bool dirty_{true};
};

class Aip33628Light : public light::LightOutput {
 public:
  void set_panel(Aip33628Panel *panel) { panel_ = panel; }

  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    traits.set_supported_color_modes({light::ColorMode::RGB});
    return traits;
  }

  void write_state(light::LightState *state) override {
    auto v = state->current_values;
    // Color ratio comes from the raw components, which ESPHome has already
    // normalized so the largest is 1. Magnitude comes through the helper
    // rather than v.get_brightness(), because only the helper carries the
    // transition state, without which a fade never moves. Both arrive linear,
    // since gamma lives in the component.
    float bright;
    state->current_values_as_brightness(&bright);
    panel_->set_light(v.is_on(), v.get_red(), v.get_green(), v.get_blue(), bright);
  }

 protected:
  Aip33628Panel *panel_{nullptr};
};

}  // namespace aip33628
}  // namespace esphome
