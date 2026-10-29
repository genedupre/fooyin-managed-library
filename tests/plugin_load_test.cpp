#include <QCoreApplication>
#include <QPluginLoader>
#include <QDebug>
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(argc!=2) return 2;
    QPluginLoader p(QString::fromLocal8Bit(argv[1]));
    if(!p.instance()) { qCritical()<<p.errorString(); return 1; }
    qInfo()<<p.metaData();
    return 0;
}
