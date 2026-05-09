/* test_opencv.cpp - OpenCV 采集测试
 *
 * 注意：third_party 中的 opencv-mobile（rk_aiq 版）默认用 MIPI/rkisp，
 * cap.open(0) 往往对应 /dev/video13 而非 USB UVC。无 CSI 传感器时会 ENUM 失败，
 * 并可能在读帧或 HW JPEG 路径上 Illegal instruction。
 * USB 摄像头请用本工程 test_uvc（纯 V4L2），或自行用 capture_v4l2.cpp 重编 opencv-mobile。
 */

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

int main(int argc, char** argv)
{
    const int cam_index = (argc > 1) ? atoi(argv[1]) : 0;

    cv::VideoCapture cap;
    if (!cap.open(cam_index))
    {
        fprintf(stderr, "VideoCapture open(%d) failed. USB UVC 请使用: test_uvc /dev/video0\n", cam_index);
        return 1;
    }
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 320);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 240);

    if (!cap.isOpened())
    {
        fprintf(stderr, "camera not opened\n");
        return 1;
    }

    const int w = (int)cap.get(cv::CAP_PROP_FRAME_WIDTH);
    const int h = (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    fprintf(stderr, "%d x %d\n", w, h);

    cv::Mat bgr[9];
    for (int i = 0; i < 9; i++)
    {
        cap >> bgr[i];

        sleep(1);
    }

    cap.release();

    // combine into big image
    {
        cv::Mat out(h * 3, w * 3, CV_8UC3);
        bgr[0].copyTo(out(cv::Rect(0, 0, w, h)));
        bgr[1].copyTo(out(cv::Rect(w, 0, w, h)));
        bgr[2].copyTo(out(cv::Rect(w * 2, 0, w, h)));
        bgr[3].copyTo(out(cv::Rect(0, h, w, h)));
        bgr[4].copyTo(out(cv::Rect(w, h, w, h)));
        bgr[5].copyTo(out(cv::Rect(w * 2, h, w, h)));
        bgr[6].copyTo(out(cv::Rect(0, h * 2, w, h)));
        bgr[7].copyTo(out(cv::Rect(w, h * 2, w, h)));
        bgr[8].copyTo(out(cv::Rect(w * 2, h * 2, w, h)));

        cv::imwrite("out.jpg", out);
    }

    return 0;
}
