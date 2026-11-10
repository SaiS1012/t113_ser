#include "HailinFingerprint.h"
#include <QDebug>
#include <QThread>

HailinFingerprint::HailinFingerprint(const QString& portName, QObject* parent)
    : QObject(parent), m_portName(portName)
{
    m_serial.setPortName(m_portName);
    m_serial.setBaudRate(QSerialPort::Baud57600);
    m_serial.setDataBits(QSerialPort::Data8);
    m_serial.setParity(QSerialPort::NoParity);
    m_serial.setStopBits(QSerialPort::OneStop);
    m_serial.setFlowControl(QSerialPort::NoFlowControl);
}

HailinFingerprint::~HailinFingerprint() {
    close();
}

// 功能：打开指纹模块串口并准备后续通信。
bool HailinFingerprint::open() {
    QMutexLocker locker(&m_mutex);
    if (m_serial.isOpen()) return true;
    if (!m_serial.open(QIODevice::ReadWrite)) {
        debug(QString("[指纹] 打开串口失败: %1").arg(m_serial.errorString()));
        return false;
    }
    debug(QString("[指纹] 串口打开成功: %1").arg(m_portName));
    return true;
}

// 功能：关闭指纹模块串口并释放相关资源。
void HailinFingerprint::close() {
    QMutexLocker locker(&m_mutex);
    if (m_serial.isOpen()) {
        m_serial.close();
        debug("[指纹] 串口已关闭");
    }
}

// 功能：检查指纹模块串口是否已经打开。
bool HailinFingerprint::isOpen() const {
    return m_serial.isOpen();
}

// 功能：向指纹模块发送自动识别命令并等待识别结果。
bool HailinFingerprint::autoIdentify(quint8 scoreLevel, int timeoutMs) {
    QMutexLocker locker(&m_mutex);
    if (!m_serial.isOpen()) {
        debug("[指纹] 串口未打开");
        return false;
    }
    QByteArray payload;
    payload.append(0x32); 
    payload.append(scoreLevel); 
    payload.append((char)0xFF); 
    payload.append((char)0xFF); 
    payload.append((char)0x00); 
    QByteArray pkt = buildPacket(payload);
    if (!sendPacket(pkt)) {
        debug("[指纹] 自动识别指令发送失败");
        return false;
    }
    QByteArray resp = readPacket(timeoutMs);
    if (resp.isEmpty()) {
        debug("[指纹] 自动识别无应答");
        emit identifyResult(false, -1, 0, "无应答");
        return false;
    }
    if ((quint8)resp[6] != PKT_ACK) {
        debug("[指纹] 应答包标识错误");
        emit identifyResult(false, -1, 0, "应答包标识错误");
        return false;
    }
    quint8 confirm = (quint8)resp[9];
    quint8 param = (quint8)resp[10];
    quint16 id = ((quint8)resp[11] << 8) | (quint8)resp[12];
    quint16 score = ((quint8)resp[13] << 8) | (quint8)resp[14];
    QString msg = QString("确认码=%1, param=%2, id=%3, score=%4").arg(confirm,2,16,QChar('0')).arg(param,2,16,QChar('0')).arg(id).arg(score);
    debug("[指纹] 自动识别应答: " + msg);
    if (confirm == 0x00 && param == 0x05) {
        emit identifyResult(true, id, score, "识别成功");
        return true;
    } else {
        emit identifyResult(false, -1, 0, msg);
        return false;
    }
}

enum { ENROLL_PARAM_DEFAULT = 0x00 };

