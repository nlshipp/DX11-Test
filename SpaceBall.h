//
// SpaceBall handler
//

#pragma once

HRESULT OpenPort();
HRESULT ClosePort();
HRESULT UpdateDeviceState();

#pragma pack(push,2)
struct sbVect
{
    int16_t period;
    int16_t tx, ty, tz;
    int16_t rx, ry, rz;
};

struct sbButtons
{
    int16_t buttons;
};

#pragma pack(pop)

extern sbVect SbState;
extern sbButtons SbButtons;

#define SB_BTN_PICK 0x1000
#define SB_BTN_1    0x0001
#define SB_BTN_2    0x0002
#define SB_BTN_3    0x0004
#define SB_BTN_4    0x0008
#define SB_BTN_5    0x0100
#define SB_BTN_6    0x0200
#define SB_BTN_7    0x0400
#define SB_BTN_8    0x0800
