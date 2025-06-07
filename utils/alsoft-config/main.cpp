#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    const auto a = QApplication(argc, argv);
    auto w = MainWindow{};
    w.show();

    return a.exec();
}
