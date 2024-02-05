#include "camera.hpp"

#pragma comment(lib, "strmiids")

using namespace std;

//define release maco
#define ReleaseInterface(x) \
if (NULL != x)              \
{                           \
    x->Release( );          \
    x = NULL;               \
}

#define SAFE_RELEASE(x) \
if (x)                  \
{                       \
  x->Release();         \
  x = NULL;             \
}

#define CHECK_HR_GO_DONE(hr)                                          \
do {                                                                  \
  if (FAILED(hr)) {                                                   \
      printf("%s:%s:%d hr=0x%x\n", __FILE__, __func__, __LINE__, hr); \
      goto done;                                                      \
  }                                                                   \
} while(0)

#define CHECK_HR_RET_FALSE(hr)                                      \
do {                                                                \
  if (FAILED(hr)) {                                                 \
    CameraCloseInterface();                                         \
    printf("%s:%s:%d hr=0x%x\n", __FILE__, __func__, __LINE__, hr); \
    return false;                                                   \
  }                                                                 \
}while(0)

//#define CALLBACKMODE 1

static IGraphBuilder *g_pGraph;
static ICaptureGraphBuilder2 *g_pCapture;
static IMediaControl *g_pMC;
static IBaseFilter *g_pSrcFilter;
static ISampleGrabber *g_pGrabber;
static IMediaEventEx *g_pME;
static SampleGrabberCallback *g_pSampleGrabCallback;
static bool g_InitOK;
static bool g_IsOpened;
static FormatList g_FormatList;
static DeviceList g_DeviceList;

//Converting a WChar string to a Ansi string
std::string WChar2Ansi(LPCWSTR pwszSrc)
{
    int nLen = WideCharToMultiByte(CP_ACP, 0, pwszSrc, -1, NULL, 0, NULL, NULL);

    if (nLen <= 0) return std::string("");

    char* pszDst = new char[nLen];
    if (NULL == pszDst) return std::string("");

    WideCharToMultiByte(CP_ACP, 0, pwszSrc, -1, pszDst, nLen, NULL, NULL);
    pszDst[nLen - 1] = 0;

    std::string strTemp(pszDst);
    delete[] pszDst;

    return strTemp;
}

std::wstring Ansi2WChar(LPCSTR pszSrc, int nLen)

{
    int nSize = MultiByteToWideChar(CP_ACP, 0, (LPCSTR)pszSrc, nLen, 0, 0);
    if (nSize <= 0) return NULL;

    WCHAR *pwszDst = new WCHAR[nSize + 1];
    if (NULL == pwszDst) return NULL;

    MultiByteToWideChar(CP_ACP, 0, (LPCSTR)pszSrc, nLen, pwszDst, nSize);
    pwszDst[nSize] = 0;

    if (pwszDst[0] == 0xFEFF)                    // skip Oxfeff
        for (int i = 0; i < nSize; i++)
            pwszDst[i] = pwszDst[i + 1];

    wstring wcharString(pwszDst);
    delete pwszDst;

    return wcharString;
}

string ws2s(wstring& inputws)
{
    return WChar2Ansi(inputws.c_str());
}

std::wstring s2ws(const string& s)
{
    return Ansi2WChar(s.c_str(), s.size());
}

HRESULT FindPinByName(IBaseFilter * inFilter, PIN_DIRECTION PinDir, IPin **ppPin, const char * inPartialName)
{
    bool bFound = false;

    if (inFilter) {
        IEnumPins * pinEnum = NULL;

        if (SUCCEEDED(inFilter->EnumPins(&pinEnum))) {
            pinEnum->Reset();

            IPin * pin = NULL;
            ULONG fetchCount = 0;
            while (!bFound && SUCCEEDED(pinEnum->Next(1, &pin, &fetchCount)) && fetchCount) {
                if (pin) {
                    PIN_INFO pinInfo;
                    if (SUCCEEDED(pin->QueryPinInfo(&pinInfo))) {
                        if (pinInfo.dir == PinDir) {
                            // Ignore the pin name
                            if (!inPartialName) {
                                pin->AddRef();
                                *ppPin = pin;
                                bFound = true;
                            }
                            else {
                                char pinName[128];
                                ::WideCharToMultiByte(CP_ACP, 0, pinInfo.achName,
                                    -1, pinName, 128, NULL, NULL);
                                if (::strstr(pinName, inPartialName)) {
                                    pin->AddRef();
                                    *ppPin = pin;
                                    bFound = true;
                                }
                            }
                        }
                        pinInfo.pFilter->Release();
                    }
                    pin->Release();
                }
            }
            pinEnum->Release();
        }
    }

    return (bFound ? S_OK : VFW_E_NOT_FOUND);
}

