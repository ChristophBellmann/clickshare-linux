/*
 * csdup.dll - adds IDXGIOutput1::DuplicateOutput / IDXGIOutput5::DuplicateOutput1
 * to Wine's dxgi (which only has stubs) so that the ClickShare app can capture
 * the desktop. Frames are grabbed with GDI BitBlt and uploaded into a D3D11
 * texture on the caller's device.
 *
 * The DLL patches the (shared) IDXGIOutput vtable of Wine's dxgi when loaded.
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#include <stdio.h>

static HRESULT (STDMETHODCALLTYPE *orig_output_GetDesc)(IDXGIOutput1 *, DXGI_OUTPUT_DESC *);

static void logmsg(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    FILE *f = fopen("C:\\csdup.log", "a");
    if (f) { fputs(buf, f); fputc('\n', f); fclose(f); }
}

/* ---------------------------------------------------------------- */

typedef struct dup
{
    IDXGIOutputDuplication iface;
    LONG ref;
    IDXGIOutput1 *output;
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    ID3D11Texture2D *texture;
    RECT rect;
    int width, height;
    HDC screen_dc, mem_dc;
    HBITMAP dib;
    void *bits;
    LARGE_INTEGER last, freq;
    BOOL frame_held;
} dup;

static inline dup *impl(IDXGIOutputDuplication *iface)
{
    return CONTAINING_RECORD(iface, dup, iface);
}

