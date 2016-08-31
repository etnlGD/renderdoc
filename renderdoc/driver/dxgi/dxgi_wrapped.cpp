/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2015-2016 Baldur Karlsson
 * Copyright (c) 2014 Crytek
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "driver/dxgi/dxgi_wrapped.h"
#include <stddef.h>
#include <stdio.h>
#include "core/core.h"
#include "serialise/serialiser.h"

string ToStrHelper<false, IID>::Get(const IID &el)
{
  char tostrBuf[256] = {0};
  StringFormat::snprintf(tostrBuf, 255, "GUID {%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                         el.Data1, (unsigned int)el.Data2, (unsigned int)el.Data3, el.Data4[0],
                         el.Data4[1], el.Data4[2], el.Data4[3], el.Data4[4], el.Data4[5],
                         el.Data4[6], el.Data4[7]);

  return tostrBuf;
}

WRAPPED_POOL_INST(WrappedIDXGIDevice);
WRAPPED_POOL_INST(WrappedIDXGIDevice1);
WRAPPED_POOL_INST(WrappedIDXGIDevice2);
WRAPPED_POOL_INST(WrappedIDXGIDevice3);

std::vector<D3DDeviceCallback> WrappedIDXGISwapChain3::m_D3DCallbacks;

ID3DDevice *GetD3DDevice(IUnknown *pDevice)
{
  ID3DDevice *wrapDevice = NULL;

  if(WrappedIDXGIDevice::IsAlloc(pDevice) || WrappedIDXGIDevice1::IsAlloc(pDevice) ||
     WrappedIDXGIDevice2::IsAlloc(pDevice) || WrappedIDXGIDevice3::IsAlloc(pDevice))
  {
    if(WrappedIDXGIDevice::IsAlloc(pDevice))
      wrapDevice = ((WrappedIDXGIDevice *)(IDXGIDevice *)pDevice)->GetD3DDevice();
    if(WrappedIDXGIDevice1::IsAlloc(pDevice))
      wrapDevice = ((WrappedIDXGIDevice1 *)(IDXGIDevice1 *)pDevice)->GetD3DDevice();
    if(WrappedIDXGIDevice2::IsAlloc(pDevice))
      wrapDevice = ((WrappedIDXGIDevice2 *)(IDXGIDevice2 *)pDevice)->GetD3DDevice();
    if(WrappedIDXGIDevice3::IsAlloc(pDevice))
      wrapDevice = ((WrappedIDXGIDevice3 *)(IDXGIDevice3 *)pDevice)->GetD3DDevice();
  }

  if(wrapDevice == NULL)
    wrapDevice = WrappedIDXGISwapChain3::GetD3DDevice(pDevice);

  return wrapDevice;
}

HRESULT WrappedIDXGIFactory::staticCreateSwapChain(IDXGIFactory *factory, IUnknown *pDevice,
                                                   DXGI_SWAP_CHAIN_DESC *pDesc,
                                                   IDXGISwapChain **ppSwapChain)
{
  ID3DDevice *wrapDevice = GetD3DDevice(pDevice);

  if(wrapDevice)
  {
    if(!RenderDoc::Inst().GetCaptureOptions().AllowFullscreen && pDesc)
    {
      pDesc->Windowed = TRUE;
    }

    HRESULT ret = factory->CreateSwapChain(wrapDevice->GetRealIUnknown(), pDesc, ppSwapChain);

    if(SUCCEEDED(ret))
    {
      *ppSwapChain =
          new WrappedIDXGISwapChain3(*ppSwapChain, pDesc ? pDesc->OutputWindow : NULL, wrapDevice);
    }

    return ret;
  }

  RDCERR("Creating swap chain with non-hooked device!");

  return factory->CreateSwapChain(pDevice, pDesc, ppSwapChain);
}

HRESULT WrappedIDXGIFactory2::staticCreateSwapChainForHwnd(
    IDXGIFactory2 *factory, IUnknown *pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc, IDXGIOutput *pRestrictToOutput,
    IDXGISwapChain1 **ppSwapChain)
{
  ID3DDevice *wrapDevice = GetD3DDevice(pDevice);

  if(wrapDevice)
  {
    if(!RenderDoc::Inst().GetCaptureOptions().AllowFullscreen && pFullscreenDesc)
    {
      pFullscreenDesc = NULL;
    }

    HRESULT ret = factory->CreateSwapChainForHwnd(wrapDevice->GetRealIUnknown(), hWnd, pDesc,
                                                  pFullscreenDesc, pRestrictToOutput, ppSwapChain);

    if(SUCCEEDED(ret))
    {
      *ppSwapChain = new WrappedIDXGISwapChain3(*ppSwapChain, hWnd, wrapDevice);
    }

    return ret;
  }
  else
  {
    RDCERR("Creating swap chain with non-hooked device!");
  }

  return factory->CreateSwapChainForHwnd(pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput,
                                         ppSwapChain);
}