//Windows-classic-samples/Samples/Win7Samples/multimedia/directshow/common/dshowutil.h
HRESULT FindPinByIndex(IBaseFilter *pFilter, PIN_DIRECTION PinDir,
    UINT nIndex, IPin **ppPin)
{
    if (!pFilter || !ppPin) {
        return E_POINTER;
    }

    HRESULT hr = S_OK;
    bool bFound = false;
    UINT count = 0;

    IEnumPins *pEnum = NULL;
    IPin *pPin = NULL;

    hr = pFilter->EnumPins(&pEnum);
    CHECK_HR_GO_DONE(hr);

    while (S_OK == (hr = pEnum->Next(1, &pPin, NULL))) {
        PIN_DIRECTION ThisDir;
        hr = pPin->QueryDirection(&ThisDir);
        CHECK_HR_GO_DONE(hr);

        if (ThisDir == PinDir) {
            if (nIndex == count) {
                *ppPin = pPin;            // return to caller
                (*ppPin)->AddRef();
                bFound = true;
                break;
            }
            count++;
        }

        SAFE_RELEASE(pPin);
    }

done:
    SAFE_RELEASE(pPin);
    SAFE_RELEASE(pEnum);

    return (bFound ? S_OK : VFW_E_NOT_FOUND);
}

void FreeMediaType(AM_MEDIA_TYPE& mt)
{
    if (mt.cbFormat != 0) {
        CoTaskMemFree((PVOID)mt.pbFormat);
        mt.cbFormat = 0;
        mt.pbFormat = NULL;
    }

    if (mt.pUnk != NULL) {
        // pUnk should not be used.
        mt.pUnk->Release();
        mt.pUnk = NULL;
    }
}

///////////////////////////////////////////////////////////////////////
// Name: DisconnectPin
// Desc: Disconnect a pin from its peer.
//
// Note: If the pin is not connected, the function returns S_OK (no-op).
///////////////////////////////////////////////////////////////////////
inline HRESULT DisconnectPin(IGraphBuilder *pGraph, IPin *pPin)
{
    if (!pGraph || !pPin) {
        return E_POINTER;
    }

    HRESULT hr = S_OK;
    IPin *pPinTo = NULL;

    hr = pPin->ConnectedTo(&pPinTo);

    if (hr == VFW_E_NOT_CONNECTED) {
        // This pin is not connected.
        return S_OK; // no-op
    }

    // Disconnect the first pin.
    if (SUCCEEDED(hr)) {
        hr = pGraph->Disconnect(pPin);
    }

    if (SUCCEEDED(hr)) {
        // Disconnect the other pin.
        hr = pGraph->Disconnect(pPinTo);
    }

    SAFE_RELEASE(pPinTo);
    return hr;
}

///////////////////////////////////////////////////////////////////////
// Name: IsPinConnected
// Desc: Query whether a pin is connected to another pin.
//
// Note: If you need to get the other pin, use IPin::ConnectedTo.
///////////////////////////////////////////////////////////////////////
inline HRESULT IsPinConnected(IPin *pPin, BOOL *pResult)
{
    if (pPin == NULL || pResult == NULL) {
        return E_POINTER;
    }

    IPin *pTmp = NULL;
    HRESULT hr = pPin->ConnectedTo(&pTmp);

    if (SUCCEEDED(hr)) {
        *pResult = TRUE;
    }
    else if (hr == VFW_E_NOT_CONNECTED) {
        // The pin is not connected. This is not an error for our purposes.
        *pResult = FALSE;
        hr = S_OK;
    }

    SAFE_RELEASE(pTmp);
    return hr;
}

