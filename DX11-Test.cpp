// DX11-Test.cpp : displays rendered cube
//

#include "stdafx.h"
#include "DX11-Test.h"

using namespace DirectX;

#define MAX_LOADSTRING 100

// global structures

struct SimpleVertex
{
    XMFLOAT3 f;
    XMFLOAT4 c;
};

//  for passing matrices to vertex shader
struct ConstantBuffer
{
    XMMATRIX mWorld;
    XMMATRIX mView;
    XMMATRIX mProjection;
};

// Global Variables:
HINSTANCE hInst;                                // current instance
HWND      g_hWnd = nullptr;

D3D_DRIVER_TYPE     g_driverType = D3D_DRIVER_TYPE_HARDWARE;
D3D_FEATURE_LEVEL   g_d3dFeatureLevel;

ID3D11Device *      g_pd3dDevice = nullptr;
ID3D11DeviceContext *   g_pd3dDeviceContext = nullptr;
IDXGISwapChain *        g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pRenderTargetView = nullptr;
ID3D11InputLayout *     g_pVertexLayout = nullptr;
ID3D11Buffer *          g_pVertexBuffer = nullptr;
ID3D11VertexShader*     g_pVertexShader = nullptr;
ID3D11PixelShader*      g_pPixelShader = nullptr;
ID3D11Buffer *          g_pIndexBuffer = nullptr;
ID3D11Buffer *          g_pConstantBuffer = nullptr;
XMMATRIX                g_World;
XMMATRIX                g_View;
XMMATRIX                g_Projection;

POINT       g_ptStart = { 0, 0 };
POINT       g_ptCurrent = { 0, 0 };
WPARAM      g_wParam = 0;
BOOL        lButtonDown = FALSE;
BOOL        g_mouseTrack = FALSE;
BOOL        g_SpaceballPick = FALSE;

WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name


// Forward declarations of functions included in this code module:
ATOM    MyRegisterClass(HINSTANCE hInstance);
BOOL    InitInstance(HINSTANCE, int);
BOOL    InitDevice();
void    CleanupDevice();
void    Render();
BOOL    MouseHandler(MSG *msg);
void    GenerateSpaceballRotationMatrix();

LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_DX11TEST, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    // Perform DirectX initialization:
    if (!InitDevice())
    {
        CleanupDevice();
        return FALSE;
    }

    OpenPort();

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_DX11TEST));

    MSG msg = { 0 };

    // Main message loop:
    while (WM_QUIT != msg.message)
    {
        UpdateDeviceState();

        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
            {
                BOOL handled = FALSE;

                if ((msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST) || msg.message == WM_MOUSELEAVE)
                {
                    handled = MouseHandler(&msg);
                }

                if (!handled)
                {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
        }
        else
        {
            Render();
        }
    }

    CleanupDevice();

    ClosePort();

    return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_DX11TEST));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_DX11TEST);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   g_hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!g_hWnd)
   {
      return FALSE;
   }

   ShowWindow(g_hWnd, nCmdShow);
   UpdateWindow(g_hWnd);

   return TRUE;
}

