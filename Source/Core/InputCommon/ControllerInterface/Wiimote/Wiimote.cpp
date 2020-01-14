// Copyright 2020 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "InputCommon/ControllerInterface/Wiimote/Wiimote.h"

#include "Common/BitUtils.h"
#include "Common/Logging/Log.h"
#include "Common/MathUtil.h"
#include "Core/Config/SYSCONFSettings.h"
#include "Core/HW/WiimoteEmu/ExtensionPort.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"

namespace ciface::Wiimote
{
template <typename T>
class Button final : public Core::Device::Input
{
public:
  Button(const T* value, std::common_type_t<T> mask, std::string name)
      : m_value(*value), m_mask(mask), m_name(std::move(name))
  {
  }

  std::string GetName() const override { return m_name; }

  ControlState GetState() const override { return (m_value & m_mask) != 0; }

private:
  const T& m_value;
  const T m_mask;
  const std::string m_name;
};

template <typename T>
class AnalogInput : public Core::Device::Input
{
public:
  AnalogInput(const T* value, std::string name, ControlState range)
      : m_value(*value), m_name(std::move(name)), m_range(range)
  {
  }

  std::string GetName() const override { return m_name; }

  ControlState GetState() const final override { return ControlState(m_value) / m_range; }

protected:
  const T& m_value;
  const std::string m_name;
  const ControlState m_range;
};

// TODO: rename.
template <typename T>
class UndetectableAnalogInput final : public AnalogInput<T>
{
public:
  using AnalogInput<T>::AnalogInput;

  bool IsDetectable() override { return false; }
};

// TODO: Rename or kill. `SignedInput`?
class StickInput final : public AnalogInput<float>
{
public:
  using AnalogInput<float>::AnalogInput;

  std::string GetName() const override { return std::string(m_name) + (m_range < 0 ? '-' : '+'); }
};

class Motor final : public Core::Device::Output
{
public:
  Motor(ControlState* value) : m_value(*value) {}

  std::string GetName() const override { return "Motor"; }

