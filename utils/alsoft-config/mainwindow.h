#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QListWidget>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void saveCurrentConfig();

    void saveConfigAsFile();
    void loadConfigFromFile();

    void updatePeriodSizeEdit(int size);
    void updatePeriodSizeSlider();
    void updatePeriodCountEdit(int size);
    void updatePeriodCountSlider();

    void addHrtfFile();
    void removeHrtfFile();

    void updateHrtfRemoveButton();

    void showEnabledBackendMenu(QPoint pt);
    void showDisabledBackendMenu(QPoint pt);

    void updateReverbBoostEdit(int size);
    void updateReverbBoostSlider(QString value);

private:
    Ui::MainWindow *ui;

    QValidator *mPeriodSizeValidator;
    QValidator *mPeriodCountValidator;
    QValidator *mSourceCountValidator;
    QValidator *mEffectSlotValidator;
    QValidator *mSourceSendValidator;
    QValidator *mSampleRateValidator;
    QValidator *mReverbBoostValidator;

    void loadConfig(const QString &fname);
    void saveConfig(const QString &fname) const;
};

#endif // MAINWINDOW_H