static HRESULT STDMETHODCALLTYPE d_QueryInterface(IDXGIOutputDuplication *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDXGIObject)
            || IsEqualGUID(riid, &IID_IDXGIOutputDuplication))
    {
        IDXGIOutputDuplication_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d_AddRef(IDXGIOutputDuplication *iface)
{
    return InterlockedIncrement(&impl(iface)->ref);
}

static ULONG STDMETHODCALLTYPE d_Release(IDXGIOutputDuplication *iface)
{
    dup *d = impl(iface);
    ULONG ref = InterlockedDecrement(&d->ref);
    if (!ref)
    {
        if (d->texture) ID3D11Texture2D_Release(d->texture);
        if (d->context) ID3D11DeviceContext_Release(d->context);
        if (d->device) ID3D11Device_Release(d->device);
        if (d->output) IDXGIOutput1_Release(d->output);
        if (d->dib) DeleteObject(d->dib);
        if (d->mem_dc) DeleteDC(d->mem_dc);
        if (d->screen_dc) ReleaseDC(NULL, d->screen_dc);
        HeapFree(GetProcessHeap(), 0, d);
        logmsg("csdup: duplication released");
    }
    return ref;
}

static HRESULT STDMETHODCALLTYPE d_SetPrivateData(IDXGIOutputDuplication *iface, REFGUID g, UINT s, const void *p)
{ return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE d_SetPrivateDataInterface(IDXGIOutputDuplication *iface, REFGUID g, const IUnknown *p)
{ return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE d_GetPrivateData(IDXGIOutputDuplication *iface, REFGUID g, UINT *s, void *p)
{ return E_NOTIMPL; }

static HRESULT STDMETHODCALLTYPE d_GetParent(IDXGIOutputDuplication *iface, REFIID riid, void **parent)
{
    return IDXGIOutput1_QueryInterface(impl(iface)->output, riid, parent);
}

static void STDMETHODCALLTYPE d_GetDesc(IDXGIOutputDuplication *iface, DXGI_OUTDUPL_DESC *desc)
{
    dup *d = impl(iface);
    memset(desc, 0, sizeof(*desc));
    desc->ModeDesc.Width = d->width;
    desc->ModeDesc.Height = d->height;
    desc->ModeDesc.RefreshRate.Numerator = 60;
    desc->ModeDesc.RefreshRate.Denominator = 1;
    desc->ModeDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc->Rotation = DXGI_MODE_ROTATION_IDENTITY;
    desc->DesktopImageInSystemMemory = FALSE;
}

static void grab(dup *d)
{
    CURSORINFO ci = { sizeof(ci) };

    BitBlt(d->mem_dc, 0, 0, d->width, d->height, d->screen_dc, d->rect.left, d->rect.top, SRCCOPY | CAPTUREBLT);
    if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING))
    {
        ICONINFO ii;
        if (GetIconInfo(ci.hCursor, &ii))
        {
            DrawIconEx(d->mem_dc, ci.ptScreenPos.x - d->rect.left - (int)ii.xHotspot,
                    ci.ptScreenPos.y - d->rect.top - (int)ii.yHotspot, ci.hCursor, 0, 0, 0, NULL, DI_NORMAL);
            if (ii.hbmMask) DeleteObject(ii.hbmMask);
            if (ii.hbmColor) DeleteObject(ii.hbmColor);
        }
    }
    GdiFlush();
    /* GDI leaves alpha at 0; make the image opaque. */
    {
        DWORD *p = d->bits, *end = p + d->width * d->height;
        for (; p < end; ++p) *p |= 0xff000000;
    }
    ID3D11DeviceContext_UpdateSubresource(d->context, (ID3D11Resource *)d->texture, 0, NULL,
            d->bits, d->width * 4, 0);
}

static HRESULT STDMETHODCALLTYPE d_AcquireNextFrame(IDXGIOutputDuplication *iface, UINT timeout,
        DXGI_OUTDUPL_FRAME_INFO *info, IDXGIResource **resource)
{
    dup *d = impl(iface);
    const LONGLONG interval = d->freq.QuadPart / 30; /* ~30 fps */
    LARGE_INTEGER now;
    LONGLONG wait_ms;

    QueryPerformanceCounter(&now);
    wait_ms = (d->last.QuadPart + interval - now.QuadPart) * 1000 / d->freq.QuadPart;
    if (wait_ms > 0)
    {
        if ((UINT)wait_ms > timeout)
        {
            Sleep(timeout);
            return DXGI_ERROR_WAIT_TIMEOUT;
        }
        Sleep((DWORD)wait_ms);
        QueryPerformanceCounter(&now);
    }
    d->last = now;

    grab(d);

    memset(info, 0, sizeof(*info));
    info->LastPresentTime = now;
    info->AccumulatedFrames = 1;
    info->TotalMetadataBufferSize = sizeof(RECT);
    info->PointerPosition.Visible = FALSE;

    d->frame_held = TRUE;
    return ID3D11Texture2D_QueryInterface(d->texture, &IID_IDXGIResource, (void **)resource);
}

static HRESULT STDMETHODCALLTYPE d_GetFrameDirtyRects(IDXGIOutputDuplication *iface, UINT size,
        RECT *rects, UINT *required)
{
    dup *d = impl(iface);
    *required = sizeof(RECT);
    if (size < sizeof(RECT)) return DXGI_ERROR_MORE_DATA;
    SetRect(rects, 0, 0, d->width, d->height);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_GetFrameMoveRects(IDXGIOutputDuplication *iface, UINT size,
        DXGI_OUTDUPL_MOVE_RECT *rects, UINT *required)
{
    *required = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_GetFramePointerShape(IDXGIOutputDuplication *iface, UINT size,
        void *buffer, UINT *required, DXGI_OUTDUPL_POINTER_SHAPE_INFO *info)
{
    *required = 0;
    if (info) memset(info, 0, sizeof(*info));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_MapDesktopSurface(IDXGIOutputDuplication *iface, DXGI_MAPPED_RECT *rect)
{ return DXGI_ERROR_UNSUPPORTED; }
static HRESULT STDMETHODCALLTYPE d_UnMapDesktopSurface(IDXGIOutputDuplication *iface)
{ return DXGI_ERROR_INVALID_CALL; }

static HRESULT STDMETHODCALLTYPE d_ReleaseFrame(IDXGIOutputDuplication *iface)
{
    impl(iface)->frame_held = FALSE;
    return S_OK;
}

static IDXGIOutputDuplicationVtbl dup_vtbl =
{
    d_QueryInterface, d_AddRef, d_Release,
    d_SetPrivateData, d_SetPrivateDataInterface, d_GetPrivateData, d_GetParent,
    d_GetDesc, d_AcquireNextFrame, d_GetFrameDirtyRects, d_GetFrameMoveRects,
    d_GetFramePointerShape, d_MapDesktopSurface, d_UnMapDesktopSurface, d_ReleaseFrame,
};

static HRESULT create_dup(IDXGIOutput1 *output, IUnknown *device, IDXGIOutputDuplication **out)
{
    DXGI_OUTPUT_DESC odesc;
    D3D11_TEXTURE2D_DESC tdesc = {0};
    BITMAPINFO bmi = {0};
    HRESULT hr;
    dup *d;

    *out = NULL;
    if (!device) return E_INVALIDARG;
    if (FAILED(hr = orig_output_GetDesc(output, &odesc))) return hr;

    d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    d->iface.lpVtbl = &dup_vtbl;
    d->ref = 1;
    d->rect = odesc.DesktopCoordinates;
    d->width = d->rect.right - d->rect.left;
    d->height = d->rect.bottom - d->rect.top;
    IDXGIOutput1_AddRef(output);
    d->output = output;
    QueryPerformanceFrequency(&d->freq);

    if (FAILED(hr = IUnknown_QueryInterface(device, &IID_ID3D11Device, (void **)&d->device)))
    {
        logmsg("csdup: device is not a D3D11 device (hr %#lx)", hr);
        d_Release(&d->iface);
        return hr;
    }
    ID3D11Device_GetImmediateContext(d->device, &d->context);

    tdesc.Width = d->width;
    tdesc.Height = d->height;
    tdesc.MipLevels = 1;
    tdesc.ArraySize = 1;
    tdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    tdesc.SampleDesc.Count = 1;
    tdesc.Usage = D3D11_USAGE_DEFAULT;
    tdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    if (FAILED(hr = ID3D11Device_CreateTexture2D(d->device, &tdesc, NULL, &d->texture)))
    {
        logmsg("csdup: CreateTexture2D failed (hr %#lx)", hr);
        d_Release(&d->iface);
        return hr;
    }

    d->screen_dc = GetDC(NULL);
    d->mem_dc = CreateCompatibleDC(d->screen_dc);
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = d->width;
    bmi.bmiHeader.biHeight = -d->height; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    d->dib = CreateDIBSection(d->mem_dc, &bmi, DIB_RGB_COLORS, &d->bits, NULL, 0);
    SelectObject(d->mem_dc, d->dib);

    logmsg("csdup: duplication created %dx%d at %ld,%ld", d->width, d->height, d->rect.left, d->rect.top);
    *out = &d->iface;
    return S_OK;
}

/* ---------------------------------------------------------------- */

static HRESULT STDMETHODCALLTYPE hook_DuplicateOutput(IDXGIOutput1 *iface, IUnknown *device,
        IDXGIOutputDuplication **dup)
{
    logmsg("csdup: DuplicateOutput called");
    return create_dup(iface, device, dup);
}

static HRESULT STDMETHODCALLTYPE hook_DuplicateOutput1(IDXGIOutput5 *iface, IUnknown *device, UINT flags,
        UINT format_count, const DXGI_FORMAT *formats, IDXGIOutputDuplication **dup)
{
    logmsg("csdup: DuplicateOutput1 called");
    return create_dup((IDXGIOutput1 *)iface, device, dup);
}

static void patch(void **slot, void *fn)
{
    DWORD old;
    VirtualProtect(slot, sizeof(void *), PAGE_EXECUTE_READWRITE, &old);
    *slot = fn;
    VirtualProtect(slot, sizeof(void *), old, &old);
}

static DWORD WINAPI install(void *arg)
{
    IDXGIFactory1 *factory;
    IDXGIAdapter1 *adapter;
    IDXGIOutput *output;
    IDXGIOutput1 *output1;
    IDXGIOutput5 *output5;
    HRESULT hr;

    if (FAILED(hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory)))
    {
        logmsg("csdup: CreateDXGIFactory1 failed %#lx", hr);
        return 1;
    }
    if (FAILED(hr = IDXGIFactory1_EnumAdapters1(factory, 0, &adapter)))
    {
        logmsg("csdup: EnumAdapters1 failed %#lx", hr);
        IDXGIFactory1_Release(factory);
        return 1;
    }
    if (FAILED(hr = IDXGIAdapter1_EnumOutputs(adapter, 0, &output)))
    {
        logmsg("csdup: EnumOutputs failed %#lx", hr);
        IDXGIAdapter1_Release(adapter);
        IDXGIFactory1_Release(factory);
        return 1;
    }
    if (SUCCEEDED(IDXGIOutput_QueryInterface(output, &IID_IDXGIOutput1, (void **)&output1)))
    {
        orig_output_GetDesc = output1->lpVtbl->GetDesc;
        patch((void **)&output1->lpVtbl->DuplicateOutput, hook_DuplicateOutput);
        IDXGIOutput1_Release(output1);
        logmsg("csdup: patched DuplicateOutput");
    }
    if (SUCCEEDED(IDXGIOutput_QueryInterface(output, &IID_IDXGIOutput5, (void **)&output5)))
    {
        patch((void **)&output5->lpVtbl->DuplicateOutput1, hook_DuplicateOutput1);
        IDXGIOutput5_Release(output5);
        logmsg("csdup: patched DuplicateOutput1");
    }
    IDXGIOutput_Release(output);
    IDXGIAdapter1_Release(adapter);
    IDXGIFactory1_Release(factory);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        HANDLE t;
        DisableThreadLibraryCalls(inst);
        /* Pin ourselves: the vtable will point into this module. */
        {
            HMODULE self;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                    (LPCWSTR)DllMain, &self);
        }
        if ((t = CreateThread(NULL, 0, install, NULL, 0, NULL))) CloseHandle(t);
    }
    return TRUE;
}