HRESULT WrappedIDXGIFactory2::staticCreateSwapChainForCoreWindow(IDXGIFactory2 *factory,
                                                                 IUnknown *pDevice, IUnknown *pWindow,
                                                                 const DXGI_SWAP_CHAIN_DESC1 *pDesc,
                                                                 IDXGIOutput *pRestrictToOutput,
                                                                 IDXGISwapChain1 **ppSwapChain)
{
  ID3DDevice *wrapDevice = GetD3DDevice(pDevice);

  if(!RenderDoc::Inst().GetCaptureOptions().AllowFullscreen)
  {
    RDCWARN("Impossible to disallow fullscreen on call to CreateSwapChainForCoreWindow");
  }

  if(wrapDevice)
  {
    HRESULT ret = factory->CreateSwapChainForCoreWindow(wrapDevice->GetRealIUnknown(), pWindow,
                                                        pDesc, pRestrictToOutput, ppSwapChain);

    if(SUCCEEDED(ret))
    {
      HWND wnd = NULL;
      (*ppSwapChain)->GetHwnd(&wnd);
      *ppSwapChain = new WrappedIDXGISwapChain3(*ppSwapChain, wnd, wrapDevice);
    }

    return ret;
  }
  else
  {
    RDCERR("Creating swap chain with non-hooked device!");
  }

  return factory->CreateSwapChainForCoreWindow(pDevice, pWindow, pDesc, pRestrictToOutput,
                                               ppSwapChain);
}

HRESULT WrappedIDXGIFactory2::staticCreateSwapChainForComposition(IDXGIFactory2 *factory,
                                                                  IUnknown *pDevice,
                                                                  const DXGI_SWAP_CHAIN_DESC1 *pDesc,
                                                                  IDXGIOutput *pRestrictToOutput,
                                                                  IDXGISwapChain1 **ppSwapChain)
{
  ID3DDevice *wrapDevice = GetD3DDevice(pDevice);

  if(!RenderDoc::Inst().GetCaptureOptions().AllowFullscreen)
  {
    RDCWARN("Impossible to disallow fullscreen on call to CreateSwapChainForComposition");
  }

  if(wrapDevice)
  {
    HRESULT ret = factory->CreateSwapChainForComposition(wrapDevice->GetRealIUnknown(), pDesc,
                                                         pRestrictToOutput, ppSwapChain);

    if(SUCCEEDED(ret))
    {
      HWND wnd = NULL;
      (*ppSwapChain)->GetHwnd(&wnd);
      *ppSwapChain = new WrappedIDXGISwapChain3(*ppSwapChain, wnd, wrapDevice);
    }

    return ret;
  }
  else
  {
    RDCERR("Creating swap chain with non-hooked device!");
  }

  return factory->CreateSwapChainForComposition(pDevice, pDesc, pRestrictToOutput, ppSwapChain);
}

WrappedIDXGISwapChain3::WrappedIDXGISwapChain3(IDXGISwapChain *real, HWND wnd, ID3DDevice *device)
    : RefCountDXGIObject(real), m_pReal(real), m_pDevice(device), m_iRefcount(1), m_Wnd(wnd)
{
  DXGI_SWAP_CHAIN_DESC desc;
  real->GetDesc(&desc);

  m_pDevice->AddRef();

  m_pReal1 = NULL;
  real->QueryInterface(__uuidof(IDXGISwapChain1), (void **)&m_pReal1);
  m_pReal2 = NULL;
  real->QueryInterface(__uuidof(IDXGISwapChain2), (void **)&m_pReal2);
  m_pReal3 = NULL;
  real->QueryInterface(__uuidof(IDXGISwapChain3), (void **)&m_pReal3);

  WrapBuffersAfterResize();

  // we do a 'fake' present right at the start, so that we can capture frame 1, by
  // going from this fake present to the first present.
  m_pDevice->FirstFrame(this);
}

