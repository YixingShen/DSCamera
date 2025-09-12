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
#include <conio.h>
#include <stdio.h>

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
#define DEFAULT_STILLIMAGE_EN 1
#define DEFAULT_CAPTRUE_MAXNUM 10
#define SAVE_FRAME_DIR        L"./saveframe"

using namespace std;
using namespace chrono;
using namespace cv;

#define QUEUE_FRAM_NUM      (3)

typedef struct {
    BYTE * buffer;
    int size;
} queue_t;

static Format framefmt;
static volatile bool saveFrameExecute = false;
static volatile bool showFrameExecute = true;
static BYTE *cpBuffer = NULL;
static volatile LONG cpBufferLenMax = 0;
static LONG captureMaxNum = 0;
BOOL WINAPI CtrlHandler(DWORD fdwCtrlType);
bool CompareGuid(GUID guid1, GUID guid2);
static volatile int frame_iptr = 0;
static volatile int frame_optr = 0;
static volatile int frame_count = 0;
queue_t queueframe[QUEUE_FRAM_NUM];

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
    string winname = "Frame ";
    static BYTE *pBuffer_NV12 = NULL;
    static BYTE *pBuffer_RAW10_LSB = NULL;
    cv::Mat dst;// (DEFAULT_FRAME_HEIGHT, DEFAULT_FRAME_WIDTH, CV_32F, cv::Scalar::all(0));
    bool drawed = false;

    if (subtyep == MEDIASUBTYPE_MJPG) {
        cv::Mat src(1, lBufferSize, CV_8UC1, (void*)pBuffer);
        dst = imdecode(src, cv::IMREAD_COLOR);
        winname += "MJPG";
        drawed = true;
    }
    else {
        if (subtyep == MEDIASUBTYPE_NV12) {
            cv::Mat src(height * 12 / 8, width, CV_8UC1, (void*)pBuffer);
            dst = cv::Mat(height, width, CV_8UC3, cv::Scalar::all(0));
            cv::cvtColor(src, dst, cv::COLOR_YUV2BGR_NV12);
            winname = "NV12";
            drawed = true;
        }
        else if (subtyep == MEDIASUBTYPE_Y8) { //frame based payload 
            cv::Mat src(height, width, CV_8UC1, (void*)pBuffer); //frame based payload 
            dst = cv::Mat(height, width, CV_8UC3, cv::Scalar::all(0));
            cv::cvtColor(src, dst, cv::COLOR_GRAY2BGR);
            winname += "Y8";
            drawed = true;
        }
        else if (subtyep == MEDIASUBTYPE_YUY2) { //YUY2
            cv::Mat src(height, width, CV_8UC2, (void*)pBuffer); //uncompress payload
            dst = cv::Mat(height, width, CV_8UC3, cv::Scalar::all(0));
            cv::cvtColor(src, dst, cv::COLOR_YUV2BGR_YUY2);
            winname += "YUY2";
            drawed = true;
        }
        else if (subtyep == MEDIASUBTYPE_M420) { //M420
            //M420 frame size 17x2
            //start +  0 : Y・00  Y・01  Y・02  Y・03
            //start +  4 : Y・10  Y・11  Y・12  Y・13
            //start +  8 : Cb00  Cr00  Cb01  Cr01
            //start + 16 : Y・20  Y・21  Y・22  Y・23
            //start + 20 : Y・30  Y・31  Y・32  Y・33
            //start + 24 : Cb10  Cr10  Cb11  Cr11

            //NV12 frame size 17x2
            //start + 0  : Y・00  Y・01  Y・02  Y・03
            //start + 4  : Y・10  Y・11  Y・12  Y・13
            //start + 8  : Y・20  Y・21  Y・22  Y・23
            //start + 12 : Y・30  Y・31  Y・32  Y・33
            //start + 16 : Cb00  Cr00  Cb01  Cr01
            //start + 20 : Cb10  Cr10  Cb11  Cr11

            if (!pBuffer_NV12)
                pBuffer_NV12 = (BYTE *)malloc(lBufferSize);

            int nv12_uv_planar_pos = width * height;
            int m420_y = 0;
            //M420 to NV12
            for (int y = 0; y < height; y += 2) {
                memcpy((void*)&pBuffer_NV12[(y + 0) * width],    (void*)&pBuffer[(m420_y + 0) * width], width);
                memcpy((void*)&pBuffer_NV12[(y + 1) * width],    (void*)&pBuffer[(m420_y + 1) * width], width);
                memcpy((void*)&pBuffer_NV12[nv12_uv_planar_pos], (void*)&pBuffer[(m420_y + 2) * width], width);
                nv12_uv_planar_pos += width;
                m420_y += 3;
            }

            cv::Mat src(height * 12 / 8, width, CV_8UC1, (void*)pBuffer_NV12); //uncompress payload
            dst = cv::Mat(height, width, CV_8UC3, cv::Scalar::all(0));
            cv::cvtColor(src, dst, cv::COLOR_YUV2BGR_NV12);
            winname += "M420";
            drawed = true;
        }
    }

    if (drawed) {
        //cv::namedWindow(winname, WINDOW_AUTOSIZE);
        //printf("image cols %d rows %d\n", dst.cols, dst.rows);
        if (dst.rows != 0 && dst.cols != 0) {
            cv::imshow(winname, dst);
            int cvkey = cv::waitKey(1) & 0xFF;

            if (27 == cvkey) { //27 for ASCII ESC
                cv::destroyAllWindows();
                showFrameExecute = false;
            }
        }
    }
}

