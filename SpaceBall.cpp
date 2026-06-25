//
// SpaceBall/Mouse handler
// Neil Shipp, 2026
//

#include "stdafx.h"

#define BUFSIZE 256

#define USE_SPACEBALL 0
#define USE_SPACEORB 1
#define USE_SPACEMOUSE 0

enum SB_STATE
{
    eUNKNOWN,
    eRESET,
    eRESET_MSG,
    eCONFIG,
    eCONFIG_MSG,
    eMODESWITCH,
    eMODESWITCH_MSG,
    ePOLL
};

sbVect SbState = { 0 };
uint16_t SbConfigIdx = 0;
sbButtons SbButtons = { 0 };
SB_DEVICE SbDevice = eNONE;

#if USE_SPACEBALL

const char* resetString = "@RESET\r";
const char* configStrings[] = {
    "CBT\r",    // Communication Binary mode + 20 tenths of a second x-on timeout- 0x54 (T) & 0x3f = 20
    "P@r@r\r",  // Pulse data/request packets maxpulse,minpulse = 50ms, 50ms - 0x40 (@) 0x72 (r) & 0x3f 0x3f
    "NT\r",     // Null region size- 0x54 (T) & 0x3f = 0x14 ?@A..Z[\]^_`a..z{|} 0x3f..0x7e
    "FBp\r",    // Feel [T|F|Both]p = 0x70 (p) & 0x3F = 0x30 ?@A..Z[\]^_`a..z{|} 0x3f..0x7e
    "L+\r"      // button click noise
};
const char* modeSwitchString = "MSSV\r";    // translation and rotation streaming mode

#elif USE_SPACEMOUSE

const char* configStrings[] = {
    "c00\r",    // compress mode
    "m3\r",     // mode - translation + rotation
    "pAA\r",    // pulse period 40ms min, 40ms max
    "qHH\r",    // sensitivity = 8
    "nH\r",     // null radius = 8
    "l000\r"    // turn off LEDs
};
const char* modeSwitchString = "c33\r";    // compress mode - translation + rotation, ext key + compress

#else

const char* configStrings[] = {
    "?\r",          // query packet (return model and firmware version)
    "P\x80\xB0\r",  // pulse period 96ms
    "n\r",          // query null region
    "$\r"           // Returns "Ignoredx"  ???
};
const char* modeSwitchString = "\r";    // SpaceOrb has no mode switch command

#endif

const char  nibbleValues[] = {
    0x30, 0x41, 0x42, 0x33, 0x44, 0x35, 0x36, 0x47, 0x48, 0x39, 0x3A, 0x4B, 0x3C, 0x4D, 0x4E, 0x3F
};

extern HWND     g_hWnd;     // handle to hWnd to use for MessageBoxA

static HANDLE   hPort = INVALID_HANDLE_VALUE;   // serial port handle

static char     buf[BUFSIZE];       // circular buffer
static DWORD    head, tail;         // head is writer index, tail is reader index.

char            response[BUFSIZE * 2];

