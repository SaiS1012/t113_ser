#include "widget.h"
#include "ui_widget.h"
#include <QDebug>
#include <QMessageBox>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include "audiosenderthread.h"
#include "audioreceiverthread.h"
#include <QTimer>
#include <QImage>
#include <QPixmap>
#include <QFile>
#include <pthread.h>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QElapsedTimer>
#include <opencv2/opencv.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/face.hpp>
#include <QBuffer>
#include <QImageReader>
#include <QPainter>
#include <QElapsedTimer>
#include <QResizeEvent>
#include <QUdpSocket>
#include <QHostAddress>
#include <QTcpSocket>
#include <unistd.h>
#include <QtConcurrent/QtConcurrent>
#include "HailinFingerprint.h"
#include "form_passwd.h"
#include <QMutex>
#include <memory>
#include "camera/v4l2camera.h"
#include "camera/camera_udp_server.h"
#include <QInputMethod>

extern "C" {
     unsigned char* g_last_jpeg = nullptr;
     size_t g_last_jpeg_size = 0;
     pthread_mutex_t g_jpeg_mutex = PTHREAD_MUTEX_INITIALIZER;
}

Widget::Widget(QWidget *parent): QWidget(parent), ui(new Ui::Widget)
{    
/*------------------------------------------------------------------------*/
/*----------------------- 1.初始化界面和时间显示 --------------------------*/
/*------------------------------------------------------------------------*/
    ui->setupUi(this);      // 根据 widget.ui 创建实际的按钮、标签等控件。
    // 将这些按钮放在同一个列表中，方便统一设置。
    // NoFocus 表示点击后不保留键盘焦点效果，不会禁用按钮，
    // 按钮仍然可以正常点击。
    QList<QPushButton*> btns = { ui->btnadd, ui->btncall, ui->btnface, ui->btnpower,ui->btnpsswd,ui->btnfinger_step};
    for (QPushButton* btn : btns) {
        btn->setFocusPolicy(Qt::NoFocus);
    }
    // 创建一个定时器，每秒更新时间显示。
    QTimer *dateTimeTimer = new QTimer(this);
    connect(dateTimeTimer, &QTimer::timeout, this, [this]() {
        QDateTime current = QDateTime::currentDateTime();
        ui->labelDate->setText(current.date().toString("yyyy-MM-dd"));  // 按“年-月-日”格式显示日期。
        ui->labelTime->setText(current.time().toString("HH:mm:ss"));    // 按“时:分:秒”格式显示时间。
    });
    dateTimeTimer->start(1000); 
    // 定时器要等 1 秒才第一次触发。
    // 所以这里先立即更新一次，避免窗口刚打开时日期和时间为空。
    QDateTime current = QDateTime::currentDateTime();
    ui->labelDate->setText(current.date().toString("yyyy-MM-dd"));
    ui->labelTime->setText(current.time().toString("HH:mm:ss"));
    qDebug() << "[界面] 时间显示初始化完成";
    this->setCursor(Qt::BlankCursor);    // 隐藏鼠标指针
    // 设置摄像头画面区域的显示样式。
    // 允许画面标签及其父控件使用透明背景，
    // 再给画面标签设置灰色背景并去掉边框。
    ui->labelcam->setAttribute(Qt::WA_TranslucentBackground, true);
    if (ui->labelcam->parentWidget())
        ui->labelcam->parentWidget()->setAttribute(Qt::WA_TranslucentBackground, true);
    ui->labelcam->setStyleSheet(
        "background-color: rgb(85, 87, 83);"
        "border: none;"
    );
/*------------------------------------------------------------------------*/
/*----------------------- 2.GPIO 和指纹模块 --------------------------*/
/*------------------------------------------------------------------------*/
    // 打开 gpiochip0 这组 GPIO 控制器。
    struct gpiod_chip *chip = gpiod_chip_open_by_name("gpiochip0");
    line5 = gpiod_chip_get_line(chip, 133); 
    gpiod_line_request_output(line5, "my_app", 0);
    // 创建指纹模块对象，指定它通过 /dev/ttyS4 这个串口通信。   
    fingerprint = new HailinFingerprint("/dev/ttyS4", this);
    if (fingerprint->open()) {
        qDebug() << "[指纹] 模块串口打开成功";
    } else {
        qDebug() << "[错误] 指纹模块串口打开失败";
    }
    // 创建指纹管理器，并告诉它要通过哪个指纹模块对象操作设备。
    fingerprintManager = new FingerprintManager(this);
    fingerprintManager->setDevice(fingerprint); 
    connect(fingerprintManager, &FingerprintManager::identifyResult, this, [this](bool success, int id, int score, const QString& msg){
        if (msg == "请按下指纹进行识别") { return;}
        if (success) {
            qDebug() << "指纹识别成功";
            audioManager->playAsync("/tmp/pcm/fingerdoor.pcm");
        } else {
            qDebug() << "指纹识别失败";
            audioManager->playAsync("/tmp/pcm/fingererr.pcm");
        }
    });
    //  监听指纹注册过程发出的进度和结果。
    connect(fingerprintManager, &FingerprintManager::enrollResult, this, [this](bool success, int id, const QString& msg){
        if (msg.contains("请按下手指进行")) {
            audioManager->playAsync("/tmp/pcm/caijifinger.pcm");
            return;
        }
        if (msg.contains("采集成功")) {
            audioManager->playAsync("/tmp/pcm/fingerone.pcm"); 
            return;
        }
        if (success && msg == "注册成功") {   
            audioManager->playAsync("/tmp/pcm/opendoor.pcm");
        } else if (!success) {
            audioManager->playAsync("/tmp/pcm/fingererr.pcm");
        }
    });
/*------------------------------------------------------------------------*/
/*----------------------- 3.初始化各功能管理器 ----------------------------*/
/*------------------------------------------------------------------------*/
    audioManager = new AudioManager(this);      // 创建普通音频管理器，供指纹、人脸、密码等功能播放提示音。    
    connAudioManager = new AudioManager(this);  // 创建通话提示音专用管理器，和普通提示音分开控制。
    faceManager = new FaceManager(this);        // 创建人脸业务管理器，负责组织人脸采集、训练和识别。
    qputenv("QT_IM_MODULE", QByteArray("qtvirtualkeyboard"));   // 指定 Qt 使用虚拟键盘输入模块，供触摸屏输入时调用。
    qDebug() << "\n========== [系统启动] 智能门禁系统初始化 ==========";
    // 加载 Haar 人脸检测分类器，它负责在图像中找出人脸的位置；加载成功与否保存在 loaded 中。
    bool loaded = loadFaceCascade("/root/haarcascade_frontalface_default.xml");
    qDebug() << "[人脸检测] 分类器加载" << (loaded ? "成功" : "失败");
    // 创建 LBPH 人脸识别器。它用于区分“检测到的人脸是谁”；这里只创建识别器，后续还需要注册人脸样本并训练，才能实际识别身份。
    faceRecognizer = cv::face::LBPHFaceRecognizer::create();
    qDebug() << "[人脸识别] LBPH识别器初始化完成";
    this->setStyleSheet("QPushButton:focus { outline: none; }");
/*------------------------------------------------------------------------*/
/*----------------------- 4.摄像头和画面刷新 ------------------------------*/
/*------------------------------------------------------------------------*/
    camera = new V4L2Camera("/dev/video0"); // 创建摄像头对象，指定设备文件为 /dev/video0。
    if (!camera->open()) {   // 打开摄像头设备。
        qDebug() << "[错误] 无法打开摄像头设备";
        QMessageBox::critical(this, "错误", "无法打开摄像头");
    } else {
        qDebug() << "[摄像头] V4L2设备打开成功";
    }
    // 创建摄像头画面的 UDP 服务，端口为 50000，它用于网络端请求摄像头画面。
    cameraUdpServer = new CameraUdpServer(camera, 50000, this);
    timer = new QTimer(this);                                       // 创建界面刷新定时器。
    connect(timer, &QTimer::timeout, this, &Widget::updateFrame);   // 每次定时器触发，都调用 updateFrame() 处理并显示一帧。
    timer->start(66);                                               // 定时器每66ms触发一次，约15fps。    
    qDebug() << "[视频显示] 摄像头独立线程采集，界面约15fps刷新";
/*------------------------------------------------------------------------*/
/*----------------------- 5.网络连接和信令 -------------------------------*/
/*------------------------------------------------------------------------*/
    tcpSignalSocket = new QTcpSocket(this); // 创建 TCP 客户端，用于连接服务器并收发通话等控制消息。    
    connect(tcpSignalSocket, &QTcpSocket::readyRead, this, &Widget::onTcpReadyRead);    // 收到服务器发来的 TCP 数据时，交给 onTcpReadyRead() 处理。
    connect(tcpSignalSocket, &QTcpSocket::disconnected, this, [this]() {    // TCP 连接断开后，等待 3 秒，再尝试重新连接服务器。
        qDebug() << "[信令] TCP连接断开，3秒后自动重连...";
        QTimer::singleShot(3000, this, [this]() {
            qDebug() << "[信令] 正在尝试重连...";
            tcpSignalSocket->connectToHost("192.168.31.29", 8887);
        });
    });
    tcpSignalSocket->connectToHost("192.168.31.29", 8887);  // 程序启动时主动连接服务器的 8887 端口.
    qDebug() << "[完成] 系统初始化完成，等待用户操作...";    
    qDebug() << "===============================================\n";
/*------------------------------------------------------------------------*/
/*----------------------- 6.GPIO 输入状态监测 ----------------------------*/
/*------------------------------------------------------------------------*/
    pe4Thread = new GpioPe4MonitorThread(this); // 创建 PE4 GPIO 监测线程。    
    connect(pe4Thread, &GpioPe4MonitorThread::pe4Low, this, [this](){hasPerson = false;});// PE4 变低时，记录当前没有人。
    connect(pe4Thread, &GpioPe4MonitorThread::pe4High, this, [this](){hasPerson = true;});// PE4 变高时，记录当前有人。
    pe4Thread->start(); // 启动 GPIO 监测线程。
/*------------------------------------------------------------------------*/
/*----------------------- 3.连接各功能管理器的结果 ------------------------*/
/*------------------------------------------------------------------------*/
    // 指纹模块识别、注册或输出调试信息时，交给 Widget 对应的处理函数。
    connect(fingerprint, &HailinFingerprint::identifyResult, this, &Widget::onFingerprintIdentify);
    connect(fingerprint, &HailinFingerprint::enrollResult, this, &Widget::onFingerprintEnroll);
    connect(fingerprint, &HailinFingerprint::debugInfo, this, &Widget::onFingerprintDebug);
    // 用户点击密码按钮时，调用密码按钮的处理函数，通常会打开密码输入界面。
    connect(ui->btnpsswd, &QPushButton::clicked, this, &Widget::on_btnpsswd_clicked);   
    // 指纹模块分步注册、分步识别过程中上报状态时，交给 Widget 更新处理。
    connect(fingerprint, &HailinFingerprint::stepRegisterStatus, this, &Widget::onStepRegisterStatus);
    connect(fingerprint, &HailinFingerprint::stepIdentifyStatus, this, &Widget::onStepIdentifyStatus);
    // 通话中的音频发送或接收线程连接断开时，调用挂断流程进行清理。
    if(audioThread) connect(audioThread, &AudioSenderThread::connectionLost, this, &Widget::hangupCall);
    if(audioRecvThread) connect(audioRecvThread, &AudioReceiverThread::connectionLost, this, &Widget::hangupCall);
    // 人脸采集模块每采集到一张样本，就会发出 faceSampled 信号。
    // current 表示当前采集到第几张，total 表示目标总数。
    connect(faceManager, &FaceManager::faceSampled, this, [this](int current, int total){
        if (current == 1) {
            audioManager->playAsync("/tmp/pcm/faceone.pcm");
            gpiod_line_set_value(line5, 1);
        }else if (current == 2) audioManager->playAsync("/tmp/pcm/facetwo.pcm");
        else if (current == 3) {
            audioManager->playAsync("/tmp/pcm/facethree.pcm");
            gpiod_line_set_value(line5, 0);
        }
    });
    // 人脸样本训练结束后，根据结果输出成功或失败信息。
    connect(faceManager, &FaceManager::trainFinished, this, [this](bool success){
        if (success) {qDebug() << "[人脸] 训练完成";}
        else {qDebug() << "[人脸] 训练失败，样本不足";}
    });
    // 人脸识别完成后，根据结果关闭补光灯，并播放对应提示音。
    connect(faceManager, &FaceManager::recognizeResult, this, [this](bool success, double confidence){
        qDebug() << "[人脸识别] 结果:" << (success ? "成功" : "失败") << ", confidence=" << confidence;
         gpiod_line_set_value(line5, 0);     // 识别结束，关闭补光灯
        if (success) {
            audioManager->playAsync("/tmp/pcm/opendoor.pcm");
        } else {
            audioManager->playAsync("/tmp/pcm/faceerr.pcm");
        }
    });
    // 创建密码管理器，负责检查输入的密码。
    passwordManager = new PasswordManager(this);   
    // 密码检查完成后，根据结果播放提示音。
    // 成功时关闭密码输入窗口；失败时播放错误提示音。
    connect(passwordManager, &PasswordManager::passwordChecked, this, [this](bool success){
        if (success) {
            audioManager->playAsync("/tmp/pcm/opendoor.pcm");
            if (passwdForm) {
                passwdForm->close();
                passwdForm = nullptr;
            }
        } else {
            audioManager->playAsync("/tmp/pcm/passwderr.pcm");
        }
    });
}