static HRESULT BindSrcFilter(int deviceIndex, IBaseFilter **pBaseFilter)
{
    ICreateDevEnum *pDevEnum;
    IEnumMoniker   *pEnumMon;
    IMoniker       *pMoniker;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
        IID_ICreateDevEnum, (LPVOID*)&pDevEnum);

    if (SUCCEEDED(hr)) {
        hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnumMon, 0);
        CHECK_HR_GO_DONE(hr);

        pEnumMon->Reset();
        ULONG cFetched;
        int index = 0;
        hr = pEnumMon->Next(1, &pMoniker, &cFetched);

        while (hr == S_OK && index <= deviceIndex) {
            IPropertyBag *pProBag;
            hr = pMoniker->BindToStorage(0, 0, IID_IPropertyBag, (LPVOID*)&pProBag);

            if (SUCCEEDED(hr)) {
                if (index == deviceIndex) {
                    pMoniker->BindToObject(0, 0, IID_IBaseFilter, (LPVOID*)pBaseFilter);
                }
            }
            pMoniker->Release();
            index++;
            hr = pEnumMon->Next(1, &pMoniker, &cFetched);
        }

        pEnumMon->Release();
    }

    done:
    return hr;
}

static HRESULT EnumMediaFormat()
{
    HRESULT hr;
    IAMStreamConfig *pAMStreamConfig = NULL;
    g_FormatList.FormatNum = 0;

    hr = g_pCapture->FindInterface(&PIN_CATEGORY_CAPTURE, 0, g_pSrcFilter, IID_IAMStreamConfig, (void**)&pAMStreamConfig);
    CHECK_HR_GO_DONE(hr);

    int iCount = 0, iSize = 0;
    hr = pAMStreamConfig->GetNumberOfCapabilities(&iCount, &iSize);
    CHECK_HR_GO_DONE(hr);

    // Check the size to make sure we pass in the correct structure.
    if (iSize == sizeof(VIDEO_STREAM_CONFIG_CAPS)) {
        //Use the video capabilities structure.  
        for (int iFormat = 0; iFormat < iCount; iFormat++) {
            VIDEO_STREAM_CONFIG_CAPS scc;
            VIDEOINFOHEADER *pVih;
            AM_MEDIA_TYPE *pmt;
            hr = pAMStreamConfig->GetStreamCaps(iFormat, &pmt, (BYTE*)&scc);

            if (SUCCEEDED(hr)) {
                //Examine the format, and possibly use it.
                if ((pmt->majortype == MEDIATYPE_Video) &&
                    (pmt->formattype == FORMAT_VideoInfo) &&
                    (pmt->cbFormat >= sizeof(VIDEOINFOHEADER)) &&
                    (pmt->pbFormat != NULL)) {
                    pVih = (VIDEOINFOHEADER*)pmt->pbFormat;
                    // pVih contains the detailed format information.
                    if (g_FormatList.FormatNum < MaxFormatNum) {
                        g_FormatList.FormatItems[g_FormatList.FormatNum].Width = pVih->bmiHeader.biWidth;
                        g_FormatList.FormatItems[g_FormatList.FormatNum].Height = pVih->bmiHeader.biHeight;
                        g_FormatList.FormatItems[g_FormatList.FormatNum].MediaSubtype = pmt->subtype;
                        g_FormatList.FormatItems[g_FormatList.FormatNum].AvgTimePerFrame = pVih->AvgTimePerFrame;
                        //g_FormatList.FormatItems[g_FormatList.FormatNum].MaxFrameInterval = scc.MaxFrameInterval;
                        //g_FormatList.FormatItems[g_FormatList.FormatNum].MinFrameInterval = scc.MinFrameInterval;
                        g_FormatList.FormatNum++;
                    }
                }
            }

            FreeMediaType(*pmt);
        }
    }

    SAFE_RELEASE(pAMStreamConfig);

    done:
    return hr;
}

static HRESULT GetMediaFormat(AM_MEDIA_TYPE* mediatype)
{
    HRESULT hr;
    IAMStreamConfig *pAMStreamConfig = NULL;
    hr = g_pCapture->FindInterface(&PIN_CATEGORY_CAPTURE, 0, g_pSrcFilter, IID_IAMStreamConfig, (void**)&pAMStreamConfig);
    CHECK_HR_GO_DONE(hr);

    AM_MEDIA_TYPE *pmt;
    hr = pAMStreamConfig->GetFormat(&pmt);
    CHECK_HR_GO_DONE(hr);

    CopyMemory(mediatype, pmt, sizeof(AM_MEDIA_TYPE));
    SAFE_RELEASE(pAMStreamConfig);
    FreeMediaType(*pmt);

    done:
    return hr;
}

