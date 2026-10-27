#pragma once
#include "app.h"
#include "config/config.hpp"
#include "state_manager/state_manager.h"
#include "profiling.hpp"
#include "fdcan_tx_policy.hpp"
#include "firmware_crc.hpp"
#include <voltbro/motors/bldc/vbdrive/vbdrive.hpp>
#include <cyphal/node/node_info_handler.h>
#include <cyphal/node/registers_handler.hpp>
#include <cyphal/node/registers_utils.hpp>
#include <cyphal/providers/G4CAN.h>

#include <uavcan/node/Mode_1_0.hpp>
#include <uavcan/primitive/Empty_1_0.hpp>
#include <uavcan/primitive/array/Real32_1_0.hpp>
#include <uavcan/primitive/scalar/Real32_1_0.hpp>
#include <uavcan/si/unit/angular_velocity/Scalar_1_0.hpp>
#include <uavcan/si/unit/angle/Scalar_1_0.hpp>
#include <uavcan/si/unit/torque/Scalar_1_0.hpp>
#include <uavcan/si/unit/voltage/Scalar_1_0.hpp>
#include <voltbro/foc/command_2_0.hpp>
#include <voltbro/foc/sync_2_0.hpp>
#include <voltbro/foc/command_status_1_0.hpp>
#include <voltbro/foc/Servo_1_0.hpp>
#include <voltbro/foc/state_simple_1_0.hpp>


extern FDCAN_HandleTypeDef hfdcan1;
extern VBInverter motor_inverter;
std::shared_ptr<CyphalInterface> get_interface();

static constexpr CanardPortID FOC_COMMAND_PORT = 2300;
static constexpr CanardPortID FOC_SYNC_PORT = 3809;
static constexpr CanardPortID FOC_COMMAND_STATUS_PORT = 3810;
static constexpr CanardPortID FOC_STATE_PORT = 3811;
static constexpr CanardPortID SERVO_PORT = 3407;

void publish_command_status(const CommandStatus& status) {
    static CanardTransferID transfer_id = 0;
    voltbro_foc_command_status_1_0 msg{};
    msg.cycle_id = status.cycle_id;
    msg.status = static_cast<uint8_t>(status.status);
    msg.reason = static_cast<uint8_t>(status.reason);
    msg.apply_offset_microsecond = status.apply_offset_microsecond;
    const auto pending = hfdcan1.Instance->TXBRP & FDCAN_TXBRP_TRP;
    if (vbdrive::fdcan::should_preempt_for_critical_status(pending, HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1))) {
        (void)HAL_FDCAN_AbortTxRequest(&hfdcan1, pending);
    }
    get_interface()->send_msg(&msg, FOC_COMMAND_STATUS_PORT, &transfer_id,
                              DEFAULT_TIMEOUT_MICROS, CanardPriorityImmediate);
    get_interface()->process_canard_tx(false);
}

void publish_foc_statuses() {
    for (unsigned count = 0; count < 8; ++count) {
        std::optional<CommandStatus> status;
        CRITICAL_SECTION({ status = foc_cycle_sync.pop_status(); })
        if (!status) break;
        publish_command_status(*status);
    }
}