Widget::~Widget()
{
    if(cameraPushThread) {
        cameraPushThread->stop();
        cameraPushThread->wait();
        delete cameraPushThread;
        cameraPushThread = nullptr;
    }
    if(audioThread) {
        audioThread->stop();
        audioThread->wait();
        delete audioThread;
    }
    if(audioRecvThread) {
        audioRecvThread->stop();
        audioRecvThread->wait();
        delete audioRecvThread;
    }
    if (audioSocketFd >= 0) {
        ::close(audioSocketFd);
        audioSocketFd = -1;
    }
    if(timer) {
        timer->stop();
        delete timer;
        timer = nullptr;
    }
    if (pe4Thread) {
        pe4Thread->stop();
        pe4Thread->wait();
        delete pe4Thread;
        pe4Thread = nullptr;
    }
    if (fingerDetectThread) {
        fingerDetectThread->stop();
        fingerDetectThread->wait();
        delete fingerDetectThread;
        fingerDetectThread = nullptr;
    }
    if (fingerprint) {
        fingerprint->close();
        delete fingerprint;
        fingerprint = nullptr;
    }
    if (camera) {
        camera->close();
        delete camera;
        camera = nullptr;
    }
    delete ui;
}

void Widget::hangupCall()
{
    if (connAudioManager) connAudioManager->stop(); 
    if (callForm) {
        callForm->stopCallTimer();
        callForm->close();
    }
    qDebug() << "[hangupCall] Stopping cameraPushThread...";
    if (cameraPushThread) {
        cameraPushThread->stop();
        if (!cameraPushThread->wait(3000)) {
            qWarning() << "[hangupCall] cameraPushThread wait timeout!";
        }
        delete cameraPushThread;
        cameraPushThread = nullptr;
        qDebug() << "[hangupCall] cameraPushThread stopped.";
    }
    qDebug() << "[hangupCall] Stopping audioThread...";
    if (audioThread) {
        audioThread->stop();
        if (!audioThread->wait(3000)) {
            qWarning() << "[hangupCall] audioThread wait timeout!";
        }
        delete audioThread;
        audioThread = nullptr;
        qDebug() << "[hangupCall] audioThread stopped.";
    }
    qDebug() << "[hangupCall] Stopping audioRecvThread...";
    if (audioRecvThread) {
        audioRecvThread->stop();
        if (!audioRecvThread->wait(3000)) {
            qWarning() << "[hangupCall] audioRecvThread wait timeout!";
        }
        delete audioRecvThread;
        audioRecvThread = nullptr;
        qDebug() << "[hangupCall] audioRecvThread stopped.";
    }
    if (callForm) {    
        delete callForm;
        callForm = nullptr;
    }
    if (audioSocketFd >= 0) {   
        ::close(audioSocketFd);
        audioSocketFd = -1;
        qDebug() << "[hangupCall] audioSocketFd closed.";
    }
    if (pe4Thread) {
        pe4Thread->resume();
    }
}

