#include "stmpe610.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

#include <algorithm>

namespace esphome {
namespace stmpe610 {

static const char *const TAG = "stmpe610";

void STMPE610Component::setup() {
  ESP_LOGD(TAG, "setup");
  if (this->irq_pin_ != nullptr) {
    // The pin reports a touch with a falling edge. Unfortunately the pin goes also changes state
    // while the channels are read and wiring it as an interrupt is not straightforward and would
    // need careful masking. A GPIO poll is cheap so we'll just use that.

    this->irq_pin_->setup();  // INPUT
    this->irq_pin_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
    this->irq_pin_->setup();
    this->attach_interrupt_(this->irq_pin_, gpio::INTERRUPT_FALLING_EDGE);
  }
  this->spi_setup();
  this->version_ = this->get_version_();
  if (this->version_ != 0x811) {
    this->set_mode(spi::MODE1);
    this->version_ = this->get_version_();
    if (this->version_ != 0x811) {
      this->version_ = 0xffff;
      return;
    }
  }

  // Init stolen from https://github.com/adafruit/Adafruit_STMPE610

  this->write_reg_8(STMPE_SYS_CTRL1, STMPE_SYS_CTRL1_RESET);
  delay(10);

  for (uint8_t i = 0; i < 65; i++) {
    this->read_reg_8(i);
  }

  this->write_reg_8(STMPE_SYS_CTRL2, 0x0); // turn on clocks!
  this->write_reg_8(STMPE_TSC_CTRL,
                 STMPE_TSC_CTRL_XYZ | STMPE_TSC_CTRL_EN); // XYZ and enable!
  // Serial.println(readRegister8(STMPE_TSC_CTRL), HEX);
  this->write_reg_8(STMPE_INT_EN, STMPE_INT_EN_TOUCHDET);
  this->write_reg_8(STMPE_ADC_CTRL1, STMPE_ADC_CTRL1_10BIT |
                                      (0x6 << 4)); // 96 clocks per conversion
  this->write_reg_8(STMPE_ADC_CTRL2, STMPE_ADC_CTRL2_6_5MHZ);
  this->write_reg_8(STMPE_TSC_CFG, STMPE_TSC_CFG_4SAMPLE |
                                    STMPE_TSC_CFG_DELAY_1MS |
                                    STMPE_TSC_CFG_SETTLE_5MS);
  this->write_reg_8(STMPE_TSC_FRACTION_Z, 0x6);
  this->write_reg_8(STMPE_FIFO_TH, 1);
  this->write_reg_8(STMPE_FIFO_STA, STMPE_FIFO_STA_RESET);
  this->write_reg_8(STMPE_FIFO_STA, 0); // unreset
  this->write_reg_8(STMPE_TSC_I_DRIVE, STMPE_TSC_I_DRIVE_50MA);
  this->write_reg_8(STMPE_INT_STA, 0xFF); // reset all ints
  this->write_reg_8(STMPE_INT_CTRL,
                 STMPE_INT_CTRL_POL_HIGH | STMPE_INT_CTRL_ENABLE);
}

void STMPE610Component::update_touches() {
  while (!this->is_buffer_empty()) {
    int16_t x_raw { 0 };
    int16_t y_raw { 0 };
    int8_t z_raw { 0 };


    uint8_t data[4];
    for (uint8_t i = 0; i < 4; i++)
      data[i] = this->read_reg_8(0xD7);

    x_raw = (data[0] << 4) | (data[1] >> 4);
    y_raw = ((data[1] & 0x0F) << 8) | data[2];
    z_raw = data[3];


    ESP_LOGD(TAG, "Touchscreen Update [%d, %d], z = %d", x_raw, y_raw, z_raw);

    this->add_raw_touch_position_(0, x_raw, y_raw, z_raw);

  }

  if (this->is_buffer_empty())
    this->write_reg_8(STMPE_INT_STA, 0xFF); // reset all ints
    

  
#if 0
  int16_t data[6], x_raw, y_raw, z_raw;
  bool touch = false;

  enable();

  int16_t touch_pressure_1 = this->read_adc_(0xB1 /* touch_pressure_1 */);
  int16_t touch_pressure_2 = this->read_adc_(0xC1 /* touch_pressure_2 */);
  z_raw = touch_pressure_1 + 0Xfff - touch_pressure_2;
  ESP_LOGVV(TAG, "Touchscreen Update z = %d", z_raw);
  touch = (z_raw >= this->threshold_);
  if (touch) {
    read_adc_(0xD1 /* X */);  // dummy Y measure, 1st is always noisy
    data[0] = this->read_adc_(0x91 /* Y */);
    data[1] = this->read_adc_(0xD1 /* X */);  // make 3 x-y measurements
    data[2] = this->read_adc_(0x91 /* Y */);
    data[3] = this->read_adc_(0xD1 /* X */);
    data[4] = this->read_adc_(0x91 /* Y */);
  }

  data[5] = this->read_adc_(0xD0 /* X */);  // Last X touch power down

  disable();

  if (touch) {
    x_raw = best_two_avg(data[1], data[3], data[5]);
    y_raw = best_two_avg(data[0], data[2], data[4]);

    ESP_LOGD(TAG, "Touchscreen Update [%d, %d], z = %d", x_raw, y_raw, z_raw);

    this->add_raw_touch_position_(0, x_raw, y_raw, z_raw);
  }
#endif
}

void STMPE610Component::dump_config() {
  ESP_LOGCONFIG(TAG, "STMPE610:");

  LOG_PIN("  IRQ Pin: ", this->irq_pin_);
  ESP_LOGCONFIG(TAG, "  X min: %d", this->x_raw_min_);
  ESP_LOGCONFIG(TAG, "  X max: %d", this->x_raw_max_);
  ESP_LOGCONFIG(TAG, "  Y min: %d", this->y_raw_min_);
  ESP_LOGCONFIG(TAG, "  Y max: %d", this->y_raw_max_);

  ESP_LOGCONFIG(TAG, "  Swap X/Y: %s", YESNO(this->swap_x_y_));
  ESP_LOGCONFIG(TAG, "  Invert X: %s", YESNO(this->invert_x_));
  ESP_LOGCONFIG(TAG, "  Invert Y: %s", YESNO(this->invert_y_));

  ESP_LOGCONFIG(TAG, "  threshold: %d", this->threshold_);
  ESP_LOGCONFIG(TAG, "  srcver: 1");
  ESP_LOGCONFIG(TAG, "  hwver: %x", this->version_);

  LOG_UPDATE_INTERVAL(this);
}

// float STMPE610Component::get_setup_priority() const { return setup_priority::DATA; }


uint8_t STMPE610Component::read_reg_8(uint8_t reg) {
  // write register to device with 0x80 read flag
  enable();
  this->write_byte(0x80 | reg);
  delay(1);
  uint8_t value = this->read_byte();
  disable();
  return value;
}

void STMPE610Component::write_reg_8(uint8_t reg, uint8_t value) {
  // write register to device with 0x80 read flag
  enable();
  this->write_byte(reg);
  this->write_byte(value);
  disable();
}

uint16_t STMPE610Component::get_version_() {  // NOLINT

  uint16_t v = (this->read_reg_8(0) << 8 ) | this->read_reg_8(1);

  return v;
}

bool STMPE610Component::is_touched() {
  return this->reg_read_8(STMPE_TSC_CTRL) & 0x80;
}

bool STMPE610Component::is_buffer_empty() {
  return this->reg_read_8(STMPE_FIFO_STA) & STMPE_FIFO_STA_EMPTY;
}




}  // namespace stmpe610
}  // namespace esphome
