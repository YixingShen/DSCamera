// DSCamera.cpp : Defines the entry point for the console application.
//

#include <chrono>
#include <thread>
#include <iostream>
#include <string>
#include <opencv.hpp>
#include <shlwapi.h>
#include "ks.h"
#include "ksmedia.h"
#include "camera.hpp"

#if _DEBUG
#pragma comment(lib, "../opencv/build/x64/vc14/lib/opencv_world460d.lib")
#else
#pragma comment(lib, "../opencv/build/x64/vc14/lib/opencv_world460.lib")
#endif

#pragma comment(lib, "Shlwapi.lib")

#define REQUIRE_DEVICE_IDX   0
#define REQUIRE_FORTMAT_IDX  0
#define REQUIRE_STREAM_IDX   0
#define REQUIRE_FRAME_RATE   30
#define SAVE_FRAME_DIR       L"./saveframe"

using namespace std;
using namespace chrono;
using namespace cv;

static Format framefmt;
static bool saveFrameExecute = false;
static bool showFrameExecute = true;
static cv::Mat cvFrameRGB(320, 320, CV_32F, cv::Scalar::all(0));

void saveFrame(int32_t frameIndex, GUID subtyep, BYTE *pBuffer, long lBufferSize)
{
    TCHAR buff[50];

    if (subtyep == MEDIASUBTYPE_MJPG)
       swprintf(buff, _countof(buff), SAVE_FRAME_DIR"/frame_%d.jpg", frameIndex);
    else
       swprintf(buff, _countof(buff), SAVE_FRAME_DIR"/frame_%d.bin", frameIndex);

    DWORD dwWritten = 0;
    HANDLE hFile = CreateFile(buff, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, 0, NULL);
   
    if (hFile == INVALID_HANDLE_VALUE) {
        printf("Invalid file handle: %d\n", GetLastError());
        return;
    }

    WriteFile(hFile, pBuffer, lBufferSize, &dwWritten, NULL);
    CloseHandle(hFile);

    wprintf(L"saveFrame %s\n", buff);
}

void showFrame(int32_t frameIndex, int width, int height, GUID subtyep, BYTE *pBuffer, long lBufferSize)
{
    string winname = "showFrame";

    if (subtyep == MEDIASUBTYPE_MJPG) {
        cv::Mat rawData(1, lBufferSize, CV_8UC1, (void*)pBuffer);
        cvFrameRGB = imdecode(rawData, cv::IMREAD_COLOR);
        winname = "MJPG";
    }
    else {
        if (subtyep == MEDIASUBTYPE_NV12) {
            cv::Mat src(height * 12 / 8, width, CV_8UC1, (void*)pBuffer);
            cv::cvtColor(src, cvFrameRGB, cv::COLOR_YUV2RGB_NV12);
            winname = "NV12";
        }
        else if (subtyep == MEDIASUBTYPE_YUY2) { //YUY2
            cv::Mat src(height, width, CV_8UC2, (void*)pBuffer);
            cv::cvtColor(src, cvFrameRGB, cv::COLOR_YUV2BGR_YUY2);
            winname = "YUY2";
        }

        //cv::resize(cvFrameRGB, cvFrameRGB, Size(width * 2, height * 2), 0, 0, INTER_NEAREST);
    }

End:

    cv::namedWindow(winname, WINDOW_AUTOSIZE);
    cv::imshow(winname, cvFrameRGB);

    int cvkey = cv::waitKey(1) & 0xFF;

    if (27 == cvkey) { //27 for ASCII ESC
        cv::destroyAllWindows();
        showFrameExecute = false;
    }
}

int videoCallback(double time, BYTE *buff, LONG len)
{
    static auto last_time = system_clock::now();
    static int32_t frameCount = 0;

    auto cur_time = system_clock::now();
    auto dt = duration_cast<milliseconds>(cur_time - last_time);
    int32_t dt_v = (int32_t)dt.count();
    last_time = cur_time;
    GUID frameSubtype = framefmt.MediaSubtype;
    int frameWidth = framefmt.Width;
    int frameHeight = framefmt.Height;

    if (buff != NULL && len > 0 && \
        frameSubtype != MEDIASUBTYPE_NONE && \
        frameWidth != 0 && frameHeight != 0) {

        if (showFrameExecute)
            showFrame(frameCount, frameWidth, frameHeight, frameSubtype, buff, len);

        if (saveFrameExecute)
            saveFrame(frameCount, frameSubtype, buff, len);
    }

    printf("frame[%d] w:%d h:%d size:%d fps:%0.02f\n",
        frameCount, frameWidth, frameHeight, len, dt_v == 0 ? 0 : (1000.0f / dt_v));
    //printf(" subtype:%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X\n\n",
    //    frameSubtype.Data1, frameSubtype.Data2, frameSubtype.Data3,
    //    frameSubtype.Data4[0],
    //    frameSubtype.Data4[1],
    //    frameSubtype.Data4[2],
    //    frameSubtype.Data4[3],
    //    frameSubtype.Data4[4],
    //    frameSubtype.Data4[5],
    //    frameSubtype.Data4[6],
    //    frameSubtype.Data4[7]);

    frameCount++;
    return 0;
}

