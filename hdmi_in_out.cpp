#include <opencv2/opencv.hpp>
#include <iostream>

int main() {
    // 配置HDMI输入设备
    int input_device = 40;
    int input_width = 1920;
    int input_height = 1080;
    
    // 打开HDMI输入设备
    cv::VideoCapture cap(input_device);
    if (!cap.isOpened()) {
        std::cerr << "无法打开HDMI输入设备" << std::endl;
        return -1;
    }
    
    // 设置输入分辨率
    cap.set(cv::CAP_PROP_FRAME_WIDTH, input_width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, input_height);
    
    // 检查实际设置的分辨率
    int actual_width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    int actual_height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    std::cout << "输入设备分辨率: " << actual_width << "x" << actual_height << std::endl;
    
    // 创建窗口用于显示输出
    cv::namedWindow("HDMI Output", cv::WINDOW_NORMAL);
    cv::setWindowProperty("HDMI Output", cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);
    
    std::cout << "成功打开HDMI输入设备，开始视频传输..." << std::endl;
    
    try {
        cv::Mat frame;
        while (true) {
            cap >> frame;
            if (frame.empty()) {
                std::cerr << "无法读取帧，退出..." << std::endl;
                break;
            }
            
            // 可以在这里进行帧处理，例如添加水印、调整亮度等
            
            // 显示帧
            cv::imshow("HDMI Output", frame);
            
            // 按ESC键退出循环
            if (cv::waitKey(1) == 27) {  // ESC键
                break;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "异常发生: " << e.what() << std::endl;
        return -1;
    }
    
    // 释放资源
    cap.release();
    cv::destroyAllWindows();
    std::cout << "资源已释放" << std::endl;
    
    return 0;
}    