// 功能：向指纹模块发送自动注册命令并等待注册结果。
bool HailinFingerprint::autoEnroll(quint16 id, quint8 enrollTimes, quint8 param, int timeoutMs) {
    QMutexLocker locker(&m_mutex);
    if (!m_serial.isOpen()) {
        debug("[指纹] 串口未打开");
        return false;
    }
    QByteArray payload;
    payload.append(0x31); 
    payload.append((char)(id >> 8)); 
    payload.append((char)(id & 0xFF)); 
    payload.append(enrollTimes); 
    payload.append(param); 
    QByteArray pkt = buildPacket(payload);
    if (!sendPacket(pkt)) {
        debug("[指纹] 自动注册指令发送失败");
        return false;
    }
    int elapsed = 0;
    int step = 0;
    while (elapsed < timeoutMs) {
        QByteArray resp = readPacket(2000);
        if (resp.isEmpty()) {
            elapsed += 2000;
            continue;
        }
        if ((quint8)resp[6] != PKT_ACK) {
            debug("[指纹] 注册应答包标识错误");
            emit enrollResult(false, id, "应答包标识错误");
            return false;
        }
        quint8 confirm = (quint8)resp[9];
        quint8 param1 = (quint8)resp[10];
        quint8 param2 = (quint8)resp[11];
        QString msg = QString("step=%1, 确认码=%2, param1=%3, param2=%4").arg(step++).arg(confirm,2,16,QChar('0')).arg(param1,2,16,QChar('0')).arg(param2,2,16,QChar('0'));
        debug("[指纹] 自动注册应答: " + msg);
        if (confirm == 0x00 && param1 == 0x06) { 
            emit enrollResult(true, id, "注册成功");
            return true;
        } else if (confirm == 0x01 || confirm == 0x0A || confirm == 0x0B || confirm == 0x1F || confirm == 0x22 || confirm == 0x25 || confirm == 0x26 || confirm == 0x27) {
            emit enrollResult(false, id, msg);
            return false;
        }
        elapsed += 2000;
    }
    emit enrollResult(false, id, "注册超时");
    return false;
}

// 功能：向指纹模块发送取消当前操作的命令。
bool HailinFingerprint::cancel() {
    QMutexLocker locker(&m_mutex);
    if (!m_serial.isOpen()) return false;
    QByteArray payload;
    payload.append(0x30); 
    QByteArray pkt = buildPacket(payload);
    if (!sendPacket(pkt)) {
        debug("[指纹] 取消指令发送失败");
        return false;
    }
    QByteArray resp = readPacket(1000);
    if (resp.isEmpty()) {
        debug("[指纹] 取消无应答");
        return false;
    }
    quint8 confirm = (quint8)resp[9];
    debug(QString("[指纹] 取消应答: 确认码=%1").arg(confirm,2,16,QChar('0')));
    return confirm == 0x00;
}

// 功能：按模块通信协议组装待发送的数据包。
QByteArray HailinFingerprint::buildPacket(const QByteArray& payload) {
    QByteArray pkt;
    pkt.append((char)0xEF);
    pkt.append((char)0x01);
    pkt.append((char)0xFF);
    pkt.append((char)0xFF);
    pkt.append((char)0xFF);
    pkt.append((char)0xFF);
    pkt.append(PKT_CMD); 
    quint16 pktLen = payload.size() + 2; 
    pkt.append((char)(pktLen >> 8));
    pkt.append((char)(pktLen & 0xFF));
    pkt.append(payload);
    quint16 sum = 0;
    for (int i = 6; i < pkt.size(); ++i) sum += (quint8)pkt[i];
    pkt.append((char)(sum >> 8));
    pkt.append((char)(sum & 0xFF));
    return pkt;
}

// 功能：把完整协议数据包写入指纹模块串口。
bool HailinFingerprint::sendPacket(const QByteArray& pkt) {
    qint64 written = m_serial.write(pkt);
    if (written != pkt.size()) {
        debug("[指纹] 串口写入不完整");
        return false;
    }
    if (!m_serial.waitForBytesWritten(200)) {
        debug("[指纹] 串口写入超时");
        return false;
    }
    return true;
}

// 功能：在指定时间内从串口读取并解析一个数据包。
QByteArray HailinFingerprint::readPacket(int timeoutMs) {
    QByteArray buf;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (m_serial.waitForReadyRead(100)) {
            buf += m_serial.readAll();
            if (buf.size() >= 12) {
                quint16 pktLen = ((quint8)buf[7] << 8) | (quint8)buf[8];
                int totalLen = 9 + pktLen;
                if (buf.size() >= totalLen) {
                    QByteArray pkt = buf.left(totalLen);
                    return pkt;
                }
            }
        }
    }
    return QByteArray();
}

// 功能：计算协议数据包的校验值。
quint16 HailinFingerprint::calcChecksum(const QByteArray& pkt) {
    quint16 sum = 0;
    for (int i = 6; i < pkt.size(); ++i) sum += (quint8)pkt[i];
    return sum;
}

// 功能：输出并转发指纹模块的调试信息。
void HailinFingerprint::debug(const QString& msg) {
    qDebug() << msg;
    emit debugInfo(msg);
}