int main(int argc, char **argv)
{  
    int deviceIndex = REQUIRE_DEVICE_IDX;
    int formatIndex = REQUIRE_FORTMAT_IDX;
    int streamIndex = REQUIRE_STREAM_IDX;
    int frameRate = REQUIRE_FRAME_RATE;

    DeviceList dlist;
    FormatList flist;

    if (argc >= 2)
        deviceIndex = strtol(argv[1], NULL, 10);

    if (argc >= 3)
        formatIndex = strtol(argv[2], NULL, 10);

    if (argc >= 4)
        streamIndex = strtol(argv[3], NULL, 10);

    if (PathIsDirectory(SAVE_FRAME_DIR))
        saveFrameExecute = true;
    else
        saveFrameExecute = false;

    bool ret;

    printf("create interface\n");
    ret = CameraCreateInterface();

    if (!ret) {
        printf("failed to CameraCreateInterface!\n");
        goto End;
    }

    ZeroMemory(&dlist, sizeof(DeviceList));
    CameraGetDeviceList(&dlist);

    if (dlist.DeviceNum == 0) {
        printf("enumeration has no device!\n");
        goto End;
    }

    printf("list device:\n");

    for (int idx = 0; idx < dlist.DeviceNum; idx++) {
        printf(" device[%d]\n", idx);
        printf("  %ls\n", dlist.DeviceItems[idx].FriendlyName);
        printf("  %ls\n", dlist.DeviceItems[idx].DevicePath);
    }

    printf("request device[%d]\n", deviceIndex);
    ret = CameraSetDevice(deviceIndex);

    if (!ret) {
        printf("failed to SetDevice(%d) !\n", deviceIndex);
        goto End;
    }

    ZeroMemory(&flist, sizeof(FormatList));
    CameraGetFormatList(&flist);

    printf("list format:\n");

    for (int idx = 0; idx < flist.FormatNum; idx++) {
        GUID MediaType = flist.FormatItems[idx].MediaSubtype;
        UINT Width = flist.FormatItems[idx].Width;
        UINT Height = flist.FormatItems[idx].Height;
        LONGLONG AvgTimePerFrame = flist.FormatItems[idx].AvgTimePerFrame;

        printf(" format[%d] \n", idx);
        printf("  w:%d h:%d fps:%d subtype:%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X\n",
            Width, Height, 10000000 / AvgTimePerFrame,
            MediaType.Data1, MediaType.Data2, MediaType.Data3,
            MediaType.Data4[0],
            MediaType.Data4[1],
            MediaType.Data4[2],
            MediaType.Data4[3],
            MediaType.Data4[4],
            MediaType.Data4[5],
            MediaType.Data4[6],
            MediaType.Data4[7]);
    }

    printf("request fomat[%d]\n", formatIndex);
    ret = CameraSetFormat(formatIndex);

    if (!ret) {
        printf("failed to SetFormat(%d) !\n", formatIndex);
        goto End;
    }

    printf("request frame rate %d\n", frameRate);
    ret = CameraSetFrameRate(frameRate);

    if (!ret) {
        printf("failed to SetFrameRate(%d) !\n", frameRate);
        goto End;
    }

    printf("get current format\n");
    ZeroMemory(&framefmt, sizeof(framefmt));
    ret = CameraGetFormat(&framefmt);

    if (!ret) {
        printf("Failed to GetFormat !\n");
        goto End;
    }

    GUID frameSubtype = framefmt.MediaSubtype;
    int frameWidth = framefmt.Width;
    int frameHeight = framefmt.Height;
    LONGLONG AvgTimePerFrame = framefmt.AvgTimePerFrame;

    printf(" w:%d h:%d fps:%d\n",
        frameWidth, frameHeight, 10000000 / AvgTimePerFrame);
    printf(" subtype:%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X\n",
        frameSubtype.Data1, frameSubtype.Data2, frameSubtype.Data3,
        frameSubtype.Data4[0],
        frameSubtype.Data4[1],
        frameSubtype.Data4[2],
        frameSubtype.Data4[3],
        frameSubtype.Data4[4],
        frameSubtype.Data4[5],
        frameSubtype.Data4[6],
        frameSubtype.Data4[7]);

    printf("set video callback\n");
    CameraSetVideoCallback(videoCallback);

    printf("open stream\n");
    ret = CameraOpenStream(streamIndex);

    if (!ret) {
        printf("failed to OpenStream(%d) !\n", streamIndex);
        goto End;
    }

End:

    while (1) {
        this_thread::sleep_for(chrono::seconds(1));
#if 0
        printf("stop stream\n");
        ret = CameraStopStream();

        if (!ret) {
            printf("failed to StopStream !\n");
        }

        printf("close interface\n");
        CameraCloseInterface();
        return 0;
#endif
    }

    return 0;
}