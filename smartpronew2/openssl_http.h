#ifndef OPENSSL_HTTP_H
#define OPENSSL_HTTP_H

#include <QByteArray>
#include <QString>

QByteArray https_post(const QString &host, const QString &path, const QByteArray &postData, const QString &headers);
QByteArray https_get(const QString &host, const QString &path, const QString &headers);

#endif 
