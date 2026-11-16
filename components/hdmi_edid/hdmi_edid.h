#pragma once
#include <string>

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include "edid_builder.h"

namespace esphome {
namespace hdmi_edid {

// Programs an EDID image into the 24C02-style EEPROM that sits on the HDMI DDC lines.
// The soundbar (HDMI source) reads that EEPROM at I2C address 0x50 and so learns
// "I am a TV with LPCM audio, you are on HDMI port X".
//
// The EEPROM is wired through jumpers: in "programming" mode it is connected to the ESP,
// in "HDMI" mode it is connected to the soundbar. The component therefore never writes
// on its own. It only
//   * checks every few seconds whether an EEPROM answers on the bus ("connected"),
//   * compares its content with the generated EDID ("programmed"),
//   * writes it when program() is called (the Home Assistant button).
// The last verified result is remembered in flash, so with the jumpers in HDMI mode the
// component can still report "programmed earlier" and notice when the EDID settings in the
// YAML changed since then.
class HdmiEdid : public Component, public i2c::I2CDevice {
 public:
  void set_physical_address(uint16_t pa) { this->params_.physical_address = pa; }
  void set_monitor_name(const std::string &name) { this->params_.monitor_name = name; }
  void set_manufacturer(const std::string &m) { this->params_.manufacturer = m; }
  void set_check_interval(uint32_t ms) { this->check_interval_ = ms; }
  void set_connected_sensor(binary_sensor::BinarySensor *s) { this->connected_sensor_ = s; }
  void set_programmed_sensor(binary_sensor::BinarySensor *s) { this->programmed_sensor_ = s; }
  void set_status_sensor(text_sensor::TextSensor *s) { this->status_sensor_ = s; }

  // Start writing the EEPROM (no-op while busy; reports an error if no EEPROM answers).
  void program();

  bool is_connected() const { return this->connected_; }
  // True when the EEPROM holds the current EDID (or, with no EEPROM attached, when it was
  // verified earlier with identical settings).
  bool is_programmed() const { return this->connected_ ? this->matches_ : this->stored_ok_; }
  bool is_busy() const { return this->state_ != State::IDLE; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

 protected:
  enum class State : uint8_t { IDLE, WRITING, VERIFY };

  static constexpr size_t PAGE_SIZE = 8;         // 24C02 page size
  static constexpr uint32_t WRITE_CYCLE_MS = 6;  // tWR is 5 ms max
  static constexpr uint8_t MAX_PAGE_RETRIES = 20;

  bool probe_presence_();
  bool read_eeprom_(uint8_t *out);
  void observe_(bool connected, bool matches);
  void remember_(uint32_t value);
  void fail_(const char *why);
  void publish_();

  EdidParams params_;
  EdidBytes edid_{};
  uint32_t edid_hash_{1};
  ESPPreferenceObject pref_;

  uint32_t check_interval_{5000};
  State state_{State::IDLE};
  bool connected_{false};
  bool matches_{false};
  bool stored_ok_{false};
  std::string last_error_;
  std::string last_status_;
  size_t next_page_{0};
  uint8_t page_retries_{0};
  uint32_t last_ms_{0};
  uint32_t last_probe_{0};

  binary_sensor::BinarySensor *connected_sensor_{nullptr};
  binary_sensor::BinarySensor *programmed_sensor_{nullptr};
  text_sensor::TextSensor *status_sensor_{nullptr};
};

class ProgramButton : public button::Button, public Parented<HdmiEdid> {
 protected:
  void press_action() override { this->parent_->program(); }
};

}  // namespace hdmi_edid
}  // namespace esphome