WrappedIDXGISwapChain3::~WrappedIDXGISwapChain3()
{
  m_pDevice->ReleaseSwapchainResources(this);

  SAFE_RELEASE(m_pDevice);

  SAFE_RELEASE(m_pReal1);
  SAFE_RELEASE(m_pReal2);
  SAFE_RELEASE(m_pReal3);
  SAFE_RELEASE(m_pReal);
}

void WrappedIDXGISwapChain3::ReleaseBuffersForResize()
{
  m_pDevice->ReleaseSwapchainResources(this);
}

void WrappedIDXGISwapChain3::WrapBuffersAfterResize()
{
  DXGI_SWAP_CHAIN_DESC desc;
  m_pReal->GetDesc(&desc);

  int bufCount = desc.BufferCount;

  if(desc.SwapEffect == DXGI_SWAP_EFFECT_DISCARD)
    bufCount = 1;

  RDCASSERT(bufCount < MAX_NUM_BACKBUFFERS);

  for(int i = 0; i < MAX_NUM_BACKBUFFERS; i++)
  {
    m_pBackBuffers[i] = NULL;

    if(i < bufCount)
    {
      GetBuffer(i, m_pDevice->GetBackbufferUUID(), (void **)&m_pBackBuffers[i]);
      m_pDevice->NewSwapchainBuffer(m_pBackBuffers[i]);
    }
  }
}

HRESULT WrappedIDXGISwapChain3::ResizeBuffers(
    /* [in] */ UINT BufferCount,
    /* [in] */ UINT Width,
    /* [in] */ UINT Height,
    /* [in] */ DXGI_FORMAT NewFormat,
    /* [in] */ UINT SwapChainFlags)
{
  ReleaseBuffersForResize();

  HRESULT ret = m_pReal->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);

  WrapBuffersAfterResize();

  return ret;
}

HRESULT WrappedIDXGISwapChain3::ResizeBuffers1(_In_ UINT BufferCount, _In_ UINT Width,
                                               _In_ UINT Height, _In_ DXGI_FORMAT Format,
                                               _In_ UINT SwapChainFlags,
                                               _In_reads_(BufferCount) const UINT *pCreationNodeMask,
                                               _In_reads_(BufferCount)
                                                   IUnknown *const *ppPresentQueue)
{
  ReleaseBuffersForResize();

  HRESULT ret = m_pReal3->ResizeBuffers1(BufferCount, Width, Height, Format, SwapChainFlags,
                                         pCreationNodeMask, ppPresentQueue);

  WrapBuffersAfterResize();

  return ret;
}

HRESULT WrappedIDXGISwapChain3::SetFullscreenState(
    /* [in] */ BOOL Fullscreen,
    /* [in] */ IDXGIOutput *pTarget)
{
  if(RenderDoc::Inst().GetCaptureOptions().AllowFullscreen)
    return m_pReal->SetFullscreenState(Fullscreen, pTarget);

  return S_OK;
}

HRESULT WrappedIDXGISwapChain3::GetFullscreenState(
    /* [out] */ BOOL *pFullscreen,
    /* [out] */ IDXGIOutput **ppTarget)
{
  return m_pReal->GetFullscreenState(pFullscreen, ppTarget);
}

HRESULT WrappedIDXGISwapChain3::GetBuffer(
    /* [in] */ UINT Buffer,
    /* [in] */ REFIID riid,
    /* [out][in] */ void **ppSurface)
{
  if(ppSurface == NULL)
    return E_INVALIDARG;

  // ID3D10Texture2D UUID {9B7E4C04-342C-4106-A19F-4F2704F689F0}
  static const GUID ID3D10Texture2D_uuid = {
      0x9b7e4c04, 0x342c, 0x4106, {0xa1, 0x9f, 0x4f, 0x27, 0x04, 0xf6, 0x89, 0xf0}};

  // ID3D10Resource  UUID {9B7E4C01-342C-4106-A19F-4F2704F689F0}
  static const GUID ID3D10Resource_uuid = {
      0x9b7e4c01, 0x342c, 0x4106, {0xa1, 0x9f, 0x4f, 0x27, 0x04, 0xf6, 0x89, 0xf0}};

  if(riid == ID3D10Texture2D_uuid || riid == ID3D10Resource_uuid)
  {
    RDCERR("Querying swapchain buffers via D3D10 interface UUIDs is not supported");
    return E_NOINTERFACE;
  }
  else if(riid != __uuidof(ID3D11Texture2D) && riid != __uuidof(ID3D11Resource) &&
          riid != __uuidof(ID3D12Resource))
  {
    RDCERR("Unsupported or unrecognised UUID passed to IDXGISwapChain::GetBuffer - %s",
           ToStr::Get(riid).c_str());
    return E_NOINTERFACE;
  }

  RDCASSERT(riid == __uuidof(ID3D11Texture2D) || riid == __uuidof(ID3D11Resource) ||
            riid == __uuidof(ID3D12Resource));

  HRESULT ret = m_pReal->GetBuffer(Buffer, riid, ppSurface);

  {
    IUnknown *realSurface = (IUnknown *)*ppSurface;
    IUnknown *tex = realSurface;

    if(FAILED(ret))
    {
      RDCERR("Failed to get swapchain backbuffer %d: %08x", Buffer, ret);
      SAFE_RELEASE(realSurface);
      tex = NULL;
    }
    else
    {
      DXGI_SWAP_CHAIN_DESC desc;
      GetDesc(&desc);
      tex = m_pDevice->WrapSwapchainBuffer(this, &desc, Buffer, realSurface);
    }

    *ppSurface = tex;
  }

  return ret;
}

