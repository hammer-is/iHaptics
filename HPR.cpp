// Interface to control Simagic Haptic Pedal Reactors (HPRs) - only tested with P2000
// Code is a C++20 port of the original C# implementation https://github.com/mherbold/SimagicHPR

#include "HPR.h"
#include <Windows.h>
#include <hidsdi.h>
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "user32.lib")

HPR::HPR() noexcept
    : _pedals(PedalsDevice::None),
    _initialized(false),
    _deviceHandle(INVALID_HANDLE_VALUE)
{
    ZeroMemory(_vibrateBuffer, sizeof(_vibrateBuffer));
    for (int i = 0; i < 3; ++i)
    {
        _lastState[i] = -1;
        _lastFrequency[i] = -1;
        _lastAmplitude[i] = -1;
    }
}

HPR::~HPR()
{
    Uninitialize();
}

static std::string WStringToString(const std::wstring& s)
{
    if (s.empty()) return {};
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0, NULL, NULL);
    std::string out(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], size_needed, NULL, NULL);
    return out;
}

HANDLE HPR::OpenRawDeviceStream(const std::wstring& devicePath) const
{
    // CreateFile expects the device path as a wide string; keep GENERIC_READ|GENERIC_WRITE to match original behavior
    HANDLE h = CreateFileW(devicePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

std::string HPR::GetFriendlyName(HANDLE hFile) const
{
    if (hFile == INVALID_HANDLE_VALUE) return {};

    auto ReadHidString = [&](BOOLEAN(*fn)(HANDLE, PVOID, ULONG)) -> std::wstring
        {
            const ULONG bufChars = 512;
            std::vector<wchar_t> buffer(bufChars);
            if (fn(hFile, buffer.data(), static_cast<ULONG>(buffer.size() * sizeof(wchar_t))))
            {
                return std::wstring(buffer.data());
            }
            return {};
        };

    std::wstring manufacturer = ReadHidString(HidD_GetManufacturerString);
    std::wstring product = ReadHidString(HidD_GetProductString);

    if (!manufacturer.empty() && !product.empty())
    {
        std::wstring combined = manufacturer + L" " + product;
        return WStringToString(combined);
    }

    if (!product.empty()) return WStringToString(product);
    if (!manufacturer.empty()) return WStringToString(manufacturer);

    return {};
}

std::wstring HPR::GetDeviceName(HANDLE hDevice) const
{
    // Query the required size first
    UINT requiredChars = 0;
    UINT rc = GetRawInputDeviceInfoW((HANDLE)hDevice, RIDI_DEVICENAME, nullptr, &requiredChars);
    if (rc != 0 || requiredChars == 0) return {};

    std::vector<wchar_t> buffer(requiredChars);
    rc = GetRawInputDeviceInfoW((HANDLE)hDevice, RIDI_DEVICENAME, buffer.data(), &requiredChars);
    if (rc == (UINT)-1) return {};

    return std::wstring(buffer.data());
}

HPR::PedalsDevice HPR::Initialize(bool enabled, std::function<void(const std::string&)> onDeviceFound)
{
    Uninitialize();

    UINT deviceCount = 0;
    UINT size = sizeof(RAWINPUTDEVICELIST);
    if (GetRawInputDeviceList(nullptr, &deviceCount, size) != 0 || deviceCount == 0)
    {
        return _pedals;
    }

    std::vector<RAWINPUTDEVICELIST> devices(deviceCount);
    if (GetRawInputDeviceList(devices.data(), &deviceCount, size) == (UINT)-1)
    {
        return _pedals;
    }

    HANDLE selectedDeviceHandle = nullptr;

    for (UINT i = 0; i < deviceCount; ++i)
    {
        RAWINPUTDEVICELIST& d = devices[i];

        // Get device info
        UINT cbSize = sizeof(RID_DEVICE_INFO);
        RID_DEVICE_INFO devInfo;
        ZeroMemory(&devInfo, sizeof(devInfo));
        devInfo.cbSize = cbSize;

        UINT res = GetRawInputDeviceInfoW(d.hDevice, RIDI_DEVICEINFO, &devInfo, &cbSize);
        if (res == (UINT)-1) continue;

        bool isHid = (devInfo.dwType == RIM_TYPEHID);
        if (!isHid) continue;

        auto vid = devInfo.hid.dwVendorId;
        auto pid = devInfo.hid.dwProductId;
        auto usagePage = devInfo.hid.usUsagePage;
        auto usage = devInfo.hid.usUsage;
        bool isGameCtrl = (usagePage == 0x01) && (usage == 0x04 || usage == 0x05);

        std::wstring deviceName = GetDeviceName(d.hDevice);
        std::string friendlyName;
        if (!deviceName.empty())
        {
            // Attempt to open temporarily to read strings
            HANDLE tmp = OpenRawDeviceStream(deviceName);
            if (tmp != INVALID_HANDLE_VALUE)
            {
                friendlyName = GetFriendlyName(tmp);
                CloseHandle(tmp);
            }
        }

        std::string shownName = !friendlyName.empty() ? friendlyName : WStringToString(deviceName);

        // Hex formatting lambda: outputs values like "0x3670" (zero-padded to 4 hex digits, uppercase)
        auto Hex = [](unsigned int v, int width = 4) -> std::string
        {
            std::ostringstream ss;
            ss << "0x" << std::hex << std::uppercase << std::setw(width) << std::setfill('0') << v;
            return ss.str();
        };

        if (onDeviceFound) //ToDo: Is this check necessary?
        {
            onDeviceFound("VID=" + Hex(vid) + ", PID=" + Hex(pid) + ", Name=" + shownName + ", IsGameCtrl=" + (isGameCtrl ? "true" : "false"));
        }

        if (isGameCtrl)
        {
            if (vid == 0x3670 && pid == 0x0903)
            {
                _pedals = PedalsDevice::P500;
                selectedDeviceHandle = d.hDevice;
            }
            else if (vid == 0x3670 && pid == 0x0905)
            {
                _pedals = PedalsDevice::P700;
                selectedDeviceHandle = d.hDevice;
            }
            else if (vid == 0x0483 && pid == 0x0525)
            {
                _pedals = PedalsDevice::P1000;
                selectedDeviceHandle = d.hDevice;
            }
            else if (vid == 0x3670 && pid == 0x0902)
            {
                _pedals = PedalsDevice::P2000;
                selectedDeviceHandle = d.hDevice;
            }
            
            if (selectedDeviceHandle != nullptr)
            {
                break; // stop after finding the first supported device
			}
        }
    }

    if (selectedDeviceHandle != nullptr && enabled)
    {
        std::wstring devicePath = GetDeviceName(selectedDeviceHandle);
        if (!devicePath.empty())
        {
            HANDLE fh = OpenRawDeviceStream(devicePath);
            if (fh != INVALID_HANDLE_VALUE)
            {
                _deviceHandle = fh;
                _initialized = true;

                // ensure buffer is zeroed and send off to all channels
                ZeroMemory(_vibrateBuffer, sizeof(_vibrateBuffer));
                VibratePedal(Channel::Clutch, 0.0f, 0.0f);
                VibratePedal(Channel::Brake, 0.0f, 0.0f);
                VibratePedal(Channel::Throttle, 0.0f, 0.0f);
            }
        }
    }

    return _pedals;
}

void HPR::Uninitialize() noexcept
{
    for (int i = 0; i < 3; ++i)
    {
        _lastState[i] = -1;
        _lastFrequency[i] = -1;
        _lastAmplitude[i] = -1;
    }

    if (_initialized)
    {
        VibratePedal(Channel::Clutch, 0.0f, 0.0f);
        VibratePedal(Channel::Brake, 0.0f, 0.0f);
        VibratePedal(Channel::Throttle, 0.0f, 0.0f);
    }

    _initialized = false;

    if (_deviceHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(_deviceHandle);
        _deviceHandle = INVALID_HANDLE_VALUE;
    }
}

// P2000 seems to limit each vibration to max ~3 secs. However frequency and amplitude can be updated within that timeframe.
// Limit is possibly in place to prevent overheating
void HPR::VibratePedal(Channel channel, float frequency, float amplitude)
{
    if (!_initialized) return;
    if (_deviceHandle == INVALID_HANDLE_VALUE) return;

    State state = State::On;

    int ch = static_cast<int>(channel);

    int intFrequency = static_cast<int>(std::clamp(frequency, 0.0f, 50.0f));
    int intAmplitude = static_cast<int>(std::clamp(amplitude, 0.0f, 100.0f));

    if (intFrequency == 0 || intAmplitude == 0)
    {
        intFrequency = 0;
        intAmplitude = 0;
        state = State::Off;
    }

    if (_lastState[ch] != static_cast<int>(state) || _lastFrequency[ch] != intFrequency || _lastAmplitude[ch] != intAmplitude)
    {
#if defined(_DEBUG)        
        std::cout << "VibratePedal channel=" << static_cast<int>(channel)
            << ", state=" << static_cast<int>(state)
            << ", frequency=" << frequency
            << ", amplitude=" << amplitude
            << std::endl;        
#endif        
        _lastState[ch] = static_cast<int>(state);
        _lastFrequency[ch] = intFrequency;
        _lastAmplitude[ch] = intAmplitude;

        // Prepare 64-byte report - matches the layout used in C#
        ZeroMemory(_vibrateBuffer, sizeof(_vibrateBuffer));
        _vibrateBuffer[0] = 0xF1; // frameHeader
        _vibrateBuffer[1] = 0xEC; // commandCode
        _vibrateBuffer[2] = static_cast<unsigned char>(ch);               // channel
        _vibrateBuffer[3] = static_cast<unsigned char>(state);           // state
        _vibrateBuffer[4] = static_cast<unsigned char>(intFrequency);    // frequency
        _vibrateBuffer[5] = static_cast<unsigned char>(intAmplitude);    // amplitude

        // Send feature report
        BOOL ok = HidD_SetFeature(_deviceHandle, _vibrateBuffer, static_cast<ULONG>(sizeof(_vibrateBuffer)));
        (void)ok; // nothing to do on failure in this minimal port - caller may extend logging/handling
    }
}