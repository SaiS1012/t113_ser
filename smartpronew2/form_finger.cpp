#include "form_finger.h"
#include "ui_form_finger.h"
#include <QTimer>
#include "widget.h"

Form_finger::Form_finger(QWidget *parent) :

    QWidget(parent),
    ui(new Ui::Form_finger),
    currentStep(0),
    waitingFinger(false)
{
    ui->setupUi(this);
    connect(ui->addface, &QPushButton::clicked, this, &Form_finger::addFaceRequested);
    connect(ui->addfinger, &QPushButton::clicked, this, &Form_finger::addFingerRequested);
    connect(ui->addquit, &QPushButton::clicked, this, &Form_finger::onAddQuitClicked);
    QList<QPushButton*> btns = {
        ui->addface, ui->addfinger, ui->addquit
    };
    for (QPushButton* btn : btns) {
        btn->setFocusPolicy(Qt::NoFocus);
    }
}

// 功能：处理指纹添加界面的退出操作。
void Form_finger::onAddQuitClicked()
{
    this->close();
}

// 功能：通知主界面开始指纹注册流程。
void Form_finger::addFingerRequested()
{
    if (waitingFinger) return; 
    currentStep = 1;
    startFingerRegisterStep(currentStep);
}

// 功能：启动指定步骤的指纹采集流程。
void Form_finger::startFingerRegisterStep(int step)
{
    QWidget* w = parentWidget();
    while (w && !qobject_cast<Widget*>(w)) w = w->parentWidget();
    Widget* mainWidget = qobject_cast<Widget*>(w);
    if (!mainWidget || !mainWidget->getFingerprint()) return;
    waitingFinger = true;
    if (step == 1) mainWidget->audioManager->playAsync("/tmp/pcm/caijifinger.pcm");
    else if (step == 2) mainWidget->audioManager->playAsync("/tmp/pcm/caijifinger.pcm");
    else if (step == 3) mainWidget->audioManager->playAsync("/tmp/pcm/caijifinger.pcm");
    QTimer* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [=]() {
        QByteArray payload;
        payload.append((char)0x01); 
        QByteArray pkt = mainWidget->getFingerprint()->buildPacket(payload);
        mainWidget->getFingerprint()->sendPacket(pkt);
        QByteArray resp = mainWidget->getFingerprint()->readPacket(500);
        if (resp.size() >= 12 && (quint8)resp[9] == 0x00) {
            timer->stop();
            timer->deleteLater();
            qDebug() << QString("第%1次检测到手指，开始采集").arg(step);
            doFingerCollect(step);
        } else {
        }
    });
    timer->start(500);
}

// 功能：根据当前注册步骤采集并提交指纹。
void Form_finger::doFingerCollect(int step)
{
    QWidget* w = parentWidget();
    while (w && !qobject_cast<Widget*>(w)) w = w->parentWidget();
    Widget* mainWidget = qobject_cast<Widget*>(w);
    if (!mainWidget || !mainWidget->getFingerprint()) return;
    int id = mainWidget->getNextFingerId();
    QByteArray payload;
    payload.append((char)0x01); 
    QByteArray pkt = mainWidget->getFingerprint()->buildPacket(payload);
    mainWidget->getFingerprint()->sendPacket(pkt);
    QByteArray resp = mainWidget->getFingerprint()->readPacket(2000);
    if (resp.size() < 12 || (quint8)resp[9] != 0x00) {
        qDebug() << QString("第%1次采集图像失败").arg(step);
        waitingFinger = false;
        return;
    }
    payload.clear();
    payload.append((char)0x02); 
    payload.append((char)step); 
    pkt = mainWidget->getFingerprint()->buildPacket(payload);
    mainWidget->getFingerprint()->sendPacket(pkt);
    resp = mainWidget->getFingerprint()->readPacket(2000);
    if (resp.size() < 12 || (quint8)resp[9] != 0x00) {
        qDebug() << QString("第%1次生成特征失败").arg(step);
        waitingFinger = false;
        return;
    }
    qDebug() << QString("第%1次采集成功").arg(step);
    if (step == 1) mainWidget->audioManager->playAsync("/tmp/pcm/fingerone.pcm");
    else if (step == 2) mainWidget->audioManager->playAsync("/tmp/pcm/fingertwo.pcm");
    else if (step == 3) mainWidget->audioManager->playAsync("/tmp/pcm/fingerthree.pcm");
    if (step < 3) {
        currentStep++;
        QTimer::singleShot(1500, this, [=]() { startFingerRegisterStep(currentStep); });
    } else {
        payload.clear();
        payload.append((char)0x05); 
        pkt = mainWidget->getFingerprint()->buildPacket(payload);
        mainWidget->getFingerprint()->sendPacket(pkt);
        resp = mainWidget->getFingerprint()->readPacket(2000);
        if (resp.size() < 12 || (quint8)resp[9] != 0x00) {
            qDebug() << "合并特征失败";
            waitingFinger = false;
            return;
        }
        payload.clear();
        payload.append((char)0x06); 
        payload.append((char)1); 
        payload.append((char)(id >> 8));
        payload.append((char)(id & 0xFF));
        pkt = mainWidget->getFingerprint()->buildPacket(payload);
        mainWidget->getFingerprint()->sendPacket(pkt);
        resp = mainWidget->getFingerprint()->readPacket(2000);
        if (resp.size() < 12 || (quint8)resp[9] != 0x00) {
            qDebug() << "存储模板失败";
            waitingFinger = false;
            return;
        }
        qDebug() << "注册成功";
        mainWidget->incrementNextFingerId();
        waitingFinger = false;
    }
}

Form_finger::~Form_finger()
{
    delete ui;
}
