#ifndef FORM_FINGER_H
#define FORM_FINGER_H

#include <QWidget>

namespace Ui {

class Form_finger;

}

class Form_finger : public QWidget

{
    Q_OBJECT

public:

    explicit Form_finger(QWidget *parent = nullptr);
    ~Form_finger();

signals:

    void addFaceRequested();

public slots:

    void addFingerRequested();
    void onAddQuitClicked();

private:

    Ui::Form_finger *ui;
    int currentStep;
    bool waitingFinger;
    void startFingerRegisterStep(int step);
    void doFingerCollect(int step);

};

#endif 
