#include "form_passwd.h"
#include "ui_form_passwd.h"
#include "widget.h"
#include <QFile>
#include <QProcess>
#include <QDebug>

Form_passwd::Form_passwd(QWidget *parent) :

    QWidget(parent),

    ui(new Ui::Form_passwd)
{
    ui->setupUi(this);
    QList<QPushButton*> digitButtons = {
        ui->btn0, ui->btn1, ui->btn2, ui->btn3, ui->btn4,
        ui->btn5, ui->btn6, ui->btn7, ui->btn8, ui->btn9
    };
    for (auto btn : digitButtons) {
        connect(btn, &QPushButton::clicked, this, &Form_passwd::onDigitClicked);
        btn->setFocusPolicy(Qt::NoFocus);
    }
    connect(ui->btnbck, &QPushButton::clicked, this, &Form_passwd::on_btnbck_clicked);
    connect(ui->btnok, &QPushButton::clicked, this, &Form_passwd::on_btnok_clicked);
    inputBuffer.clear();
    updateDisplay();
}

Form_passwd::~Form_passwd()
{
    delete ui;
}

// 功能：把用户按下的数字追加到密码输入内容。
void Form_passwd::onDigitClicked()
{
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    qDebug() << "[passwd] Digit clicked:" << btn->text();
    if (inputBuffer.size() >= 8) return; 
    inputBuffer += btn->text();
    playKeySound("/tmp/pcm/beep.pcm");
    updateDisplay();
}

// 功能：删除密码输入内容中的最后一位。
void Form_passwd::on_btnbck_clicked()
{
    qDebug() << "[passwd] Backspace clicked";
    if (!inputBuffer.isEmpty()) {
        inputBuffer.chop(1);
        playKeySound("/tmp/pcm/beep.pcm");
        updateDisplay();
    }
}

// 功能：提交输入的密码并处理校验结果。
void Form_passwd::on_btnok_clicked()
{
    qDebug() << "[passwd] OK clicked, inputBuffer=" << inputBuffer << ", hex=" << inputBuffer.toUtf8().toHex();
    if (!inputBuffer.isEmpty())
        emit passwordEntered(inputBuffer);
    inputBuffer.clear();
    updateDisplay();
    this->close();
}

// 功能：刷新密码输入框的显示内容。
void Form_passwd::updateDisplay()
{
}

// 功能：返回密码界面的主控件。
QWidget* Form_passwd::getMainWidget() {
    QWidget* w = parentWidget();
    while (w && !qobject_cast<class Widget*>(w)) w = w->parentWidget();
    return w;
}

// 功能：播放密码按键对应的提示音。
void Form_passwd::playKeySound(const QString& path)
{
    qDebug() << "[passwd] playKeySound called with path:" << path;
    QWidget* mainWin = getMainWidget();
    if (mainWin) {
        auto widget = qobject_cast<class Widget*>(mainWin);
        if (widget) {
            qDebug() << "[passwd] Calling playPcmFileAsync:" << path;
            widget->audioManager->playAsync(path);
        } else {
            qDebug() << "[passwd] mainWin is not Widget*";
        }
    } else {
        qDebug() << "[passwd] mainWin is nullptr";
    }
}

void Form_passwd::on_btn1_clicked()
{
}