void in_loop_reporting(millis current_t) {
    auto motor = get_motor();
    if (motor == nullptr) {
        return;
    }

    static millis report_time = 0;
    static bool stator_temperature_initialized = false;
    static float filtered_stator_temperature_c = NAN;
    static millis last_stator_temperature_update_ms = 0;
    if (static_cast<millis>(current_t - report_time) >= 5U) {
        report_time = current_t; // One fresh sample; never replay overdue slots.
        voltbro_foc_state_simple_1_0 state_msg = {};

        state_msg.timestamp.microsecond = system_time();

        state_msg.angle.radian = motor->get_angle();
        state_msg.velocity.radian_per_second = motor->get_velocity();
        // OFF must not report retained current/torque estimates as energized output.
        if (motor->is_on()) {
            state_msg._torque.newton_meter = motor->get_torque();
            state_msg.current.ampere = motor->get_working_current();
        }
        state_msg.bus_voltage.volt = motor_inverter.get_busV();
        state_msg.mcu_temp.kelvin = motor_inverter.get_mcu_temperature() + 273.15f;
        const float raw_stator_temperature_c = motor_inverter.get_stator_temperature();
        if (!std::isfinite(raw_stator_temperature_c) ||
            raw_stator_temperature_c < -40.0f || raw_stator_temperature_c > 200.0f) {
            state_msg.stator_temp.kelvin = NAN;
        } else {
            // Telemetry only: rising temperatures are immediate; suppress cold spikes.
            if (!stator_temperature_initialized || raw_stator_temperature_c >= filtered_stator_temperature_c) {
                stator_temperature_initialized = true;
                filtered_stator_temperature_c = raw_stator_temperature_c;
            } else {
                const float max_drop = static_cast<millis>(current_t - last_stator_temperature_update_ms) / 1000.0f;
                filtered_stator_temperature_c = std::fmax(raw_stator_temperature_c, filtered_stator_temperature_c - max_drop);
            }
            last_stator_temperature_update_ms = current_t;
            state_msg.stator_temp.kelvin = filtered_stator_temperature_c + 273.15f;
        }
        state_msg.has_fault.value = false; // DRV_FAULT integration is deferred, as for is_fault.

        static CanardTransferID state_transfer_id = 0;
        get_interface()->send_msg(&state_msg, FOC_STATE_PORT, &state_transfer_id, 1000);
        VBDRIVE_PROFILE_COUNT(state_messages_queued)
    }
}

class FOCCommandSub: public AbstractSubscription<voltbro_foc_command_2_0> {
public:
    FOCCommandSub(InterfacePtr interface, CanardPortID port_id): AbstractSubscription<voltbro_foc_command_2_0>(interface, port_id) {
        sub.transfer_id_timeout_usec = 0U; // cycle_id owns replay protection, not a stale CAN session.
    };
    void accept(CanardRxTransfer* transfer) override {
        // These sealed motion types must not use DSDL's implicit zero extension.
        if (transfer->payload_size != voltbro_foc_command_2_0_SERIALIZATION_BUFFER_SIZE_BYTES_) {
            record_invalid_command();
            return;
        }
        voltbro_foc_command_2_0 msg{};
        size_t size = transfer->payload_size;
        if (voltbro_foc_command_2_0_deserialize_(&msg, static_cast<const uint8_t*>(transfer->payload), &size) < 0) {
            record_invalid_command();
            return;
        }
        handler(msg, transfer);
    }
    void handler(const voltbro_foc_command_2_0& msg, CanardRxTransfer* transfer) override {
        if (!transfer || transfer->metadata.remote_node_id != 42) return;
        VBDRIVE_PROFILE_HANDLER(mit_handler_timing);
        const FOCTarget target{
            .torque = msg._torque.newton_meter, .angle = msg.angle.radian,
            .velocity = msg.velocity.radian_per_second, .angle_kp = msg.angle_kp.value,
            .velocity_kp = msg.velocity_kp.value};
        const auto motor = get_motor();
        const bool valid = get_app_manager().is_app_running() && motor &&
            motor->get_status() == VBDriveStatus::NOMINAL && motor->is_foc_point_valid(target) &&
            std::isfinite(msg.I_kp.value) && std::isfinite(msg.I_ki.value);
        StageResult result;
        CRITICAL_SECTION({
            result = foc_cycle_sync.stage(CycleCommand{
                .cycle_id = msg.cycle_id,
                .target = {target.torque, target.angle, target.velocity, target.angle_kp, target.velocity_kp},
                .current_kp = msg.I_kp.value, .current_ki = msg.I_ki.value}, valid, transfer->timestamp_usec);
        })
        const bool is_valid = result == StageResult::Staged;
        VBDRIVE_PROFILE_RESULT(mit, is_valid)
        if (!is_valid) {
            record_invalid_command();
        } else {
            foc_cycle_sync.note_staged_status_published(msg.cycle_id);
            publish_command_status({msg.cycle_id, StatusCode::Staged, StatusReason::None, 0});
        }
    }
};