void Widget::on_btncall_clicked()
{
    audioManager->playAsync("/tmp/pcm/calling.pcm");    
    QMetaObject::Connection* conn = new QMetaObject::Connection;
    *conn = connect(audioManager, &AudioManager::playbackFinished, this, [this, conn](const QString& file){
        if (file == "/tmp/pcm/calling.pcm") {
            connAudioManager->playAsync("/tmp/pcm/conn.pcm");
            QObject::disconnect(*conn);
            delete conn;
        }
    });
    if (callForm) {
        callForm->close();
        delete callForm;
        callForm = nullptr;
    }
    callForm = new Form_CALL(this);
    QPoint globalPos = ui->widget_3->mapToGlobal(QPoint(0, 0));
    callForm->move(globalPos);
    callForm->setStatusText("正在呼叫...");
    callForm->show();
    callForm->raise();
    connect(callForm, &Form_CALL::hangupRequested, this, [this](){
        if (audioManager) audioManager->stop();
        if (connAudioManager) connAudioManager->stop();
        hangupCall();
    });
    if (tcpSignalSocket->state() == QAbstractSocket::ConnectedState) {        
        tcpSignalSocket->write("call\n");
        tcpSignalSocket->flush();
    } else {
        QMessageBox::warning(this, "错误", "信令服务器未连接");
        return;
    }
    qDebug() << "[完成] 信令已发送，等待服务器accept...";
    qDebug() << "=============================================";
    if (pe4Thread) {
        pe4Thread->pause();
    }
}