static HRESULT SetMediaFormat(int formatIndex)
{
    HRESULT hr = S_OK;

    if (g_FormatList.FormatNum == 0)
        hr = E_FAIL;

    CHECK_HR_GO_DONE(hr);

    if (formatIndex > (g_FormatList.FormatNum - 1))
        hr = E_FAIL;

    CHECK_HR_GO_DONE(hr);

    IAMStreamConfig *pAMStreamConfig = NULL;
    hr = g_pCapture->FindInterface(&PIN_CATEGORY_CAPTURE, 0, g_pSrcFilter, IID_IAMStreamConfig, (void**)&pAMStreamConfig);
    CHECK_HR_GO_DONE(hr);

    AM_MEDIA_TYPE *pmt;
    hr = pAMStreamConfig->GetFormat(&pmt);
    CHECK_HR_GO_DONE(hr);

    pmt->subtype = g_FormatList.FormatItems[formatIndex].MediaSubtype;
    VIDEOINFOHEADER *pVih = (VIDEOINFOHEADER*)pmt->pbFormat;
    pVih->bmiHeader.biWidth = g_FormatList.FormatItems[formatIndex].Width;
    pVih->bmiHeader.biHeight = g_FormatList.FormatItems[formatIndex].Height;
    //pVih->bmiHeader.biSizeImage = DIBSIZE(pVih->bmiHeader);
    pVih->AvgTimePerFrame = g_FormatList.FormatItems[formatIndex].AvgTimePerFrame;

    hr = pAMStreamConfig->SetFormat(pmt);
    CHECK_HR_GO_DONE(hr);

    SAFE_RELEASE(pAMStreamConfig);
    FreeMediaType(*pmt);

    done:
    return hr;
}

static HRESULT SetFrameRate(int frameRate)
{
    HRESULT hr;

    IAMStreamConfig *pAMStreamConfig = NULL;
    hr = g_pCapture->FindInterface(&PIN_CATEGORY_CAPTURE, 0, g_pSrcFilter, IID_IAMStreamConfig, (void**)&pAMStreamConfig);
    CHECK_HR_GO_DONE(hr);

    AM_MEDIA_TYPE *pmt;
    hr = pAMStreamConfig->GetFormat(&pmt);
    CHECK_HR_GO_DONE(hr);

    VIDEOINFOHEADER *pVih = (VIDEOINFOHEADER*)pmt->pbFormat;
    pVih->AvgTimePerFrame = (LONGLONG)(10000000 / frameRate);

    hr = pAMStreamConfig->SetFormat(pmt);
    CHECK_HR_GO_DONE(hr);

    SAFE_RELEASE(pAMStreamConfig);
    FreeMediaType(*pmt);

    done:
    return hr;
}

