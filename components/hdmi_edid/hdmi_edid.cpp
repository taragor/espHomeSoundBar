#include "hdmi_edid.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cstring>

namespace esphome {
namespace hdmi_edid {

static const char *const TAG = "hdmi_edid";

bool HdmiEdid::read_eeprom_(uint8_t *out) {
  // Point the EEPROM's address counter at 0, then read sequentially in small chunks.
  uint8_t addr = 0;
  if (this->write(&addr, 1) != i2c::ERROR_OK)
    return false;
  for (size_t off = 0; off < EDID_SIZE; off += 16) {
    if (this->read(out + off, 16) != i2c::ERROR_OK)
      return false;
  }
  return true;
}

void HdmiEdid::fail_(const char *why) {
  ESP_LOGW(TAG, "%s - retrying in %u s", why, (unsigned) (RETRY_MS / 1000));
  this->status_set_warning();
  this->state_ = State::FAILED;
  this->last_ms_ = millis();
}

void HdmiEdid::setup() {
  this->edid_ = build_edid(this->params_);
  this->state_ = State::READ_BACK;
  this->last_ms_ = millis();
}

void HdmiEdid::loop() {
  uint32_t now = millis();
  switch (this->state_) {
    case State::READ_BACK: {
      uint8_t cur[EDID_SIZE];
      if (!this->read_eeprom_(cur)) {
        this->fail_("EEPROM did not answer (power? wiring? level shifter?)");
        return;
      }
      if (!this->force_write_ && std::memcmp(cur, this->edid_.data(), EDID_SIZE) == 0) {
        ESP_LOGI(TAG, "EEPROM already holds the current EDID");
        this->status_clear_warning();
        this->state_ = State::DONE;
        return;
      }
      ESP_LOGI(TAG, "EEPROM content differs, writing %u bytes", (unsigned) EDID_SIZE);
      this->next_page_ = 0;
      this->page_retries_ = 0;
      this->last_ms_ = now - WRITE_CYCLE_MS;
      this->state_ = State::WRITING;
      break;
    }
    case State::WRITING: {
      if (now - this->last_ms_ < WRITE_CYCLE_MS)
        return;
      size_t base = this->next_page_ * PAGE_SIZE;
      uint8_t buf[1 + PAGE_SIZE];
      buf[0] = static_cast<uint8_t>(base);
      std::memcpy(buf + 1, this->edid_.data() + base, PAGE_SIZE);
      this->last_ms_ = now;
      if (this->write(buf, sizeof(buf)) != i2c::ERROR_OK) {
        // The chip NACKs while it is still busy with the previous cycle: poll again.
        if (++this->page_retries_ > MAX_PAGE_RETRIES)
          this->fail_("EEPROM write failed");
        return;
      }
      this->page_retries_ = 0;
      this->wrote_ = true;
      if (++this->next_page_ * PAGE_SIZE >= EDID_SIZE)
        this->state_ = State::VERIFY;
      break;
    }
    case State::VERIFY: {
      if (now - this->last_ms_ < WRITE_CYCLE_MS)
        return;
      uint8_t cur[EDID_SIZE];
      if (!this->read_eeprom_(cur) || std::memcmp(cur, this->edid_.data(), EDID_SIZE) != 0) {
        this->fail_("EEPROM verify failed");
        return;
      }
      ESP_LOGI(TAG, "EDID written and verified");
      this->status_clear_warning();
      this->state_ = State::DONE;
      break;
    }
    case State::FAILED:
      if (now - this->last_ms_ >= RETRY_MS)
        this->state_ = State::READ_BACK;
      break;
    case State::DONE:
      this->disable_loop();
      break;
  }
}

void HdmiEdid::dump_config() {
  ESP_LOGCONFIG(TAG, "HDMI EDID EEPROM:");
  LOG_I2C_DEVICE(this);
  ESP_LOGCONFIG(TAG, "  Soundbar will learn physical address: %X.%X.%X.%X", (this->params_.physical_address >> 12) & 0xF,
                (this->params_.physical_address >> 8) & 0xF, (this->params_.physical_address >> 4) & 0xF,
                this->params_.physical_address & 0xF);
  ESP_LOGCONFIG(TAG, "  Monitor name: %s", this->params_.monitor_name.c_str());
  ESP_LOGCONFIG(TAG, "  Force write: %s", YESNO(this->force_write_));
}

}  // namespace hdmi_edid
}  // namespace esphome