class FOCSyncSub: public AbstractSubscription<voltbro_foc_sync_2_0> {
public:
    FOCSyncSub(InterfacePtr interface, CanardPortID port_id): AbstractSubscription<voltbro_foc_sync_2_0>(interface, port_id) {
        sub.transfer_id_timeout_usec = 0U;
    };
    void accept(CanardRxTransfer* transfer) override {
        if (transfer->payload_size != voltbro_foc_sync_2_0_SERIALIZATION_BUFFER_SIZE_BYTES_) {
            record_invalid_command();
            return;
        }
        voltbro_foc_sync_2_0 msg{};
        size_t size = transfer->payload_size;
        if (voltbro_foc_sync_2_0_deserialize_(&msg, static_cast<const uint8_t*>(transfer->payload), &size) < 0) {
            record_invalid_command();
            return;
        }
        handler(msg, transfer);
    }
    void handler(const voltbro_foc_sync_2_0& msg, CanardRxTransfer* transfer) override {
        if (!transfer || transfer->metadata.remote_node_id != 42 || !get_app_manager().is_app_running()) return;
        const auto motor = get_motor();
        if (!motor || motor->get_status() != VBDriveStatus::NOMINAL) return;
        SyncResult result;
        CRITICAL_SECTION({
            result = foc_cycle_sync.on_sync(msg.cycle_id, static_cast<SyncPhase>(msg.phase), transfer->timestamp_usec);
            if (foc_cycle_sync.mode() == SyncMode::Immediate && result != SyncResult::Rejected) {
                foc_cycle_sync.immediate_marker(msg.cycle_id, transfer->timestamp_usec);
            }
        })
        if (result == SyncResult::Rejected) record_invalid_command();
    }
};

class ServoSub: public AbstractSubscription<voltbro_foc_Servo_1_0> {
public:
    ServoSub(InterfacePtr interface, CanardPortID port_id): AbstractSubscription<voltbro_foc_Servo_1_0>(interface, port_id) {};
    void handler(const voltbro_foc_Servo_1_0& msg, CanardRxTransfer*) override {
        VBDRIVE_PROFILE_HANDLER(servo_handler_timing);
        if (!apply_servo_command(msg.control_type, msg.set_point_value, msg.command_idx.count != 0,
                                 msg.command_idx.count ? msg.command_idx.elements[0] : 0)) {
            record_invalid_command();
        }
    }
};

// NOTE: underlying CanardRxSubscriptions are HUGE - 552 bytes each. C++ wrapper size is negligible in comparison
ReservedObject<NodeInfoReader> node_info_reader;
ReservedObject<RegistersHandler<PARAMETER_CATALOG.size(), StaticRegisters<PARAMETER_CATALOG.size()>>> registers_handler;
ReservedObject<FOCCommandSub> foc_command_sub;
ReservedObject<FOCSyncSub> foc_sync_sub;
ReservedObject<ServoSub> servo_sub;