HRESULT WrappedIDXGISwapChain3::GetDevice(
    /* [in] */ REFIID riid,
    /* [retval][out] */ void **ppDevice)
{
  HRESULT ret = m_pReal->GetDevice(riid, ppDevice);

  if(SUCCEEDED(ret))
  {
    // try one of the trivial wraps, we don't mind making a new one of those
    if(riid == m_pDevice->GetDeviceUUID())
    {
      // probably they're asking for the device device.
      *ppDevice = m_pDevice->GetDeviceInterface();
      m_pDevice->AddRef();
    }
    else if(riid == __uuidof(IDXGISwapChain))
    {
      // don't think anyone would try this, but what the hell.
      *ppDevice = this;
      AddRef();
    }
    else if(!HandleWrap(riid, ppDevice))
    {
      // can probably get away with returning the real result here,
      // but it worries me a bit.
      RDCUNIMPLEMENTED("Not returning trivial type");
    }
  }

  return ret;
}

HRESULT WrappedIDXGISwapChain3::Present(
    /* [in] */ UINT SyncInterval,
    /* [in] */ UINT Flags)
{
  if(!RenderDoc::Inst().GetCaptureOptions().AllowVSync)
  {
    SyncInterval = 0;
  }

  m_pDevice->Present(this, SyncInterval, Flags);

  return m_pReal->Present(SyncInterval, Flags);
}

HRESULT WrappedIDXGISwapChain3::Present1(UINT SyncInterval, UINT Flags,
                                         const DXGI_PRESENT_PARAMETERS *pPresentParameters)
{
  if(!RenderDoc::Inst().GetCaptureOptions().AllowVSync)
  {
    SyncInterval = 0;
  }

  m_pDevice->Present(this, SyncInterval, Flags);

  return m_pReal1->Present1(SyncInterval, Flags, pPresentParameters);
}

