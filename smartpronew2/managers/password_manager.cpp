#include "password_manager.h"

PasswordManager::PasswordManager(QObject* parent)
    : QObject(parent)
{
}

PasswordManager::~PasswordManager() {}

// 功能：设置用于验证的正确密码。
void PasswordManager::setPassword(const QString& pwd)
{
    password = pwd;
    emit passwordChanged();
}

// 功能：比较用户输入与已设置密码并报告校验结果。
void PasswordManager::checkPassword(const QString& input)
{
    qDebug() << "[PasswordManager] checkPassword: input=" << input << ", password=" << password << ", inputHex=" << input.toUtf8().toHex() << ", pwdHex=" << password.toUtf8().toHex();
    emit passwordChecked(input == password);
}
