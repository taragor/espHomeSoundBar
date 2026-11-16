#include "hdmi_edid.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cstring>

namespace esphome {
namespace hdmi_edid {

static const char *const TAG = "hdmi_edid";
static const uint32_t PREF_KEY = 0xED1D0001UL;

static uint32_t fnv1a(const uint8_t *data, size_t len) {
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < len; i++) {
    h ^= data[i];
    h *= 16777619UL;
  }
  return h != 0 ? h : 1;  // 0 means "not programmed" in the stored value
}

// Does anything answer at 0x50? (sets the address pointer to 0 and reads one byte)
bool HdmiEdid::probe_presence_() {
  uint8_t addr = 0, b = 0;
  return this->write(&addr, 1) == i2c::ERROR_OK && this->read(&b, 1) == i2c::ERROR_OK;
}

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

void HdmiEdid::remember_(uint32_t value) { this->pref_.save(&value); }

// Called whenever we learn something about the EEPROM; keeps the flash copy in step.
void HdmiEdid::observe_(bool connected, bool matches) {
  this->connected_ = connected;
  this->matches_ = connected && matches;
  if (connected) {
    this->last_error_.clear();
    this->stored_ok_ = this->matches_;
    this->remember_(this->matches_ ? this->edid_hash_ : 0);
  }
  this->publish_();
}

void HdmiEdid::fail_(const char *why) {
  ESP_LOGW(TAG, "%s", why);
  this->last_error_ = std::string("Programming failed: ") + why;
  this->state_ = State::IDLE;
  this->matches_ = false;
  this->stored_ok_ = false;
  this->remember_(0);
  this->last_probe_ = millis();
  this->publish_();
}

void HdmiEdid::publish_() {
  std::string status;
  if (this->state_ != State::IDLE) {
    status = "Programming...";
  } else if (this->connected_) {
    if (this->matches_)
      status = "EEPROM connected, EDID OK";
    else if (!this->last_error_.empty())
      status = this->last_error_;
    else
      status = "EEPROM connected, needs programming";
  } else if (!this->last_error_.empty()) {
    status = this->last_error_;
  } else if (this->stored_ok_) {
    status = "EEPROM not connected (programmed earlier)";
  } else {
    status = "EEPROM not connected (programming not confirmed)";
  }

  if (this->connected_sensor_ != nullptr)
    this->connected_sensor_->publish_state(this->connected_);
  if (this->programmed_sensor_ != nullptr)
    this->programmed_sensor_->publish_state(this->is_programmed());
  if (this->status_sensor_ != nullptr && status != this->last_status_) {
    this->status_sensor_->publish_state(status);
    this->last_status_ = status;
  }
}

void HdmiEdid::program() {
  if (this->state_ != State::IDLE) {
    ESP_LOGW(TAG, "Already programming");
    return;
  }
  this->last_error_.clear();
  if (!this->probe_presence_()) {
    ESP_LOGW(TAG, "No EEPROM answers: set the jumpers to programming mode");
    this->observe_(false, false);
    this->last_error_ = "Cannot program: EEPROM not connected (set jumpers to programming mode)";
    this->publish_();
    return;
  }
  ESP_LOGI(TAG, "Programming %u bytes", (unsigned) EDID_SIZE);
  this->next_page_ = 0;
  this->page_retries_ = 0;
  this->last_ms_ = millis() - WRITE_CYCLE_MS;
  this->connected_ = true;
  this->state_ = State::WRITING;
  this->publish_();
}

void HdmiEdid::setup() {
  this->edid_ = build_edid(this->params_);
  this->edid_hash_ = fnv1a(this->edid_.data(), EDID_SIZE);
  this->pref_ = global_preferences->make_preference<uint32_t>(PREF_KEY);
  uint32_t saved = 0;
  if (this->pref_.load(&saved))
    this->stored_ok_ = (saved == this->edid_hash_);
  this->last_probe_ = millis() - this->check_interval_;  // probe on the first loop
  this->publish_();
}

void HdmiEdid::loop() {
  uint32_t now = millis();
  switch (this->state_) {
    case State::IDLE: {
      if (now - this->last_probe_ < this->check_interval_)
        return;
      this->last_probe_ = now;
      if (!this->probe_presence_()) {
        if (this->connected_) {
          ESP_LOGI(TAG, "EEPROM disconnected");
          this->observe_(false, false);
        }
        return;
      }
      if (this->connected_)
        return;  // still there, nothing to do
      uint8_t cur[EDID_SIZE];
      if (!this->read_eeprom_(cur))
        return;
      bool match = std::memcmp(cur, this->edid_.data(), EDID_SIZE) == 0;
      ESP_LOGI(TAG, "EEPROM connected, EDID %s", match ? "matches" : "differs (press the program button)");
      this->observe_(true, match);
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
          this->fail_("write failed, check the jumpers");
        return;
      }
      this->page_retries_ = 0;
      if (++this->next_page_ * PAGE_SIZE >= EDID_SIZE)
        this->state_ = State::VERIFY;
      break;
    }
    case State::VERIFY: {
      if (now - this->last_ms_ < WRITE_CYCLE_MS)
        return;
      uint8_t cur[EDID_SIZE];
      if (!this->read_eeprom_(cur) || std::memcmp(cur, this->edid_.data(), EDID_SIZE) != 0) {
        this->fail_("verify failed");
        return;
      }
      ESP_LOGI(TAG, "EDID written and verified");
      this->state_ = State::IDLE;
      this->last_probe_ = now;
      this->observe_(true, true);
      break;
    }
  }
}

void HdmiEdid::dump_config() {
  ESP_LOGCONFIG(TAG, "HDMI EDID EEPROM:");
  LOG_I2C_DEVICE(this);
  ESP_LOGCONFIG(TAG, "  Soundbar will learn physical address: %X.%X.%X.%X", (this->params_.physical_address >> 12) & 0xF,
                (this->params_.physical_address >> 8) & 0xF, (this->params_.physical_address >> 4) & 0xF,
                this->params_.physical_address & 0xF);
  ESP_LOGCONFIG(TAG, "  Monitor name: %s", this->params_.monitor_name.c_str());
  ESP_LOGCONFIG(TAG, "  Check interval: %u ms", (unsigned) this->check_interval_);
  ESP_LOGCONFIG(TAG, "  Programmed earlier with these settings: %s", YESNO(this->stored_ok_));
}

}  // namespace hdmi_edid
}  // namespace esphome
