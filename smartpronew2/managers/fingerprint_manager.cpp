#include "fingerprint_manager.h"
#include "HailinFingerprint.h"
#include <QTimer>
#include <QDateTime>
#include <QDebug>

FingerprintManager::FingerprintManager(QObject* parent)
    : QObject(parent)
{
}

FingerprintManager::~FingerprintManager() {}

// 功能：设置管理器要操作的指纹模块对象。
void FingerprintManager::setDevice(HailinFingerprint* device)
{
    fingerprint = device;
}

// 功能：启动指纹识别并将结果通过信号通知界面。
void FingerprintManager::startIdentify()
{
    if (!fingerprint) return;
    static bool waitingFinger = false;
    if (waitingFinger) return;
    waitingFinger = true;
    emit identifyResult(false, -1, 0, "请按下指纹进行识别"); 
    QTimer* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [=]() mutable {
        QByteArray payload;
        payload.append((char)0x01); 
        QByteArray pkt = fingerprint->buildPacket(payload);
        fingerprint->sendPacket(pkt);
        QByteArray resp = fingerprint->readPacket(500);
        if (resp.size() >= 12 && (quint8)resp[9] == 0x00) {
            timer->stop();
            timer->deleteLater();
            QByteArray payload2;
            payload2.append((char)0x02); 
            payload2.append((char)1);    
            QByteArray pkt2 = fingerprint->buildPacket(payload2);
            fingerprint->sendPacket(pkt2);
            QByteArray resp2 = fingerprint->readPacket(2000);
            if (resp2.size() < 12 || (quint8)resp2[9] != 0x00) {
                emit identifyResult(false, -1, 0, "生成特征失败");
                waitingFinger = false;
                return;
            }
            QByteArray payload3;
            payload3.append((char)0x04); 
            payload3.append((char)1);    
            payload3.append((char)0x00); 
            payload3.append((char)0x01); 
            payload3.append((char)0x00); 
            payload3.append((char)0x01); 
            QByteArray pkt3 = fingerprint->buildPacket(payload3);
            fingerprint->sendPacket(pkt3);
            QByteArray resp3 = fingerprint->readPacket(2000);
            if (resp3.size() >= 16 && (quint8)resp3[9] == 0x00) {
                int id = ((quint8)resp3[11] << 8) | (quint8)resp3[12];
                int score = ((quint8)resp3[13] << 8) | (quint8)resp3[14];
                qDebug() << "指纹识别成功";
                emit identifyResult(true, id, score, "指纹识别成功");
            } else {
                qDebug() << "指纹识别失败";
                emit identifyResult(false, -1, 0, "指纹识别失败");
            }
            waitingFinger = false;
        }
    });
    timer->start(500);
}

// 功能：启动指定编号的指纹注册流程。
void FingerprintManager::startEnroll(int id)
{
    if (!fingerprint) return;
    static bool enrolling = false;
    if (enrolling) return;
    enrolling = true;
    int* currentStep = new int(1); 
    emit enrollResult(false, id, "请按下手指进行第1次采集");
    QTimer* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [=]() mutable {
        QByteArray payload;
        payload.append((char)0x01); 
        QByteArray pkt = fingerprint->buildPacket(payload);
        fingerprint->sendPacket(pkt);
        QByteArray resp = fingerprint->readPacket(500);
        if (resp.size() >= 12 && (quint8)resp[9] == 0x00) {
            QByteArray payload2;
            payload2.append((char)0x02); 
            payload2.append((char)(*currentStep)); 
            QByteArray pkt2 = fingerprint->buildPacket(payload2);
            fingerprint->sendPacket(pkt2);
            QByteArray resp2 = fingerprint->readPacket(2000);
            if (resp2.size() < 12 || (quint8)resp2[9] != 0x00) {
                emit enrollResult(false, id, QString("第%1次生成特征失败").arg(*currentStep));
                enrolling = false;
                timer->stop();
                timer->deleteLater();
                delete currentStep;
                return;
            }
            emit enrollResult(true, id, QString("第%1次采集成功").arg(*currentStep));
            (*currentStep)++;
            if (*currentStep > 3) {
                QByteArray payload3;
                payload3.append((char)0x05); 
                QByteArray pkt3 = fingerprint->buildPacket(payload3);
                fingerprint->sendPacket(pkt3);
                QByteArray resp3 = fingerprint->readPacket(2000);
                if (resp3.size() < 12 || (quint8)resp3[9] != 0x00) {
                    emit enrollResult(false, id, "合并特征失败");
                    enrolling = false;
                    timer->stop();
                    timer->deleteLater();
                    delete currentStep;
                    return;
                }
                QByteArray payload4;
                payload4.append((char)0x06); 
                payload4.append((char)1); 
                payload4.append((char)(id >> 8));
                payload4.append((char)(id & 0xFF));
                QByteArray pkt4 = fingerprint->buildPacket(payload4);
                fingerprint->sendPacket(pkt4);
                QByteArray resp4 = fingerprint->readPacket(2000);
                if (resp4.size() < 12 || (quint8)resp4[9] != 0x00) {
                    emit enrollResult(false, id, "存储模板失败");
                } else {
                    emit enrollResult(true, id, "注册成功");
                }
                enrolling = false;
                timer->stop();
                timer->deleteLater();
                delete currentStep;
                return;
            } else {
                emit enrollResult(false, id, QString("请抬手后再按下，进行第%1次采集").arg(*currentStep));
            }
        }
    });
    timer->start(500);
}
