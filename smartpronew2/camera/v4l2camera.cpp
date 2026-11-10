#include "v4l2camera.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <QImage>
#include <QDebug>
#include <cstring>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <QByteArray>
#include <QMutexLocker>
#include <string.h>

extern "C" {
    extern unsigned char* g_last_jpeg;
    extern size_t g_last_jpeg_size;
    extern pthread_mutex_t g_jpeg_mutex;
}

V4L2Camera::V4L2Camera(const QString& devPath): QObject(nullptr), devicePath(devPath)
{
}

V4L2Camera::~V4L2Camera() {
    close();
}

bool V4L2Camera::open() {
    close();    // 如果之前打开过摄像头，先停止旧线程并释放旧资源。
    // 打开摄像头设备，例如 /dev/video0。
    // O_NONBLOCK 表示非阻塞打开，后续配合 poll() 等待画面数据。
    v4l2_fd = ::open(devicePath.toStdString().c_str(), O_RDWR | O_NONBLOCK);
    if (v4l2_fd < 0) {
        qDebug() << "[V4L2Camera] 打开摄像头失败";
        return false;
    }
    // 告诉摄像头：希望用多大的画面、什么图像格式采集。
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));                   // 先清零，避免结构体里有未初始化的数据
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;         // 普通摄像头采集
    fmt.fmt.pix.width = v4l2_width;                 // 期望画面宽度
    fmt.fmt.pix.height = v4l2_height;               // 期望画面高度
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;   // 使用压缩的 MJPEG 格式
    fmt.fmt.pix.field = V4L2_FIELD_NONE;            // 用来告诉摄像头驱动：图像是逐行扫描摄像头采集一帧时，直接得到完整画面。
    // 把图像格式要求提交给驱动。
    if (ioctl(v4l2_fd, VIDIOC_S_FMT, &fmt) < 0) {
        qDebug() << "[V4L2Camera] 设置格式失败";
        ::close(v4l2_fd);
        v4l2_fd = -1;
        return false;
    }
    // 驱动可能无法完全满足期望值，所以保存它实际接受的宽高。例如程序请求 640×480，驱动实际只能提供 640×360，那这里保存的就是 640 和 360，后续代码就按实际画面尺寸处理。
    v4l2_width = fmt.fmt.pix.width;
    v4l2_height = fmt.fmt.pix.height;
    // 向驱动申请 4 个采集缓冲区，让摄像头可以轮换使用它们。
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 4;                              // 缓冲区期望数量
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;     // 采集缓冲区类型
    req.memory = V4L2_MEMORY_MMAP;              // 使用 mmap 映射方式访问
    if (ioctl(v4l2_fd, VIDIOC_REQBUFS, &req) < 0) {
        qDebug() << "[V4L2Camera] 请求缓冲区失败";
        ::close(v4l2_fd);
        v4l2_fd = -1;
        return false;
    }
    if (req.count < 2) {    // req.count 是驱动实际分配的数量，不一定正好是 4。
        qDebug() << "[V4L2Camera] 驱动分配的采集缓冲区不足";
        close();
        return false;
    }
    // 逐个查询、映射并入队驱动分配的缓冲区。
    captureBuffers.reserve(req.count);
    for (unsigned int i = 0; i < req.count; ++i) {
        struct v4l2_buffer buf;                 // 创建缓冲区信息结构体
        memset(&buf, 0, sizeof(buf));           
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // 说明这是摄像头采集用的缓冲区
        buf.memory = V4L2_MEMORY_MMAP;          // 说明缓冲区通过 mmap 方式访问
        buf.index = i;         

        // 查询第 i 块缓冲区的信息。
        // 驱动会把缓冲区长度 buf.length 和映射偏移量 buf.m.offset 填回 buf。
        if (ioctl(v4l2_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            qDebug() << "[V4L2Camera] 查询缓冲区失败:" << i;
            close();
            return false;
        }
        // 把驱动缓冲区映射到当前进程，之后程序可以通过 mapped 访问它。
        void* mapped = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, v4l2_fd, buf.m.offset);
        if (mapped == MAP_FAILED) {
            qDebug() << "[V4L2Camera] mmap失败:" << i;
            close();
            return false;
        }

        // 保存映射地址和缓冲区长度，后续取帧时根据编号找到对应缓冲区。
        captureBuffers.push_back({mapped, buf.length});
        // 把空缓冲区交给驱动，允许摄像头把新画面写进去。
        if (ioctl(v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
            qDebug() << "[V4L2Camera] 缓冲区入队失败:" << i;
            close();
            return false;
        }
    }

    // 指定要启动的是摄像头采集流。
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    // 通知驱动：缓冲区已经准备好，可以开始采集画面。
    if (ioctl(v4l2_fd, VIDIOC_STREAMON, &type) < 0) {
        qDebug() << "[V4L2Camera] 启动采集失败";
        close();
        return false;
    }
    streaming = true;               // 记录摄像头流已经启动。
    captureRunning.store(true);     // 通知后台采集循环继续运行。
    try {
        // 创建一个后台线程，在线程里持续执行 captureLoop() 取摄像头画面。
        // this 表示 captureLoop() 属于当前这个 V4L2Camera 对象。
        captureThread = std::thread(&V4L2Camera::captureLoop, this);
    } catch (...) {
        qDebug() << "[V4L2Camera] 无法创建采集线程";
        close();        // 线程创建失败，停止采集并清理资源
        return false;
    }
    qDebug() << "[V4L2Camera] 摄像头打开成功";
    return true;        // 摄像头已启动，后台采集线程也已创建
}

