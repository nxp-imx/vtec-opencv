/*
   Copyright 2023 NXP

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 */

#include <iostream>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include "opencv2/videoio.hpp"
#include "opencv2/imx2d.hpp"

#define PF_ENABLED
#include "profiler.hpp"

namespace {

PF_ENTRY_PERIOD_MS(__resize, 1000);
PF_ENTRY_PERIOD_MS(__flip, 1000);
PF_ENTRY_PERIOD_MS(__rotate, 1000);


const std::string about =
        "Camera capture and accelerated video resize\n";
const std::string keys  =
        "{help h usage ? |        | print this message }"
        "{cid            | 0      | video capture index }"
        "{cstr           | <none> | video capture string }"
        "{cv4l2          |        | V4L2 videocapture API }"
        "{cgst           |        | GStreamer videocapture API }"
        "{flip           | 0      | flip mode 0:none 1:horizontal 2:vertical 3:both }"
        "{ifps           | 30     | input video FPS }"
        "{iw             | 640    | input video width }"
        "{ih             | 480    | input video height }"
        "{ow             | -1     | output video width (negative means no width resize) }"
        "{oh             | -1     | output video height (negative means no height resize) }"
        "{rotate         | 0      | rotate mode (degrees clockwise) 0:none 1:90 2:180 3:270 }"
        "{imx2d          | true   | i.MX 2D acceleration }"
        "{alloc          | true   | i.MX 2D graphic allocator enabled }"
        ;
}

int main(int argc, char *argv[]) {
    cv::CommandLineParser parser(argc, argv, keys);
    parser.about(about);
    if (parser.has("help"))
    {
        parser.printMessage();
        exit(0);
    }

    int captureIndex = parser.get<unsigned int>("cid");
    std::string captureStr = parser.get<std::string>("cstr");
    bool v4l2Api = parser.has("cv4l2");
    bool gstApi = parser.has("cgst");
    int videoInfps = parser.get<int>("ifps");
    int videoInWidth = parser.get<int>("iw");
    int videoInHeight = parser.get<int>("ih");
    int videoOutWidth = parser.get<int>("ow");
    int videoOutHeight = parser.get<int>("oh");
    int flipMode = parser.get<int>("flip");
    int rotateMode = parser.get<int>("rotate");

    std::cout
        << " captureIndex:" << captureIndex
        << " captureStr:" << captureStr
        << " v4l2Api:" << v4l2Api
        << " gstApi:" << gstApi
        << " videoInfps:" << videoInfps
        << " videoInWidth:" << videoInWidth
        << " videoInHeight:" << videoInHeight
        << " videoOutWidth:" << videoOutWidth
        << " videoOutHeight:" << videoOutHeight
        << std::endl;

    int api = cv::CAP_ANY;
    if (v4l2Api)
        api = cv::CAP_V4L2;
    else if (gstApi)
        api = cv::CAP_GSTREAMER;

    bool useImx2d = parser.get<bool>("imx2d");
    bool useCustomAllocator = parser.get<bool>("alloc");

    std::cout
        << " useImx2d:" << useImx2d
        << " useCustomAllocator:" << useCustomAllocator
        << std::endl;

    cv::imx2d::setUseImx2d(useImx2d);
    cv::imx2d::setUseGMatAllocator(useCustomAllocator);

    cv::VideoCapture cap;

    std::string windowName;
    bool ret;
    if (parser.has("cstr"))
        ret = cap.open(captureStr, api);
    else
        ret = cap.open(captureIndex, api);

    if (!parser.has("cstr"))
        captureStr = std::to_string(captureIndex);

    if (!ret) {
        std::cerr << "Video capture failed to open [" << captureStr << "]" << std::endl;
        exit(-1);
    }
    windowName = cap.getBackendName() + " " + captureStr;

    cap.set(cv::CAP_PROP_FRAME_WIDTH, videoInWidth);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, videoInHeight);
    cap.set(cv::CAP_PROP_FPS, videoInfps);

    bool resize = false;
    if ((videoOutWidth >= 0) || (videoOutHeight >= 0))
        resize = true;
    if (videoOutWidth < 0)
        videoOutWidth = videoInWidth;
    if (videoOutHeight < 0)
        videoOutHeight = videoInHeight;
    cv::Size dstSize(videoOutWidth, videoOutHeight);

    int flipCode = 0;
    if (flipMode) {
        if (flipMode == 1)
            flipCode = 1;
        else if (flipMode == 2)
            flipCode = 0;
        else if (flipMode == 3)
            flipCode = -1;
    }

    int rotateCode = 0;
    if (rotateMode) {
        if (rotateMode == 1)
            rotateCode = cv::ROTATE_90_CLOCKWISE;
        else if (rotateMode == 2)
            rotateCode = cv::ROTATE_180;
        else if (rotateMode == 3)
            rotateCode = cv::ROTATE_90_COUNTERCLOCKWISE;
    }

    cv::Mat src, dstRsz, dstFlip, dstRot;
    while (true) {
        cap >> src;
        if (src.empty()) {
            std::cerr << "Empty frame!" << std::endl;
            cap.set(cv::CAP_PROP_POS_FRAMES, 0);
            continue;
        }

        if (flipMode) {
            PF_ENTER(__flip);
            cv::flip(src, dstFlip, flipCode);
            PF_EXIT(__flip);
        } else {
            dstFlip = src;
        }

        if (rotateMode) {
            PF_ENTER(__rotate);
            cv::rotate(dstFlip, dstRot, rotateCode);
            PF_EXIT(__rotate);
        } else {
            dstRot = dstFlip;
        }

        if (resize) {
            PF_ENTER(__resize);
            cv::resize(dstRot, dstRsz, dstSize, 0, 0, cv::INTER_LINEAR);
            PF_EXIT(__resize);
        } else {
            dstRsz = dstRot;
        }

        cv::imshow(windowName, dstRsz);

        int c = cv::pollKey();
        if (c == 27)
           break;
    }

    cap.release();
    return 0;
}
