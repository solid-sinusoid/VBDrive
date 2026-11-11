#pragma once
#include "stm32g4xx_hal.h"
#include "voltbro/utils.hpp"
class GenericEncoder {
protected:
    bool last_error = false;
    encoder_data value = 0;
    int revolutions = 0;
public:
    const encoder_data CPR;
    const bool is_inverted;
    GenericEncoder(encoder_data c, bool inv, bool, int) : CPR(c), is_inverted(inv) {}
    virtual HAL_StatusTypeDef init() { return HAL_OK; }
    virtual void update_value() {}
    virtual bool healthy() const { return true; }
    encoder_data get_value() const { return value; }
    void incr_revolutions() { ++revolutions; }
    void decr_revolutions() { --revolutions; }
};