// 功能：停止采集并释放摄像头线程、映射缓冲区和设备句柄。
void V4L2Camera::close()
{
    captureRunning.store(false);
    if (captureThread.joinable())
        captureThread.join();
    if (v4l2_fd >= 0 && streaming) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(v4l2_fd, VIDIOC_STREAMOFF, &type);
        streaming = false;
    }
    for (const CaptureBuffer& buffer : captureBuffers) {
        if (buffer.start && buffer.length > 0)
            munmap(buffer.start, buffer.length);
    }
    captureBuffers.clear();
    if (v4l2_fd >= 0) {
        ::close(v4l2_fd);
        v4l2_fd = -1;
    }
    QMutexLocker locker(&frameMutex);
    lastFrame = QImage();
}

// 功能：返回摄像头当前是否处于打开状态。
bool V4L2Camera::isOpen() const 
{    
    return v4l2_fd >= 0;
}


// 后台取帧和处理帧的线程
// 功能：后台循环取出摄像头帧、归还缓冲区并更新最新画面。
void V4L2Camera::captureLoop() 
{
    // 准备 poll 等待信息：监听摄像头设备是否有可读取的数据。
    struct pollfd cameraPoll;
    cameraPoll.fd = v4l2_fd;            // 要监听的摄像头设备
    cameraPoll.events = POLLIN;         // 等待“有数据可读”事件

    // 只要运行标志为 true，就持续取帧。
    while (captureRunning.load()) 
    {
        cameraPoll.revents = 0;     // 清空上一次 poll 返回的事件

        // 等待摄像头数据，最多等待 200 毫秒。
        // 有超时上限，循环就能定期检查 captureRunning 是否变成 false。
        int ready = poll(&cameraPoll, 1, 200);
        if (ready < 0) {
            // EINTR 表示等待期间被信号打断，通常可以重新等待。
            if (errno == EINTR)
                continue;

            // 其他错误无法继续取帧，记录错误后离开循环。
            qWarning() << "[V4L2Camera] 等待摄像头数据失败:" << strerror(errno);
            break;
        }
        if (ready == 0) // ready == 0 表示等了 200 毫秒仍没有新帧，回到循环检查运行标志。
            continue;


        // 准备接收驱动返回的缓冲区信息。
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;     // 摄像头采集缓冲区
        buf.memory = V4L2_MEMORY_MMAP;              // 使用 mmap 映射方式

        // 从驱动队列取出一块已经采集完成的缓冲区。
        // 成功后 buf.index 表示是哪一块，buf.bytesused 表示其中有多少图像数据。
        if (ioctl(v4l2_fd, VIDIOC_DQBUF, &buf) < 0)
        {   
            // EAGAIN 表示此刻还没有可取的帧；EINTR 表示操作被信号打断。
            // 这两种情况都回到循环重试。
            if (errno == EAGAIN || errno == EINTR)
                continue;
            qWarning() << "[V4L2Camera] 取出采集帧失败:" << strerror(errno);
            break;
        }

        // 检查驱动返回的编号是否对应已保存的缓冲区。
        if (buf.index >= captureBuffers.size()) {
            qWarning() << "[V4L2Camera] 驱动返回了无效缓冲区编号";
            break;
        }
        const CaptureBuffer& buffer = captureBuffers[buf.index];    // 根据编号找到这块缓冲区在程序中的映射地址和长度。

        QByteArray jpeg;                     // 准备一份 JPEG 数据副本。
        if (buf.bytesused <= buffer.length)  // 只有实际数据长度没有超过缓冲区容量时，才复制图像数据。
            jpeg = QByteArray(static_cast<const char*>(buffer.start),
                              static_cast<int>(buf.bytesused));

        // 尽快把缓冲区交还驱动，让摄像头可以继续把新画面写进来。
        // 后面的 JPEG 解码使用 jpeg 副本，不再占用摄像头缓冲区。
        if (ioctl(v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
            qWarning() << "[V4L2Camera] 归还采集缓冲区失败:" << strerror(errno);
            break;
        }

        // 没有得到有效图像数据就跳过本轮。
        if (jpeg.isEmpty())
            continue;

        // 将 JPEG 压缩数据解码成 QImage。
        QImage img;
        if (img.loadFromData(jpeg, "JPEG")) 
        {
            QMutexLocker locker(&frameMutex);   // 加锁后更新共享的最新画面，避免其他线程同时读写 lastFrame。
            lastFrame = img;
            // locker 离开作用域时会自动解锁。
        }
    }

    // 循环结束后标记采集线程已停止运行。
    captureRunning.store(false);
}

// 功能：取得当前最新的一帧画面供界面或网络模块使用。
QImage V4L2Camera::getFrame() 
{    
    QMutexLocker locker(&frameMutex);   
    return lastFrame;
}

// 功能：返回摄像头当前画面的宽度。
int V4L2Camera::width() const { return v4l2_width; }

// 功能：返回摄像头当前画面的高度。
int V4L2Camera::height() const { return v4l2_height; }