  void SetState(ControlState state) override { m_value = state; }

private:
  ControlState& m_value;
};

template <typename T>
void Device::QueueReport(T&& report, std::function<void(ErrorCode)> ack_handler)
{
  // Maintain proper rumble state.
  report.rumble = m_rumble;

  m_wiimote->QueueReport(report);

  if (ack_handler)
    AddAckHandler(report.REPORT_ID, std::move(ack_handler));
}

// TODO: Kill if not needed.
void Init()
{
}

void Shutdown()
{
}

void PopulateDevices()
{
  std::lock_guard<std::mutex> wm_lk(WiimoteReal::g_wiimotes_mutex);

  int index = 0;
  for (auto& wiimote : WiimoteReal::g_wiimote_pool)
  {
    if (!wiimote->Connect(index))
    {
      WARN_LOG(WIIMOTE, "WiiRemote: Failed to connect.");
      continue;
    }

    // TODO: Ugly call needed for our silly real wiimote interface to have a valid channel.
    constexpr u8 report[] = {WiimoteReal::WR_SET_REPORT | WiimoteReal::BT_OUTPUT,
                             u8(OutputReportID::Rumble), 0};
    wiimote->InterruptChannel(1, report, sizeof(report));

    g_controller_interface.AddDevice(std::make_shared<Device>(std::move(wiimote), index));

    ++index;
  }

  WiimoteReal::g_wiimote_pool.clear();
}

Device::Device(std::unique_ptr<WiimoteReal::Wiimote> wiimote, u8 index)
    : m_wiimote(std::move(wiimote)), m_index(index)
{
  static constexpr u16 button_masks[] = {
      Wiimote::BUTTON_A,     Wiimote::BUTTON_B,    Wiimote::BUTTON_ONE,  Wiimote::BUTTON_TWO,
      Wiimote::BUTTON_MINUS, Wiimote::BUTTON_PLUS, Wiimote::BUTTON_HOME,
  };

  static constexpr const char* const button_names[] = {
      "Button A", "Button B", "Button 1", "Button 2", "Button -", "Button +", "Button HOME",
  };

  for (std::size_t i = 0; i != std::size(button_masks); ++i)
    AddInput(new Button(&m_core_data.hex, button_masks[i], button_names[i]));

  static constexpr u16 dpad_masks[] = {
      Wiimote::PAD_UP,
      Wiimote::PAD_DOWN,
      Wiimote::PAD_LEFT,
      Wiimote::PAD_RIGHT,
  };

  // TODO: naming?
  for (std::size_t i = 0; i != std::size(dpad_masks); ++i)
    AddInput(new Button(&m_core_data.hex, dpad_masks[i], named_directions[i]));

  static constexpr std::array<std::array<const char*, 2>, 3> accel_names = {{
      {"Accel Left", "Accel Right"},
      {"Accel Backward", "Accel Forward"},
      {"Accel Up", "Accel Down"},
  }};

  for (std::size_t i = 0; i != m_accel_data.data.size(); ++i)
  {
    AddInput(new UndetectableAnalogInput<float>(&m_accel_data.data[i], accel_names[i][0], 1));
    AddInput(new UndetectableAnalogInput<float>(&m_accel_data.data[i], accel_names[i][1], -1));
  }

  // TODO: naming? Up Down Left Right?
  static constexpr const char* const ir_names[] = {"Cursor X", "Cursor Y"};

  for (std::size_t i = 0; i != std::size(ir_names); ++i)
  {
    // TODO: Should these be detectable?
    AddInput(new StickInput(&m_ir_state.center_position.data[i], ir_names[i], -1.f));
    AddInput(new StickInput(&m_ir_state.center_position.data[i], ir_names[i], 1.f));
  }

  // TODO: naming? Up Down Left Right?
  static constexpr const char* const point_names[] = {"Point X", "Point Y"};
  for (std::size_t i = 0; i != std::size(point_names); ++i)
  {
    // TODO: Should these be detectable?
    AddInput(new StickInput(&m_ir_state.pointer_position.data[i], point_names[i], -1.f));
    AddInput(new StickInput(&m_ir_state.pointer_position.data[i], point_names[i], 1.f));
  }

  // TODO: Hide, Hidden, Visible? IR, Point, Cursor?
  AddInput(new Button(&m_ir_state.is_hidden, true, "Cursor Hide"));

  // TODO: Add these inputs only after M+ is activated.
  static constexpr std::array<std::array<const char*, 2>, 3> gyro_names = {{
      {"Gyro Pitch Up", "Gyro Pitch Down"},
      {"Gyro Roll Left", "Gyro Roll Right"},
      {"Gyro Yaw Right", "Gyro Yaw Left"},
  }};

  for (std::size_t i = 0; i != m_accel_data.data.size(); ++i)
  {
    AddInput(
        new UndetectableAnalogInput<float>(&m_mplus_state.gyro_data.data[i], gyro_names[i][0], 1));
    AddInput(
        new UndetectableAnalogInput<float>(&m_mplus_state.gyro_data.data[i], gyro_names[i][1], -1));
  }

  // TODO: Add these inputs only when nunchuck is present.
  using WiimoteEmu::Nunchuk;
  const std::string nunchuk_prefix = "Nunchuk ";

  // TODO: naming?
  AddInput(new Button(&m_nunchuk_state.buttons, Nunchuk::BUTTON_C, nunchuk_prefix + "Button C"));
  AddInput(new Button(&m_nunchuk_state.buttons, Nunchuk::BUTTON_Z, nunchuk_prefix + "Button Z"));

  // TODO: Should all X/Y input names be changed to up/down/left/right ?

  static constexpr const char* const nunchuk_stick_names[] = {"Stick X", "Stick Y"};
  for (std::size_t i = 0; i != std::size(nunchuk_stick_names); ++i)
  {
    AddInput(new StickInput(&m_nunchuk_state.stick.data[i], nunchuk_prefix + nunchuk_stick_names[i],
                            -1.f));
    AddInput(new StickInput(&m_nunchuk_state.stick.data[i], nunchuk_prefix + nunchuk_stick_names[i],
                            1.f));
  }

  for (std::size_t i = 0; i != m_accel_data.data.size(); ++i)
  {
    AddInput(new UndetectableAnalogInput<float>(&m_nunchuk_state.accel.data[i],
                                                nunchuk_prefix + accel_names[i][0], 1));
    AddInput(new UndetectableAnalogInput<float>(&m_nunchuk_state.accel.data[i],
                                                nunchuk_prefix + accel_names[i][1], -1));
  }

  // TODO: Add these inputs only when classic controller is present.
  using WiimoteEmu::Classic;
  const std::string classic_prefix = "Classic ";

  static constexpr u16 classic_dpad_masks[] = {
      Classic::PAD_UP,
      Classic::PAD_DOWN,
      Classic::PAD_LEFT,
      Classic::PAD_RIGHT,
  };

  for (std::size_t i = 0; i != std::size(classic_dpad_masks); ++i)
    AddInput(new Button(&m_classic_state.buttons, classic_dpad_masks[i],
                        classic_prefix + named_directions[i]));

  static constexpr u16 classic_button_masks[] = {
      Classic::BUTTON_A,     Classic::BUTTON_B,    Classic::BUTTON_X,    Classic::BUTTON_Y,
      Classic::TRIGGER_L,    Classic::TRIGGER_R,   Classic::BUTTON_ZL,   Classic::BUTTON_ZR,
      Classic::BUTTON_MINUS, Classic::BUTTON_PLUS, Classic::BUTTON_HOME,
  };

  static constexpr const char* const classic_button_names[] = {
      "Button A", "Button B", "Button X", "Button Y", "L",           "R",
      "ZL",       "ZR",       "Button -", "Button +", "Button HOME",
  };

  for (std::size_t i = 0; i != std::size(classic_button_masks); ++i)
    AddInput(new Button(&m_classic_state.buttons, classic_button_masks[i],
                        classic_prefix + classic_button_names[i]));

  static constexpr const char* const classic_stick_names[][2] = {
      {"Left Stick X", "Left Stick Y"}, {"Right Stick X", "Right Stick Y"}};

  for (std::size_t s = 0; s != std::size(m_classic_state.sticks); ++s)
  {
    for (std::size_t i = 0; i != std::size(m_classic_state.sticks[0].data); ++i)
    {
      AddInput(new StickInput(&m_classic_state.sticks[s].data[i],
                              classic_prefix + classic_stick_names[s][i], -1.f));
      AddInput(new StickInput(&m_classic_state.sticks[s].data[i],
                              classic_prefix + classic_stick_names[s][i], 1.f));
    }
  }

  AddInput(new AnalogInput(&m_classic_state.triggers[0], classic_prefix + "L-Analog", 1.f));
  AddInput(new AnalogInput(&m_classic_state.triggers[1], classic_prefix + "R-Analog", 1.f));

  // Specialty inputs:
  AddInput(new UndetectableAnalogInput<u8>(
      &m_battery, "Battery", WiimoteCommon::MAX_BATTERY_LEVEL / ciface::BATTERY_INPUT_MAX_VALUE));
  // TODO: naming of these:
  AddInput(new UndetectableAnalogInput<WiimoteEmu::ExtensionNumber>(
      &m_extension_number_input, "Attached Extension", WiimoteEmu::ExtensionNumber(1)));
  AddInput(new UndetectableAnalogInput<bool>(&m_mplus_attached_input, "Attached MotionPlus", 1));

  // Output:
  AddOutput(new Motor(&m_rumble_level));
}

Device::~Device()
{
  std::lock_guard<std::mutex> wm_lk(WiimoteReal::g_wiimotes_mutex);
  m_wiimote->EmuStop();
  WiimoteReal::g_wiimote_pool.emplace_back(std::move(m_wiimote));
}

Device::Device(Device&&) = default;

std::string Device::GetName() const
{
  return "Wii Remote";
}

std::string Device::GetSource() const
{
  return "Bluetooth";
}

void Device::RunTasks()
{
  if (IsPerformingTask())
    return;

  // TODO:
  // disable + mute speaker

  // Request status.
  if (Clock::now() >= m_status_outdated_time)
  {
    QueueReport(OutputReportRequestStatus());

    AddReportHandler([this](const InputReportStatus& status) {
      DEBUG_LOG(WIIMOTE, "WiiRemote: Received requested status.");
      ProcessStatusReport(status);
    });

    return;
  }

  // Set LEDs.
  // TODO: Support LEDs beyond 4
  const auto desired_leds = (1 << m_index);
  if (m_leds != desired_leds)
  {
    OutputReportLeds rpt = {};
    rpt.ack = 1;
    rpt.leds = desired_leds;

    QueueReport(rpt, [this, desired_leds](ErrorCode result) {
      if (result != ErrorCode::Success)
        return;

      INFO_LOG(WIIMOTE, "WiiRemote: Set LEDs.");

      m_leds = desired_leds;
    });

    return;
  }

  // Set reporting mode to one that supports every component.
  constexpr auto desired_reporting_mode = InputReportID::ReportCoreAccelIR10Ext6;
  if (m_reporting_mode != desired_reporting_mode)
  {
    OutputReportMode mode = {};
    mode.ack = 1;
    mode.mode = desired_reporting_mode;
    QueueReport(mode, [this](ErrorCode error) {
      if (error != ErrorCode::Success)
        return;

      m_reporting_mode = desired_reporting_mode;

      INFO_LOG(WIIMOTE, "WiiRemote: Set reporting mode.");
    });

    return;
  }

  // Read accelerometer calibration.
  if (!m_accel_calibration.has_value())
  {
    constexpr u16 accel_calibration_addr = 0x16;

    ReadData(AddressSpace::EEPROM, 0, accel_calibration_addr, sizeof(AccelerometerCalibration),
             [this](ReadResponse response) {
               if (!response)
                 return;

               INFO_LOG(WIIMOTE, "WiiRemote: Read accelerometer calibration.");

               auto& calibration_data = *response;

               m_accel_calibration =
                   Common::BitCastPtr<AccelerometerCalibration>(calibration_data.data());

               WiimoteEmu::UpdateCalibrationDataChecksum(calibration_data, 1);

               // We could potentially try the second block at 0x26 if the checksum is bad.
               if (m_accel_calibration->checksum != calibration_data.back())
                 WARN_LOG(WIIMOTE, "WiiRemote: Bad accelerometer calibration checksum.");
             });

    return;
  }

  if (!m_ir_state.IsFullyConfigured())
  {
    ConfigureIRCamera();

    return;
  }

  if (!m_speaker_configured)
  {
    ConfigureSpeaker();

    return;
  }
  else
  {
#if 0
    std::vector<u8> speaker_data;

    u16 sample;
    // TODO: hacks.
    while (speaker_data.size() < 20 && m_speaker_file.read((char*)&sample, sizeof(sample)))
    {
      speaker_data.push_back(sample / 0x100);
    }

    if (!speaker_data.empty())
    {
      // TODO: magic number.
      // WriteData(AddressSpace::I2CBus, 0x51, 0x00, speaker_data, [this](ErrorCode response) {
      //   if (response != ErrorCode::Success)
      //   {
      //     WARN_LOG(WIIMOTE, "Bad speaker write: %d", int(response));
      //     return;
      //   }
      // });

      OutputReportSpeakerData rpt = {};
      rpt.length = speaker_data.size();
      std::copy(speaker_data.begin(), speaker_data.end(), rpt.data);
      QueueReport(rpt);
    }
#endif
  }

  // Perform the following tasks only after M+ is settled.
  if (IsWaitingForMotionPlus())
    return;

  // Read the "active" extension ID. (This also gives us the current M+ mode)
  // This will fail on an un-intialized other extension.
  // But extension initialization is the same as M+ de-activation so we must try this first.
  if (m_extension_port == true &&
      (!IsMotionPlusStateKnown() || (!IsMotionPlusActive() && !m_extension_id.has_value())))
  {
    constexpr u16 encryption_addr = 0xfb;
    constexpr u8 encryption_value = 0x00;

    // First disable encryption. Note this is a no-op when performed on the M+.
    WriteData(AddressSpace::I2CBus, WiimoteEmu::ExtensionPort::REPORT_I2C_SLAVE, encryption_addr,
              {encryption_value}, [this](ErrorCode error) {
                if (error != ErrorCode::Success)
                  return;

                ReadActiveExtensionID();
              });

    return;
  }

  constexpr u16 init_addr = 0xf0;
  constexpr u8 init_value = 0x55;

  // Initialize "active" extension if ID was not recognized.
  // Note this is done before M+ setup to determine the required passthrough mode.
  if (m_extension_id == ExtensionID::Unsupported)
  {
    // Note that this signal also DE-activates a M+.
    WriteData(AddressSpace::I2CBus, WiimoteEmu::ExtensionPort::REPORT_I2C_SLAVE, init_addr,
              {init_value}, [this](ErrorCode result) {
                INFO_LOG(WIIMOTE, "WiiRemote: Initialized extension: %d.", int(result));

                m_extension_id = std::nullopt;
              });

    return;
  }

  // The following tasks require a known M+ state.
  if (!IsMotionPlusStateKnown())
    return;

  // We now know the status of the M+.
  // Updating it too frequently results off/on flashes on mode change.
  m_mplus_attached_input = IsMotionPlusActive();

  // Extension removal status is known here. Attachment status is updated after the ID is read.
  if (m_extension_port != true)
    m_extension_number_input = WiimoteEmu::ExtensionNumber::NONE;

  // Periodically try to activate an inactive M+.
  if (!IsMotionPlusActive() && m_mplus_desired_mode.has_value() &&
      m_mplus_state.current_mode != m_mplus_desired_mode)
  {
    WriteData(AddressSpace::I2CBus, WiimoteEmu::MotionPlus::INACTIVE_DEVICE_ADDR, init_addr,
              {init_value}, [this](ErrorCode result) {
                INFO_LOG(WIIMOTE, "WiiRemote: M+ initialization: %d", int(result));
                if (result != ErrorCode::Success)
                {
                  // No need for additional checks if an extension is attached.
                  // (not possible for M+ to become attached)
                  if (m_extension_port == true)
                    m_mplus_desired_mode = MotionPlusState::PassthroughMode{};
                  else
                    WaitForMotionPlus();

                  return;
                }

                TriggerMotionPlusModeChange();
              });

    return;
  }

  // Change active M+ passthrough mode.
  if (IsMotionPlusActive() && m_mplus_desired_mode.has_value() &&
      m_mplus_state.current_mode != m_mplus_desired_mode)
  {
    TriggerMotionPlusModeChange();

    return;
  }

  // Read passthrough extension ID.
  // This will also give us a desired M+ passthrough mode.
  if (IsMotionPlusActive() && m_mplus_state.passthrough_port == true && !m_extension_id.has_value())
  {
    // The M+ reads the passthrough ext ID and stores it at 0xf6,f8,f9.
    // TODO: magic numbers.
    ReadData(AddressSpace::I2CBus, WiimoteEmu::MotionPlus::ACTIVE_DEVICE_ADDR, 0xf6, 4,
             [this](ReadResponse response) {
               if (!response)
               {
                 WARN_LOG(WIIMOTE, "WiiRemote: Failed to read passthrough extension ID.");
                 return;
               }

               // Port status may have changed since the read was sent.
               // In which case this data read would succeed but be useless.
               if (m_mplus_state.passthrough_port != true)
                 return;

               auto& identifier = *response;

               ProcessExtensionID(identifier[2], identifier[0], identifier[3]);
             });

    return;
  }

  // The following tasks require M+ configuration to be done.
  if (!IsMotionPlusInDesiredMode())
    return;

  // Now that M+ config has settled we can update the extension number.
  // Updating it too frequently results off/on flashes on M+ mode change.
  UpdateExtensionNumberInput();

  constexpr u16 normal_calibration_addr = 0x20;

  // Read M+ calibration.
  if (IsMotionPlusActive() && !m_mplus_state.calibration.has_value())
  {
    ReadData(AddressSpace::I2CBus, WiimoteEmu::MotionPlus::ACTIVE_DEVICE_ADDR,
             normal_calibration_addr, sizeof(WiimoteEmu::MotionPlus::CalibrationData),
             [this](ReadResponse response) {
               if (!response)
                 return;

               INFO_LOG(WIIMOTE, "WiiRemote: Read M+ calibration.");

               // TODO: Try setting some hardcoded ranges once we have orientations.
               // The scaling seems to be bad. Returned rad/s are a bit high.

               WiimoteEmu::MotionPlus::CalibrationData calibration =
                   Common::BitCastPtr<WiimoteEmu::MotionPlus::CalibrationData>(response->data());

               const auto read_checksum = std::pair(calibration.crc32_lsb, calibration.crc32_msb);

               calibration.UpdateChecksum();

               m_mplus_state.SetCalibrationData(calibration);

               if (read_checksum != std::pair(calibration.crc32_lsb, calibration.crc32_msb))
               {
                 // We could potentially try another read or call the M+ unusable.
                 WARN_LOG(WIIMOTE, "WiiRemote: Bad M+ calibration checksum.");
               }
             });

    return;
  }

  // Read normal extension calibration.
  if ((m_extension_id == ExtensionID::Nunchuk && !m_nunchuk_state.calibration) ||
      (m_extension_id == ExtensionID::Classic && !m_classic_state.calibration))
  {
    // Extension calibration is normally at 0x20 but M+ reads and stores it at 0x40.
    constexpr u16 passthrough_calibration_addr = 0x40;

    const u16 calibration_addr =
        IsMotionPlusActive() ? passthrough_calibration_addr : normal_calibration_addr;
    constexpr u16 calibration_size = 0x10;

    ReadData(
        AddressSpace::I2CBus, WiimoteEmu::ExtensionPort::REPORT_I2C_SLAVE, calibration_addr,
        calibration_size, [this](ReadResponse response) {
          if (!response)
            return;

          INFO_LOG(WIIMOTE, "WiiRemote: Read extension calibration.");

          auto& calibration_data = *response;

          const auto read_checksum = std::pair(calibration_data[calibration_size - 2],
                                               calibration_data[calibration_size - 1]);

          WiimoteEmu::UpdateCalibrationDataChecksum(calibration_data, 2);

          if (read_checksum != std::pair(calibration_data[calibration_size - 2],
                                         calibration_data[calibration_size - 1]))
          {
            // We could potentially try another block or call the extension unusable.
            WARN_LOG(WIIMOTE, "WiiRemote: Bad extension calibration checksum.");
          }

          if (m_extension_id == ExtensionID::Nunchuk)
          {
            m_nunchuk_state.SetCalibrationData(
                Common::BitCastPtr<WiimoteEmu::Nunchuk::CalibrationData>(calibration_data.data()));
          }
          else if (m_extension_id == ExtensionID::Classic)
          {
            m_classic_state.SetCalibrationData(
                Common::BitCastPtr<WiimoteEmu::Classic::CalibrationData>(calibration_data.data()));
          }
        });

    return;
  }
}

void Device::UpdateExtensionNumberInput()
{
  switch (m_extension_id.value_or(ExtensionID::Unsupported))
  {
  case ExtensionID::Nunchuk:
    m_extension_number_input = WiimoteEmu::ExtensionNumber::NUNCHUK;
    break;
  case ExtensionID::Classic:
    m_extension_number_input = WiimoteEmu::ExtensionNumber::CLASSIC;
    break;
  case ExtensionID::Unsupported:
  default:
    m_extension_number_input = WiimoteEmu::ExtensionNumber::NONE;
    break;
  }
}

void Device::ProcessExtensionEvent(bool connected)
{
  // Reset extension state.
  m_nunchuk_state = {};
  m_classic_state = {};

  m_extension_id = std::nullopt;

  // We won't know the desired mode until we get the extension ID.
  if (connected)
    m_mplus_desired_mode = std::nullopt;
}

void Device::ProcessExtensionID(u8 id_0, u8 id_4, u8 id_5)
{
  // TODO: Provide an input for the current extension, used to set emu-ext on physical swap.

  if (id_4 == 0x00 && id_5 == 0x00)
  {
    INFO_LOG(WIIMOTE, "WiiRemote: Nunchuk is attached.");
    m_extension_id = ExtensionID::Nunchuk;

    m_mplus_desired_mode = MotionPlusState::PassthroughMode::Nunchuk;
  }
  else if (id_4 == 0x01 && id_5 == 0x01)
  {
    INFO_LOG(WIIMOTE, "WiiRemote: Classic Controller is attached.");
    m_extension_id = ExtensionID::Classic;

    m_mplus_desired_mode = MotionPlusState::PassthroughMode::Classic;
  }
  else
  {
    INFO_LOG(WIIMOTE, "WiiRemote: Unknown extension: %d %d %d.", id_0, id_4, id_5);
    m_extension_id = ExtensionID::Unsupported;
  }
}

void Device::MotionPlusState::SetCalibrationData(
    const WiimoteEmu::MotionPlus::CalibrationData& data)
{
  INFO_LOG(WIIMOTE, "WiiRemote: Set M+ calibration.");

  calibration.emplace();

  calibration->fast = data.fast;
  calibration->slow = data.slow;
}

void Device::NunchukState::SetCalibrationData(const WiimoteEmu::Nunchuk::CalibrationData& data)
{
  INFO_LOG(WIIMOTE, "WiiRemote: Set Nunchuk calibration.");

  calibration.emplace();

  calibration->stick = data.GetStick();
  calibration->accel = data.GetAcceleration();
}

void Device::ClassicState::SetCalibrationData(const WiimoteEmu::Classic::CalibrationData& data)
{
  INFO_LOG(WIIMOTE, "WiiRemote: Set Classic Controller calibration.");

  calibration.emplace();

  calibration->left_stick = data.GetLeftStick();
  calibration->right_stick = data.GetRightStick();
  calibration->triggers = {{data.GetLeftTrigger(), data.GetRightTrigger()}};
}

void Device::ReadActiveExtensionID()
{
  constexpr u16 ext_id_addr = 0xfa;
  constexpr u16 ext_id_size = 6;

  ReadData(AddressSpace::I2CBus, WiimoteEmu::ExtensionPort::REPORT_I2C_SLAVE, ext_id_addr,
           ext_id_size, [this](ReadResponse response) {
             if (!response)
               return;

             auto& identifier = *response;

             // Check for M+ ID.
             if (identifier[5] == 0x05)
             {
               const auto passthrough_mode = MotionPlusState::PassthroughMode(identifier[4]);

               m_mplus_state.current_mode = passthrough_mode;

               INFO_LOG(WIIMOTE, "WiiRemote: M+ is active in mode: %d.", int(passthrough_mode));
             }
             else
             {
               m_mplus_state.current_mode = MotionPlusState::PassthroughMode{};

               ProcessExtensionID(identifier[0], identifier[4], identifier[5]);
             }
           });
}

bool Device::IRState::IsFullyConfigured() const
{
  return enabled && sensitivity_set && mode_set;
}

void Device::ConfigureIRCamera()
{
  struct IRSensitivityConfig
  {
    std::array<u8, 9> block1;
    std::array<u8, 2> block2;
  };

  // Data for Wii levels 1 to 5.
  static constexpr std::array<IRSensitivityConfig, 5> sensitivity_configs = {{
      {{0x02, 0x00, 0x00, 0x71, 0x01, 0x00, 0x64, 0x00, 0xfe}, {0xfd, 0x05}},
      {{0x02, 0x00, 0x00, 0x71, 0x01, 0x00, 0x96, 0x00, 0xb4}, {0xb3, 0x04}},
      {{0x02, 0x00, 0x00, 0x71, 0x01, 0x00, 0xaa, 0x00, 0x64}, {0x63, 0x03}},
      {{0x02, 0x00, 0x00, 0x71, 0x01, 0x00, 0xc8, 0x00, 0x36}, {0x35, 0x03}},
      {{0x07, 0x00, 0x00, 0x71, 0x01, 0x00, 0x72, 0x00, 0x20}, {0x1f, 0x03}},
  }};

  constexpr u16 block1_addr = 0x00;
  constexpr u16 block2_addr = 0x1a;

  // TODO: change sensitivity when config changes!

  // Wii stores values from 1 to 5.
  u32 requested_sensitivity = Config::Get(Config::SYSCONF_SENSOR_BAR_SENSITIVITY) - 1;

  // Default to the middle value on bad value.
  if (requested_sensitivity >= std::size(sensitivity_configs))
    requested_sensitivity = 2;

  const auto& sensitivity_config = sensitivity_configs[requested_sensitivity];

  // TODO: magic numbers for days!
  if (!m_ir_state.enabled)
  {
    OutputReportIRLogicEnable2 ir_logic2 = {};
    ir_logic2.ack = 1;
    ir_logic2.enable = 1;
    QueueReport(ir_logic2, [this](ErrorCode result) {
      if (result != ErrorCode::Success)
        return;

      OutputReportIRLogicEnable ir_logic = {};
      ir_logic.ack = 1;
      ir_logic.enable = 1;
      QueueReport(ir_logic, [this](ErrorCode ir_result) {
        if (ir_result != ErrorCode::Success)
          return;

        INFO_LOG(WIIMOTE, "WiiRemote: IR enabled.");

        m_ir_state.enabled = true;
      });
    });

    return;
  }

  if (!m_ir_state.sensitivity_set)
  {
    WriteData(AddressSpace::I2CBus, WiimoteEmu::CameraLogic::I2C_ADDR, 0x30, {0x01},
              [&](ErrorCode result) {
                if (result != ErrorCode::Success)
                  return;

                // TODO: would be nice if conversion to vector was not needed.
                std::vector block1(std::begin(sensitivity_config.block1),
                                   std::end(sensitivity_config.block1));

                WriteData(AddressSpace::I2CBus, WiimoteEmu::CameraLogic::I2C_ADDR, block1_addr,
                          block1, [&](ErrorCode block_result) {
                            if (block_result != ErrorCode::Success)
                              return;

                            // TODO: would be nice if conversion to vector was not needed.
                            std::vector block2(std::begin(sensitivity_config.block2),
                                               std::end(sensitivity_config.block2));

                            WriteData(AddressSpace::I2CBus, WiimoteEmu::CameraLogic::I2C_ADDR,
                                      block2_addr, block2, [this](ErrorCode block2_result) {
                                        if (block2_result != ErrorCode::Success)
                                          return;

                                        INFO_LOG(WIIMOTE, "WiiRemote: IR sensitivity set.");

                                        m_ir_state.sensitivity_set = true;
                                      });
                          });
              });

    return;
  }

  if (!m_ir_state.mode_set)
  {
    // We only support "Basic" mode (it's all that fits in ReportCoreAccelIR10Ext6).
    WriteData(AddressSpace::I2CBus, WiimoteEmu::CameraLogic::I2C_ADDR, 0x33,
              {WiimoteEmu::CameraLogic::IR_MODE_BASIC}, [this](ErrorCode mode_result) {
                if (mode_result != ErrorCode::Success)
                  return;

                WriteData(AddressSpace::I2CBus, WiimoteEmu::CameraLogic::I2C_ADDR, 0x30, {0x08},
                          [this](ErrorCode result) {
                            if (result != ErrorCode::Success)
                              return;

                            INFO_LOG(WIIMOTE, "WiiRemote: IR mode set.");

                            m_ir_state.mode_set = true;
                          });
              });
  }
}

void Device::ConfigureSpeaker()
{
  m_speaker_file.open("wave.raw");

  OutputReportSpeakerEnable spkr = {};
  spkr.enable = 1;
  QueueReport(spkr);

  OutputReportSpeakerMute mute = {};
  mute.enable = 1;
  QueueReport(mute);

  // TODO: magic number.
  WriteData(AddressSpace::I2CBus, 0x51, 0x09, {0x01}, [this](ErrorCode response) {
    if (response != ErrorCode::Success)
    {
      WARN_LOG(WIIMOTE, "Bad speaker write.");
      return;
    }

    // TODO: magic number.
    // TODO: writing 0x08 (per wiibrew) does one thing
    // writing 0x80 (like games do) does another thing (different sound.. sometimes..)
    // Even writing 0x00 works. is this the decoder state?
    WriteData(AddressSpace::I2CBus, 0x51, 0x01, {0x08}, [this](ErrorCode response) {
      if (response != ErrorCode::Success)
      {
        WARN_LOG(WIIMOTE, "Bad speaker write.");
        return;
      }

      const std::vector<u8> configuration = {0x00, 0x40, 0xd0, 0x07, 0xff, 0x00, 0x00};
      // const std::vector<u8> configuration = {0x00, 0x40, 0x40, 0x1f, 0x7f, 0x0c, 0x0e};

      // TODO: magic number.
      WriteData(AddressSpace::I2CBus, 0x51, 0x01, configuration, [this](ErrorCode response) {
        if (response != ErrorCode::Success)
        {
          WARN_LOG(WIIMOTE, "Bad speaker write.");
          return;
        }
      });

      // This is the "play" trigger, it seems only the first bit is checked.
      // (e.g. 0xfe does not trigger play but 0x05 does)
      // TODO: test if a write of 0 will pause after playing is started. (IT DOES!)
      // TODO: test if you can buffer data then play it afterward :/
      WriteData(AddressSpace::I2CBus, 0x51, 0x08, {0x01}, [this](ErrorCode response) {
        if (response != ErrorCode::Success)
        {
          WARN_LOG(WIIMOTE, "Bad speaker write.");
          return;
        }

        m_speaker_configured = true;
      });
    });
  });

  mute.enable = 0;
  QueueReport(mute);

  // TODO: magic number.
  // ReadData(AddressSpace::I2CBus, 0x51, 0, 0x10, [](ReadResponse response) {
  //   if (!response)
  //   {
  //     WARN_LOG(WIIMOTE, "Bad speaker read.");
  //     return;
  //   }

  //   WARN_LOG(WIIMOTE, "Speaker read: %s",
  //            ArrayToString(response->data(), response->size()).c_str());
  // });
}

void Device::TriggerMotionPlusModeChange()
{
  if (!m_mplus_desired_mode.has_value())
    return;

  const u8 passthrough_mode = u8(*m_mplus_desired_mode);

  const u8 device_addr = IsMotionPlusActive() ? WiimoteEmu::MotionPlus::ACTIVE_DEVICE_ADDR :
                                                WiimoteEmu::MotionPlus::INACTIVE_DEVICE_ADDR;

  WriteData(AddressSpace::I2CBus, device_addr, WiimoteEmu::MotionPlus::PASSTHROUGH_MODE_OFFSET,
            {passthrough_mode}, [this](ErrorCode activation_result) {
              INFO_LOG(WIIMOTE, "WiiRemote: M+ activation: %d", int(activation_result));

              WaitForMotionPlus();

              // Sometimes M+ activation does not cause an extension port event.
              // The mode will be read back after some time.
              m_mplus_state.current_mode = std::nullopt;
            });
}

void Device::TriggerMotionPlusCalibration()
{
  constexpr u16 calibration_trigger_addr = 0xf2;
  constexpr u8 calibration_trigger_value = 0x00;

  // This triggers a hardware "zero" calibration.
  // The effect is notiecable but output still strays from calibration data.
  // It seems we're better off just manually determining "zero".
  WriteData(AddressSpace::I2CBus, WiimoteEmu::MotionPlus::ACTIVE_DEVICE_ADDR,
            calibration_trigger_addr, {calibration_trigger_value}, [this](ErrorCode result) {
              INFO_LOG(WIIMOTE, "WiiRemote: M+ calibration trigger done: %d", int(result));
            });
}

bool Device::IsMotionPlusStateKnown() const
{
  return m_mplus_state.current_mode.has_value();
}

bool Device::IsMotionPlusActive() const
{
  return m_mplus_state.current_mode != MotionPlusState::PassthroughMode{};
}

bool Device::IsMotionPlusInDesiredMode() const
{
  return m_mplus_state.current_mode.has_value() &&
         (m_mplus_state.current_mode == m_mplus_desired_mode);
}

void Device::ProcessInputReport(WiimoteReal::Report& report)
{
  auto report_id = InputReportID(report[1]);

  for (auto it = m_report_handlers.begin(); true;)
  {
    if (it == m_report_handlers.end())
    {
      if (report_id == InputReportID::Status)
      {
        if (report.size() - 2 < sizeof(InputReportStatus))
        {
          WARN_LOG(WIIMOTE, "WiiRemote: Bad report size.");
        }
        else
        {
          ProcessStatusReport(Common::BitCastPtr<InputReportStatus>(report.data() + 2));
        }
      }
      else if (report_id < InputReportID::ReportCore)
      {
        WARN_LOG(WIIMOTE, "WiiRemote: Unhandled input report: %s",
                 ArrayToString(report.data(), report.size()).c_str());
      }

      break;
    }

    if (it->IsExpired())
    {
      WARN_LOG(WIIMOTE, "WiiRemote: Removing expired handler: %d.", int(it->GetRelevantID()));
      it = m_report_handlers.erase(it);
      continue;
    }

    const auto result = it->Handle(report);

    if (result == ReportHandler::HandlerResult::Handled)
    {
      it = m_report_handlers.erase(it);
      break;
    }

    ++it;
  }

  if (report_id < InputReportID::ReportCore)
  {
    // Normal input reports can be processed as "ReportCore".
    report_id = InputReportID::ReportCore;
  }
  else
  {
    // We can assume the last received input report is the current reporting mode.
    m_reporting_mode = InputReportID(report_id);
  }

  // TODO: Magic numbers.
  auto manipulator = MakeDataReportManipulator(report_id, report.data() + 2);

  if (manipulator->GetDataSize() > report.size() + 2)
  {
    WARN_LOG(WIIMOTE, "WiiRemote: Bad report size.");
    return;
  }

  // Read buttons.
  manipulator->GetCoreData(&m_core_data);

  // Process accel data.
  if (manipulator->HasAccel() && m_accel_calibration.has_value())
  {
    // FYI: This logic fails to properly handle the (never used) "interleaved" reports.
    DataReportManipulator::AccelData accel_data = {};
    manipulator->GetAccelData(&accel_data);

    m_accel_data = accel_data.GetAcceleration(*m_accel_calibration);
  }

  // Process IR data.
  if (manipulator->HasIR() && m_ir_state.IsFullyConfigured())
  {
    m_ir_state.ProcessData(
        Common::BitCastPtr<std::array<WiimoteEmu::IRBasic, 2>>(manipulator->GetIRDataPtr()));

    // Update oriented version of IR data.

    // TODO: Should the accelerometer be smoothed for this? YES, it is very shakey.
    // We could use gyro data too (Same math used for IMU cursor)
    const auto roll = std::atan2(m_accel_data.x, m_accel_data.z);

    const auto rotated_position =
        Common::Matrix33::RotateZ(-roll) *
        Common::Vec3(m_ir_state.center_position.x, m_ir_state.center_position.y, 0.f);
    m_ir_state.pointer_position = {rotated_position.x, rotated_position.y};
  }

  // Process extension data.
  if (IsMotionPlusStateKnown())
  {
    const auto ext_data = manipulator->GetExtDataPtr();
    const auto ext_size = manipulator->GetExtDataSize();

    // TODO: If M+ is not present, generate gyro data from accel/IR
    // Generalize ComplementaryFilter so it can take IR data with a forward normal.

    if (IsMotionPlusActive())
      ProcessMotionPlusExtensionData(ext_data, ext_size);
    else
      ProcessNormalExtensionData(ext_data, ext_size);
  }
}

void Device::IRState::ProcessData(const std::array<WiimoteEmu::IRBasic, 2>& data)
{
  // A better implementation might extrapolate points when they fall out of camera view.
  // But just averaging visible points actually seems to work very well.

  Common::Vec2 point_total;
  int point_count = 0;

  const auto camera_max = WiimoteEmu::IRObject{1024 - 1, 768 - 1};

  const auto add_point = [&](WiimoteEmu::IRObject point) {
    // Non-visible points are 0xFF-filled.
    if (point.y > camera_max.y)
      return;

    point_total += Common::Vec2(point);
    ++point_count;
  };

  for (auto& block : data)
  {
    add_point(block.GetObject1());
    add_point(block.GetObject2());
  }

  is_hidden = !point_count;

  if (point_count)
  {
    center_position =
        point_total / float(point_count) / Common::Vec2(camera_max) * -2.f + Common::Vec2(1, 1);
  }
}

void Device::ProcessMotionPlusExtensionData(const u8* ext_data, u32 ext_size)
{
  if (ext_size < sizeof(WiimoteEmu::MotionPlus::DataFormat))
    return;

  const WiimoteEmu::MotionPlus::DataFormat mplus_data =
      Common::BitCastPtr<WiimoteEmu::MotionPlus::DataFormat>(ext_data);

  const bool is_ext_connected = mplus_data.extension_connected;

  // Handle passthrough extension change.
  if (is_ext_connected != m_mplus_state.passthrough_port)
  {
    m_mplus_state.passthrough_port = is_ext_connected;

    INFO_LOG(WIIMOTE, "WiiRemote: M+ passthrough port event: %d.", is_ext_connected);

    // With no passthrough extension we'll be happy with the current mode.
    if (!is_ext_connected)
      m_mplus_desired_mode = m_mplus_state.current_mode;

    ProcessExtensionEvent(is_ext_connected);
  }

  if (mplus_data.is_mp_data)
  {
    // WARN_LOG(WIIMOTE, "m+ data: %s", ArrayToString(ext_data, ext_size).c_str());

    m_mplus_state.ProcessData(mplus_data);
    return;
  }

  if (!IsMotionPlusInDesiredMode())
  {
    DEBUG_LOG(WIIMOTE, "WiiRemote: Ignoring unwanted passthrough data.");
    return;
  }

  std::array<u8, sizeof(WiimoteEmu::Nunchuk::DataFormat)> data;
  std::copy_n(ext_data, ext_size, data.begin());

  // Undo bit-hacks of M+ passthrough.
  WiimoteEmu::MotionPlus::ReversePassthroughModifications(*m_mplus_state.current_mode, data.data());

  ProcessNormalExtensionData(data.data(), data.size());
}

void Device::ProcessNormalExtensionData(const u8* ext_data, u32 ext_size)
{
  if (m_extension_id == ExtensionID::Nunchuk)
  {
    if (ext_size < sizeof(WiimoteEmu::MotionPlus::DataFormat))
      return;

    const WiimoteEmu::Nunchuk::DataFormat nunchuk_data =
        Common::BitCastPtr<WiimoteEmu::Nunchuk::DataFormat>(ext_data);

    m_nunchuk_state.ProcessData(nunchuk_data);
  }
  else if (m_extension_id == ExtensionID::Classic)
  {
    if (ext_size < sizeof(WiimoteEmu::Classic::DataFormat))
      return;

    const WiimoteEmu::Classic::DataFormat cc_data =
        Common::BitCastPtr<WiimoteEmu::Classic::DataFormat>(ext_data);

    m_classic_state.ProcessData(cc_data);
  }
}

void Device::UpdateRumble()
{
  constexpr auto rumble_period = std::chrono::milliseconds(100);

  const auto on_time = std::chrono::duration_cast<Clock::duration>(rumble_period * m_rumble_level);
  const auto off_time = rumble_period - on_time;

  const auto now = Clock::now();

  if (m_rumble && (now < m_last_rumble_change + on_time || !off_time.count()))
    return;

  if (!m_rumble && (now < m_last_rumble_change + off_time || !on_time.count()))
    return;

  m_last_rumble_change = now;
  m_rumble ^= true;

  // Rumble flag will be set within QueueReport.
  QueueReport(OutputReportRumble{});
}

void Device::UpdateInput()
{
  UpdateRumble();
  RunTasks();

  WiimoteReal::Report report;
  while (m_wiimote->GetNextReport(&report))
  {
    if (report.size() < 4)
    {
      WARN_LOG(WIIMOTE, "WiiRemote: Bad report size.");
      continue;
    }

    ProcessInputReport(report);
    RunTasks();
  }
}

void Device::MotionPlusState::ProcessData(const WiimoteEmu::MotionPlus::DataFormat& data)
{
  // We need the calibration block read to know the sensor orientations.
  if (!calibration.has_value())
    return;

  // Unfortunately M+ calibration zero values are very poor.
  // We calibrate when we receive a few seconds of stable data.
  const auto unadjusted_gyro_data = data.GetData().GetAngularVelocity(*calibration);

  // Use zero-data calibration until acquired.
  const auto adjusted_gyro_data =
      unadjusted_gyro_data - m_dynamic_calibration.value_or(Common::Vec3{});

  constexpr auto UNSTABLE_ROTATION = float(MathUtil::TAU / 100);

  const bool is_stable = (adjusted_gyro_data - gyro_data).Length() < UNSTABLE_ROTATION;

  gyro_data = adjusted_gyro_data;

  // If we've yet to achieve calibration acquire one more quickly.
  const auto required_stable_frames = m_dynamic_calibration.has_value() ? 100u : 5u;

  if (is_stable)
  {
    if (++m_new_calibration_frames < required_stable_frames)
    {
      m_new_dynamic_calibration += unadjusted_gyro_data;
    }
    else
    {
      m_dynamic_calibration = m_new_dynamic_calibration / m_new_calibration_frames;
      m_new_dynamic_calibration = {};
      m_new_calibration_frames = 0;

      INFO_LOG(WIIMOTE, "WiiRemote: M+ applied dynamic calibration.");
    }
  }
  else
  {
    m_new_dynamic_calibration = {};
    m_new_calibration_frames = 0;
  }
}

bool Device::IsWaitingForMotionPlus() const
{
  // desired_mode.has_value() && (current_mode != desired_mode) &&
  return Clock::now() < m_mplus_wait_time;
}

void Device::WaitForMotionPlus()
{
  m_mplus_wait_time = Clock::now() + std::chrono::seconds{2};
}

void Device::NunchukState::ProcessData(const WiimoteEmu::Nunchuk::DataFormat& data)
{
  buttons = data.GetButtons();

  // Stick/accel require calibration data.
  if (!calibration.has_value())
    return;

  stick = data.GetStick().GetNormalizedValue(calibration->stick);
  accel = data.GetAccelData().GetNormalizedValue(calibration->accel) *
          float(MathUtil::GRAVITY_ACCELERATION);
}

void Device::ClassicState::ProcessData(const WiimoteEmu::Classic::DataFormat& data)
{
  buttons = data.GetButtons();

  // Sticks/triggers require calibration data.
  if (!calibration.has_value())
    return;

  sticks[0] = data.GetLeftStickValue().GetNormalizedValue(calibration->left_stick);
  sticks[1] = data.GetRightStickValue().GetNormalizedValue(calibration->right_stick);
  triggers[0] = data.GetLeftTriggerValue().GetNormalizedValue(calibration->triggers[0]);
  triggers[1] = data.GetRightTriggerValue().GetNormalizedValue(calibration->triggers[1]);
}

void Device::ReadData(AddressSpace space, u8 slave, u16 address, u16 size,
                      std::function<void(ReadResponse)> callback)
{
  OutputReportReadData read_data{};
  read_data.space = u8(space);
  read_data.slave_address = slave;
  read_data.address[0] = u8(address >> 8);
  read_data.address[1] = u8(address);
  read_data.size[0] = u8(size >> 8);
  read_data.size[1] = u8(size);
  QueueReport(read_data);

  AddReadDataReplyHandler(space, slave, address, size, {}, std::move(callback));
}

void Device::AddReadDataReplyHandler(AddressSpace space, u8 slave, u16 address, u16 size,
                                     std::vector<u8> starting_data,
                                     std::function<void(ReadResponse)> callback)
{
  AddReportHandler([this, space, slave, address, size, data = std::move(starting_data),
                    callback = std::move(callback)](const InputReportReadDataReply& reply) mutable {
    if (Common::swap16(reply.address) != address)
      return ReportHandler::HandlerResult::NotHandled;

    RemoveHandler(InputReportID::Ack);

    if (reply.error != u8(ErrorCode::Success))
    {
      INFO_LOG(WIIMOTE, "WiiRemote: Read error.");
      callback(ReadResponse{});

      return ReportHandler::HandlerResult::Handled;
    }

    const auto read_count = reply.size_minus_one + 1;

    data.insert(data.end(), reply.data, reply.data + read_count);

    if (read_count < size)
    {
      // We have more data to acquire.
      AddReadDataReplyHandler(space, slave, address + read_count, size - read_count,
                              std::move(data), std::move(callback));
    }
    else
    {
      DEBUG_LOG(WIIMOTE, "WiiRemote: Read complete.");
      // We have all the data.
      callback(std::move(data));
    }

    return ReportHandler::HandlerResult::Handled;
  });

  // Data read may return a busy ack.
  AddAckHandler(OutputReportID::ReadData, [this](ErrorCode result) {
    WARN_LOG(WIIMOTE, "WiiRemote: read error: %d.", int(result));
    RemoveHandler(InputReportID::ReadDataReply);
  });
}

// TODO: accept more than std::vector
void Device::WriteData(AddressSpace space, u8 slave, u16 address, const std::vector<u8>& data,
                       std::function<void(ErrorCode)> callback)
{
  OutputReportWriteData write_data = {};
  write_data.space = u8(space);
  write_data.slave_address = slave;
  write_data.address[0] = u8(address >> 8);
  write_data.address[1] = u8(address);

  constexpr auto MAX_DATA_SIZE = std::size(write_data.data);
  write_data.size = std::min(std::size(data), MAX_DATA_SIZE);

  std::copy_n(std::begin(data), write_data.size, write_data.data);

  // Writes of more than 16 bytes must be done in multiple reports.
  if (std::size(data) > MAX_DATA_SIZE)
  {
    auto next_write = [this, space, slave, address,
                       additional_data =
                           std::vector<u8>{std::begin(data) + MAX_DATA_SIZE, std::end(data)},
                       callback = std::move(callback)](ErrorCode result) mutable {
      if (result != ErrorCode::Success)
        callback(result);
      else
        WriteData(space, slave, address + MAX_DATA_SIZE, additional_data, std::move(callback));
    };

    AddAckHandler(OutputReportID::WriteData, std::move(next_write));
  }
  else
  {
    AddAckHandler(OutputReportID::WriteData, std::move(callback));
  }

  QueueReport(write_data);
}

// TODO: Make a report handler accept multiple handlers for different report IDs.
// Needed for ReadData where ReadDataReply or Ack can be returned.
template <typename T>
void Device::AddReportHandler(T&& handler)
{
  m_report_handlers.emplace_back(std::function(std::forward<T>(handler)));
}

void Device::AddAckHandler(OutputReportID report_id,
                           std::function<void(WiimoteCommon::ErrorCode)> callback)
{
  AddReportHandler([report_id, callback = std::move(callback)](const InputReportAck& reply) {
    if (reply.rpt_id != report_id)
      return ReportHandler::HandlerResult::NotHandled;

    callback(reply.error_code);

    return ReportHandler::HandlerResult::Handled;
  });
}

// TODO: The usage of this function is a bit ugly because it doesn't exactly match our handler.
void Device::RemoveHandler(InputReportID report_id)
{
  auto it = std::find_if(
      m_report_handlers.begin(), m_report_handlers.end(),
      [report_id](ReportHandler& handler) { return handler.GetRelevantID() == report_id; });

  if (it != m_report_handlers.end())
    m_report_handlers.erase(it);
}

template <typename T>
Device::ReportHandler::ReportHandler(std::function<void(const T&)> handler)
    : m_expired_time(Clock::now() + std::chrono::seconds{5}), m_relevant_id(T::REPORT_ID)
{
  m_callback = [handler = std::move(handler)](const WiimoteReal::Report& report) {
    if (report.size() - 2 < sizeof(T))
    {
      WARN_LOG(WIIMOTE, "WiiRemote: Bad report size: %d.", int(report.size()));
      return ReportHandler::HandlerResult::Handled;
    }

    handler(Common::BitCastPtr<T>(report.data() + 2));
    return ReportHandler::HandlerResult::Handled;
  };
}

template <typename T>
Device::ReportHandler::ReportHandler(std::function<HandlerResult(const T&)> handler)
    : m_expired_time(Clock::now() + std::chrono::seconds{5}), m_relevant_id(T::REPORT_ID)
{
  m_callback = [handler = std::move(handler)](const WiimoteReal::Report& report) {
    if (report.size() - 2 < sizeof(T))
    {
      WARN_LOG(WIIMOTE, "WiiRemote: Bad report size: %d.", int(report.size()));
      return ReportHandler::HandlerResult::Handled;
    }

    return handler(Common::BitCastPtr<T>(report.data() + 2));
  };
}

auto Device::ReportHandler::Handle(const WiimoteReal::Report& report) -> HandlerResult
{
  if (report[1] != u8(m_relevant_id))
    return HandlerResult::NotHandled;

  return m_callback(report);
}

bool Device::ReportHandler::IsExpired() const
{
  return Clock::now() >= m_expired_time;
}

InputReportID Device::ReportHandler::GetRelevantID() const
{
  return m_relevant_id;
}

bool Device::IsPerformingTask() const
{
  return !m_report_handlers.empty();
}

void Device::ProcessStatusReport(const InputReportStatus& status)
{
  // Update status periodically to keep battery level value up to date.
  m_status_outdated_time = Clock::now() + std::chrono::seconds(10);

  m_battery = status.battery;
  m_leds = status.leds;

  if (!status.ir)
    m_ir_state = {};

  const bool is_ext_connected = status.extension;

  // Handle extension port state change.
  if (is_ext_connected != m_extension_port)
  {
    INFO_LOG(WIIMOTE, "WiiRemote: Extension port event: %d.", is_ext_connected);

    m_extension_port = is_ext_connected;

    // Data reporting stops on an extension port event.
    m_reporting_mode = InputReportID::ReportDisabled;

    ProcessExtensionEvent(is_ext_connected);

    // The M+ is now in an unknown state.
    m_mplus_state = {};

    // TODO: Provide an input for the current M+ status to passthrough to emu-wm
    // We might not want the status to change so frequently like the real one does, tho.
    // Only change the status when not waiting on M+.

    if (is_ext_connected)
    {
      // We can assume the M+ is settled on an attachment event.
      m_mplus_wait_time = Clock::now();
    }
    else
    {
      // "Nunchuk" will be the most used mode and also works with no passthrough extension.
      m_mplus_desired_mode = MotionPlusState::PassthroughMode::Nunchuk;

      // If an extension is not connected the M+ is either disabled or resetting.
      m_mplus_state.current_mode = MotionPlusState::PassthroughMode{};
    }
  }
}

}  // namespace ciface::Wiimote
