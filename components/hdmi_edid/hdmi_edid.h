#pragma once
#include <vector>

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"

#include "edid_builder.h"

namespace esphome {
namespace hdmi_edid {

// Keeps an EDID image in the 24C02-style EEPROM that sits on the HDMI DDC lines.
// The soundbar (HDMI source) reads that EEPROM at I2C address 0x50 and so learns
// "I am a TV with LPCM audio, you are on HDMI port X". The ESP only writes the
// EEPROM when its content differs from the generated image, so after the first run
// the component only does a short verify read at boot.
class HdmiEdid : public Component, public i2c::I2CDevice {
 public:
  void set_physical_address(uint16_t pa) { this->params_.physical_address = pa; }
  void set_monitor_name(const std::string &name) { this->params_.monitor_name = name; }
  void set_manufacturer(const std::string &m) { this->params_.manufacturer = m; }
  void set_force_write(bool force) { this->force_write_ = force; }

  bool is_up_to_date() const { return this->state_ == State::DONE; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

 protected:
  enum class State : uint8_t { READ_BACK, WRITING, VERIFY, DONE, FAILED };

  static constexpr size_t PAGE_SIZE = 8;     // 24C02 page size
  static constexpr uint32_t WRITE_CYCLE_MS = 6;  // tWR is 5 ms max
  static constexpr uint32_t RETRY_MS = 10000;
  static constexpr uint8_t MAX_PAGE_RETRIES = 20;

  bool read_eeprom_(uint8_t *out);
  void fail_(const char *why);

  EdidParams params_;
  EdidBytes edid_{};
  bool force_write_{false};
  State state_{State::READ_BACK};
  size_t next_page_{0};
  uint8_t page_retries_{0};
  uint32_t last_ms_{0};
  bool wrote_{false};
};

}  // namespace hdmi_edid
}  // namespace esphome