static int videoCallback(double time, BYTE *buff, LONG len)
{
    static int count = 0;
    static auto last_time = system_clock::now();
    static float fps = 0;
    count++;

    if (count == 10) {
        auto cur_time = system_clock::now();
        auto dt_us = duration_cast<microseconds>(cur_time - last_time);
        int32_t dt_us_v = (int32_t)dt_us.count();
        fps = dt_us_v == 0 ? 0 : ((1000000.0f / (dt_us_v / 10)));
        count = 0;
        last_time = cur_time;
    }

    printf("frame[%d] size:%d fps:%0.02f @ %d frames\n", frame_count, len, fps, 10);
    const int pre_frame_iptr = ((frame_iptr + 1) >= QUEUE_FRAM_NUM) ? 0 : (frame_iptr + 1);
    if (pre_frame_iptr == frame_optr) {
      printf("skip frame");
    }
    else {
      queueframe[frame_iptr].buffer = (BYTE *)(cpBuffer + (cpBufferLenMax*frame_iptr));
      queueframe[frame_iptr].size = len;
      BYTE * dst = (BYTE *)(cpBuffer + (cpBufferLenMax*frame_iptr));
      memcpy(queueframe[frame_iptr].buffer, buff, len);
      frame_iptr = pre_frame_iptr;
    }

    frame_count++;
    return 0;
}

#if defined(SUPPORT_STILL_IMAGE)
static Format stillimagefmt;
static volatile bool saveStillImageExecute = false;
static volatile bool showStillImageExecute = true;
static int enableStillImage = DEFAULT_STILLIMAGE_EN;
static BYTE *cpBufferStill = NULL;
static volatile LONG cpBufferLenMaxStill = 0;
static LONG captureMaxNum_Still = 0;
queue_t queueframeStill[QUEUE_FRAM_NUM];
static volatile int stillframe_iptr = 0;
static volatile int stillframe_optr = 0;
static volatile int stillframe_count = 0;

void saveStillImage(int32_t frameIndex, GUID subtyep, BYTE *pBuffer, long lBufferSize)
{
    TCHAR buff[50];

    if (subtyep == MEDIASUBTYPE_MJPG)
        swprintf(buff, _countof(buff), SAVE_FRAME_DIR"/stillimage_%d.jpg", frameIndex);
    else
        swprintf(buff, _countof(buff), SAVE_FRAME_DIR"/stillimage_%d.bin", frameIndex);

    DWORD dwWritten = 0;
    HANDLE hFile = CreateFile(buff, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, 0, NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        printf("Invalid file handle: %d\n", GetLastError());
        return;
    }

    WriteFile(hFile, pBuffer, lBufferSize, &dwWritten, NULL);
    CloseHandle(hFile);

    wprintf(L"saveStillImage %s\n", buff);
}

