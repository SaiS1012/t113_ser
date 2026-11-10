#include "form_call.h"
#include "ui_form_call.h"
#include "widget.h"

Form_CALL::Form_CALL(QWidget *parent) :

    QWidget(parent),

    ui(new Ui::Form_CALL)
{
    ui->setupUi(this);
    this->setAttribute(Qt::WA_TranslucentBackground, true);
    ui->btnstop->setFocusPolicy(Qt::NoFocus);
    connect(ui->btnstop, &QPushButton::clicked, this, [this]() {
        QWidget* w = parentWidget();
        while (w && !qobject_cast<Widget*>(w)) w = w->parentWidget();
        if (w) {
            auto widget = qobject_cast<Widget*>(w);
            if (widget) {
                widget->audioManager->stop();
                if (widget->tcpSignalSocket && widget->tcpSignalSocket->state() == QAbstractSocket::ConnectedState) {
                    widget->tcpSignalSocket->write("stopsend\n");
                    widget->tcpSignalSocket->flush();
                }
            }
        }
        emit hangupRequested();
    });
    callTimer = new QTimer(this);
    connect(callTimer, &QTimer::timeout, this, [this]() {
        callSeconds++;
        int min = callSeconds / 60;
        int sec = callSeconds % 60;
        ui->label->setText(QString("通话中 %1:%2").arg(min, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0')));
    });
}

// 功能：更新通话界面上的状态提示文字。
void Form_CALL::setStatusText(const QString& text) {
    ui->label->setText(text);
}

// 功能：启动通话计时并定期刷新通话时长。
void Form_CALL::startCallTimer() {
    callSeconds = 0;
    callTimer->start(1000);
    ui->label->setText("通话中 00:00");
}

// 功能：停止通话计时器。
void Form_CALL::stopCallTimer() {
    callTimer->stop();
}

Form_CALL::~Form_CALL()
{
    delete ui;
}
