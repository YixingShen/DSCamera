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
#include "getopt.h"

#if _DEBUG
#pragma comment(lib, "../opencv/build/x64/vc14/lib/opencv_world460d.lib")
#else
#pragma comment(lib, "../opencv/build/x64/vc14/lib/opencv_world460.lib")
#endif

#pragma comment(lib, "Shlwapi.lib")

#define DEFAULT_DEVICE_IDX    0
#define DEFAULT_STREAM_IDX    0
#define DEFAULT_FRAME_RATE    30
#define DEFAULT_FRAME_WIDTH   320
#define DEFAULT_FRAME_HEIGHT  240
#define DEFAULT_FRAME_SUBTYPE MEDIASUBTYPE_YUY2

#define SAVE_FRAME_DIR        L"./saveframe"

using namespace std;
using namespace chrono;
using namespace cv;

static Format framefmt;
static bool saveFrameExecute = false;
static bool showFrameExecute = true;
static cv::Mat cvFrameRGB(DEFAULT_FRAME_HEIGHT, DEFAULT_FRAME_WIDTH, CV_32F, cv::Scalar::all(0));
static BYTE *cpBuffer = NULL;
static LONG cpBufferLen = 0;
static LONG cpBufferLenMax = 0;

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

void showFrame(int width, int height, GUID subtyep, BYTE *pBuffer, long lBufferSize)
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
            cv::cvtColor(src, cvFrameRGB, cv::COLOR_YUV2BGR_NV12);
            winname = "NV12";
        }
        else if (subtyep == MEDIASUBTYPE_YUY2) { //YUY2
            cv::Mat src(height, width, CV_8UC2, (void*)pBuffer);
            cv::cvtColor(src, cvFrameRGB, cv::COLOR_YUV2BGR_YUY2);
            winname = "YUY2";
        }

        //cv::resize(cvFrameRGB, cvFrameRGB, Size(width * 2, height * 2), 0, 0, INTER_NEAREST);
    }

//End:

    cv::namedWindow(winname, WINDOW_AUTOSIZE);
    cv::imshow(winname, cvFrameRGB);

    int cvkey = cv::waitKey(1) & 0xFF;

    if (27 == cvkey) { //27 for ASCII ESC
        cv::destroyAllWindows();
        showFrameExecute = false;
    }
}

static int videoCallback(double time, BYTE *buff, LONG len)
{
    if (cpBufferLen == 0 && cpBufferLenMax >= len) {
        memcpy(cpBuffer, buff, len);
        cpBufferLen = len;
    }

    return 0;
}

static const char *shortopts = "t:d:s:w:h:f";

static struct option long_options[] = {
    { "help",   no_argument,       NULL, '?' },
    { "type",   required_argument, NULL, 't' },
    { "device", required_argument, NULL, 'd' },
    { "stream", required_argument, NULL, 's' },
    { "width",  required_argument, NULL, 'w' },
    { "height", required_argument, NULL, 'h' },
    { "framerate",    required_argument, NULL, 'f' },
    { 0 ,0, 0, 0 }
};

void usage_long_options() {
    printf("Usage: DSCamera\n");
    printf("Options:\n");
    printf(" --device DEVICE          specify device index (default=0)\n");
    printf(" --type {YUY2,NV12,MJPG}  request video type (default=YUY2)\n");
    printf(" --stream STREAM          specify video stream index (default=0)\n");
    printf(" --width WIDTH            request video width (default=320)\n");
    printf(" --height HEIGHT          request video height (default=240)\n");
    printf(" --framerate FRAMERATE    request video frame rate (default=30)\n");
    printf(" --help                   help message\n");
    printf("\n");
    printf("Example:\n");
    printf(" DSCamera --device=0 --type=YUY2 --framerate=30\n");
}