static HRESULT EnumDevice(void)
{
    HRESULT hr;
    IBaseFilter *pSrc = NULL;
    IMoniker *pMoniker = NULL;
    ULONG cFetched;

    g_DeviceList.DeviceNum = 0;

    // Create the system device enumerator
    ICreateDevEnum *pDevEnum = NULL;

    hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC, IID_ICreateDevEnum, (void**)&pDevEnum);
    CHECK_HR_GO_DONE(hr);

    // Create an enumerator for the video capture devices
    IEnumMoniker *pClassEnum = NULL;

    hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pClassEnum, 0);
    CHECK_HR_GO_DONE(hr);

    pDevEnum->Release();

    // If there are no enumerators for the requested type, then 
    // CreateClassEnumerator will succeed, but pClassEnum will be NULL.
    if (pClassEnum == NULL) {
        hr = E_FAIL;
        CHECK_HR_GO_DONE(hr);
    }

    // Use the first video capture device on the device list.
    // Note that if the Next() call succeeds but there are no monikers,
    // it will return S_FALSE (which is not a failure).  Therefore, we
    // check that the return code is S_OK instead of using SUCCEEDED() macro.3
    while ((pClassEnum->Next(1, &pMoniker, &cFetched)) == S_OK) {
        IPropertyBag *pPropBag;
        HRESULT hr = pMoniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)&pPropBag);

        if (SUCCEEDED(hr)) {
            VARIANT varName;
            LPOLESTR strName = NULL;
            VariantInit(&varName);
            hr = pPropBag->Read(L"FriendlyName", &varName, 0);
            HRESULT hr_ = pMoniker->GetDisplayName(NULL, NULL, &strName);

            if (SUCCEEDED(hr))
            {
                if (g_DeviceList.DeviceNum < MaxDeviceNum) {
                    //USES_CONVERSION;
                    g_DeviceList.DeviceItems[g_DeviceList.DeviceNum].FriendlyName = W2T(varName.bstrVal);

                    if SUCCEEDED(hr_)
                        g_DeviceList.DeviceItems[g_DeviceList.DeviceNum].DevicePath = OLE2T(strName);

                    g_DeviceList.DeviceNum++;
                }
            }
            pPropBag->Release();
        }

        pMoniker->Release();
    }

    pClassEnum->Release();

    done:
    return hr;
}

void CameraCloseInterface(void)
{
    g_InitOK = false;

    if (g_pMC)
        g_pMC->Stop();

    ReleaseInterface(g_pSrcFilter);
    ReleaseInterface(g_pMC);
    ReleaseInterface(g_pME);
    ReleaseInterface(g_pGraph);
    ReleaseInterface(g_pCapture);
    ReleaseInterface(g_pGrabber);
    ReleaseInterface(g_pSampleGrabCallback);

    CoUninitialize();
}

bool CameraCreateInterface(void)
{
    g_pSampleGrabCallback = new SampleGrabberCallback();
    g_InitOK = false;
    g_IsOpened = false;
    g_pSrcFilter = NULL;
    g_pCapture = NULL;
    g_pGraph = NULL;
    g_pMC = NULL;
    g_pME = NULL;
    g_pGrabber = NULL;
    g_FormatList = { 0 };
    g_DeviceList = { 0 };

    //HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    
    if (SUCCEEDED(hr)) {
        g_InitOK = true;
    }

    //Create the filter graph
    hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
        IID_IGraphBuilder, (LPVOID*)&g_pGraph);
    CHECK_HR_RET_FALSE(hr);

    //Create the capture graph builder
    hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, NULL, CLSCTX_INPROC_SERVER,
        IID_ICaptureGraphBuilder2, (LPVOID*)&g_pCapture);
    CHECK_HR_RET_FALSE(hr);

    return true;
}

void CameraSetVideoCallback(VideoCallbackFunc callback)
{
    if (g_pSampleGrabCallback)
        g_pSampleGrabCallback->videcallback = callback;
}

void CameraGetFormatList(FormatList *list)
{
    if (g_InitOK == false)
        return;

    EnumMediaFormat();
    CopyMemory(list, &g_FormatList, sizeof(FormatList));
}

bool CameraGetFormat(Format* format)
{
    if (g_InitOK == false)
        return false;

    AM_MEDIA_TYPE mediatype;
    HRESULT hr = GetMediaFormat(&mediatype);

    if (FAILED(hr))
        return false;

    VIDEOINFOHEADER *pVih = (VIDEOINFOHEADER*)mediatype.pbFormat;
    format->Width = pVih->bmiHeader.biWidth;
    format->Height = pVih->bmiHeader.biHeight;
    format->MediaSubtype = mediatype.subtype;
    format->AvgTimePerFrame = pVih->AvgTimePerFrame;
    return true;
}

void CameraGetDeviceList(DeviceList *list)
{
    if (g_InitOK == false)
        return;

    EnumDevice();
    CopyMemory(list, &g_DeviceList, sizeof(DeviceList));
}

bool CameraStopStream(void)
{
    if (g_InitOK == false)
        return false;

    if (g_pMC)
        g_pMC->Stop();

    return true;
}

