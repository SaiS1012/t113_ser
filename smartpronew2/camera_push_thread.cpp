#include "camera_push_thread.h"
#include <QDebug>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <cstring>
#include <QBuffer>
#include <QImage>

CameraPushThread::CameraPushThread(V4L2Camera* cam, const QString& ip, quint16 port, QObject* parent)
    : QThread(parent), camera(cam), serverIp(ip), serverPort(port), running(false)
{}

// 功能：通知视频推送线程停止运行。
void CameraPushThread::stop() {
    running = false;
}

// 功能：循环获取摄像头画面并推送到远程服务端。
void CameraPushThread::run() {
    qDebug() << "[视频推送] 线程启动 -> 目标服务器:" << serverIp << ":" << serverPort;
    running = true;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        qWarning() << "[视频推送] 创建UDP socket失败";
        return;
    }
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(serverPort);
    inet_pton(AF_INET, serverIp.toStdString().c_str(), &serv_addr.sin_addr);
    qDebug() << "[视频推送] UDP连接已建立，开始推送视频流...";
    int frame_count = 0;
    int success_count = 0;
    while (running) {
        QImage frame = camera ? camera->getFrame() : QImage();
        if (!frame.isNull()) {
            QByteArray jpegData;
            QBuffer buffer(&jpegData);
            buffer.open(QIODevice::WriteOnly);
            frame.save(&buffer, "JPEG", 75); 
            int ret = sendto(sock, jpegData.data(), jpegData.size(), 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
            if (ret < 0) {
                qWarning() << "[视频推送] 发送失败:" << strerror(errno);
            } else {
                success_count++;
                frame_count++;
                if (frame_count % 100 == 0) {
                    qDebug() << "[视频推送] 已成功推送" << success_count << "帧，当前帧大小:" << jpegData.size() << "字节";
                }
            }
        } else {
            static int no_data_count = 0;
            if (no_data_count < 5) {
                qDebug() << "[视频推送] 等待视频数据...";
                no_data_count++;
            }
        }
        for (int i = 0; i < 8 && running; ++i) {
            usleep(5000); 
        }
    }
    qDebug() << "[视频推送] 线程结束，共推送" << success_count << "帧";
    close(sock);
} 
