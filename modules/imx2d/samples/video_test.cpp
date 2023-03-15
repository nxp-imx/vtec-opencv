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

const std::string about =
        "Camera capture and accelerated video resize\n";
const std::string keys  =
        "{help h usage ? |        | print this message }"
        "{cid            | 0      | video capture index }"
        "{cstr           | <none> | video capture string }"
        "{cv4l2          |        | V4L2 videocapture API }"
        "{cgst           |        | GStreamer videocapture API }"
        "{ifps           | 30     | input video FPS }"
        "{iw             | 640    | input video width }"
        "{ih             | 480    | input video height }"
        "{ow             | 1920   | output video width }"
        "{oh             | 1080   | output video height }"
        "{imx2d          | true   | i.MX 2D acceleration }"
        "{alloc          | true   | i.MX 2D custom allocator enabled }"
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

    unsigned int captureIndex = parser.get<unsigned int>("cid");
    std::string captureStr = parser.get<std::string>("cstr");
    bool v4l2Api = parser.has("cv4l2");
    bool gstApi = parser.has("cgst");
    unsigned int videoInfps = parser.get<unsigned int>("ifps");
    unsigned int videoInWidth = parser.get<unsigned int>("iw");
    unsigned int videoInHeight = parser.get<unsigned int>("ih");
    unsigned int videoOutWidth = parser.get<unsigned int>("ow");
    unsigned int videoOutHeight = parser.get<unsigned int>("oh");

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

    cv::Mat src, dst;
    while (true) {
        cap >> src;
        if (src.empty()) {
            std::cerr << "Empty frame!" << std::endl;
            cap.set(cv::CAP_PROP_POS_FRAMES, 0);
            continue;
        }

        cv::Size dstSize(videoOutWidth, videoOutHeight);

        PF_ENTER(__resize);
        cv::resize(src, dst, dstSize, 0, 0, cv::INTER_LINEAR);
        PF_EXIT(__resize);

        cv::imshow(windowName, dst);

        int c = cv::pollKey();
        if (c == 27)
           break;
    }

    cap.release();
    return 0;
}