void showStillImage(int width, int height, GUID subtyep, BYTE *pBuffer, long lBufferSize)
{
    string winname = "Still ";
    static BYTE *pBuffer_NV12 = NULL;
    cv::Mat dst;// (DEFAULT_FRAME_HEIGHT, DEFAULT_FRAME_WIDTH, CV_32F, cv::Scalar::all(0));
    bool drawed = false;

    if (subtyep == MEDIASUBTYPE_MJPG) {
        cv::Mat src(1, lBufferSize, CV_8UC1, (void*)pBuffer);
        dst = imdecode(src, cv::IMREAD_COLOR);
        winname += "MJPG";
        drawed = true;
    }
    else {
        if (subtyep == MEDIASUBTYPE_NV12) {
            cv::Mat src(height * 12 / 8, width, CV_8UC1, (void*)pBuffer);
            dst = cv::Mat(height, width, CV_8UC3, cv::Scalar::all(0));
            cv::cvtColor(src, dst, cv::COLOR_YUV2BGR_NV12);
            winname = "NV12";
            drawed = true;
        }
        else if (subtyep == MEDIASUBTYPE_Y8) { //frame based payload 
            cv::Mat src(height, width, CV_8UC1, (void*)pBuffer); //frame based payload 
            dst = cv::Mat(height, width, CV_8UC3, cv::Scalar::all(0));
            cv::cvtColor(src, dst, cv::COLOR_GRAY2BGR);
            winname += "Y8";
            drawed = true;
        }
        else if (subtyep == MEDIASUBTYPE_YUY2) { //YUY2
            cv::Mat src(height, width, CV_8UC2, (void*)pBuffer); //uncompress payload
            dst = cv::Mat(height, width, CV_8UC3, cv::Scalar::all(0));
            cv::cvtColor(src, dst, cv::COLOR_YUV2BGR_YUY2);
            winname += "YUY2";
            drawed = true;
        }
        else if (subtyep == MEDIASUBTYPE_M420) { //M420
                                                 //M420 frame size 17x2
                                                 //start +  0 : Y・00  Y・01  Y・02  Y・03
                                                 //start +  4 : Y・10  Y・11  Y・12  Y・13
                                                 //start +  8 : Cb00  Cr00  Cb01  Cr01
                                                 //start + 16 : Y・20  Y・21  Y・22  Y・23
                                                 //start + 20 : Y・30  Y・31  Y・32  Y・33
                                                 //start + 24 : Cb10  Cr10  Cb11  Cr11

                                                 //NV12 frame size 17x2
                                                 //start + 0  : Y・00  Y・01  Y・02  Y・03
                                                 //start + 4  : Y・10  Y・11  Y・12  Y・13
                                                 //start + 8  : Y・20  Y・21  Y・22  Y・23
                                                 //start + 12 : Y・30  Y・31  Y・32  Y・33
                                                 //start + 16 : Cb00  Cr00  Cb01  Cr01
                                                 //start + 20 : Cb10  Cr10  Cb11  Cr11

            if (!pBuffer_NV12)
                pBuffer_NV12 = (BYTE *)malloc(lBufferSize);

            int nv12_uv_planar_pos = width * height;
            int m420_y = 0;
            //M420 to NV12
            for (int y = 0; y < height; y += 2) {
                memcpy((void*)&pBuffer_NV12[(y + 0) * width],    (void*)&pBuffer[(m420_y + 0) * width], width);
                memcpy((void*)&pBuffer_NV12[(y + 1) * width],    (void*)&pBuffer[(m420_y + 1) * width], width);
                memcpy((void*)&pBuffer_NV12[nv12_uv_planar_pos], (void*)&pBuffer[(m420_y + 2) * width], width);
                nv12_uv_planar_pos += width;
                m420_y += 3;
            }

            cv::Mat src(height * 12 / 8, width, CV_8UC1, (void*)pBuffer_NV12); //uncompress payload
            dst = cv::Mat(height, width, CV_8UC3, cv::Scalar::all(0));
            cv::cvtColor(src, dst, cv::COLOR_YUV2BGR_NV12);
            winname += "M420";
            drawed = true;
        }
    }

    if (drawed) {
        //printf("stillimage cols %d rows %d\n", dst.cols, dst.rows);
        //cv::namedWindow(winname, WINDOW_AUTOSIZE);
        if (dst.rows != 0 && dst.cols != 0) {
            cv::imshow(winname, dst);
            int cvkey = cv::waitKey(1) & 0xFF;

            if (27 == cvkey) { //27 for ASCII ESC
                cv::destroyAllWindows();
                showStillImageExecute = false;
            }
        }
    }
}