BOOL InitDevice()
{
    RECT rect;
    GetClientRect(g_hWnd, &rect);

    const D3D_FEATURE_LEVEL   featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    const UINT                numFeatureLevels = ARRAYSIZE(featureLevels);

    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 1;
    sd.BufferDesc.Width = rect.right- rect.left;
    sd.BufferDesc.Height = rect.bottom - rect.top;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;

    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, g_driverType, nullptr, 0, featureLevels, numFeatureLevels,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &g_d3dFeatureLevel, &g_pd3dDeviceContext)))
    {
        return FALSE;
    }

    //  Create a target render view

    ID3D11Texture2D * pBackBuffer = nullptr;
    if (FAILED(g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID *)&pBackBuffer)))
    {
        return FALSE;
    }

    HRESULT hr;
    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr))
    {
        return FALSE;
    }
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);

    // initialize viewport

    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)sd.BufferDesc.Width;
    vp.Height = (FLOAT)sd.BufferDesc.Height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    g_pd3dDeviceContext->RSSetViewports(1, &vp);

    // Compile a vertex shader

    // NOTE: in DX11 there don't appear to be any "default" shaders (flat, gourad, phong, etc), all have to be compiled

    ID3DBlob *pVertexShaderBlob = nullptr;
    hr = S_OK;

    DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
    ID3DBlob * pErrorBlob = nullptr;
    hr = D3DCompileFromFile(L"shader.foo", nullptr, nullptr, "VS" /* entryPoint */, "vs_4_0" /* szShaderModel */,
        dwShaderFlags, 0, &pVertexShaderBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        if (pErrorBlob)
        {
            OutputDebugStringA((char *)pErrorBlob->GetBufferPointer());
        }
    }

    if (pErrorBlob)
    {
        pErrorBlob->Release();
        pErrorBlob = nullptr;
    }

    if (FAILED(hr))
    {
        return FALSE;
    }

    hr = g_pd3dDevice->CreateVertexShader(pVertexShaderBlob->GetBufferPointer(), pVertexShaderBlob->GetBufferSize(), nullptr, &g_pVertexShader);
    if (FAILED(hr))
    {
        pVertexShaderBlob->Release();
        return hr;
    }

    // Create vertex input layout 

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    UINT numElements = ARRAYSIZE(layout);

    hr = g_pd3dDevice->CreateInputLayout(layout, numElements, pVertexShaderBlob->GetBufferPointer(),
        pVertexShaderBlob->GetBufferSize(), &g_pVertexLayout);

    pVertexShaderBlob->Release();

    if (FAILED(hr))
    {
        return FALSE;
    }

    // Set input layout

    g_pd3dDeviceContext->IASetInputLayout(g_pVertexLayout);

    // Compile pixel shader

    ID3DBlob * pPixelShaderBlob = nullptr;

    hr = D3DCompileFromFile(L"shader.foo", nullptr, nullptr, "PS" /* entryPoint */, "ps_4_0" /* szShaderModel */,
        dwShaderFlags, 0, &pPixelShaderBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        if (pErrorBlob)
        {
            OutputDebugStringA((char *)pErrorBlob->GetBufferPointer());
        }
    }

    if (pErrorBlob)
    {
        pErrorBlob->Release();
        pErrorBlob = nullptr;
    }

    if (FAILED(hr))
    {
        return FALSE;
    }

    hr = g_pd3dDevice->CreatePixelShader(pPixelShaderBlob->GetBufferPointer(), pPixelShaderBlob->GetBufferSize(), nullptr, &g_pPixelShader);
    pPixelShaderBlob->Release();
    if (FAILED(hr))
    {
        return hr;
    }

    // Create vertex buffer - Cube

    SimpleVertex vertices[] =
    {
        { XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f) },
        { XMFLOAT3(1.0f,  1.0f, -1.0f), XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f) },
        { XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT4(0.0f, 1.0f, 1.0f, 1.0f) },
        { XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f) },
        { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(1.0f, 0.0f, 1.0f, 1.0f) },
        { XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT4(1.0f, 1.0f, 0.0f, 1.0f) },
        { XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f) },
        { XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f) },
    };

    D3D11_BUFFER_DESC bd;
    ZeroMemory(&bd, sizeof(bd));
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(SimpleVertex) * ARRAYSIZE(vertices);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;
    bd.MiscFlags = 0;

    D3D11_SUBRESOURCE_DATA initData;
    ZeroMemory(&initData, sizeof(initData));
    initData.pSysMem = vertices;
    if (FAILED(g_pd3dDevice->CreateBuffer(&bd, &initData, &g_pVertexBuffer)))
    {
        return FALSE;
    }

    // set vertex buffer

    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;
    g_pd3dDeviceContext->IASetVertexBuffers(0, 1, &g_pVertexBuffer, &stride, &offset);

    // Create index buffer
    WORD indices[] =
    {
        3,1,0,
        2,1,3,

        0,5,4,
        1,5,0,

        3,4,7,
        0,4,3,

        1,6,5,
        2,6,1,

        2,7,6,
        3,7,2,

        6,4,5,
        7,4,6,
    };

    ZeroMemory(&bd, sizeof(bd));
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(WORD) * ARRAYSIZE(indices);
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.CPUAccessFlags = 0;
    bd.MiscFlags = 0;

    ZeroMemory(&initData, sizeof(initData));
    initData.pSysMem = indices;
    if (FAILED(g_pd3dDevice->CreateBuffer(&bd, &initData, &g_pIndexBuffer)))
    {
        return FALSE;
    }

    // set index buffer
    g_pd3dDeviceContext->IASetIndexBuffer(g_pIndexBuffer, DXGI_FORMAT_R16_UINT , 0);

    g_pd3dDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Create constant buffer for matrices
    ZeroMemory(&bd, sizeof(bd));
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(ConstantBuffer);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = 0;
    if (FAILED(g_pd3dDevice->CreateBuffer(&bd, NULL, &g_pConstantBuffer)))
    {
        return FALSE;
    }

    // Initialize world matrix
    g_World = XMMatrixIdentity();

    // Initialize view matrix
    XMVECTOR eye = XMVectorSet(0.0f, 2.0f, -5.0f, 0.0f);
    XMVECTOR at = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    g_View = XMMatrixLookAtLH(eye, at, up);

    // Initalize projection matrix
    g_Projection = XMMatrixPerspectiveFovLH(XM_PIDIV2, vp.Width / vp.Height, 0.01f, 100.0f);

    return TRUE;
}

