/*
   Copyright 2022 NXP

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

#ifndef __OPENCV_WARP_HPP__
#define __OPENCV_WARP_HPP__

#include <opencv2/core.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <cv4l-helpers.h>
#pragma GCC diagnostic pop


/** @defgroup warp Warping hardware accelerated methods
*/

namespace cv { namespace warp {

//! @addtogroup warp
//! @{

class CV_EXPORTS_W Warper
{
public:
    /**
     * @brief Initialize the Warper.
     * @param index V4L2 M2M DW100 device node
     * @param image_count Number of images to process per I/O calls
     */
    CV_WRAP Warper(int index, int image_count = 1);
    ~Warper();
    /**
     * @brief Set input stream format.
     * @param width Input stream width in pixels
     * @param height Input stream height in pixels
     * @param fourcc Input stream fourcc code
     */
    CV_WRAP int setInputFormat(unsigned width, unsigned height, int fourcc);
    /**
     * @brief Set output stream format.
     * @param width output stream width in pixels
     * @param height output stream height in pixels
     * @param fourcc output stream fourcc code
     */
    CV_WRAP int setOutputFormat(unsigned width, unsigned height, int fourcc);
    /**
     * @brief Set dewarping mapping look-Up table.
     * @param map Dewarping mapping Look-Up table array
     */
    CV_WRAP int setMap(InputArray map);
    /**
     * @brief Cancel the user provided dewarping mapping.
     * This forces the driver to use an identity map for next active stream.
     */
    CV_WRAP int cancelMap();
    /**
     * @brief Compute the output image
     * @param inImg Input array of streams
     * @param inImg Output array of streams
     */
    CV_WRAP void warp(InputArray inImg, OutputArray outImg);
    /**
     * @brief Start streaming operations
     */
    CV_WRAP int start_streaming();
    /**
     * @brief Stop streaming operations
     */
    CV_WRAP int stop_streaming();

    CV_WRAP unsigned getInputWidth();
    CV_WRAP unsigned getInputHeight();
    CV_WRAP unsigned getInputFourcc();
    CV_WRAP unsigned getInputSizeimage();
    CV_WRAP unsigned getOutputWidth();
    CV_WRAP unsigned getOutputHeight();
    CV_WRAP unsigned getOutputFourcc();
    CV_WRAP unsigned getOutputSizeimage();

private:
    int setFormat(bool isCapture, unsigned width, unsigned height, int fourcc);
    int write(InputArrayOfArrays images);
    int read(OutputArrayOfArrays images);
    int setup_input_queue();
    int setup_output_queue();
    struct v4l2_capability vcap;
    struct v4l2_format inFmt;
    struct v4l2_format outFmt;
    Mat mapping;
    int buf_count;
    bool isStreaming;
    cv4l_fd c_fd;
    cv4l_queue qin;
    cv4l_queue qout;

};

#define DEFAULT_WIDTH 640
#define DEFAULT_HEIGHT 480
#define DEFAULT_FOURCC V4L2_PIX_FMT_YUYV
#define DEFAULT_SZ_IMAGE 614400 //640*480*2

//! @}
}
} // cv::warp::

#endif //__OPENCV_WARP_HPP__