static int stillImageCallback(double time, BYTE *buff, LONG len) {
    printf("stillframe[%d] size:%d\n", stillframe_count, len);

    const int pre_stillframe_iptr = ((stillframe_iptr + 1) >= QUEUE_FRAM_NUM) ? 0 : (stillframe_iptr + 1);
    if (pre_stillframe_iptr == stillframe_optr) {
        printf("skip stillframe\n");
    }
    else {
        queueframeStill[stillframe_iptr].buffer = (BYTE *)(cpBufferStill + (cpBufferLenMaxStill*stillframe_iptr));
        queueframeStill[stillframe_iptr].size = len;
        BYTE * dst = (BYTE *)(cpBufferStill + (cpBufferLenMaxStill*stillframe_iptr));
        memcpy(queueframeStill[stillframe_iptr].buffer, buff, len);
        stillframe_iptr = pre_stillframe_iptr;
    }

    stillframe_count++;
    return 0;
}
#endif

static const char *shortopts = "t:d:s:w:h:f";

static struct option long_options[] = {
    { "help",   no_argument,       NULL, '?' },
    { "type",   required_argument, NULL, 't' },
    { "device", required_argument, NULL, 'd' },
    { "stream", required_argument, NULL, 's' },
    { "width",  required_argument, NULL, 'w' },
    { "height", required_argument, NULL, 'h' },
    { "framerate",    required_argument, NULL, 'f' },
#if defined(SUPPORT_STILL_IMAGE)
    { "stillimage", required_argument, NULL, 'S' },
#endif
    { 0 ,0, 0, 0 }
};

void usage_long_options() {
    printf("Usage: DSCamera\n");
    printf("Options:\n");
    printf(" --device DEVICE                  specify device index (default=0)\n");
    printf(" --type TYPE                      request video type (default=YUY2)\n");
    printf("  TYPE: YUY2,NV12,M420,MJPG,Y8\n");
    printf(" --stream STREAM                  specify video stream index (default=0)\n");
    printf(" --width WIDTH                    request video width (default=320)\n");
    printf(" --height HEIGHT                  request video height (default=240)\n");
    printf(" --framerate FRAMERATE            request video frame rate (default=30)\n");
#if defined(SUPPORT_STILL_IMAGE)
    printf(" --stillimage ENABLE              enable sitll image (default=0)\n");
#endif
    printf(" --deviceName STR                 specify device name (e.g., USB Camera)\n");
    printf(" --help                           help message\n");
    printf("\n");
    printf("Example:\n");
    printf(" DSCamera --device=0 --type=YUY2 --framerate=30\n");
}

void lptstr2str(LPTSTR tch, char* &pch)
{
#ifndef UNICODE
    std::memcpy(pch, tch, strlen(tch) + 1);
#else
    size_t n =
        sizeof(TCHAR) / sizeof(char)* wcsnlen(tch, std::string::npos);
    pch = new char[n + 1];
    std::memcpy(pch, tch, n + 1);
    size_t len = n - std::count(pch, pch + n, NULL);
    std::remove(pch, pch + n, NULL);
    pch[len] = NULL;
#endif
}