bool CameraOpenStream(int srcPinOut)
{
    HRESULT hr;

    if (g_InitOK == false)
        return false;

    if (g_IsOpened) {
        if (g_pMC)
            g_pMC->Run();

        return true;
    }

    IBaseFilter *pGrabberF;
    IBaseFilter *pNullF;

    hr = g_pGraph->QueryInterface(IID_IMediaControl, (LPVOID*)&g_pMC);
    CHECK_HR_RET_FALSE(hr);

    hr = g_pGraph->QueryInterface(IID_IMediaEventEx, (LPVOID*)&g_pME);
    CHECK_HR_RET_FALSE(hr);

    hr = g_pCapture->SetFiltergraph(g_pGraph);
    CHECK_HR_RET_FALSE(hr);

    hr = CoCreateInstance(CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (LPVOID*)&pGrabberF);
    CHECK_HR_RET_FALSE(hr);

    hr = g_pGraph->AddFilter(pGrabberF, L"Sample Grabber");
    CHECK_HR_RET_FALSE(hr);

    hr = pGrabberF->QueryInterface(IID_ISampleGrabber, (LPVOID*)&g_pGrabber);
    CHECK_HR_RET_FALSE(hr);

    hr = CoCreateInstance(CLSID_NullRenderer, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)(&pNullF));
    CHECK_HR_RET_FALSE(hr);

    hr = g_pGraph->AddFilter(pNullF, L"NullRenderer");
    CHECK_HR_RET_FALSE(hr);

    // connect source filter to grabber filter
#if 0
    hr = g_pCapture->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video,
        g_pSrcFilter, pGrabberF, pNullF);
    CHECK_HR_RET_FALSE(hr);
#else
    IPin *pSrcPinOut = NULL;
    IPin *pGrabPinIn = NULL;
    IPin *pGrabPinOut = NULL;
    IPin *pRenderPinIn = NULL;

    hr = FindPinByIndex(g_pSrcFilter, PINDIR_OUTPUT, srcPinOut, &pSrcPinOut);
    CHECK_HR_RET_FALSE(hr);

    hr = FindPinByName(pGrabberF, PINDIR_INPUT,  &pGrabPinIn,  "Input");
    CHECK_HR_RET_FALSE(hr);

    hr = FindPinByName(pGrabberF, PINDIR_OUTPUT, &pGrabPinOut, "Output");
    CHECK_HR_RET_FALSE(hr);

    hr = FindPinByName(pNullF, PINDIR_INPUT, &pRenderPinIn, "In");
    CHECK_HR_RET_FALSE(hr);

    hr = g_pGraph->Connect(pSrcPinOut, pGrabPinIn);
    CHECK_HR_RET_FALSE(hr);

    hr = g_pGraph->Connect(pGrabPinOut, pRenderPinIn);
    CHECK_HR_RET_FALSE(hr);
#endif

    pNullF->Release();
    pGrabberF->Release();

    // CALLBACKMODE == 1
    {
        hr = g_pGrabber->SetOneShot(FALSE);
        CHECK_HR_RET_FALSE(hr);

        hr = g_pGrabber->SetBufferSamples(FALSE);
        CHECK_HR_RET_FALSE(hr);

        int nMode = 1; //0--SampleCB,1--BufferCB
        hr = g_pGrabber->SetCallback(g_pSampleGrabCallback, nMode);
        CHECK_HR_RET_FALSE(hr);
    }

    g_IsOpened = TRUE;
    hr = g_pMC->Run();
    CHECK_HR_RET_FALSE(hr);

    return true;
}

bool CameraSetDevice(int deviceIndex)
{
    if (g_InitOK == false)
        return false;

    HRESULT hr;

    hr = BindSrcFilter(deviceIndex, &g_pSrcFilter);
    CHECK_HR_RET_FALSE(hr);

    hr = g_pGraph->AddFilter(g_pSrcFilter, L"Video Filter");
    CHECK_HR_RET_FALSE(hr);

    return true;
}

bool CameraSetFormat(int formatIndex)
{
    if (g_InitOK == false)
        return false;

    HRESULT hr;

    hr = SetMediaFormat(formatIndex);
    CHECK_HR_RET_FALSE(hr);

    return true;
}

bool CameraSetFrameRate(int frameRate)
{
    if (g_InitOK == false)
        return false;

    HRESULT hr;

    hr = SetFrameRate(frameRate);
    CHECK_HR_RET_FALSE(hr);

    return true;
}
