
#include "camera_udp_server.h"
#include "widget.h"
#include <QBuffer>
#include <QImage>
#include <QHostAddress>

CameraUdpServer::CameraUdpServer(V4L2Camera* camera, quint16 port, QObject* parent)
    : QObject(parent), camera(camera)
{
    udpSocket = new QUdpSocket(this);
    qDebug() << "[CameraUdpServer调试] 开始初始化UDP socket，端口:" << port;
    if (udpSocket->bind(QHostAddress(SELF_IP), port)) {
        qDebug() << "[CameraUdpServer调试] ✓ UDP socket绑定成功，端口:" << port;
        qDebug() << "[CameraUdpServer调试] UDP socket状态:" << udpSocket->state();
    } else {
        qDebug() << "[CameraUdpServer调试] ✗ UDP socket绑定失败:" << udpSocket->errorString();
        qDebug() << "[CameraUdpServer调试] UDP socket错误代码:" << udpSocket->error();
    }
    connect(udpSocket, &QUdpSocket::readyRead, this, &CameraUdpServer::onReadyRead);
    sendTimer = new QTimer(this);
    sendTimer->setInterval(50); 
    connect(sendTimer, &QTimer::timeout, this, &CameraUdpServer::sendFrame);
    qDebug() << "[CameraUdpServer调试] CameraUdpServer初始化完成";
}

// 功能：处理远程客户端发来的 UDP 请求。
void CameraUdpServer::onReadyRead()
{
    qDebug() << "[CameraUdpServer调试] ========== onReadyRead 被调用 ==========";
    qDebug() << "[CameraUdpServer调试] 待处理数据包数量:" << udpSocket->hasPendingDatagrams();
    while (udpSocket->hasPendingDatagrams()) {
        QHostAddress sender;
        quint16 senderPort;
        QByteArray datagram;
        datagram.resize(udpSocket->pendingDatagramSize());
        qint64 bytesRead = udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        QString msg = QString::fromUtf8(datagram).trimmed();
        if (msg == "getframe") {
            lastClientAddr = sender;
            lastClientPort = senderPort;
            if (!sending) {
                sending = true;
                sendTimer->start();
            } else {
            }
        } else if (msg == "stop") {
            qDebug() << "[CameraUdpServer调试] ✓ 收到stop请求，停止发送视频帧";
            sending = false;
            sendTimer->stop();
        } else {
        }
    }
}

// 功能：获取当前摄像头画面并通过 UDP 发送给客户端。
void CameraUdpServer::sendFrame()
{
    if (!sending || lastClientAddr.isNull() || lastClientPort == 0) {
        qDebug() << "[CameraUdpServer调试] sendFrame被调用但条件不满足，跳过发送";
        qDebug() << "[CameraUdpServer调试] sending:" << sending << ", lastClientAddr:" << lastClientAddr.toString() << ", lastClientPort:" << lastClientPort;
        return;
    }
    QImage frame = camera->getFrame();
    if (!frame.isNull()) {
        QByteArray jpegData;
        QBuffer buffer(&jpegData);
        buffer.open(QIODevice::WriteOnly);
        frame.save(&buffer, "JPEG");
        qint64 sent = udpSocket->writeDatagram(jpegData, lastClientAddr, lastClientPort);
        if (sent != jpegData.size()) {
        } else {
        }
    } else {
    }
}
