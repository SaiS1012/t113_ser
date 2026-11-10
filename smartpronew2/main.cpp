#include "widget.h"
#include <QApplication>
#include <QSurfaceFormat>

// 功能：程序入口：创建 Qt 应用和主窗口，并启动事件循环。
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    Widget w;
    QSurfaceFormat format;
    format.setVersion(2, 0);  
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setRenderableType(QSurfaceFormat::OpenGLES);
    QSurfaceFormat::setDefaultFormat(format);
    w.show();
    return a.exec();
}