static SB_STATE state = eUNKNOWN;   // Spaceball state
static char     nullRegion = 0;     // Device null region
static char     rotationSensitivity = 0;    // Sensitivity
static char     translationSensitivity = 0;    // Sensitivity
static uint16_t pulseMin = 0 ;      // pulse timing minimum
static uint16_t pulseMax = 0;       // pulse timing maximum
static char     error[5];
// 
// Attempt to fill circular buffer with data from serial port
//   When head == tail, buffer is empty.
//   When head+1 == tail (modulo buf size), buffer is full.
//
HRESULT readBuffer()
{
    DWORD cb;
    HRESULT hr = S_OK;

    if (tail == ((head + 1) % sizeof(buf)))
    {
        return -1; // buffer full
    }

    // calculate free space in buffer
    DWORD bufSize = (head - tail) % sizeof(buf);
    DWORD space = sizeof(buf) - (bufSize + 1);

    // Read can't overflow buffer
    if ((head + space) < sizeof(buf))
    {
//        if (!ReadFile(hPort, buf + head, 1, &cb, nullptr))
        if (!ReadFile(hPort, buf + head, space, &cb, nullptr))
        {
            hr = GetLastError();
            MessageBoxA(g_hWnd, "Error reading port", "SpaceBall", MB_ICONERROR);

            return hr;
        }

        head += cb;
        assert(head < sizeof(buf));
    }
    else // Two reads required to fill buffer
    {
//        if (!ReadFile(hPort, buf + head, 1, &cb, nullptr))
        if (!ReadFile(hPort, buf + head, sizeof(buf) - head, &cb, nullptr))
        {
            hr = GetLastError();
            MessageBoxA(g_hWnd, "Error reading port", "SpaceBall", MB_ICONERROR);

            return hr;
        }

        // less data read than asked for, update head index and exit
        if (cb != sizeof(buf) - head)
        {
            head = (head + cb) % sizeof(buf);
            return hr;
        }

        // update head index and attempt to read more
        head = (head + cb) % sizeof(buf);
        space -= cb;

        assert(head == 0);
        assert(tail > space);

        if (!ReadFile(hPort, buf + head, space, &cb, nullptr))
        {
            hr = GetLastError();
            MessageBoxA(g_hWnd, "Error reading port", "SpaceBall", MB_ICONERROR);

            return hr;
        }

        head = (head + cb) % sizeof(buf);
    }

    return hr;
}

#if USE_SPACEBALL
// escape X-ON, X-OFF and CR values
HRESULT readUntilCr(char *output, DWORD *count)
{
    static BOOL escape = FALSE;
    static char lastChar = '\r';
    *count = 0;

    if (tail == head)
    {
        // empty buffer
        return -1;
    }

    while (tail != head)
    {
        if (buf[tail] != '\r' && buf[tail] != '^' && buf[tail] != '\n')
        {
            if (escape && buf[tail] == 'S')
            {
                output[*count] = 19;
            }
            else if (escape && buf[tail] == 'Q')
            {
                output[*count] = 17;
            }
            else if (escape && buf[tail] == 'M')
            {
                output[*count] = 13;
            }
            else
            {
                output[*count] = buf[tail];
            }
            (*count)++;
            escape = false;
        }

        if (buf[tail] == '\r')
            break;

        if (buf[tail] == '\n' && lastChar != '\r')
        {
            output[*count] = '\n';
            (*count)++;
            escape = false;
        }

        if (buf[tail] == '^')
        {
            if (escape)
            {
                output[*count] = '^';
                (*count)++;
                escape = FALSE;
            }
            else
            {
                escape = TRUE;
            }
        }

        lastChar = buf[tail];
        tail = (tail + 1) % sizeof(buf);
    }

    if (tail != head)
    {
        // consume last CR.
        tail = (tail + 1) % sizeof(buf);
    }
    else
    {
        // no CR in buffer
        return -1;
    }

    return 0;
}
#else
// No escaping, binary stream
HRESULT readUntilCr(char *output, DWORD *count)
{
    static BOOL escape = FALSE;
    static char lastChar = '\r';
    *count = 0;

    if (tail == head)
    {
        // empty buffer
        return -1;
    }

    while (tail != head)
    {
        if (buf[tail] != '\r' && buf[tail] != '\n')
        {
            output[*count] = buf[tail];
            (*count)++;
        }

        if (buf[tail] == '\r')
            break;

        if (buf[tail] == '\n' && lastChar != '\r')
        {
            output[*count] = '\n';
            (*count)++;
            escape = false;
        }

        lastChar = buf[tail];
        tail = (tail + 1) % sizeof(buf);
    }

    if (tail != head)
    {
        // consume last CR.
        tail = (tail + 1) % sizeof(buf);
    }
    else
    {
        // no CR in buffer
        return -1;
    }

    return 0;
}
#endif

