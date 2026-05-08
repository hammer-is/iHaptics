// Interface to control Simagic Haptic Pedal Reactors (HPRs) - only tested with P2000
// Code is a C++20 port of the original C# implementation https://github.com/mherbold/SimagicHPR

#pragma once
#include <Windows.h>
#include <string>
#include <functional>
#include <cstdint>

class HPR
{
public:
    enum class PedalsDevice
    {
        None,
        P500,
        P700,
        P1000,
        P2000
    };

    enum class Channel : uint8_t
    {
        Clutch = 0,
        Brake = 1,
        Throttle = 2
    };

    enum class State : uint8_t
    {
        Off = 0,
        On = 1
    };

    HPR() noexcept;
    ~HPR();

    // Returns which pedals were found (None if no match)
    PedalsDevice Initialize(bool enabled, std::function<void(const std::string&)> onDeviceFound = {});
    void Uninitialize() noexcept;

    // frequency: 0-50, amplitude: 0-100
    void VibratePedal(Channel channel, float frequency, float amplitude);

private:
    HANDLE OpenRawDeviceStream(const std::wstring& devicePath) const;
    std::wstring GetDeviceName(HANDLE hDevice) const;
    std::string GetFriendlyName(HANDLE hFile) const;

private:
    PedalsDevice _pedals;
    bool _initialized;
    HANDLE _deviceHandle;
    unsigned char _vibrateBuffer[64];
    int _lastState[3];
    int _lastFrequency[3];
    int _lastAmplitude[3];
};