#include "util/rumble.hpp"

#include <borealis.hpp>

#ifdef __SWITCH__
#include <switch.h>

namespace
{

struct Device
{
    HidVibrationDeviceHandle handles[2];
    int count;
};

// The vibration devices of what is held: handheld Joy-Cons, or player 1's
// controller (Pro Controller, two Joy-Cons, or one held sideways).
bool open(HidNpadIdType id, Device& device)
{
    u32 styles = hidGetNpadStyleSet(id);
    HidNpadStyleTag style;
    device.count = 2;
    if (id == HidNpadIdType_Handheld && (styles & HidNpadStyleTag_NpadHandheld))
        style = HidNpadStyleTag_NpadHandheld;
    else if (styles & HidNpadStyleTag_NpadFullKey)
        style = HidNpadStyleTag_NpadFullKey;
    else if (styles & HidNpadStyleTag_NpadJoyDual)
        style = HidNpadStyleTag_NpadJoyDual;
    else if (styles & HidNpadStyleTag_NpadJoyLeft)
        style = HidNpadStyleTag_NpadJoyLeft, device.count = 1;
    else if (styles & HidNpadStyleTag_NpadJoyRight)
        style = HidNpadStyleTag_NpadJoyRight, device.count = 1;
    else
        return false;
    return R_SUCCEEDED(hidInitializeVibrationDevices(device.handles, device.count, id, style));
}

void send(const Device& device, float amplitude)
{
    HidVibrationValue values[2];
    for (auto& v : values)
    {
        v.amp_low   = amplitude;
        v.freq_low  = 160.0f;
        v.amp_high  = amplitude;
        v.freq_high = 320.0f;
    }
    hidSendVibrationValues(device.handles, values, device.count);
}

} // namespace

namespace rumble
{

void pulse()
{
    for (HidNpadIdType id : { HidNpadIdType_Handheld, HidNpadIdType_No1 })
    {
        Device device;
        if (!open(id, device))
            continue;
        send(device, 0.3f);
        brls::delay(60, [device] { send(device, 0.0f); });
    }
}

} // namespace rumble

#else

namespace rumble
{
void pulse() { }
} // namespace rumble

#endif
