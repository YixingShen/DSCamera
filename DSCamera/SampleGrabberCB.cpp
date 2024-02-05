#include "SampleGrabberCB.hpp"

SampleGrabberCallback::SampleGrabberCallback()
{
    videcallback = NULL;
}

ULONG STDMETHODCALLTYPE SampleGrabberCallback::AddRef()
{
    return 1;
}

ULONG STDMETHODCALLTYPE SampleGrabberCallback::Release()
{
    return 2;
}

HRESULT STDMETHODCALLTYPE SampleGrabberCallback::QueryInterface(REFIID riid, void** ppvObject)
{
    if (NULL == ppvObject) return E_POINTER;
    if (riid == __uuidof(IUnknown))
    {
        *ppvObject = static_cast<IUnknown*>(this);
        return S_OK;
    }
    if (riid == __uuidof(ISampleGrabberCB))
    {
        *ppvObject = static_cast<ISampleGrabberCB*>(this);
        return S_OK;
    }
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE SampleGrabberCallback::SampleCB(double Time, IMediaSample *pSample)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE SampleGrabberCallback::BufferCB(double Time, BYTE * pBuffer, long BufferLen)
{
    if (videcallback)
        videcallback(Time, pBuffer, BufferLen);

    return S_OK;
}