HRESULT writeCommand(const char *commandString)
{
    HRESULT hr = S_OK;
    DWORD cb = 0;
    DWORD cbCmd = strlen(commandString);

    if (!WriteFile(hPort, commandString, cbCmd, &cb, nullptr))
    {
        hr = GetLastError();
        MessageBoxA(g_hWnd, "Error sending reset", "SpaceBall", MB_ICONERROR);

        return hr;
    }

    if (cb != cbCmd)
    {
        MessageBoxA(g_hWnd, "Not all command sent to device", "SpaceBall", MB_ICONERROR);
    }

    return hr;
}


HRESULT OpenPort()
{
//    LPCWSTR port = L"\\\\.\\COM15";
    LPCWSTR port = L"\\\\.\\COM37";
    HRESULT hr = S_OK;
    DWORD   modemStatus = 0;

    hPort = CreateFile(port, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, NULL);
    if (hPort == INVALID_HANDLE_VALUE)
    {
        hr = GetLastError();
        MessageBoxA(g_hWnd, "Error opening serial port", "SpaceBall", MB_ICONERROR);

        return hr;
    }

    DCB portState = { 0 };
    portState.DCBlength = sizeof(DCB);

    if (!GetCommState(hPort, &portState))
    {
        hr = GetLastError();
        MessageBoxA(g_hWnd, "Error retrieving CommState", "SpaceBall", MB_ICONERROR);

        return hr;
    }

    if (!GetCommModemStatus(hPort, &modemStatus))
    {
        hr = GetLastError();
        MessageBoxA(g_hWnd, "Error retrieving CommModemStatus", "SpaceBall", MB_ICONERROR);

        return hr;
    }

    // 9600 baud 8N2
    portState.BaudRate = CBR_9600;
    portState.ByteSize = 8;
    portState.Parity = NOPARITY;

    portState.StopBits = ONESTOPBIT;
    portState.fBinary = TRUE;

    // Enable DTR and RTS lines
    // Disable DSR sensitivity
    portState.fDtrControl = DTR_CONTROL_ENABLE;
    portState.fRtsControl = RTS_CONTROL_ENABLE;
    portState.fDsrSensitivity = FALSE;

    // use CTS handshaking
    portState.fOutxCtsFlow = TRUE;
    portState.fOutxDsrFlow = FALSE;
    portState.fInX = FALSE;
    portState.fOutX = FALSE;
    portState.fTXContinueOnXoff = TRUE;
    portState.XoffChar = 19;
    portState.XonChar = 17;

#if USE_SPACEBALL
    // use X-ON/OFF handshaking
    portState.fOutxCtsFlow = FALSE;
    portState.fOutxDsrFlow = FALSE;
    portState.fInX = TRUE;
    portState.fOutX = TRUE;

//    portState.StopBits = TWOSTOPBITS;
#elif USE_SPACEORB
    // no handshaking
    portState.fOutxCtsFlow = FALSE;
    portState.fOutxDsrFlow = FALSE;
    portState.fInX = FALSE;
    portState.fOutX = FALSE;
#else
    // use CTS handshaking
    portState.fOutxCtsFlow = TRUE;
    portState.fOutxDsrFlow = FALSE;
    portState.fInX = FALSE;
    portState.fOutX = FALSE;
#endif
    if (!SetCommState(hPort, &portState))
    {
        hr = GetLastError();
        MessageBoxA(g_hWnd, "Error setting CommState", "SpaceBall", MB_ICONERROR);

        return hr;
    }

    COMMTIMEOUTS timeouts = { 0 };
    if (!GetCommTimeouts(hPort, &timeouts))
    {
        hr = GetLastError();
        MessageBoxA(g_hWnd, "Error getting CommTimeouts", "SpaceBall", MB_ICONERROR);

        return hr;
    }

/*
    // blocking reads
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutMultiplier = 1;
    timeouts.ReadTotalTimeoutConstant = 200;
*/
    // non-blocking reads
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.ReadTotalTimeoutMultiplier = 0;

    if (!SetCommTimeouts(hPort, &timeouts))
    {
        hr = GetLastError();
        MessageBoxA(g_hWnd, "Error setting CommTimeouts", "SpaceBall", MB_ICONERROR);

        return hr;
    }

    head = 0;
    tail = 0;
#if USE_SPACEORB
/*
    if (!GetCommModemStatus(hPort, &modemStatus))
    {
        hr = GetLastError();
        MessageBoxA(g_hWnd, "Error retrieving CommModemStatus", "SpaceBall", MB_ICONERROR);

        return hr;
    }
*/
    if (!EscapeCommFunction(hPort, CLRRTS))
    {
        hr = GetLastError();
        MessageBoxA(g_hWnd, "Error clearing RTS", "SpaceBall", MB_ICONERROR);

        return hr;
    }
    if (!EscapeCommFunction(hPort, CLRDTR))
    {
        hr = GetLastError();
        MessageBoxA(g_hWnd, "Error clearing DTR", "SpaceBall", MB_ICONERROR);

        return hr;
    }

    Sleep(1000);

    // clear any stale data on port
    do
    {
        head = 0;
        tail = 0;

        readBuffer();

    } while (tail != head);

    head = 0;
    tail = 0;

    if (!EscapeCommFunction(hPort, SETDTR))
    {
        hr = GetLastError();
        MessageBoxA(g_hWnd, "Error setting DTR", "SpaceBall", MB_ICONERROR);

        return hr;
    }

    Sleep(200);

    if (!EscapeCommFunction(hPort, SETRTS))
    {
        hr = GetLastError();
        MessageBoxA(g_hWnd, "Error setting RTS", "SpaceBall", MB_ICONERROR);

        return hr;
    }

#else

    Sleep(200);

    // clear any stale data on port
    do
    {
        head = 0;
        tail = 0;

        readBuffer();

    } while (tail != head);

#endif

    head = 0;
    tail = 0;

    state = eUNKNOWN;
    SbDevice = eNONE;

#if USE_SPACEBALL
    SbDevice = eSPACEBALL;
#elif USE_SPACEORB
    SbDevice = eSPACEORB360;
#else
    SbDevice = eSPACEMOUSE;
#endif

    return hr;
}

