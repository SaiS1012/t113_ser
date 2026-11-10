#ifndef V4L2CAMERA_H
#define V4L2CAMERA_H

#include <QString>
#include <QImage>
#include <QObject>
#include <cstddef>
#include <atomic>
#include <thread>
#include <vector>
#include <QMutex>


// 封装摄像头打开、采集、取帧和关闭操作。
class V4L2Camera : public QObject {

    Q_OBJECT

public:

    V4L2Camera(const QString& devPath = "/dev/video0"); // 构造摄像头对象；不传路径时默认使用 /dev/video0。
    ~V4L2Camera();          // 析构函数,对象销毁时负责停止采集并释放资源。
    bool open();            // 打开摄像头、准备缓冲区并启动后台采集。
    void close();           // 停止采集并释放摄像头相关资源。
    bool isOpen() const;    // 查询摄像头设备当前是否已打开。
    QImage getFrame();      // 获取当前最新的一帧图像。

    // 获取摄像头实际使用的图像宽度和高度。
    int width() const;      
    int height() const;

private:

    void captureLoop();     // 后台线程执行的循环：等待并取出摄像头帧。
    QString devicePath;     // 摄像头设备路径，例如 /dev/video0
    int v4l2_fd = -1;       // 摄像头设备文件描述符；-1 表示未打开

    // 记录一块已映射到程序地址空间的摄像头缓冲区。
    struct CaptureBuffer {
        void* start;        // 程序访问这块缓冲区时使用的地址
        std::size_t length; // 缓冲区可容纳的数据长度
    };
    std::vector<CaptureBuffer> captureBuffers;  // 保存所有映射缓冲区的信息
    std::atomic<bool> captureRunning{false};    // 跨线程共享的运行标志：true 表示采集循环继续运行。
    std::thread captureThread;                  // 执行 captureLoop() 的后台线程
    bool streaming = false;                     // 标记摄像头驱动的视频流是否已启动
    int v4l2_width = 640, v4l2_height = 480;    // 请求的图像尺寸；打开设备后会更新为驱动实际接受的尺寸。
    QImage lastFrame;                           // 保存最新解码完成的图像
    QMutex frameMutex;                          // 保护 lastFrame，避免多线程同时读写

};

#endif 