// 功能：执行需要用户分步按压手指的注册流程。
void HailinFingerprint::stepRegister(quint16 id, int collectTimes) {
    QMutexLocker locker(&m_mutex);
    if (!m_serial.isOpen()) {
        emit stepRegisterStatus("串口未打开", 0, false);
        return;
    }
    int step = 0;
    int bufferId = 1;
    for (; bufferId <= collectTimes; ++bufferId) {
        QByteArray payload;
        payload.append((char)0x01); 
        QByteArray pkt = buildPacket(payload);
        sendPacket(pkt);
        QByteArray resp = readPacket(2000);
        if (resp.size() < 12 || (quint8)resp[9] != 0x00) {
            emit stepRegisterStatus(QString("第%1次采集图像失败").arg(bufferId), step++, false);
            return;
        }
        emit stepRegisterStatus(QString("第%1次采集图像成功").arg(bufferId), step++, true);
        payload.clear();
        payload.append((char)0x02); 
        payload.append((char)bufferId); 
        pkt = buildPacket(payload);
        sendPacket(pkt);
        resp = readPacket(2000);
        if (resp.size() < 12 || (quint8)resp[9] != 0x00) {
            emit stepRegisterStatus(QString("第%1次生成特征失败").arg(bufferId), step++, false);
            return;
        }
        emit stepRegisterStatus(QString("第%1次生成特征成功").arg(bufferId), step++, true);
    }
    QByteArray payload;
    payload.append((char)0x05); 
    QByteArray pkt = buildPacket(payload);
    sendPacket(pkt);
    QByteArray resp = readPacket(2000);
    if (resp.size() < 12 || (quint8)resp[9] != 0x00) {
        emit stepRegisterStatus("合并特征失败", step++, false);
        return;
    }
    emit stepRegisterStatus("合并特征成功", step++, true);
    payload.clear();
    payload.append((char)0x06); 
    payload.append((char)1); 
    payload.append((char)(id >> 8));
    payload.append((char)(id & 0xFF));
    pkt = buildPacket(payload);
    sendPacket(pkt);
    resp = readPacket(2000);
    if (resp.size() < 12 || (quint8)resp[9] != 0x00) {
        emit stepRegisterStatus("存储模板失败", step++, false);
        return;
    }
    emit stepRegisterStatus("存储模板成功", step++, true);
}

// 功能：执行指纹识别并上报识别状态。
void HailinFingerprint::stepIdentify(int startPage, int pageNum) {
    QMutexLocker locker(&m_mutex);
    if (!m_serial.isOpen()) {
        emit stepIdentifyStatus("串口未打开", false, -1, 0);
        return;
    }
    QByteArray payload;
    payload.append((char)0x01); 
    QByteArray pkt = buildPacket(payload);
    sendPacket(pkt);
    QByteArray resp = readPacket(2000);
    if (resp.size() < 12 || (quint8)resp[9] != 0x00) {
        emit stepIdentifyStatus("采集图像失败", false, -1, 0);
        return;
    }
    emit stepIdentifyStatus("采集图像成功", true, -1, 0);
    payload.clear();
    payload.append((char)0x02); 
    payload.append((char)1); 
    pkt = buildPacket(payload);
    sendPacket(pkt);
    resp = readPacket(2000);
    if (resp.size() < 12 || (quint8)resp[9] != 0x00) {
        emit stepIdentifyStatus("生成特征失败", false, -1, 0);
        return;
    }
    emit stepIdentifyStatus("生成特征成功", true, -1, 0);
    payload.clear();
    payload.append((char)0x04); 
    payload.append((char)1); 
    payload.append((char)(startPage >> 8));
    payload.append((char)(startPage & 0xFF));
    payload.append((char)(pageNum >> 8));
    payload.append((char)(pageNum & 0xFF));
    pkt = buildPacket(payload);
    sendPacket(pkt);
    resp = readPacket(2000);
    if (resp.size() < 16) {
        emit stepIdentifyStatus("搜索指纹失败", false, -1, 0);
        return;
    }
    quint8 confirm = (quint8)resp[9];
    quint16 id = ((quint8)resp[11] << 8) | (quint8)resp[12];
    quint16 score = ((quint8)resp[13] << 8) | (quint8)resp[14];
    if (confirm == 0x00) {
        emit stepIdentifyStatus("识别成功", true, id, score);
    } else {
        emit stepIdentifyStatus(QString("识别失败, 确认码=%1").arg(confirm,2,16,QChar('0')), false, -1, 0);
    }
}