int main(int argc, char **argv)
{  
    int deviceIndex = DEFAULT_DEVICE_IDX;
    int streamIndex = DEFAULT_STREAM_IDX;
    int frameRate = DEFAULT_FRAME_RATE;
    int frameWidth = DEFAULT_FRAME_WIDTH;
    int frameHeight = DEFAULT_FRAME_HEIGHT;
    GUID frameSubtype = DEFAULT_FRAME_SUBTYPE;
    char* videoType = "";
    DeviceList dlist;
    int opt;
    int cnt = 0;

    //while ((opt = getopt_long(argc, argv, shortopts, long_options, NULL)) != EOF)
    while ((opt = getopt_long(argc, argv, "", long_options, NULL)) != EOF)
    {
        //printf("proces index:%d\n", optind);
        //printf("option arg:%s\n", optarg);

        switch (opt)
        {
        case 't':
            videoType = optarg;
            break;
        case 'd':
            deviceIndex = strtol(optarg, NULL, 10);
            break;
        case 's':
            streamIndex = strtol(optarg, NULL, 10);
            break;
        case 'i':
            streamIndex = strtol(optarg, NULL, 10);
            break;
        case 'f':
            frameRate = strtol(optarg, NULL, 10);
            break;
        case 'w':
            frameWidth = strtol(optarg, NULL, 10);
            break;
        case 'h':
            frameHeight = strtol(optarg, NULL, 10);
            break;
        case '?':
        default:
            usage_long_options();
            /* NOTREACHED */
            break;
        }
    }

    if (!strcmp(videoType, "YUY2") || \
        !strcmp(videoType, "YUV") || \
        !strcmp(videoType, "YUYV"))
    {
        frameSubtype = MEDIASUBTYPE_YUY2;
    }
    else if (!strcmp(videoType, "MJPG") || \
             !strcmp(videoType, "MJPEG") || \
             !strcmp(videoType, "JPEG"))
    {
        frameSubtype = MEDIASUBTYPE_MJPG;
    }
    else if (!strcmp(videoType, "NV12"))
    {
        frameSubtype = MEDIASUBTYPE_NV12;
    }

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

    printf("set video callback\n");
    CameraSetVideoCallback(videoCallback);

    printf("open stream[%d]\n", streamIndex);
    ret = CameraOpenStream(streamIndex);

    if (!ret) {
        printf("failed to OpenStream(%d) !\n", streamIndex);
        goto End;
    }

    ret = CameraSetGrabFormat(frameWidth, frameHeight, frameSubtype);

    if (!ret) {
        printf("failed to SetGrabFormat !\n");
        goto End;
    }
    
    ret = CameraSetGrabFrameRate(frameRate);
    if (!ret) {
        printf("failed to SetGrabFrameRate !\n");
        goto End;
    }

    printf("get current grabber format\n");
    ZeroMemory(&framefmt, sizeof(framefmt));
    ret = CameraGetGrabFormat(&framefmt);

    if (!ret) {
        printf("Failed to GetFormat !\n");
        goto End;
    }

    frameSubtype = framefmt.MediaSubtype;
    frameWidth = framefmt.Width;
    frameHeight = framefmt.Height;
    frameRate = (int)(10000000 / framefmt.AvgTimePerFrame);

    printf(" w:%d h:%d fps:%d\n",
        frameWidth, frameHeight, frameRate);
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

    cpBufferLenMax = frameWidth * frameHeight * 3;
    cpBuffer = (BYTE *)malloc(cpBufferLenMax);
    cpBufferLen = 0;

    printf("start stream\n");
    ret = CameraStartStream();

    if (!ret) {
        printf("failed to StartStream !\n");
        goto End;
    }
End:

    while (1) {
        //this_thread::sleep_for(chrono::seconds(1));
        if (cpBufferLen) {
            static int32_t frameCount = 0;
            LONG len = cpBufferLen;
            BYTE *buff = cpBuffer;

            if (buff != NULL && len > 0 && \
                frameSubtype != MEDIASUBTYPE_NONE && \
                frameWidth != 0 && frameHeight != 0) {

                if (showFrameExecute)
                    showFrame(frameWidth, frameHeight, frameSubtype, buff, len);

                if (saveFrameExecute)
                    saveFrame(frameCount, frameSubtype, buff, len);
            }

            cpBufferLen = 0;

            static auto last_time = system_clock::now();
            auto cur_time = system_clock::now();
            auto dt = duration_cast<milliseconds>(cur_time - last_time);
            int32_t dt_v = (int32_t)dt.count();
            last_time = cur_time;

            printf("frame[%d] w:%d h:%d size:%d fps:%0.02f\n",
                frameCount, frameWidth, frameHeight, len, dt_v == 0 ? 0 : (1000.0f / dt_v));

            frameCount++;
        }
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