void Widget::onServerAccept()
{   
    if (connAudioManager) connAudioManager->stop();   
    qDebug() << "[信令] 收到服务器accept，开始创建音视频线程...";
    if (audioSocketFd < 0) {
        audioSocketFd = socket(AF_INET, SOCK_DGRAM, 0);
        if (audioSocketFd < 0) {
            qDebug() << "[音频] 创建UDP socket失败, errno=" << errno;
        } else {
            struct sockaddr_in local_addr;
            memset(&local_addr, 0, sizeof(local_addr));
            local_addr.sin_family = AF_INET;
            local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
            local_addr.sin_port = htons(12345);
            if (bind(audioSocketFd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
                qDebug() << "[音频] 绑定端口12345失败, errno=" << errno;
                ::close(audioSocketFd);
                audioSocketFd = -1;
            } else {
                qDebug() << "[音频] 成功绑定端口12345, sockfd=" << audioSocketFd;
            }
        }
    }
    if (audioSocketFd < 0) {    
        qDebug() << "[音频] 无法创建/绑定socket，音频线程不启动";
        return;
    }
    if (cameraPushThread) {
        cameraPushThread->stop();
        cameraPushThread->wait();
        delete cameraPushThread;
        cameraPushThread = nullptr;
    }
    cameraPushThread = new CameraPushThread(camera, "192.168.31.29", 23456, this);
    cameraPushThread->start();
    if (audioThread) {    
        audioThread->stop();
        audioThread->wait();
        delete audioThread;
        audioThread = nullptr;
    }
    qDebug() << "[音频] 创建AudioSenderThread...";
    audioThread = new AudioSenderThread(audioSocketFd, "192.168.31.29", 12345, this);
    connect(audioThread, &AudioSenderThread::errorMsg, this, [this](const QString &msg){
        qDebug() << "[音频错误]" << msg;
        QMessageBox::critical(this, "错误", msg);
    });
    connect(audioThread, &AudioSenderThread::finishedMsg, this, [this](){
        qDebug() << "[音频完成] 音频采集线程已结束";
    });
    audioThread->start();
    qDebug() << "[音频] AudioSenderThread已启动";
    QTimer::singleShot(400, this, [this]() 
    {
        if (audioRecvThread) {
            audioRecvThread->stop();
            audioRecvThread->wait();
            delete audioRecvThread;
            audioRecvThread = nullptr;
        }
        qDebug() << "[音频] 创建AudioReceiverThread...";
        audioRecvThread = new AudioReceiverThread(audioSocketFd, this);
        audioRecvThread->setVolume(30); 
        if (audioRecvThread) {
            qDebug() << "[音频] AudioReceiverThread 准备 start";
            audioRecvThread->start();
            qDebug() << "[音频] AudioReceiverThread 已调用 start";
            connect(audioRecvThread, &AudioReceiverThread::errorMsg, this, [this](const QString &msg){
                qWarning() << "[音频接收] 错误:" << msg;
            });
            connect(audioRecvThread, &AudioReceiverThread::finishedMsg, this, [this](){ 
                qDebug() << "[音频接收] 线程已结束";
            });
            connect(audioRecvThread, &AudioReceiverThread::connectionLost, this, [this](){ 
                qWarning() << "[音频接收] 连接丢失";
            });
        }
    });
    if (callForm) callForm->startCallTimer();
    qDebug() << "[完成] 音频线程全部启动";
}

void Widget::onTcpReadyRead()
{
    QByteArray datagram = tcpSignalSocket->readAll();
    QStringList messages = QString::fromUtf8(datagram).split('\n', QString::SkipEmptyParts);
    for (const QString& serverMsgRaw : messages) {
        QString serverMsg = serverMsgRaw.trimmed();
        qDebug() << "get server: " << serverMsg;
        if (serverMsg == "accept") {
            onServerAccept();
        }else if (serverMsg == "recstop") {
            qDebug() << "[信令] 收到recstop，自动挂断通话，当前callForm指针:" << callForm
                     << ", cameraPushThread:" << cameraPushThread
                     << ", audioThread:" << audioThread                 
                     << ", audioRecvThread:" << audioRecvThread                  
                     << ", audioSocketFd:" << audioSocketFd;        
            hangupCall();
        }
        else if (serverMsg == "unlock") {
            qDebug() << "[信令] 收到unlock，播放开门音";
            if (audioManager) audioManager->playAsync("/tmp/pcm/opendoor.pcm");
        }
    }
}

void Widget::onFingerprintIdentify(bool success, int id, int score, const QString& msg)
{
    if (success) {
        QMessageBox::information(this, "指纹识别", QString("识别成功，ID=%1，score=%2").arg(id).arg(score));
    } else {
        qDebug() << "[指纹] 识别失败, msg=" << msg;
    }
}

void Widget::onFingerprintEnroll(bool success, int id, const QString& msg)
{
    if (success) {       
        nextFingerId++;       
        QMessageBox::information(this, "指纹注册", QString("注册成功，ID=%1").arg(id));   
    } else {       
        qDebug() << "[指纹] 注册失败, ID=" << id << ", msg=" << msg;       
        QMessageBox::warning(this, "指纹注册", QString("注册失败，ID=%1\n原因：%2").arg(id).arg(msg));
    }
}

void Widget::onFingerprintDebug(const QString& info){}

void Widget::on_btnpsswd_clicked()
{
    Form_passwd* passwdForm = new Form_passwd(this);
    passwdForm->setAttribute(Qt::WA_DeleteOnClose); 
    connect(passwdForm, &Form_passwd::passwordEntered, passwordManager, &PasswordManager::checkPassword);
    QPoint globalPos = ui->widget_3->mapToGlobal(QPoint(0, 0));
    passwdForm->move(globalPos);
    passwdForm->show();
    passwdForm->raise();
    audioManager->playAsync("/tmp/pcm/inputpasswd.pcm");
}

void Widget::onStepRegisterStatus(const QString& msg, int step, bool ok) {}

void Widget::onStepIdentifyStatus(const QString& msg, bool ok, int id, int score) {}



// 取帧更新到ui界面显示
void Widget::updateFrame()
{
/*------------------------------------------------------------------------------*/
/*--------------------1.从摄像头取得最新画面，并显示到界面。----------------------*/
/*------------------------------------------------------------------------------*/
    // 人脸检测不是每次界面刷新都执行，记录刷新次数并每 10 次检测一次。
    static int detect_counter = 0;
    const int detect_interval = 10; 

    // 从摄像头对象取得最新一帧；还没有画面时就结束本次刷新。
    QImage img = camera->getFrame();
    if (img.isNull()) {return;}

    // 在画面上绘制上一次检测到的人脸框和采集数量。
    QPainter painter(&img);
    painter.setPen(QPen(Qt::red, 3));
    QFont font = painter.font();
    font.setPointSize(18);
    painter.setFont(font);
    for (const auto &face : faces) 
    {
        // 给每张已检测到的人脸画红色矩形框。
        painter.drawRect(face.x, face.y, face.width, face.height);  
    
        // 用黑色描边、白色文字显示当前人脸采集数量，提高文字可读性。     
        QString text = QString::number(collectCount);              
        painter.setPen(QPen(Qt::black, 5));        
        painter.drawText(face.x + 6, face.y + 28, text);               
        painter.setPen(QPen(Qt::white, 1));       
        painter.drawText(face.x + 6, face.y + 28, text);     
    
        // 恢复红色画笔，供下一张人脸框使用。  
        painter.setPen(QPen(Qt::red, 3));   
    }
    painter.end();

    // 将画面缩放到显示区域大小，保持比例并使用快速缩放。
    QSize labelSize(430, 475); 
    QImage scaledImg = img.scaled(labelSize, Qt::KeepAspectRatioByExpanding, Qt::FastTransformation);
    
    // 缩放后的画面可能比显示区域大，因此从中央裁剪出需要显示的部分。
    QRect cropRect((scaledImg.width() - labelSize.width())/2, (scaledImg.height() - labelSize.height())/2, labelSize.width(), labelSize.height());
   
    // 转为 Qt 显示用的 QPixmap，并更新摄像头画面控件。
    QPixmap pix = QPixmap::fromImage(scaledImg.copy(cropRect));
    ui->labelcam->setPixmap(pix);

/*------------------------------------------------------------------------------*/
/*-----------------2.定期把缩小后的画面交给后台线程做人脸检测。-------------------*/
/*------------------------------------------------------------------------------*/
    // 每 10 次界面刷新才启动一次人脸检测，减少检测对预览流畅度的影响。
    bool doDetect = (++detect_counter % detect_interval == 0);

    // 分类器有效、当前到了检测时机，并且没有其他检测任务运行时，才开始新检测。
    if (doDetect && !face_cascade.empty() &&!faceDetectRunning.exchange(true)) 
    {
        QImage smallImg = img.scaled(320, 240, Qt::KeepAspectRatio);    // 先缩小图像，再交给后台线程检测，以减少计算量。

        // QtConcurrent::run() 让大括号里的任务在后台线程执行。
        QtConcurrent::run([this, smallImg]() 
        {   
            // 后台线程在这里使用 smallImg 做人脸检测
            // 把 Qt 图像数据包装成 OpenCV 矩阵，并转换为灰度图。
            cv::Mat mat_small(smallImg.height(), smallImg.width(), CV_8UC4, (void*)smallImg.bits(), smallImg.bytesPerLine());
            cv::Mat mat_gray;
            cv::cvtColor(mat_small, mat_gray, cv::COLOR_BGRA2GRAY);

            // 使用 Haar 分类器检测人脸区域。
            std::vector<cv::Rect> faceRects;
            face_cascade.detectMultiScale(mat_gray, faceRects, 1.1, 3, 0, cv::Size(30, 30));

            // 转成 Qt 容器，准备把检测结果交回界面线程。
            QVector<cv::Rect> qfaces = QVector<cv::Rect>::fromStdVector(faceRects);

            // 把结果排入 Widget 所属的 Qt 线程，避免后台线程直接操作界面。
            QMetaObject::invokeMethod(this, [this, qfaces, w=smallImg.width(), h=smallImg.height()](){
                faceDetectRunning.store(false);     // 标记本次检测结束
                onFaceDetectResult(qfaces, w, h);   // 更新人脸检测结果
            }, Qt::QueuedConnection);
        });
    }

    // 人脸识别之间至少间隔 2 秒，避免反复识别、播放提示音。
    static QElapsedTimer faceRecognizeCooldown;
    static int faceRecognizeIntervalMs = 2000;

/*------------------------------------------------------------------------------*/
/*-----------------3.检测到人脸且满足识别条件时，裁剪最大的人脸区域。-------------------*/
/*------------------------------------------------------------------------------*/
    // 只有人体感应检测到有人时，才继续进行人脸识别或提示。
    if (hasPerson) 
    {
        if (!faces.empty()) {
            // 只有冷却时间结束后才开始下一次人脸识别。
            if (!faceRecognizeCooldown.isValid() || faceRecognizeCooldown.elapsed() > faceRecognizeIntervalMs) {
                qDebug() << "[人脸识别] 开始识别";

                // 多张人脸同时出现时，选择面积最大的一张。
                auto maxFace = std::max_element(faces.begin(), faces.end(),
                    [](const cv::Rect& a, const cv::Rect& b) { return a.area() < b.area(); });
                if (maxFace != faces.end()) {
                    // 从当前画面裁出最大人脸，缩放后交给人脸管理器识别。
                    QRect qface(maxFace->x, maxFace->y, maxFace->width, maxFace->height);
                    QImage faceImg = img.copy(qface).scaled(100, 100, Qt::KeepAspectRatio);
                    faceManager->recognize(faceImg);    // 进行人脸识别。
                    // 重新开始计时，控制下一次识别的间隔。
                    faceRecognizeCooldown.restart();
                }
            }
        } else {
            // 有人但暂未检测到人脸时，间隔播放提示音并打开补光灯。
            if (!faceRecognizeCooldown.isValid() || faceRecognizeCooldown.elapsed() > faceRecognizeIntervalMs) {
                audioManager->playAsync("/tmp/pcm/oncam.pcm");
                faceRecognizeCooldown.restart();
                gpiod_line_set_value(line5, 1);
            }
        }
    }
}


// 接收后台线程的人脸检测结果，并把检测框转换到摄像头原图的尺寸。
void Widget::onFaceDetectResult(const QVector<cv::Rect>& detected, int imgW, int imgH)
{
    // 获取摄像头当前画面，用它的宽高作为原图尺寸。
    QImage img = camera->getFrame();
    if (img.isNull()) return;

    // 后台检测使用的是缩小后的图像，因此计算缩小图到原图的宽高比例。
    double scaleX = double(img.width()) / imgW;
    double scaleY = double(img.height()) / imgH;

    // 清除上一轮的人脸框，准备保存这轮检测结果。
    faces.clear();

    // 将每个人脸框的坐标和尺寸按比例换算回原图大小。
    for (const auto& face : detected) {
        cv::Rect f(face.x * scaleX, face.y * scaleY, face.width * scaleX, face.height * scaleY);

        // 保存换算后的框，供界面绘制和后续人脸识别使用。
        faces.append(f);
    }
}

void Widget::on_btnadd_clicked()
{
    audioManager->playAsync("/tmp/pcm/regnew.pcm");
    if (!fingerForm) {
        fingerForm = new Form_finger(this);
        connect(fingerForm, &Form_finger::addFaceRequested, this, &Widget::onAddFaceFromFingerForm);
        connect(fingerForm, &Form_finger::addFingerRequested, this, &Widget::onAddFingerFromFingerForm);
    }
    QPoint globalPos = ui->widget_3->mapToGlobal(QPoint(0, 0));
    fingerForm->move(globalPos);
    fingerForm->show();
    fingerForm->raise();
}

void Widget::onAddFaceFromFingerForm()
{
    if (faces.empty()) return;
    auto maxFace = std::max_element(faces.begin(), faces.end(),[](const cv::Rect& a, const cv::Rect& b) { return a.area() < b.area(); });
    if (maxFace == faces.end()) return;
    QImage img = camera->getFrame();
    if (img.isNull()) return;
    QRect qface(maxFace->x, maxFace->y, maxFace->width, maxFace->height);
    QImage faceImg = img.copy(qface).scaled(100, 100, Qt::KeepAspectRatio);
    faceManager->addFaceSample(faceImg);
    if (faceManager->sampleCount() == 3) {
        faceManager->train();
    }
}
void Widget::onAddFingerFromFingerForm()
{
    qDebug() << "[分步注册] 开始分步注册, ID=" << nextFingerId;
    QMessageBox::information(this, "分步注册", "请将手指按在指纹模块上，按提示操作，每次采集后抬手再按。");
    QtConcurrent::run(fingerprint, &HailinFingerprint::stepRegister, nextFingerId, 3); 
}

void Widget::resizeEvent(QResizeEvent* event) 
{
    QWidget::resizeEvent(event);
}

bool Widget::loadFaceCascade(const QString& path)
{
    return face_cascade.load(path.toStdString());
}
void Widget::on_btnpower_clicked()
{
    gpiod_line_set_value(line5, 0);
}
