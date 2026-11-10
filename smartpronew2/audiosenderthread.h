#ifndef AUDIOSENDERTHREAD_H
#define AUDIOSENDERTHREAD_H

#include <QThread>
#include <atomic>
#include <alsa/asoundlib.h>
#include <QString>

class AudioSenderThread : public QThread {
    
    Q_OBJECT

public:

    AudioSenderThread(int sockfd, const QString& serverIp, quint16 serverPort, QObject *parent = nullptr);
    void stop();

protected:

    void run() override;

signals:

    void errorMsg(const QString &msg);
    void finishedMsg();
    void connectionLost();

private:

    std::atomic<bool> running;
    int sockfd; 
    QString serverIp;
    quint16 serverPort;

};

#endif 
