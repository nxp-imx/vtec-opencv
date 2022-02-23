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

#include "opencv2/core/utility.hpp"
#include "opencv2/core/private.hpp"
#include <opencv2/core/types_c.h>
#include "opencv2/core.hpp"
#include <opencv2/core/utils/logger.hpp>
#include <opencv2/warp.hpp>


#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>

#include <linux/dw100.h>

namespace cv {
namespace warp {

Warper::Warper(int index, int image_count)
    :inputWidth(inFmt.fmt.pix.width),
    inputHeight(inFmt.fmt.pix.height),
    inputFourcc(inFmt.fmt.pix.pixelformat),
    inputSizeimage(inFmt.fmt.pix.sizeimage),
    outputWidth(outFmt.fmt.pix.width),
    outputHeight(outFmt.fmt.pix.height),
    outputFourcc(outFmt.fmt.pix.pixelformat),
    outputSizeimage(outFmt.fmt.pix.sizeimage),
    buf_count(image_count), isStreaming(false)
{
    String deviceName = cv::format("/dev/video%d", index);

    c_fd.open(deviceName.c_str());
    CV_LOG_INFO(NULL, "Opening "<< deviceName << " device for "
        << image_count << " image(s) handling");

    if (c_fd.g_fd() == -1) {
        CV_Error(CV_StsBadArg, "Error while opening " + deviceName);
    }

    if (c_fd.querycap(vcap)) {
        CV_Error(CV_StsBadArg, "Error while querying capabilities" + deviceName);
    }
    setFormat(false, DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_FOURCC);
    setFormat(true, DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_FOURCC);

    qin.init(c_fd.g_type(), V4L2_MEMORY_MMAP);
    qout.init(v4l_type_invert(c_fd.g_type()), V4L2_MEMORY_MMAP);
}

Warper::~Warper()
{
    if (c_fd.g_fd() != -1)
        c_fd.close();
}

#define FOURCC_TO_STRING(x) \
        <<reinterpret_cast<char *>(&x)[0] \
        <<reinterpret_cast<char *>(&x)[1] \
        <<reinterpret_cast<char *>(&x)[2] \
        <<reinterpret_cast<char *>(&x)[3] \


int Warper::setFormat(bool isCapture, unsigned width, unsigned height, int fourcc)
{
    struct v4l2_format &vfmt = isCapture ? outFmt: inFmt;
    int type = isCapture ? V4L2_BUF_TYPE_VIDEO_CAPTURE : V4L2_BUF_TYPE_VIDEO_OUTPUT;
    int ret;

    CV_LOG_INFO(NULL, "Requested " \
        << (isCapture ? "Capture" : "Output") << " device:"\
        << width << "x" << height << " " FOURCC_TO_STRING(fourcc));

    ret = c_fd.g_fmt(vfmt,type);
    if (ret)
        return ret;

    if (isStreaming)
        return 1;

    vfmt.fmt.pix.width = width;
    vfmt.fmt.pix.height = height;
    vfmt.fmt.pix.pixelformat = fourcc;

    ret = c_fd.s_fmt(vfmt, true);

    CV_LOG_INFO(NULL, "Got " \
        << (isCapture ? "Capture" : "Output") << " device:"\
        << vfmt.fmt.pix.width << "x" << vfmt.fmt.pix.height \
        << " " FOURCC_TO_STRING(vfmt.fmt.pix.pixelformat) \
        << " Image size: " << vfmt.fmt.pix.sizeimage);

    return ret;
}

int Warper::setInputFormat(unsigned width, unsigned height, int fourcc)
{
    return setFormat(false, width, height, fourcc);
}

int Warper::setOutputFormat(unsigned width, unsigned height, int fourcc)
{
    return Warper::setFormat(true, width, height, fourcc);
}

int Warper::setMap(InputArray &map)
{
    int ret;
    int32_t nitems, length;

    struct v4l2_ext_controls ctrls;
    struct v4l2_ext_control ectrl;
    uint32_t *data;
    Mat mapMat = map.getMat();

    if (isStreaming)
        return 1;

    CV_Assert(mapMat.type() == CV_32SC1);

    data = reinterpret_cast<uint32_t*>(mapMat.data);
    length = mapMat.total() * mapMat.elemSize();
    nitems = mapMat.total();

    CV_LOG_INFO(NULL, "Set look up table to "
        << nitems << " items, " << length << " bytes");

    memset(&ectrl, 0, sizeof(ectrl));
    ectrl.id = V4L2_CID_DW100_DEWARPING_16x16_VERTEX_MAP;
    ectrl.p_u32 = data;
    ectrl.size = length;

    memset(&ctrls, 0, sizeof(ctrls));
    ctrls.which = V4L2_CTRL_WHICH_CUR_VAL;
    ctrls.count = 1;
    ctrls.controls = &ectrl;

    ret = c_fd.s_ext_ctrls(ctrls);

    if (!ret)
        mapping = map.getMat();
    else
        CV_LOG_ERROR(NULL, "Error while setting Look Up table");

    return ret;
}

int Warper::cancelMap()
{
    struct v4l2_ext_controls ctrls;
    struct v4l2_ext_control ectrl;
    uint32_t *data;
    int ret;

    if (isStreaming)
        return 1;
    if (mapping.empty())
        return 1;

    data = reinterpret_cast<uint32_t*>(mapping.data);

    CV_LOG_INFO(NULL, "Cancelling Look up table.");

    memset(&ectrl, 0, sizeof(ectrl));
    ectrl.id = V4L2_CID_DW100_DEWARPING_16x16_VERTEX_MAP;
    ectrl.p_u32 = data;
    ectrl.size = 4;

    memset(&ctrls, 0, sizeof(ctrls));
    ctrls.which = V4L2_CTRL_WHICH_CUR_VAL;
    ctrls.count = 1;
    ctrls.controls = &ectrl;

    ret = c_fd.s_ext_ctrls(ctrls);

    if (!ret)
        mapping = Mat();
    else
        CV_LOG_ERROR(NULL, "Error while cancelling Look Up table");

    return ret;
}

int Warper::write(InputArrayOfArrays images)
{
    unsigned nimages = images.rows();

    CV_Assert(nimages <= qout.g_buffers());

    for (unsigned i = 0; i < nimages; i++) {
        cv4l_buffer buf(qout);
        Mat image = images.getMat(i);
        unsigned imagesz = image.total() * image.elemSize();
            if (c_fd.querybuf(buf, i)) {
            CV_LOG_ERROR(NULL, "Error " << errno << " while querying output buffer");
            return 1;
        }

        buf.update(qout, i);
        buf.s_bytesused(buf.g_length(0));
        void *pbuf = qout.g_dataptr(buf.g_index(), 0);
        unsigned buf_len = qout.g_length(0);

        CV_Assert(imagesz == buf_len);

        memcpy(pbuf, image.ptr(), buf_len);

        buf.s_timestamp_clock();

        if (c_fd.qbuf(buf)) {
            CV_LOG_ERROR(NULL, "Error " << errno << " while queuing output buffer");
            return 1;
        }
    }

    return 0;
}

int Warper::read(OutputArrayOfArrays images)
{
    int ret;
    unsigned nimages = images.rows();
    int fd = c_fd.g_fd();
    char msg[1024];

    fd_set fds[2];
    fd_set *rd_fds = &fds[0];
    fd_set *wr_fds = &fds[1];

    struct timeval tv;

    tv.tv_sec = 1;
    tv.tv_usec = 0;

    CV_Assert(nimages <= qin.g_buffers());

    for (unsigned i = 0; i < nimages; i++) {
        cv4l_buffer buf(qin);
        cv4l_buffer bufOut(qout);
        Mat image = images.getMat(i);
        unsigned imagesz = image.total() * image.elemSize();

        if (rd_fds) {
            FD_ZERO(rd_fds);
            FD_SET(fd, rd_fds);
        }

        if (wr_fds) {
            FD_ZERO(wr_fds);
            FD_SET(fd, wr_fds);
        }

        ret = select(fd + 1, rd_fds, wr_fds, nullptr, &tv);
        if (ret == 0) {
            CV_LOG_ERROR(NULL, "Timeout while reading");
            return 1;
        } else if (ret < 0) {
            CV_LOG_ERROR(NULL, "Error on select");
            return 1;
        }

        if (!FD_ISSET(fd, rd_fds)) {
            CV_LOG_ERROR(NULL, "Fd should be ready for read operation !");
            return 1;
        }

        if (!FD_ISSET(fd, wr_fds)) {
            CV_LOG_ERROR(NULL, "Fd should be ready for write operation !");
            return 1;
        }

        ret = c_fd.dqbuf(buf);
        if (ret == EAGAIN) {
            CV_LOG_ERROR(NULL, "Error while dequeue-ing capture buffer");
            return 1;
        }

        ret = c_fd.dqbuf(bufOut);
        if (ret == EAGAIN) {
            CV_LOG_ERROR(NULL, "Error while dequeue-ing output buffer");
            return 1;
        }


        CV_LOG_INFO(NULL, "Reading from " << buf.g_index());
        unsigned char *pbuf = static_cast<unsigned char *>(qin.g_dataptr(buf.g_index(), 0));
        unsigned used = buf.g_bytesused(0);
        unsigned offset = buf.g_data_offset(0);
        used -= offset;
        pbuf += offset;

        CV_Assert(imagesz == used);
        sprintf(msg, " Writing image from %p to %p", pbuf, image.ptr());
        CV_LOG_INFO(NULL, msg);
        memcpy(image.ptr(), pbuf, used);

        if (c_fd.qbuf(buf)) {
            CV_LOG_ERROR(NULL, "Error " << errno << " while queuing input buffer");
            return 1;
        }
    }

    return 0;
}


int Warper::setup_input_queue()
{
    if (qin.reqbufs(&c_fd, buf_count))
        CV_Error(CV_StsBadArg, "Error while requesting in buffers");

    if (qin.obtain_bufs(&c_fd))
        CV_Error(CV_StsBadArg, "Error while mapping in buffers");

    if (qin.queue_all(&c_fd))
        CV_Error(CV_StsBadArg, "Error while queue-ing in buffers");

    CV_Assert(qin.g_num_planes() == 1);

    return 0;
}

int Warper::setup_output_queue()
{
    if (qout.reqbufs(&c_fd, buf_count))
        CV_Error(CV_StsBadArg, "Error while requesting out buffers");

    if (qout.obtain_bufs(&c_fd))
        CV_Error(CV_StsBadArg, "Error while mapping out buffers");

    CV_Assert(qout.g_num_planes() == 1);

    return 0;
}

int Warper::start_streaming()
{
    if (isStreaming)
        return 0;

    if (c_fd.streamon(qout.g_type()))
        CV_Error(CV_StsBadArg, "Error while starting out streaming");

    if (c_fd.streamon(qin.g_type()))
        CV_Error(CV_StsBadArg, "Error while starting in streaming");

    isStreaming = true;

    return 0;
}

int Warper::stop_streaming()
{
    if (!isStreaming)
        return 0;

    c_fd.streamoff(qin.g_type());
    c_fd.streamoff(qout.g_type());

    qin.free(&c_fd);
    qout.free(&c_fd);

    isStreaming = false;

    return 0;
}

void Warper::warp(InputArrayOfArrays inputImages, OutputArrayOfArrays outputImages)
{
    if (!isStreaming) {
        setup_input_queue();
        setup_output_queue();
        start_streaming();
    }
    CV_LOG_INFO(NULL, "Input dims: " << inputImages.dims() \
        << " channels: " << inputImages.channels() \
        << " ImageSize: " << inputSizeimage \
        << " cols: " << inputImages.cols() \
        << " rows: " << inputImages.rows());

    //TODO: Handle scaling
    outputImages.create(inputImages.rows(), outputSizeimage, inputImages.type());
    CV_LOG_INFO(NULL, "Output dims: " << outputImages.dims() \
        << " channels: " << outputImages.channels() \
        << " ImageSize: " << outputSizeimage \
        << " cols: " << outputImages.cols() \
        << " rows: " << outputImages.rows());

    if (write(inputImages))
        CV_Error(CV_StsBadArg, "Error while writing input image");

    if (read(outputImages))
        CV_Error(CV_StsBadArg, "Error while reading output image");
}


}} //cv::warp
