#ifndef __camera_h__
#define __camera_h__

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <vector>
//#include <functional>
#include <dshow.h>
#include <windows.h>
#include <initguid.h>
#include <atlconv.h>
#include "qedit.h"
#include "ks.h"
#include "ksmedia.h"
#include "SampleGrabberCB.hpp"

DEFINE_GUID(MEDIASUBTYPE_NONE, 0x00000000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

#define MaxFormatNum 32
#define MaxDeviceNum 16
#define SUPPORT_STILL_IMAGE

typedef struct FormatStruct
{
    GUID MediaSubtype;
    UINT Width;
    UINT Height;
    LONGLONG AvgTimePerFrame;
    //LONGLONG MaxFrameInterval;
    //LONGLONG MinFrameInterval;
} Format;

typedef struct FormatListStruct
{
    UINT8 FormatNum;
    Format FormatItems[MaxFormatNum];
} FormatList;

typedef struct DeviceListStruct
{
    UINT8 DeviceNum;
    struct DeviceItemsStruct
    {
        LPTSTR FriendlyName;
        LPTSTR DevicePath;
    } DeviceItems[MaxDeviceNum];
} DeviceList;

bool CameraCreateInterface(void);

void CameraGetDeviceList(DeviceList *list);

bool CameraSetDevice(int deviceIndex);

//void CameraGetFormatList(FormatList *list);

//bool CameraSetFormat(int formatIndex);

//bool CameraSetFrameRate(int frameRate);

//bool CameraGetFormat(Format* format);

void CameraSetVideoCallback(VideoCallbackFunc callback);

bool CameraOpen(int srcPinOut);

bool CameraEnableStillImage(void);

bool CameraStopStream(void);

void CameraCloseInterface(void);

bool CameraSetDeviceStream(int deviceIndex, int srcPinOut);

bool CameraStartStream(void);

bool CameraGetGrabFormat(Format* format);

bool CameraSetGrabFormat(int Width, int Height, GUID MediaSubtype);

bool CameraSetGrabFrameRate(int frameRate);

void CameraSetStillImageCallback(VideoCallbackFunc callback);

bool CameraTriggerStillImage(void);

bool CameraGetStillImageFormat(Format* format);

bool CameraSetStillImageFormat(int Width, int Height, GUID MediaSubtype);

#endif