bool RefCountDXGIObject::HandleWrap(REFIID riid, void **ppvObject)
{
  if(ppvObject == NULL || *ppvObject == NULL)
  {
    RDCWARN("HandleWrap called with NULL ppvObject");
    return false;
  }

  if(riid == __uuidof(IDXGIDevice))
  {
    // should have been handled elsewhere, so we can properly create this device
    RDCERR("Unexpected uuid in RefCountDXGIObject::HandleWrap");
    return false;
  }
  else if(riid == __uuidof(IDXGIAdapter))
  {
    IDXGIAdapter *real = (IDXGIAdapter *)(*ppvObject);
    *ppvObject = (IDXGIAdapter *)(new WrappedIDXGIAdapter(real));
    return true;
  }
  else if(riid == __uuidof(IDXGIFactory))
  {
    // yes I know PRECISELY how fucked up this is. Speak to microsoft - after KB2670838 the internal
    // D3D11 device creation function will pass in __uuidof(IDXGIFactory) then attempt to call
    // EnumDevices1 (which is in the IDXGIFactory1 vtable). Doing this *should* be safe as using a
    // IDXGIFactory1 like a IDXGIFactory should all just work by definition, but there's no way to
    // know now if someone trying to create a IDXGIFactory really means it or not.
    IDXGIFactory1 *real = (IDXGIFactory1 *)(*ppvObject);
    *ppvObject = (IDXGIFactory *)(new WrappedIDXGIFactory1(real));
    return true;
  }

  else if(riid == __uuidof(IDXGIDevice1))
  {
    // should have been handled elsewhere, so we can properly create this device
    RDCERR("Unexpected uuid in RefCountDXGIObject::HandleWrap");
    return false;
  }
  else if(riid == __uuidof(IDXGIAdapter1))
  {
    IDXGIAdapter1 *real = (IDXGIAdapter1 *)(*ppvObject);
    *ppvObject = (IDXGIAdapter1 *)(new WrappedIDXGIAdapter1(real));
    return true;
  }
  else if(riid == __uuidof(IDXGIFactory1))
  {
    IDXGIFactory1 *real = (IDXGIFactory1 *)(*ppvObject);
    *ppvObject = (IDXGIFactory1 *)(new WrappedIDXGIFactory1(real));
    return true;
  }
  else if(riid == __uuidof(IDXGIAdapter2))
  {
    IDXGIAdapter2 *real = (IDXGIAdapter2 *)(*ppvObject);
    *ppvObject = (IDXGIAdapter2 *)(new WrappedIDXGIAdapter2(real));
    return true;
  }
  else if(riid == __uuidof(IDXGIAdapter3))
  {
    IDXGIAdapter3 *real = (IDXGIAdapter3 *)(*ppvObject);
    *ppvObject = (IDXGIAdapter3 *)(new WrappedIDXGIAdapter3(real));
    return true;
  }
  else if(riid == __uuidof(IDXGIFactory2))
  {
    IDXGIFactory2 *real = (IDXGIFactory2 *)(*ppvObject);
    *ppvObject = (IDXGIFactory2 *)(new WrappedIDXGIFactory2(real));
    return true;
  }
  else if(riid == __uuidof(IDXGIFactory3))
  {
    IDXGIFactory3 *real = (IDXGIFactory3 *)(*ppvObject);
    *ppvObject = (IDXGIFactory3 *)(new WrappedIDXGIFactory3(real));
    return true;
  }
  else if(riid == __uuidof(IDXGIFactory4))
  {
    IDXGIFactory4 *real = (IDXGIFactory4 *)(*ppvObject);
    *ppvObject = (IDXGIFactory4 *)(new WrappedIDXGIFactory4(real));
    return true;
  }
  else
  {
    string guid = ToStr::Get(riid);
    RDCWARN("Querying IDXGIObject for interface: %s", guid.c_str());
  }

  return false;
}

HRESULT STDMETHODCALLTYPE RefCountDXGIObject::GetParent(
    /* [in] */ REFIID riid,
    /* [retval][out] */ void **ppParent)
{
  HRESULT ret = m_pReal->GetParent(riid, ppParent);

  if(SUCCEEDED(ret))
    HandleWrap(riid, ppParent);

  return ret;
}

