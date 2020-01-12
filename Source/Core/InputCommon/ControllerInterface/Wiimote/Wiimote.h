// Copyright 2020 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <chrono>
// TODO: kill
#include <fstream>

#include "Core/HW/WiimoteCommon/DataReport.h"
#include "Core/HW/WiimoteCommon/WiimoteReport.h"
#include "Core/HW/WiimoteEmu/Camera.h"
#include "Core/HW/WiimoteEmu/Extension/Classic.h"
#include "Core/HW/WiimoteEmu/Extension/Nunchuk.h"
#include "Core/HW/WiimoteEmu/MotionPlus.h"
#include "Core/HW/WiimoteReal/WiimoteReal.h"
#include "InputCommon/ControllerInterface/Device.h"

namespace ciface
{
namespace Wiimote
{
using namespace WiimoteCommon;

void Init();
void Shutdown();
void PopulateDevices();

class Device final : public Core::Device
{
public:
  Device(std::unique_ptr<WiimoteReal::Wiimote> wiimote, u8 index);
  ~Device();

  Device(Device&&);

  std::string GetName() const override;
  std::string GetSource() const override;

  void UpdateInput() override;

private:
  using Clock = std::chrono::steady_clock;

  enum class ExtensionID
  {
    Nunchuk,
    Classic,
    Unsupported,
  };

  class MotionPlusState
  {
  public:
    void SetCalibrationData(const WiimoteEmu::MotionPlus::CalibrationData&);
    void ProcessData(const WiimoteEmu::MotionPlus::DataFormat&);

    using PassthroughMode = WiimoteEmu::MotionPlus::PassthroughMode;

    // State is unknown by default.
    std::optional<PassthroughMode> current_mode;

    // The last known state of the passthrough port flag.
    // Used to detect passthrough extension port events.
    std::optional<bool> passthrough_port;

    Common::Vec3 gyro_data = {};

    std::optional<WiimoteEmu::MotionPlus::CalibrationBlocks> calibration;

  private:
    // Used to perform realtime calibration.
    std::optional<Common::Vec3> m_dynamic_calibration = {};
    Common::Vec3 m_new_dynamic_calibration = {};
    u32 m_new_calibration_frames = 0;
  };

  struct NunchukState
  {
    void SetCalibrationData(const WiimoteEmu::Nunchuk::CalibrationData&);
    void ProcessData(const WiimoteEmu::Nunchuk::DataFormat&);

    Common::Vec2 stick = {};
    Common::Vec3 accel = {};

    u8 buttons = 0;

    struct Calibration
    {
      // TODO: Nicer declarations.
      decltype(std::declval<WiimoteEmu::Nunchuk::CalibrationData>().GetAcceleration()) accel;
      decltype(std::declval<WiimoteEmu::Nunchuk::CalibrationData>().GetStick()) stick;
    };

    std::optional<Calibration> calibration;
  };

  struct ClassicState
  {
    void SetCalibrationData(const WiimoteEmu::Classic::CalibrationData&);
    void ProcessData(const WiimoteEmu::Classic::DataFormat&);

    std::array<Common::Vec2, 2> sticks = {};
    std::array<float, 2> triggers = {};

    u16 buttons = 0;

    struct Calibration
    {
      // TODO: combine these?
      decltype(std::declval<WiimoteEmu::Classic::CalibrationData>().GetLeftStick()) left_stick;
      decltype(std::declval<WiimoteEmu::Classic::CalibrationData>().GetRightStick()) right_stick;

      std::array<ControllerEmu::TwoPointCalibration<u8, 8>, 2> triggers;
    };

    std::optional<Calibration> calibration;
  };

  struct IRState
  {
    bool IsFullyConfigured() const;
    void ProcessData(const std::array<WiimoteEmu::IRBasic, 2>&);

    bool enabled = false;
    bool sensitivity_set = false;
    bool mode_set = false;

    Common::Vec2 center_position = {};
    bool is_hidden = true;
  };

  class ReportHandler
  {
  public:
    enum class HandlerResult
    {
      Handled,
      NotHandled,
    };