HRESULT ClosePort()
{
    HRESULT hr = S_OK;

    if (!CloseHandle(hPort))
    { 
        hr = GetLastError();
        MessageBoxA(g_hWnd, "Error closing port", "SpaceBall", MB_ICONERROR);

        return hr;
    }

    return hr;
}

HRESULT ReadPort(char *output, DWORD *count)
{
    HRESULT hr;
    DWORD index = 0;

    *count = 0;

    do 
    {
        if (*count > 480)   // fill up to 480 bytes of 512 byte response
        {
            *count = 0;
            return hr;
        }
        hr = readUntilCr(output + *count, &index);
        *count += index;
        if (hr == 0)
        {
            break;
        }

        if (hr == -1)
        {
            hr = readBuffer();
        }

        if (hr != 0)
        {
            break;
        }
    } while (*count > 0);

    return hr;
}

void EvaluatePacket(DWORD cb, char* packet)
{
    if (cb > 0)
    {
        if ((response[0] == 'D') && SbDevice == eSPACEBALL)
        {
            SbState = *(sbVect *)(&response[1]);
            SbState.rx = _byteswap_ushort(SbState.rx);
            SbState.ry = _byteswap_ushort(SbState.ry);
            SbState.rz = _byteswap_ushort(SbState.rz);
            SbState.tx = _byteswap_ushort(SbState.tx);
            SbState.ty = _byteswap_ushort(SbState.ty);
            SbState.tz = _byteswap_ushort(SbState.tz);
        }
        else if ((response[0] == 'D') && SbDevice == eSPACEORB360)
        {
            int offset = 1;
            static const char key[] = "\x00SpaceWare\x00";

            for (int i = 0; i < (int)(cb - offset); i++)
            {
                response[i + offset] ^= key[i % sizeof(key)];
            }

            // set sign bits
            SbState.tx = (response[2] & 0x40) ? 0xFC00 : 0;
            SbState.tz = (response[3] & 0x08) ? 0xFC00 : 0;     // swap z and y values to account for proper posture of
            SbState.ty = (response[4] & 0x01) ? 0xFC00 : 0;     // holding SpaceOrb upgright
            SbState.rx = (response[6] & 0x10) ? 0xFC00 : 0;
            SbState.rz = (response[7] & 0x02) ? 0xFC00 : 0;
            SbState.ry = (response[9] & 0x20) ? 0xFC00 : 0;

            // set values
            SbState.tx |= ((response[2] & 0x7F) << 3) | ((response[3] & 0x70) >> 4);
            SbState.tz |= ((response[3] & 0x0F) << 6) | ((response[4] & 0x7E) >> 1);
            SbState.ty |= ((response[4] & 0x01) << 9) | ((response[5] & 0x7F) << 2) | ((response[6] & 0x60) >> 5);
            SbState.rx |= ((response[6] & 0x1F) << 5) | ((response[7] & 0x7C) >> 2);
            SbState.rz |= ((response[7] & 0x03) << 8) | ((response[8] & 0x7F) << 1) | ((response[9] & 0x40) >> 6);
            SbState.ry |= ((response[9] & 0x3F) << 4) | ((response[10] & 0x78) >> 3);

            // scale output
            SbState.tx *= 2;
            SbState.ty *= -2;
            SbState.tz *= -2;
            SbState.rx *= 2;
            SbState.ry *= -2;
            SbState.rz *= -2;
        }
        else if ((response[0] == 'd') && SbDevice == eSPACEMOUSE)    // SpaceMouse
        {
            // compare checksums
            uint16_t cs = (uint8_t)response[1] + (uint8_t)response[2] + (uint8_t)response[3] + (uint8_t)response[4] +
                (uint8_t)response[5] + (uint8_t)response[6] + (uint8_t)response[7] + (uint8_t)response[8] +
                (uint8_t)response[9] + (uint8_t)response[10] + (uint8_t)response[11] + (uint8_t)response[12];
            uint16_t cs2 = ((response[13] & 0x3F) << 6) | (response[14] & 0x3F);
            if (cs == cs2)
            {
                SbState.tx = 4 * ((((response[1] & 0x3F) << 6) | (response[2] & 0x3F)) - 2048);
                SbState.ty = 4 * ((((response[3] & 0x3F) << 6) | (response[4] & 0x3F)) - 2048);
                SbState.tz = 4 * (-((((response[5] & 0x3F) << 6) | (response[6] & 0x3F)) - 2048));
                SbState.rx = 4 * ((((response[7] & 0x3F) << 6) | (response[8] & 0x3F)) - 2048);
                SbState.ry = 4 * (((((response[9] & 0x3F) << 6) | (response[10] & 0x3F)) - 2048));
                SbState.rz = 4 * (-((((response[11] & 0x3F) << 6) | (response[12] & 0x3F)) - 2048));
            }
        }
        else if ((response[0] == 'K') && SbDevice == eSPACEBALL)
        {
            SbButtons = *(sbButtons *)(&response[1]);
            SbButtons.buttons = _byteswap_ushort(SbButtons.buttons);
        }
        else if ((response[0] == 'K') && SbDevice == eSPACEORB360)
        {
            SbButtons.buttons = ((uint16_t)response[2] & 0x0F) | (((uint16_t)response[2] & 0x70) << 4);
            SbButtons.buttons |= ((uint16_t)response[2] & 0x01) << 12;  // duplicate button A as pick.
        }
        else if ((response[0] == 'k') && SbDevice == eSPACEMOUSE)
        {
            SbButtons.buttons = (response[1] & 0x0f) | ((response[2] << 8) & 0xf00) | ((response[3] << 12) & 0xf000);
        }
        else if (response[0] == 'R')
        {
            // SpaceOrb360 Reset packet
            // 33 byte packet "R Spaceball (R) V4.26 28-Jun-96 Copyright (C) 1996"
            int offset = 1;
            static const char key[] = { 0x80 };
            for (int i = 0; i < (int)(cb - offset); i++)
            {
                response[i + offset] ^= key[i % sizeof(key)];
            }
            error[0] = 0;
        }
        else if (response[0] == 'N')
        {
            // Null region packet
            if (SbDevice == eSPACEORB360)
            {
                nullRegion = response[1] & 0x7f;
            }
            else
            {
                nullRegion = response[1] & 0x3f;
            }
        }
        else if (response[0] == 'n')
        {
            // Null region packet
            nullRegion = response[1] & 0x3f;
        }
        else if ((response[0] == 'F') && (response[1] == 'R'))
        {
            // Feel - Rotation
            rotationSensitivity = response[2] & 0x3f;
        }
        else if ((response[0] == 'F') && (response[1] == 'T'))
        {
            // Feel - Translation
            translationSensitivity = response[2] & 0x3f;
        }
        else if (response[0] == 'q')
        {
            // SpaceMouse sensitivity
            translationSensitivity = response[2] & 0x0f;
            rotationSensitivity = response[3] & 0x0f;
        }
        else if (response[0] == 'E')
        {
            // parse error message
            error[0] = response[0];
            error[1] = response[1];
            error[2] = response[2];
            error[3] = response[3];
            error[4] = response[4];

            // 0x04 - brownout, 0x02 - checksum, 0x01 - hard fault
        }
        else if (response[0] == '!')
        {
            // SpaceOrb360 information packet
            //  expect something like "!1 Spaceball (R) V4.26 28-Jun-96 Copyright (C) 19968!2 11.52N 0.2557Nm 10bitD"

            int offset = 1;
            static const char key[] = { 0x80 };
            for (int i = 0; i < (int)(cb - offset); i++)
            {
                if (response[i + offset] & key[i % sizeof(key)])
                {
                    response[i + offset] ^= key[i % sizeof(key)];
                }
            }
            error[0] = 3;
        }
        else if (response[0] == '$')
        {
            // SpaceOrb360 packet ?

            int offset = 1;
            static const char key[] = { 0x80 };
            for (int i = 0; i < (int)(cb - offset); i++)
            {
                if (response[i + offset] & key[i % sizeof(key)])
                {
                    response[i + offset] ^= key[i % sizeof(key)];
                }
            }
            error[0] = 3;
        }
        else if (response[0] == 'P')
        {   
            // pulse timing
            if (SbDevice == eSPACEORB360)
            {
                pulseMax = response[1] & 0x7F;
                pulseMin = response[2] & 0x7F;
            }
            else
            {
                pulseMax = (response[1] & 0x3F) << 6 | (response[2] & 0x3F);
                pulseMax = (response[3] & 0x3F) << 6 | (response[4] & 0x3F);
            }
        }
        else if (response[0] == 'p')
        {
            pulseMax = response[1] & 0x0F;
            pulseMin = response[2] & 0x0F;
        }
    }
}