HRESULT RefCountDXGIObject::WrapQueryInterface(IUnknown *real, REFIID riid, void **ppvObject)
{
  HRESULT ret = real->QueryInterface(riid, ppvObject);

  if(SUCCEEDED(ret))
    HandleWrap(riid, ppvObject);

  return ret;
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain3::QueryInterface(REFIID riid, void **ppvObject)
{
  if(riid == __uuidof(IDXGISwapChain))
  {
    AddRef();
    *ppvObject = (IDXGISwapChain *)this;
    return S_OK;
  }
  else if(riid == __uuidof(IDXGISwapChain1))
  {
    if(m_pReal1)
    {
      AddRef();
      *ppvObject = (IDXGISwapChain1 *)this;
      return S_OK;
    }
    else
    {
      return E_NOINTERFACE;
    }
  }
  else if(riid == __uuidof(IDXGISwapChain2))
  {
    if(m_pReal2)
    {
      AddRef();
      *ppvObject = (IDXGISwapChain2 *)this;
      return S_OK;
    }
    else
    {
      return E_NOINTERFACE;
    }
  }
  else if(riid == __uuidof(IDXGISwapChain3))
  {
    if(m_pReal3)
    {
      AddRef();
      *ppvObject = (IDXGISwapChain3 *)this;
      return S_OK;
    }
    else
    {
      return E_NOINTERFACE;
    }
  }
  else
  {
    string guid = ToStr::Get(riid);
    RDCWARN("Querying IDXGISwapChain for interface: %s", guid.c_str());
  }

  return RefCountDXGIObject::QueryInterface(riid, ppvObject);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGIDevice::QueryInterface(REFIID riid, void **ppvObject)
{
  if(riid == m_pD3DDevice->GetDeviceUUID())
  {
    m_pD3DDevice->AddRef();
    *ppvObject = m_pD3DDevice->GetDeviceInterface();
    return S_OK;
  }
  else
  {
    string guid = ToStr::Get(riid);
    RDCWARN("Querying IDXGIDevice for interface: %s", guid.c_str());
  }

  return RefCountDXGIObject::QueryInterface(riid, ppvObject);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGIDevice1::QueryInterface(REFIID riid, void **ppvObject)
{
  HRESULT hr = S_OK;

  if(riid == m_pD3DDevice->GetDeviceUUID())
  {
    m_pD3DDevice->AddRef();
    *ppvObject = m_pD3DDevice->GetDeviceInterface();
    return S_OK;
  }
  else if(riid == __uuidof(IDXGIDevice1))
  {
    AddRef();
    *ppvObject = (IDXGIDevice1 *)this;
    return S_OK;
  }
  else if(riid == __uuidof(IDXGIDevice2))
  {
    hr = m_pReal->QueryInterface(riid, ppvObject);

    if(SUCCEEDED(hr))
    {
      IDXGIDevice2 *real = (IDXGIDevice2 *)(*ppvObject);
      *ppvObject = (IDXGIDevice2 *)(new WrappedIDXGIDevice2(real, m_pD3DDevice));
      return S_OK;
    }
    else
    {
      return E_NOINTERFACE;
    }
  }
  else if(riid == __uuidof(IDXGIDevice3))
  {
    hr = m_pReal->QueryInterface(riid, ppvObject);

    if(SUCCEEDED(hr))
    {
      IDXGIDevice3 *real = (IDXGIDevice3 *)(*ppvObject);
      *ppvObject = (IDXGIDevice3 *)(new WrappedIDXGIDevice3(real, m_pD3DDevice));
      return S_OK;
    }
    else
    {
      return E_NOINTERFACE;
    }
  }
  else
  {
    string guid = ToStr::Get(riid);
    RDCWARN("Querying IDXGIDevice1 for interface: %s", guid.c_str());
  }

  return RefCountDXGIObject::QueryInterface(riid, ppvObject);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGIDevice2::QueryInterface(REFIID riid, void **ppvObject)
{
  if(riid == m_pD3DDevice->GetDeviceUUID())
  {
    m_pD3DDevice->AddRef();
    *ppvObject = m_pD3DDevice->GetDeviceInterface();
    return S_OK;
  }
  else if(riid == __uuidof(IDXGIDevice1))
  {
    AddRef();
    *ppvObject = (IDXGIDevice1 *)this;
    return S_OK;
  }
  else if(riid == __uuidof(IDXGIDevice2))
  {
    AddRef();
    *ppvObject = (IDXGIDevice2 *)this;
    return S_OK;
  }
  else if(riid == __uuidof(IDXGIDevice3))
  {
    HRESULT hr = m_pReal->QueryInterface(riid, ppvObject);

    if(SUCCEEDED(hr))
    {
      IDXGIDevice3 *real = (IDXGIDevice3 *)(*ppvObject);
      *ppvObject = (IDXGIDevice3 *)(new WrappedIDXGIDevice3(real, m_pD3DDevice));
      return S_OK;
    }
    else
    {
      return E_NOINTERFACE;
    }
  }
  else
  {
    string guid = ToStr::Get(riid);
    RDCWARN("Querying IDXGIDevice2 for interface: %s", guid.c_str());
  }

  return RefCountDXGIObject::QueryInterface(riid, ppvObject);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGIDevice3::QueryInterface(REFIID riid, void **ppvObject)
{
  if(riid == m_pD3DDevice->GetDeviceUUID())
  {
    m_pD3DDevice->AddRef();
    *ppvObject = m_pD3DDevice->GetDeviceInterface();
    return S_OK;
  }
  else if(riid == __uuidof(IDXGIDevice1))
  {
    AddRef();
    *ppvObject = (IDXGIDevice1 *)this;
    return S_OK;
  }
  else if(riid == __uuidof(IDXGIDevice2))
  {
    AddRef();
    *ppvObject = (IDXGIDevice2 *)this;
    return S_OK;
  }
  else if(riid == __uuidof(IDXGIDevice3))
  {
    AddRef();
    *ppvObject = (IDXGIDevice3 *)this;
    return S_OK;
  }
  else
  {
    string guid = ToStr::Get(riid);
    RDCWARN("Querying IDXGIDevice3 for interface: %s", guid.c_str());
  }

  return RefCountDXGIObject::QueryInterface(riid, ppvObject);
}