static void handle_parameter_register(
    size_t index,
    const uavcan_register_Value_1_0& v_in,
    uavcan_register_Value_1_0& v_out,
    RegisterAccessResponse& response
) {
    const auto& definition = PARAMETER_CATALOG[index];
    response.persistent = definition.is_persistent;
    response._mutable = definition.is_mutable;
    if (definition.id == ParameterId::SYNC_MODE && get_motor() && get_motor()->is_on()) response._mutable = false;

    if (response._mutable && v_in._tag_ != REGISTER_EMPTY_TAG) {
        ParameterValue requested{};
        bool parsed = false;
        switch (definition.type) {
            case ParameterType::REAL32:
                parsed = parse_register_real32(v_in, requested.emplace<float>());
                break;
            case ParameterType::NATURAL32:
                parsed = parse_register_natural32(v_in, requested.emplace<uint32_t>());
                if (!parsed) {
                    int32_t signed_value = 0;
                    parsed = parse_register_integer32(v_in, signed_value) && signed_value >= 0;
                    if (parsed) requested = static_cast<uint32_t>(signed_value);
                }
                break;
            case ParameterType::INTEGER32:
                parsed = parse_register_integer32(v_in, requested.emplace<int32_t>());
                break;
            case ParameterType::BIT:
                parsed = parse_register_bit(v_in, requested.emplace<bool>());
                if (!parsed) {
                    int32_t signed_value = 0;
                    uint32_t unsigned_value = 0;
                    float real_value = 0;
                    if (parse_register_integer32(v_in, signed_value)) {
                        requested = signed_value == 1;
                        parsed = signed_value == 0 || signed_value == 1;
                    } else if (parse_register_natural32(v_in, unsigned_value)) {
                        requested = unsigned_value == 1;
                        parsed = unsigned_value == 0 || unsigned_value == 1;
                    } else if (parse_register_real32(v_in, real_value)) {
                        requested = real_value == 1;
                        parsed = real_value == 0 || real_value == 1;
                    }
                }
                break;
            case ParameterType::STRING:
                if (v_in._tag_ == REGISTER_STRING_TAG) {
                    requested = std::string_view(
                        reinterpret_cast<const char*>(v_in._string.value.elements),
                        v_in._string.value.count);
                    parsed = true;
                }
                break;
        }
        if (parsed) {
            auto& config = get_app_manager().get_committed_config();
            const auto write_result = definition.is_persistent
                ? write_persistent_parameter(config, definition.id, requested, false)
                : write_runtime_parameter(definition.id, requested);
            if (write_result == ParameterWriteResult::OK && definition.is_persistent) {
                get_app_manager().mark_config_changed();
            }
        }
    }

    ParameterValue current{};
    if (!read_parameter(get_app_manager().get_committed_config(), definition.id, current)) {
        v_out._tag_ = REGISTER_EMPTY_TAG;
        v_out.empty = {};
        return;
    }
    switch (definition.type) {
        case ParameterType::REAL32:
            fill_register_real32(v_out, std::get<float>(current));
            break;
        case ParameterType::NATURAL32:
            fill_register_natural32(v_out, std::get<uint32_t>(current));
            break;
        case ParameterType::INTEGER32:
            fill_register_integer32(v_out, std::get<int32_t>(current));
            break;
        case ParameterType::BIT:
            fill_register_bit(v_out, std::get<bool>(current));
            break;
        case ParameterType::STRING:
            fill_register_string(v_out, std::get<std::string_view>(current));
            break;
    }
}

void setup_subscriptions() {
    auto cyphal_interface = get_interface();

    HAL_FDCAN_ConfigGlobalFilter(
        &hfdcan1,
        FDCAN_REJECT,
        FDCAN_REJECT,
        FDCAN_REJECT_REMOTE,
        FDCAN_REJECT_REMOTE
    );

    const auto node_id = get_app_manager().get_node_id();
    foc_cycle_sync.set_mode(static_cast<SyncMode>(get_app_manager().get_committed_config().app.sync_mode));
    registers_handler.create(
        StaticRegisters<PARAMETER_CATALOG.size()>{PARAMETER_NAMES, handle_parameter_register},
        cyphal_interface
    );

    node_info_reader.create(
        cyphal_interface,
        "org.voltbro.vbdrive",
        uavcan_node_Version_1_0{1, 0},
        uavcan_node_Version_1_0{1, 0},
        uavcan_node_Version_1_0{VBDRIVE_VERSION_MAJOR, VBDRIVE_VERSION_MINOR},
        VBDRIVE_VCS_REVISION_ID
    );
    node_info_reader->set_image_crc(firmware_crc64(
        reinterpret_cast<const uint8_t*>(0x08003000U), 0x0801F800U - 0x08003000U));

    foc_cycle_sync.set_applied_retry_slot(node_id);
    foc_sync_sub.create(cyphal_interface, FOC_SYNC_PORT);
    servo_sub.create(cyphal_interface, SERVO_PORT + node_id);
    foc_command_sub.create(cyphal_interface, FOC_COMMAND_PORT + node_id);

    HAL_IMPORTANT(apply_filter(
        0,
        &hfdcan1,
        foc_command_sub->make_filter(node_id),
        FDCAN_FILTER_TO_RXFIFO1
    ))

    HAL_IMPORTANT(apply_filter(
        1,
        &hfdcan1,
        registers_handler->make_filter(node_id)
    ))

    HAL_IMPORTANT(apply_filter(
        2,
        &hfdcan1,
        node_info_reader->make_filter(node_id)
    ))

    HAL_IMPORTANT(apply_filter(
        3,
        &hfdcan1,
        foc_sync_sub->make_filter(node_id),
        FDCAN_FILTER_TO_RXFIFO1
    ))
    HAL_IMPORTANT(apply_filter(4, &hfdcan1, servo_sub->make_filter(node_id)))
}