HRESULT UpdateDeviceState()
{
    HRESULT hr = S_OK;
    DWORD cb = 0;

    // Expect the occasional response, poll the device
    if (state != eUNKNOWN)
    {
        hr = ReadPort(response, &cb);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    switch (state)
    {
    case eUNKNOWN:
        DWORD status;
        hr = GetCommModemStatus(hPort, &status);
        if (FAILED(hr))
        {
            MessageBoxA(g_hWnd, "GetCommModemStatus returned error", "SpaceBall", MB_ICONERROR);
        }
        SbConfigIdx = 0;
        hr = writeCommand("\r");
        if (SUCCEEDED(hr))
            state = eCONFIG;
        break;

#if USE_SPACEBALL
    case eRESET:
        SbConfigIdx = 0;
        hr = writeCommand(resetString);
        if (SUCCEEDED(hr))
        {
            state = eRESET_MSG;
        }
        break;

    case eRESET_MSG:
        if ((cb > 0) && (response[0] == '@'))
        {
            state = eCONFIG;
        }
        break;
#endif
    case eCONFIG:
        assert(SbConfigIdx < ARRAYSIZE(configStrings));

        hr = writeCommand(configStrings[SbConfigIdx]);
        if (SUCCEEDED(hr))
        {
            state = eCONFIG_MSG;
        }
        break;

    case eCONFIG_MSG:
#if USE_SPACEORB
        if (cb > 0)
        {
            if ((response[0] == '!') && (configStrings[SbConfigIdx][0] == '?'))
            {
                SbConfigIdx++;
                state = eCONFIG;
            }
            else if ((response[0] == 'N') && (configStrings[SbConfigIdx][0] == 'n'))
            {
                SbConfigIdx++;
                state = eCONFIG;
            }
            else if ((response[0] == 'P') && (configStrings[SbConfigIdx][0] == 'P'))
            {
                SbConfigIdx++;
                state = eCONFIG;
            }
            else if ((response[0] == '$') && (configStrings[SbConfigIdx][0] == '$'))
            {
                SbConfigIdx++;
                state = eCONFIG;
            }

            EvaluatePacket(cb, response);
        }
        if (SbConfigIdx == ARRAYSIZE(configStrings))
        {
            state = ePOLL;
        }
#else
        if ((cb > 0) && (response[0] == configStrings[SbConfigIdx][0]))
        {
            SbConfigIdx++;
            if (SbConfigIdx < ARRAYSIZE(configStrings))
            {
                state = eCONFIG;
            }
            else
            {
                state = eMODESWITCH;
            }

            EvaluatePacket(cb, response);
        }
#endif
        break;

    case eMODESWITCH:
        hr = writeCommand(modeSwitchString);
        if (SUCCEEDED(hr))
        {
            state = eMODESWITCH_MSG;
        }
        break;

    case eMODESWITCH_MSG:
        if ((cb > 0) && (response[0] == modeSwitchString[0]))
        {
            EvaluatePacket(cb, response);
            state = ePOLL;
        }
        break;

    case ePOLL:
        EvaluatePacket(cb, response);
        break;
    }

    return hr;
}

HRESULT SetDeviceSensitivity(uint8_t value)
{
    char cmd[16];

    if (value >= 64)
    {
        return E_INVALIDARG;
    }

    switch (SbDevice)
    {
    case eSPACEMOUSE:
        // sensitivity <translation> <rotation>
        wsprintfA(cmd, "q%c%c\r", nibbleValues[value / 4], nibbleValues[value / 4]);
        return writeCommand(cmd);

    case eSPACEBALL:
        wsprintfA(cmd, "FB%c\r", value == 0x3F ? '?' : value + '@');
        return writeCommand(cmd);

    default:
        return E_NOT_VALID_STATE;
    }
}

HRESULT SetNullRadius(uint8_t value)
{
    char cmd[16];

    if (value >= 64)
    {
        return E_INVALIDARG;
    }

    switch (SbDevice)
    {
    case eSPACEMOUSE:
        // null radius <value> 0 - 15
        wsprintfA(cmd, "n%c\r", nibbleValues[value / 4]);
        return writeCommand(cmd);

    case eSPACEBALL:
        // null radius <value> 0 - 63
        wsprintfA(cmd, "N%c\r", value == 0x3F ? '?' : value + '@');
        return writeCommand(cmd);

    default:
        return E_NOT_VALID_STATE;
    }
}