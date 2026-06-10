//
// SpaceBall/Mouse handler
// Neil Shipp, 2026
//

#include "stdafx.h"

#define BUFSIZE 256

enum SB_STATE
{
    eUNKNOWN,
    eRESET,
    eCONFIG,
    eMODESWITCH,
    ePOLL
};

sbVect SbState = { 0 };
sbButtons SbButtons = { 0 };

extern HWND     g_hWnd;     // handle to hWnd to use for MessageBoxA

static HANDLE   hPort = INVALID_HANDLE_VALUE;   // serial port handle

static char     buf[BUFSIZE];       // circular buffer
static DWORD    head, tail;         // head is writer index, tail is reader index.

static SB_STATE state = eUNKNOWN;   // Spaceball state

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

HRESULT OpenPort()
{
    LPCWSTR port = L"\\\\.\\COM15";
    HRESULT hr = S_OK;

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

    // 9600 baud 8N1
    portState.BaudRate = CBR_9600;
    portState.ByteSize = 8;
    portState.Parity = NOPARITY;
    portState.StopBits = ONESTOPBIT;
    portState.fBinary = TRUE;

    // no DTR, DSR, RTS lines
    portState.fDtrControl = DTR_CONTROL_ENABLE;
    portState.fRtsControl = RTS_CONTROL_ENABLE;
    portState.fDsrSensitivity = FALSE;

    // use X-ON/X-OFF handshaking
    portState.fOutxCtsFlow = FALSE;
    portState.fOutxDsrFlow = FALSE;
    portState.fInX = TRUE;
    portState.fOutX = TRUE;
    portState.fTXContinueOnXoff = TRUE;
    portState.XoffChar = 19;
    portState.XonChar = 17;

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

    state = eUNKNOWN;

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

HRESULT ConfigureDevice()
{
    HRESULT hr = S_OK;
    char resetString[] = "\r@RESET\r";
    DWORD cb = 0;

    if (!WriteFile(hPort, resetString, strlen(resetString), &cb, nullptr))
    {
        hr = GetLastError();
        MessageBoxA(g_hWnd, "Error sending reset", "SpaceBall", MB_ICONERROR);

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

HRESULT UpdateDeviceState()
{
    // Pulse 20 events/sec
    // Null region, Trans and Rotation to cubic
    // Modeswitch rotation = Streaming, translation = Streaming
//    const char configString[] = "P@r@r\rNT\rFT?\rFR?\rMSSV\r";
    const char configString[] = "P@r@r\rNT\rMSSV\r";
    char response[BUFSIZE];
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
        hr = ConfigureDevice();
        if (SUCCEEDED(hr))
            state = eRESET;
        break;

    case eRESET:
        if ((cb > 0) && (response[0] == '@'))
        {
            state = eCONFIG;
        }
        break;

    case eCONFIG:
        if (!WriteFile(hPort, configString, strlen(configString), &cb, nullptr))
        {
            hr = GetLastError();
            MessageBoxA(g_hWnd, "Error sending config string", "SpaceBall", MB_ICONERROR);
        }
        else
        {
            state = eMODESWITCH;
        }
        break;

    case eMODESWITCH:
        if ((cb > 0) && (response[0] == 'M'))
        {
            state = ePOLL;
        }
        break;

    case ePOLL:
        if ((cb > 0) && (response[0] == 'D'))
        {
            SbState = *(sbVect *)(&response[1]);
            SbState.rx = _byteswap_ushort(SbState.rx);
            SbState.ry = _byteswap_ushort(SbState.ry);
            SbState.rz = _byteswap_ushort(SbState.rz);
            SbState.tx = _byteswap_ushort(SbState.tx);
            SbState.ty = _byteswap_ushort(SbState.ty);
            SbState.tz = _byteswap_ushort(SbState.tz);
        }
        else if ((cb > 0) && (response[0] == 'K'))
        {
            SbButtons = *(sbButtons *)(&response[1]);
            SbButtons.buttons = _byteswap_ushort(SbButtons.buttons);
        }
    }

    return hr;
}