    template <typename T>
    ReportHandler(std::function<void(const T&)> handler);

    template <typename T>
    ReportHandler(std::function<HandlerResult(const T&)> handler);

    HandlerResult Handle(const WiimoteReal::Report& report);

    bool IsExpired() const;
    InputReportID GetRelevantID() const;

  private:
    const Clock::time_point m_expired_time;
    const InputReportID m_relevant_id;
    std::function<HandlerResult(const WiimoteReal::Report& report)> m_callback;
  };

  // TODO: make parameter const
  void ProcessInputReport(WiimoteReal::Report& report);
  void ProcessMotionPlusExtensionData(const u8* data, u32 size);
  void ProcessNormalExtensionData(const u8* data, u32 size);
  void ProcessExtensionEvent(bool connected);
  void ProcessExtensionID(u8 id_0, u8 id_4, u8 id_5);
  void ProcessStatusReport(const InputReportStatus&);

  void RunTasks();

  bool IsPerformingTask() const;

  template <typename T>
  void QueueReport(T&& report, std::function<void(ErrorCode)> ack_handler = {});

  template <typename T>
  void AddReportHandler(T&& handler);

  void AddAckHandler(OutputReportID, std::function<void(ErrorCode)>);
  void RemoveHandler(InputReportID);

  using ReadResponse = std::optional<std::vector<u8>>;

  void ReadData(AddressSpace space, u8 slave, u16 address, u16 size,
                std::function<void(ReadResponse)> callback);

  void AddReadDataReplyHandler(AddressSpace space, u8 slave, u16 address, u16 size,
                               std::vector<u8> starting_data,
                               std::function<void(ReadResponse)> callback);

  void WriteData(AddressSpace space, u8 slave, u16 address, const std::vector<u8>& data,
                 std::function<void(ErrorCode)> callback);

  void ReadActiveExtensionID();
  void ConfigureIRCamera();
  void ConfigureSpeaker();

  void TriggerMotionPlusModeChange();
  void TriggerMotionPlusCalibration();

  bool IsMotionPlusStateKnown() const;
  bool IsMotionPlusActive() const;
  bool IsMotionPlusInDesiredMode() const;

  bool IsWaitingForMotionPlus() const;
  void WaitForMotionPlus();

  void UpdateRumble();

  std::unique_ptr<WiimoteReal::Wiimote> m_wiimote;

  const u8 m_index;

  // Buttons.
  DataReportManipulator::CoreData m_core_data = {};

  // Accelerometer.
  Common::Vec3 m_accel_data = {};
  std::optional<AccelerometerCalibration> m_accel_calibration;

  MotionPlusState m_mplus_state = {};
  NunchukState m_nunchuk_state = {};
  ClassicState m_classic_state = {};
  IRState m_ir_state = {};

  // Used to poll for M+ periodically and wait for it to reset.
  Clock::time_point m_mplus_wait_time = Clock::now();

  // The desired mode is set based on the attached normal extension.
  std::optional<MotionPlusState::PassthroughMode> m_mplus_desired_mode;

  // Status report is requested every so often to update the battery level.
  Clock::time_point m_status_outdated_time = Clock::now();
  u8 m_battery = 0;
  u8 m_leds = 0;

  bool m_speaker_configured = false;
  std::ifstream m_speaker_file;

  // The last known state of the extension port status flag.
  // Used to detect extension port events.
  std::optional<bool> m_extension_port;

  // Note this refers to the passthrough extension when M+ is active.
  std::optional<ExtensionID> m_extension_id;

  // Rumble state must be saved to set the proper flag in every output report.
  bool m_rumble = false;

  // For pulse of rumble motor to simulate multiple levels.
  ControlState m_rumble_level;
  Clock::time_point m_last_rumble_change = Clock::now();

  // Assume mode is disabled so one gets set.
  InputReportID m_reporting_mode = InputReportID::ReportDisabled;

  // Holds callbacks for output report replies.
  // TODO: make this vector?
  std::list<ReportHandler> m_report_handlers;
};

}  // namespace Wiimote
}  // namespace ciface
