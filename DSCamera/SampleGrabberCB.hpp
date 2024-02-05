#ifndef __DSCamerabercb_h__
#define __DSCamerabercb_h__

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <dshow.h>
#include <windows.h>
#include <initguid.h>
#include <atlconv.h>
#include "qedit.h"

typedef int(*VideoCallbackFunc)(double time, BYTE *buff, LONG len);

class SampleGrabberCallback : public ISampleGrabberCB
{
public:
    ULONG STDMETHODCALLTYPE AddRef();
    ULONG STDMETHODCALLTYPE Release();
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject);
    HRESULT STDMETHODCALLTYPE SampleCB(double Time, IMediaSample *pSample);
    HRESULT STDMETHODCALLTYPE BufferCB(double Time, BYTE *pBuffer, long BufferLen);
    VideoCallbackFunc videcallback;
    SampleGrabberCallback();
};

#endif