void CleanupDevice()
{
    if (g_pIndexBuffer)
    {
        g_pIndexBuffer->Release();
        g_pIndexBuffer = nullptr;
    }

    if (g_pConstantBuffer)
    {
        g_pConstantBuffer->Release();
        g_pConstantBuffer = nullptr;
    }

    if (g_pVertexShader)
    {
        g_pVertexShader->Release();
        g_pVertexShader = nullptr;
    }

    if (g_pPixelShader)
    {
        g_pPixelShader->Release();
        g_pPixelShader = nullptr;
    }

    if (g_pVertexBuffer)
    {
        g_pVertexBuffer->Release();
        g_pVertexBuffer = nullptr;
    }

    if (g_pVertexLayout)
    {
        g_pVertexLayout->Release();
        g_pVertexLayout = nullptr;
    }

    if (g_pd3dDeviceContext)
    {
        g_pd3dDeviceContext->ClearState();
    }

    if (g_pRenderTargetView)
    {
        g_pRenderTargetView->Release();
        g_pRenderTargetView = nullptr;
    }

    if (g_pSwapChain)
    {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }

    if (g_pd3dDeviceContext)
    {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }

    if (g_pd3dDevice)
    {
        g_pd3dDevice->Release();
        g_pd3dDevice = NULL;
    }
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: Add any drawing code that uses hdc here...
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

BOOL MouseHandler(MSG *msg)
{
    BOOL handled = FALSE;

    switch (msg->message)
    {
    case WM_LBUTTONDOWN:
        lButtonDown = TRUE;
        g_ptStart.x = GET_X_LPARAM(msg->lParam);
        g_ptStart.y = GET_Y_LPARAM(msg->lParam);

        if (!g_mouseTrack)
        {
            TRACKMOUSEEVENT tme = { 0 };
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = msg->hwnd;
            tme.dwHoverTime = HOVER_DEFAULT; // System default hover time
            if (TrackMouseEvent(&tme))
            {
                g_mouseTrack = TRUE;
            }
        }
        break;

    case WM_LBUTTONUP:
        lButtonDown = FALSE;
        g_ptStart.x = 0;
        g_ptStart.y = 0;
        break;

    case WM_MOUSEMOVE:
        g_wParam = msg->wParam;
        g_ptCurrent.x = GET_X_LPARAM(msg->lParam);
        g_ptCurrent.y = GET_Y_LPARAM(msg->lParam);
        break;

    case WM_MOUSELEAVE:
        lButtonDown = FALSE;
        g_mouseTrack = FALSE;
        break;

    default:
        break;
    }

    return handled;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

XMMATRIX rotation;

void GenerateSpaceballRotationMatrix()
{
    static XMFLOAT3 Vcur, Vlast;
    XMVECTOR Vcross;

    Vlast = Vcur;
    Vcur = XMFLOAT3(SbState.rx, SbState.ry, SbState.rz);
    float limit = 1000.0f;

    if (fabsf(Vcur.x - Vlast.x) > limit || fabsf(Vcur.y - Vlast.y) > limit || fabsf(Vcur.z - Vlast.z) > limit)
    {
        Vcross = XMVector3Cross(XMLoadFloat3(&Vcur), XMLoadFloat3(&Vlast));
    }

    rotation = XMMatrixRotationRollPitchYaw(
        -16.0f * XM_2PI * (float)SbState.rx / (float)MAXWORD,
        -16.0f * XM_2PI * (float)SbState.ry / (float)MAXWORD,
        -16.0f * XM_2PI * (float)SbState.rz / (float)MAXWORD);

    float cos_x = cosf(-16.0f * XM_2PI * (float)SbState.rz / (float)MAXWORD);
    float cos_y = cosf(16.0f * XM_2PI * (float)SbState.ry / (float)MAXWORD);
    float cos_z = cosf(-16.0f * XM_2PI * (float)SbState.rx / (float)MAXWORD);
    float sin_x = sinf(-16.0f * XM_2PI * (float)SbState.rz / (float)MAXWORD);
    float sin_y = sinf(16.0f * XM_2PI * (float)SbState.ry / (float)MAXWORD);
    float sin_z = sinf(-16.0f * XM_2PI * (float)SbState.rx / (float)MAXWORD);

    g_World = XMMATRIX(
        cos_x * cos_y , sin_x * cos_z - cos_x * sin_y * sin_z, sin_x * sin_z + cos_x * sin_y * cos_z, 0,
        -sin_x * cos_y, cos_x * cos_z + sin_x * sin_y * sin_z, cos_x * sin_z - sin_x * sin_y * cos_z, 0,
        -sin_y        , - cos_y * sin_z                      , cos_y * cos_z,                         0,
        0, 0, 0, 1) * XMMatrixTranslation((float)SbState.tx / 256.0f, (float)SbState.ty / 256.0f, (float)SbState.tz / 256.0f);
    

}

// generate virtual trackball rotation matrix based on current mouse pointer
BOOL GenerateRotationMatrix()
{
    RECT rect;
    XMFLOAT3 vStart, vCurrent;

    if (!GetWindowRect(g_hWnd, &rect))
    {
        return FALSE;
    }

    LONG midX = (rect.right - rect.left) / 2;
    LONG midY = (rect.bottom - rect.top) / 2;

    // Scale X and Y by 1.2 to give zone at edges of window for pure rotation about Z axis
    vStart.x = 1.2f * (float)(g_ptStart.x - midX) / (float)midX;
    vStart.y = -1.2f * (float)(g_ptStart.y - midY) / (float)midY;
    float startXYsquared = (vStart.x * vStart.x) + (vStart.y * vStart.y);
    
    vCurrent.x = 1.2f * (float)(g_ptCurrent.x - midX) / (float)midX;
    vCurrent.y = -1.2f * (float)(g_ptCurrent.y - midY) / (float)midY;
    float currentXYsquared = (vCurrent.x * vCurrent.x) + (vCurrent.y * vCurrent.y);

    if (startXYsquared > 1.0f)
    {
        // XY is outside of unit circle, scale back and assume Z is zero
        vStart.x = vStart.x / sqrtf(startXYsquared);
        vStart.y = vStart.y / sqrtf(startXYsquared);
        vStart.z = 0.0f;
    }
    else
    {
        // Calculate Z of vector inside unit sphere
        vStart.z = -sqrtf(1.0f - startXYsquared);
    }

    if (currentXYsquared > 1.0f)
    {
        // XY is outside of unit circle, scale back and assume Z is zero
        vCurrent.x = vCurrent.x / sqrtf(currentXYsquared);
        vCurrent.y = vCurrent.y / sqrtf(currentXYsquared);
        vCurrent.z = 0.0f;
    }
    else
    {
        // Calculate Z of vector inside unit sphere
        vCurrent.z = -sqrtf(1.0f - currentXYsquared);
    }

    XMVECTOR rotationVector = XMVector3Cross(XMLoadFloat3(&vStart), XMLoadFloat3(&vCurrent));

    // Compute rotation angle by magnitude of rotationVector and scale
    float angle = XMVectorGetX(XMVector3Length(rotationVector)) * XM_PI;

    if (fabsf(angle) > 0.0001f)
    {
        // Only generate rotation matrix from a non-zero length rotation vector.
        g_World = XMMatrixRotationAxis(rotationVector, angle);
    }
    else
    {
        g_World = XMMatrixIdentity();
    }

    return TRUE;
}

void Render()
{
    // Update our time
    static float t = 0.0f;
    if (g_driverType == D3D_DRIVER_TYPE_REFERENCE)
    {
        t += (float)XM_PI * 0.0125f;
    }
    else if (lButtonDown != TRUE)
    {
        static DWORD dwTimeStart = 0;
        DWORD dwTimeCur = GetTickCount();
        if (dwTimeStart == 0)
            dwTimeStart = dwTimeCur;

        t = (dwTimeCur - dwTimeStart) / 1000.0f;
    }

    static int16_t lastButtons = 0;

    if ((SbButtons.buttons & SB_BTN_PICK) && !(lastButtons & SB_BTN_PICK))
    {
        g_SpaceballPick = !g_SpaceballPick;
    }

    if ((SbButtons.buttons & SB_BTN_1) && !(lastButtons & SB_BTN_1))
    {
        SetDeviceSensitivity(0x3F);
    }

    if ((SbButtons.buttons & SB_BTN_2) && !(lastButtons & SB_BTN_2))
    {
        SetDeviceSensitivity(0x2A);
    }

    if ((SbButtons.buttons & SB_BTN_3) && !(lastButtons & SB_BTN_3))
    {
        SetDeviceSensitivity(0x15);
    }

    if ((SbButtons.buttons & SB_BTN_4) && !(lastButtons & SB_BTN_4))
    {
        SetDeviceSensitivity(0x00);
    }

    if ((SbButtons.buttons & SB_BTN_5) && !(lastButtons & SB_BTN_5))
    {
        SetNullRadius(0x3F);
    }

    if ((SbButtons.buttons & SB_BTN_6) && !(lastButtons & SB_BTN_6))
    {
        SetNullRadius(0x2A);
    }

    if ((SbButtons.buttons & SB_BTN_7) && !(lastButtons & SB_BTN_7))
    {
        SetNullRadius(0x15);
    }

    if ((SbButtons.buttons & SB_BTN_8) && !(lastButtons & SB_BTN_8))
    {
        SetNullRadius(0x00);
    }

    lastButtons = SbButtons.buttons;

    if (lButtonDown)
    {
        GenerateRotationMatrix();
    }
    else if (g_SpaceballPick) // (SbState.rx != 0 || SbState.ry != 0 || SbState.rz != 0)
    {
        GenerateSpaceballRotationMatrix();
    }
    else
    {
        // Animate the cube

        g_World = XMMatrixRotationY(t);
    }

    // clear back buffer

    float clearColor[4] = { 0.0f, 0.125f, 0.6f, 1.0f }; // RGBA
    g_pd3dDeviceContext->ClearRenderTargetView(g_pRenderTargetView, clearColor);

    // Update constants
    // Note: matrices are transposed because HLSL runs faster with the transposed matrix and having the vector be on the right - matrix x v 
    // rather than v x matrix.
    ConstantBuffer cb;
    cb.mWorld = XMMatrixTranspose(g_World);
    cb.mView = XMMatrixTranspose(g_View);
    cb.mProjection = XMMatrixTranspose(g_Projection);
    g_pd3dDeviceContext->UpdateSubresource(g_pConstantBuffer, 0, nullptr, &cb, 0, 0);

    // Render cube

    g_pd3dDeviceContext->VSSetShader(g_pVertexShader, nullptr, 0);
    g_pd3dDeviceContext->VSSetConstantBuffers(0, 1, &g_pConstantBuffer);
    g_pd3dDeviceContext->PSSetShader(g_pPixelShader, nullptr, 0);
    g_pd3dDeviceContext->DrawIndexed(36, 0, 0);

    // Present back buffer to our front buffer
    
    g_pSwapChain->Present(0, 0);
}