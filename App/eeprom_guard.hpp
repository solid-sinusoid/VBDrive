#pragma once

#include <array>
#include <cstdint>
#include <cstring>

// Check before ConfigManager or sensor initialization can write any EEPROM byte.
template<class Storage>
bool eeprom_layout_safe(Storage& eeprom) {
    DriveConfig config{};
    if (eeprom.read(&config, CONFIG_PLACEMENT) != 0) return false;
    const auto* raw = reinterpret_cast<const uint8_t*>(&config);
    bool blank = true;
    for (size_t i = 0; i < sizeof(config); ++i) blank &= raw[i] == 0xff;
    if (!blank && (!voltbro_base_config_valid(&config.base) || config.app.type_id != VBDriveConfig::TYPE_ID ||
                   config.app.sync_mode > 1)) return false;
    uint32_t calibration_id = 0;
    std::array<uint8_t, 8> sensor{};
    if (eeprom.read(&calibration_id, CALIBRATION_PLACEMENT) != 0 ||
        eeprom.read(&sensor, IND_SENSOR_STATE_PLACEMENT) != 0) return false;
    uint32_t sensor_id = 0;
    std::memcpy(&sensor_id, sensor.data(), sizeof(sensor_id));
    if (blank || (calibration_id == UINT32_MAX && sensor_id == UINT32_MAX)) {
        // A blank config alone is insufficient: old calibration and sensor state
        // overlap the new address map, so require the complete used region blank.
        std::array<uint8_t, 64> block{};
        for (uint32_t address = 0; address <= IND_SENSOR_STATE_PLACEMENT; address += block.size()) {
            if (eeprom.read(&block, static_cast<uint16_t>(address)) != 0) return false;
            for (size_t i = 0; i < block.size(); ++i) {
                const auto byte_address = address + i;
                if (!blank && byte_address >= CONFIG_PLACEMENT &&
                    byte_address < CONFIG_PLACEMENT + sizeof(config)) continue;
                if (block[i] != 0xff) return false;
            }
        }
        return true;
    }
    // A persisted record is written only after sensor programming succeeded.
    if (sensor_id != InductiveSensor::State::TYPE_ID || sensor[4] != 1) return false;
    return calibration_id == CalibrationData::TYPE_ID || calibration_id == UINT32_MAX;
}
