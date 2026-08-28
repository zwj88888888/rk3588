#include <opencv2/opencv.hpp>
#include <mutex>

struct Mbuffer{
    cv::Mat img;
    std::mutex mtx;
};