int getch_noblock() {
    if (_kbhit())
        return _getch();
    else
        return -1;
}

int main(int argc, char **argv)
{  
    int deviceIndex = DEFAULT_DEVICE_IDX;
    int streamIndex = DEFAULT_STREAM_IDX;
    int frameRate = DEFAULT_FRAME_RATE;
    int frameWidth = DEFAULT_FRAME_WIDTH;
    int frameHeight = DEFAULT_FRAME_HEIGHT;
    GUID frameSubtype = DEFAULT_FRAME_SUBTYPE;
#if defined(SUPPORT_STILL_IMAGE)
    int stillimageWidth = DEFAULT_FRAME_WIDTH;
    int stillimageHeight = DEFAULT_FRAME_HEIGHT;
    GUID stillimageSubtype = DEFAULT_FRAME_SUBTYPE;
    captureMaxNum_Still = DEFAULT_CAPTRUE_MAXNUM;
#endif
    captureMaxNum = DEFAULT_CAPTRUE_MAXNUM;
    char *deviceName = "";
    bool findDeviceByDeviceName = false;
    char* videoType = "MJPEG";
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
        case 'f':
            frameRate = strtol(optarg, NULL, 10);
            break;
        case 'w':
            frameWidth = strtol(optarg, NULL, 10);
            break;
        case 'h':
            frameHeight = strtol(optarg, NULL, 10);
            break;
#if defined(SUPPORT_STILL_IMAGE)
        case 'S':
            enableStillImage = strtol(optarg, NULL, 10);
            break;
#endif
        case '?':
        default:
            usage_long_options();
            /* NOTREACHED */
            return 0;
            //break;
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
    else if (!strcmp(videoType, "M420"))
    {
        frameSubtype = MEDIASUBTYPE_M420;
    }
    else if (!strcmp(videoType, "Y8"))
    {
        frameSubtype = MEDIASUBTYPE_Y8;
    }

    if (PathIsDirectory(SAVE_FRAME_DIR)) {
        saveFrameExecute = true;
        saveStillImageExecute = true;
    }
    else {
        saveFrameExecute = false;
        saveStillImageExecute = false;
    }

    bool ret;

    printf("create interface\n");
    ret = CameraCreateInterface();
    SetConsoleCtrlHandler(CtrlHandler, TRUE);

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

    if (findDeviceByDeviceName) {
        printf("find device by deviceName %s\n", deviceName);

        UINT32 selectedVal = 0xFFFFFFFF;
        for (UINT32 i = 0; i < dlist.DeviceNum; i++) {
            char* strFriendlyName;
            lptstr2str(dlist.DeviceItems[i].FriendlyName, strFriendlyName);
            printf("%d: %s\n", i, strFriendlyName);

            if (!(strcmp(strFriendlyName, deviceName)))
                selectedVal = i;
        }
        if (selectedVal != 0xFFFFFFFF) {
            printf("Found \"%s\"\n", deviceName);
            deviceIndex = selectedVal;
        }
        else {
            printf("Did not find \"%s\"\n", deviceName);
            CameraCloseInterface();
            goto End;
        }
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
    ret = CameraOpen(streamIndex);

    if (!ret) {
        printf("failed to OpenStream(%d) !\n", streamIndex);
        goto End;
    }

    printf("List Camera Format\n");
    FormatList list;
    CameraGetFormatList(&list);

    if (list.FormatNum > 0) {
        int formatIndex[3] = { -1, -1, -1 };

        for (int i = 0; i < list.FormatNum; i++) {
            printf(" [%d] Width %d Height %d\n", i, list.FormatItems[i].Width,
                list.FormatItems[i].Height);
            printf("   subtype:%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X\n",
                list.FormatItems[i].MediaSubtype.Data1,
                list.FormatItems[i].MediaSubtype.Data2,
                list.FormatItems[i].MediaSubtype.Data3,
                list.FormatItems[i].MediaSubtype.Data4[0],
                list.FormatItems[i].MediaSubtype.Data4[1],
                list.FormatItems[i].MediaSubtype.Data4[2],
                list.FormatItems[i].MediaSubtype.Data4[3],
                list.FormatItems[i].MediaSubtype.Data4[4],
                list.FormatItems[i].MediaSubtype.Data4[5],
                list.FormatItems[i].MediaSubtype.Data4[6],
                list.FormatItems[i].MediaSubtype.Data4[7]);

            if (CompareGuid(frameSubtype, list.FormatItems[i].MediaSubtype))
            {
                formatIndex[0] = i;
                printf("--type is matched\n");

                if (list.FormatItems[i].Width == frameWidth && list.FormatItems[i].Height == frameHeight)
                {
                    formatIndex[1] = i;
                    printf("--width and --height are matched\n");

                    if (list.FormatItems[i].AvgTimePerFrame == (LONGLONG)(10000000 / frameRate))
                    {
                        formatIndex[2] = i;
                        printf("--framerate is matched\n");
                    }
                }
            }
        }

        int matchedItem = 0;
        if (formatIndex[0] >= 0) { matchedItem = 1; }
        if (formatIndex[1] >= 0) { matchedItem = 2; }
        if (formatIndex[2] >= 0) { matchedItem = 3; }

        if (matchedItem > 0) {
            printf("Found Format[%d] has %d matched item\n", formatIndex[matchedItem - 1], matchedItem);
            ret = CameraSetFormat(formatIndex[matchedItem - 1]);
            if (!ret) {
                printf("failed to CameraSetFormat !\n");
                goto End;
            }
        }
        else {
            printf("Did not find matched --type in Camera Format List\n");
        }
    }
    else {
        printf("Did find Camera Format List\n");
    }

    ret = CameraSetGrabFormat(frameWidth, frameHeight, frameSubtype);

    if (!ret) {
        printf("failed to SetGrabFormat !\n");
        goto End;
    }
#if false
    ret = CameraSetGrabFrameRate(frameRate);
    if (!ret) {
        printf("failed to SetGrabFrameRate !\n");
        goto End;
    }
#endif
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
    cpBuffer = (BYTE *)malloc(cpBufferLenMax * QUEUE_FRAM_NUM);
    frame_count = 0;
    BYTE *frame = (BYTE *)malloc(cpBufferLenMax);
#if defined(SUPPORT_STILL_IMAGE)
    stillframe_count = 0;
    BYTE *stillframe = NULL;
    stillimageWidth = frameWidth;
    stillimageHeight = frameHeight;
    stillimageSubtype = frameSubtype;

    if (enableStillImage == 1) {
        printf("enable still image\n");
        ret = CameraEnableStillImage();

        if (!ret) {
            printf("failed to EnableStillImage !\n");
            goto End;
        }

        printf("set sillimage callback NULL\n");
        CameraSetStillImageCallback(NULL);

        ret = CameraSetStillImageFormat(stillimageWidth, stillimageHeight, stillimageSubtype);
        
        if (!ret) {
            printf("failed to SetStillImageFormat !\n");
            goto End;
        }

        printf("get current stillimage format\n");
        ZeroMemory(&stillimagefmt, sizeof(stillimagefmt));
        ret = CameraGetStillImageFormat(&stillimagefmt);

        if (!ret) {
            printf("Failed to GetStillImageFormat !\n");
            goto End;
        }

        stillimageSubtype = stillimagefmt.MediaSubtype;
        stillimageWidth = stillimagefmt.Width;
        stillimageHeight = stillimagefmt.Height;

        printf(" stillimage w:%d h:%d\n",
            stillimageWidth, stillimageHeight);
        printf(" stillimage subtype:%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X\n",
            stillimageSubtype.Data1, stillimageSubtype.Data2, stillimageSubtype.Data3,
            stillimageSubtype.Data4[0],
            stillimageSubtype.Data4[1],
            stillimageSubtype.Data4[2],
            stillimageSubtype.Data4[3],
            stillimageSubtype.Data4[4],
            stillimageSubtype.Data4[5],
            stillimageSubtype.Data4[6],
            stillimageSubtype.Data4[7]);

        cpBufferLenMaxStill = stillimageWidth * stillimageHeight * 3;
        cpBufferStill = (BYTE *)malloc(cpBufferLenMaxStill * QUEUE_FRAM_NUM);
        stillframe = (BYTE *)malloc(cpBufferLenMaxStill);

        CameraSetStillImageCallback(stillImageCallback);
        printf("press kye '1' to trigger still image\n");
    }
#endif
    printf("start stream\n");
    ret = CameraStartStream();

    if (!ret) {
        printf("failed to StartStream !\n");
        goto End;
    }

    while (1) {
        //this_thread::sleep_for(chrono::seconds(1));

        if (frame_optr != frame_iptr) {
            queueframe[frame_optr].buffer = (BYTE *)(cpBuffer + (cpBufferLenMax*frame_optr));
            int len = queueframe[frame_optr].size;
            memcpy(frame, queueframe[frame_optr].buffer, len);
            frame_optr++;
            frame_optr = frame_optr >= QUEUE_FRAM_NUM ? 0 : frame_optr;

            if (frameSubtype != MEDIASUBTYPE_NONE && \
                frameWidth != 0 && frameHeight != 0) {

                if (showFrameExecute)
                    showFrame(frameWidth, frameHeight, frameSubtype, frame, len);

                if (saveFrameExecute && captureMaxNum > 0) {
                    saveFrame(frame_count, frameSubtype, frame, len);
                    captureMaxNum--;
                }
            }
        }
#if defined(SUPPORT_STILL_IMAGE)
        if (enableStillImage && (frame_optr == frame_iptr) && getch_noblock() == (int)'1')
        {
            printf("stillframe_count[%d] trigger still image\n", stillframe_count);
            CameraTriggerStillImage();
        }

        if (stillframe_optr != stillframe_iptr) {
            queueframeStill[stillframe_optr].buffer = (BYTE *)(cpBufferStill + (cpBufferLenMaxStill*stillframe_optr));
            int len = queueframeStill[stillframe_optr].size;
            memcpy(stillframe, queueframeStill[stillframe_optr].buffer, len);
            const int pre_stillframe_optr = ((stillframe_optr + 1) >= QUEUE_FRAM_NUM) ? 0 : (stillframe_optr + 1);
            stillframe_optr = pre_stillframe_optr;
            BYTE *buff = cpBufferStill;

            if (stillimageSubtype != MEDIASUBTYPE_NONE && \
                stillimageWidth != 0 && stillimageHeight != 0) {

                if (showStillImageExecute)
                    showStillImage(stillimageWidth, stillimageHeight, stillimageSubtype, buff, len);

                if (saveStillImageExecute && captureMaxNum_Still > 0) {
                    saveStillImage(stillframe_count, stillimageSubtype, buff, len);
                    captureMaxNum_Still--;
                }
            }

            printf("stillimage[%d] size:%d\n", stillframe_count, len);
        }
#endif
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

End:
    printf("close interface\n");
    CameraCloseInterface();
    return 0;
}

BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
    switch (fdwCtrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_CLOSE_EVENT:
        CameraCloseInterface();
        return TRUE;

    default:
        return FALSE;
    }
}

bool CompareGuid(GUID guid1, GUID guid2)
{
    if (guid1.Data1 == guid2.Data1 &&
        guid1.Data2 == guid2.Data2 &&
        guid1.Data3 == guid2.Data3 &&
        guid1.Data4[0] == guid2.Data4[0] &&
        guid1.Data4[1] == guid2.Data4[1] &&
        guid1.Data4[2] == guid2.Data4[2] &&
        guid1.Data4[3] == guid2.Data4[3] &&
        guid1.Data4[4] == guid2.Data4[4] &&
        guid1.Data4[5] == guid2.Data4[5] &&
        guid1.Data4[6] == guid2.Data4[6] &&
        guid1.Data4[7] == guid2.Data4[7])
    {
        return true;
    }

    return false;
}