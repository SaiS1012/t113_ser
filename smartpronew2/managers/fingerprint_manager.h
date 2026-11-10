#ifndef FINGERPRINT_MANAGER_H
#define FINGERPRINT_MANAGER_H

#include <QObject>

class HailinFingerprint;

class FingerprintManager : public QObject

{

    Q_OBJECT

public:

    explicit FingerprintManager(QObject* parent = nullptr);
    ~FingerprintManager();
    void setDevice(HailinFingerprint* device);

public slots:

    void startEnroll(int id);      
    void startIdentify();          

signals:

    void enrollResult(bool success, int id, const QString& msg);
    void identifyResult(bool success, int id, int score, const QString& msg);

private:

    HailinFingerprint* fingerprint = nullptr;

};

#endif 
