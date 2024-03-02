
#include "config.h"

#include "mainwindow.h"

#include <array>
#include <cmath>
#include <iostream>

#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QSettings>
#include <QtGlobal>
#include "ui_mainwindow.h"
#include "verstr.h"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

#include "almalloc.h"
#include "alspan.h"

namespace {

struct BackendNamePair {
    /* NOLINTBEGIN(*-avoid-c-arrays) */
    char backend_name[16];
    char full_string[32];
    /* NOLINTEND(*-avoid-c-arrays) */
};
constexpr std::array backendList{
#ifdef HAVE_PIPEWIRE
    BackendNamePair{ "pipewire", "PipeWire" },
#endif
#ifdef HAVE_PULSEAUDIO
    BackendNamePair{ "pulse", "PulseAudio" },
#endif
#ifdef HAVE_ALSA
    BackendNamePair{ "alsa", "ALSA" },
#endif
#ifdef HAVE_JACK
    BackendNamePair{ "jack", "JACK" },
#endif
#ifdef HAVE_COREAUDIO
    BackendNamePair{ "core", "CoreAudio" },
#endif
#ifdef HAVE_OSS
    BackendNamePair{ "oss", "OSS" },
#endif
#ifdef HAVE_SOLARIS
    BackendNamePair{ "solaris", "Solaris" },
#endif
#ifdef HAVE_SNDIO
    BackendNamePair{ "sndio", "SoundIO" },
#endif
#ifdef HAVE_WASAPI
    BackendNamePair{ "wasapi", "WASAPI" },
#endif
#ifdef HAVE_DSOUND
    BackendNamePair{ "dsound", "DirectSound" },
#endif
#ifdef HAVE_WINMM
    BackendNamePair{ "winmm", "Windows Multimedia" },
#endif
#ifdef HAVE_PORTAUDIO
    BackendNamePair{ "port", "PortAudio" },
#endif
#ifdef HAVE_OPENSL
    BackendNamePair{ "opensl", "OpenSL" },
#endif

    BackendNamePair{ "null", "Null Output" },
#ifdef HAVE_WAVE
    BackendNamePair{ "wave", "Wave Writer" },
#endif
};

struct NameValuePair {
    /* NOLINTBEGIN(*-avoid-c-arrays) */
    const char name[64];
    const char value[16];
    /* NOLINTEND(*-avoid-c-arrays) */
};
constexpr std::array speakerModeList{
    NameValuePair{ "Autodetect", "" },
    NameValuePair{ "Mono", "mono" },
    NameValuePair{ "Stereo", "stereo" },
    NameValuePair{ "Quadraphonic", "quad" },
    NameValuePair{ "5.1 Surround", "surround51" },
    NameValuePair{ "6.1 Surround", "surround61" },
    NameValuePair{ "7.1 Surround", "surround71" },
    NameValuePair{ "3D7.1 Surround", "surround3d71" },

    NameValuePair{ "Ambisonic, 1st Order", "ambi1" },
    NameValuePair{ "Ambisonic, 2nd Order", "ambi2" },
    NameValuePair{ "Ambisonic, 3rd Order", "ambi3" },
};
constexpr std::array sampleTypeList{
    NameValuePair{ "Autodetect", "" },
    NameValuePair{ "8-bit int", "int8" },
    NameValuePair{ "8-bit uint", "uint8" },
    NameValuePair{ "16-bit int", "int16" },
    NameValuePair{ "16-bit uint", "uint16" },
    NameValuePair{ "32-bit int", "int32" },
    NameValuePair{ "32-bit uint", "uint32" },
    NameValuePair{ "32-bit float", "float32" },
};
constexpr std::array resamplerList{
    NameValuePair{ "Point", "point" },
    NameValuePair{ "Linear", "linear" },
    NameValuePair{ "Cubic Spline", "cubic" },
    NameValuePair{ "Default (Cubic Spline)", "" },
    NameValuePair{ "11th order Sinc (fast)", "fast_bsinc12" },
    NameValuePair{ "11th order Sinc", "bsinc12" },
    NameValuePair{ "23rd order Sinc (fast)", "fast_bsinc24" },
    NameValuePair{ "23rd order Sinc", "bsinc24" },
};
constexpr std::array stereoModeList{
    NameValuePair{ "Autodetect", "" },
    NameValuePair{ "Speakers", "speakers" },
    NameValuePair{ "Headphones", "headphones" },
};
constexpr std::array stereoEncList{
    NameValuePair{ "Default", "" },
    NameValuePair{ "Basic", "panpot" },
    NameValuePair{ "UHJ", "uhj" },
    NameValuePair{ "Binaural", "hrtf" },
};
constexpr std::array ambiFormatList{
    NameValuePair{ "Default", "" },
    NameValuePair{ "AmbiX (ACN, SN3D)", "ambix" },
    NameValuePair{ "Furse-Malham", "fuma" },
    NameValuePair{ "ACN, N3D", "acn+n3d" },
    NameValuePair{ "ACN, FuMa", "acn+fuma" },
};
constexpr std::array hrtfModeList{
    NameValuePair{ "1st Order Ambisonic", "ambi1" },
    NameValuePair{ "2nd Order Ambisonic", "ambi2" },
    NameValuePair{ "3rd Order Ambisonic", "ambi3" },
    NameValuePair{ "Default (Full)", "" },
    NameValuePair{ "Full", "full" },
};

QString getDefaultConfigName()
{
#ifdef Q_OS_WIN32
    const char *fname{"alsoft.ini"};
    auto get_appdata_path = []() noexcept -> QString
    {
        QString ret;
        WCHAR *buffer{};
        if(const HRESULT hr{SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DONT_UNEXPAND,
            nullptr, &buffer)}; SUCCEEDED(hr))
            ret = QString::fromWCharArray(buffer);
        CoTaskMemFree(buffer);
        return ret;
    };
    QString base = get_appdata_path();
#else
    const char *fname{"alsoft.conf"};
    QString base = qgetenv("XDG_CONFIG_HOME");
    if(base.isEmpty())
    {
        base = qgetenv("HOME");
        if(base.isEmpty() == false)
            base += "/.config";
    }
#endif
    if(base.isEmpty() == false)
        return base +'/'+ fname;
    return fname;
}

QString getBaseDataPath()
{
#ifdef Q_OS_WIN32
    auto get_appdata_path = []() noexcept -> QString
    {
        QString ret;
        WCHAR *buffer{};
        if(const HRESULT hr{SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DONT_UNEXPAND,
            nullptr, &buffer)}; SUCCEEDED(hr))
            ret = QString::fromWCharArray(buffer);
        CoTaskMemFree(buffer);
        return ret;
    };
    QString base = get_appdata_path();
#else
    QString base = qgetenv("XDG_DATA_HOME");
    if(base.isEmpty())
    {
        base = qgetenv("HOME");
        if(!base.isEmpty())
            base += "/.local/share";
    }
#endif
    return base;
}

QStringList getAllDataPaths(const QString &append)
{
    QStringList list;
    list.append(getBaseDataPath());
#ifdef Q_OS_WIN32
    // TODO: Common AppData path
#else
    QString paths = qgetenv("XDG_DATA_DIRS");
    if(paths.isEmpty())
        paths = "/usr/local/share/:/usr/share/";
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    list += paths.split(QChar(':'), Qt::SkipEmptyParts);
#else
    list += paths.split(QChar(':'), QString::SkipEmptyParts);
#endif
#endif
    QStringList::iterator iter = list.begin();
    while(iter != list.end())
    {
        if(iter->isEmpty())
            iter = list.erase(iter);
        else
        {
            iter->append(append);
            iter++;
        }
    }
    return list;
}

QString getValueFromName(const al::span<const NameValuePair> list, const QString &str)
{
    for(size_t i{0};i < list.size();++i)
    {
        if(str == std::data(list[i].name))
            return std::data(list[i].value);
    }
    return QString{};
}

QString getNameFromValue(const al::span<const NameValuePair> list, const QString &str)
{
    for(size_t i{0};i < list.size();++i)
    {
        if(str == std::data(list[i].value))
            return std::data(list[i].name);
    }
    return QString{};
}


Qt::CheckState getCheckState(const QVariant &var)
{
    if(var.isNull())
        return Qt::PartiallyChecked;
    if(var.toBool())
        return Qt::Checked;
    return Qt::Unchecked;
}

QString getCheckValue(const QCheckBox *checkbox)
{
    const Qt::CheckState state{checkbox->checkState()};
    if(state == Qt::Checked)
        return QString{"true"};
    if(state == Qt::Unchecked)
        return QString{"false"};
    return QString{};
}

}

MainWindow::MainWindow(QWidget *parent) : QMainWindow{parent}
    , ui{std::make_unique<Ui::MainWindow>()}
{
    ui->setupUi(this);

    for(auto &item : speakerModeList)
        ui->channelConfigCombo->addItem(std::data(item.name));
    ui->channelConfigCombo->adjustSize();
    for(auto &item : sampleTypeList)
        ui->sampleFormatCombo->addItem(std::data(item.name));
    ui->sampleFormatCombo->adjustSize();
    for(auto &item : stereoModeList)
        ui->stereoModeCombo->addItem(std::data(item.name));
    ui->stereoModeCombo->adjustSize();
    for(auto &item : stereoEncList)
        ui->stereoEncodingComboBox->addItem(std::data(item.name));
    ui->stereoEncodingComboBox->adjustSize();
    for(auto &item : ambiFormatList)
        ui->ambiFormatComboBox->addItem(std::data(item.name));
    ui->ambiFormatComboBox->adjustSize();

    ui->resamplerSlider->setRange(0, resamplerList.size()-1);
    ui->hrtfmodeSlider->setRange(0, hrtfModeList.size()-1);

#if !defined(HAVE_NEON) && !defined(HAVE_SSE)
    ui->cpuExtDisabledLabel->move(ui->cpuExtDisabledLabel->x(), ui->cpuExtDisabledLabel->y() - 60);
#else
    ui->cpuExtDisabledLabel->setVisible(false);
#endif

#ifndef HAVE_NEON

#ifndef HAVE_SSE4_1
#ifndef HAVE_SSE3
#ifndef HAVE_SSE2
#ifndef HAVE_SSE
    ui->enableSSECheckBox->setVisible(false);
#endif /* !SSE */
    ui->enableSSE2CheckBox->setVisible(false);
#endif /* !SSE2 */
    ui->enableSSE3CheckBox->setVisible(false);
#endif /* !SSE3 */
    ui->enableSSE41CheckBox->setVisible(false);
#endif /* !SSE4.1 */
    ui->enableNeonCheckBox->setVisible(false);

#else /* !Neon */

#ifndef HAVE_SSE4_1
#ifndef HAVE_SSE3
#ifndef HAVE_SSE2
#ifndef HAVE_SSE
    ui->enableNeonCheckBox->move(ui->enableNeonCheckBox->x(), ui->enableNeonCheckBox->y() - 30);
    ui->enableSSECheckBox->setVisible(false);
#endif /* !SSE */
    ui->enableSSE2CheckBox->setVisible(false);
#endif /* !SSE2 */
    ui->enableSSE3CheckBox->setVisible(false);
#endif /* !SSE3 */
    ui->enableSSE41CheckBox->setVisible(false);
#endif /* !SSE4.1 */

#endif

#ifndef ALSOFT_EAX
    ui->enableEaxCheck->setChecked(Qt::Unchecked);
    ui->enableEaxCheck->setEnabled(false);
    ui->enableEaxCheck->setVisible(false);
#endif

    mPeriodSizeValidator = std::make_unique<QIntValidator>(64, 8192, this);
    ui->periodSizeEdit->setValidator(mPeriodSizeValidator.get());
    mPeriodCountValidator = std::make_unique<QIntValidator>(2, 16, this);
    ui->periodCountEdit->setValidator(mPeriodCountValidator.get());

    mSourceCountValidator = std::make_unique<QIntValidator>(0, 4096, this);
    ui->srcCountLineEdit->setValidator(mSourceCountValidator.get());
    mEffectSlotValidator = std::make_unique<QIntValidator>(0, 64, this);
    ui->effectSlotLineEdit->setValidator(mEffectSlotValidator.get());
    mSourceSendValidator = std::make_unique<QIntValidator>(0, 16, this);
    ui->srcSendLineEdit->setValidator(mSourceSendValidator.get());
    mSampleRateValidator = std::make_unique<QIntValidator>(8000, 192000, this);
    ui->sampleRateCombo->lineEdit()->setValidator(mSampleRateValidator.get());

    mJackBufferValidator = std::make_unique<QIntValidator>(0, 8192, this);
    ui->jackBufferSizeLine->setValidator(mJackBufferValidator.get());

    connect(ui->actionLoad, &QAction::triggered, this, &MainWindow::loadConfigFromFile);
    connect(ui->actionSave_As, &QAction::triggered, this, &MainWindow::saveConfigAsFile);

    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::showAboutPage);

    connect(ui->closeCancelButton, &QPushButton::clicked, this, &MainWindow::cancelCloseAction);
    connect(ui->applyButton, &QPushButton::clicked, this, &MainWindow::saveCurrentConfig);

    auto qcb_cicint = static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged);
    connect(ui->channelConfigCombo, qcb_cicint, this, &MainWindow::enableApplyButton);
    connect(ui->sampleFormatCombo, qcb_cicint, this, &MainWindow::enableApplyButton);
    connect(ui->stereoModeCombo, qcb_cicint, this, &MainWindow::enableApplyButton);
    connect(ui->sampleRateCombo, qcb_cicint, this, &MainWindow::enableApplyButton);
    connect(ui->sampleRateCombo, &QComboBox::editTextChanged, this, &MainWindow::enableApplyButton);

    connect(ui->resamplerSlider, &QSlider::valueChanged, this, &MainWindow::updateResamplerLabel);

    connect(ui->periodSizeSlider, &QSlider::valueChanged, this, &MainWindow::updatePeriodSizeEdit);
    connect(ui->periodSizeEdit, &QLineEdit::editingFinished, this, &MainWindow::updatePeriodSizeSlider);
    connect(ui->periodCountSlider, &QSlider::valueChanged, this, &MainWindow::updatePeriodCountEdit);
    connect(ui->periodCountEdit, &QLineEdit::editingFinished, this, &MainWindow::updatePeriodCountSlider);

    connect(ui->stereoEncodingComboBox, qcb_cicint, this, &MainWindow::enableApplyButton);
    connect(ui->ambiFormatComboBox, qcb_cicint, this, &MainWindow::enableApplyButton);
    connect(ui->outputLimiterCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->outputDitherCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);

    connect(ui->decoderHQModeCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->decoderDistCompCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->decoderNFEffectsCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    auto qdsb_vcd = static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged);
    connect(ui->decoderSpeakerDistSpinBox, qdsb_vcd, this, &MainWindow::enableApplyButton);
    connect(ui->decoderQuadLineEdit, &QLineEdit::textChanged, this, &MainWindow::enableApplyButton);
    connect(ui->decoderQuadButton, &QPushButton::clicked, this, &MainWindow::selectQuadDecoderFile);
    connect(ui->decoder51LineEdit, &QLineEdit::textChanged, this, &MainWindow::enableApplyButton);
    connect(ui->decoder51Button, &QPushButton::clicked, this, &MainWindow::select51DecoderFile);
    connect(ui->decoder61LineEdit, &QLineEdit::textChanged, this, &MainWindow::enableApplyButton);
    connect(ui->decoder61Button, &QPushButton::clicked, this, &MainWindow::select61DecoderFile);
    connect(ui->decoder71LineEdit, &QLineEdit::textChanged, this, &MainWindow::enableApplyButton);
    connect(ui->decoder71Button, &QPushButton::clicked, this, &MainWindow::select71DecoderFile);
    connect(ui->decoder3D71LineEdit, &QLineEdit::textChanged, this, &MainWindow::enableApplyButton);
    connect(ui->decoder3D71Button, &QPushButton::clicked, this, &MainWindow::select3D71DecoderFile);

    connect(ui->preferredHrtfComboBox, qcb_cicint, this, &MainWindow::enableApplyButton);
    connect(ui->hrtfmodeSlider, &QSlider::valueChanged, this, &MainWindow::updateHrtfModeLabel);

    connect(ui->hrtfAddButton, &QPushButton::clicked, this, &MainWindow::addHrtfFile);
    connect(ui->hrtfRemoveButton, &QPushButton::clicked, this, &MainWindow::removeHrtfFile);
    connect(ui->hrtfFileList, &QListWidget::itemSelectionChanged, this, &MainWindow::updateHrtfRemoveButton);
    connect(ui->defaultHrtfPathsCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);

    connect(ui->srcCountLineEdit, &QLineEdit::editingFinished, this, &MainWindow::enableApplyButton);
    connect(ui->srcSendLineEdit, &QLineEdit::editingFinished, this, &MainWindow::enableApplyButton);
    connect(ui->effectSlotLineEdit, &QLineEdit::editingFinished, this, &MainWindow::enableApplyButton);

    connect(ui->enableSSECheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableSSE2CheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableSSE3CheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableSSE41CheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableNeonCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);

    ui->enabledBackendList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->enabledBackendList, &QListWidget::customContextMenuRequested, this, &MainWindow::showEnabledBackendMenu);

    ui->disabledBackendList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->disabledBackendList, &QListWidget::customContextMenuRequested, this, &MainWindow::showDisabledBackendMenu);
    connect(ui->backendCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);

    connect(ui->defaultReverbComboBox, qcb_cicint, this, &MainWindow::enableApplyButton);
    connect(ui->enableEaxReverbCheck, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableStdReverbCheck, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableAutowahCheck, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableChorusCheck, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableCompressorCheck, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableDistortionCheck, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableEchoCheck, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableEqualizerCheck, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableFlangerCheck, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableFrequencyShifterCheck, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableModulatorCheck, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableDedicatedCheck, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->enablePitchShifterCheck, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableVocalMorpherCheck, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableEaxCheck, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);

    connect(ui->pulseAutospawnCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->pulseAllowMovesCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->pulseFixRateCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->pulseAdjLatencyCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);

    connect(ui->pwireAssumeAudioCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->pwireRtMixCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);

    connect(ui->wasapiResamplerCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);

    connect(ui->jackAutospawnCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->jackConnectPortsCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->jackRtMixCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->jackBufferSizeSlider, &QSlider::valueChanged, this, &MainWindow::updateJackBufferSizeEdit);
    connect(ui->jackBufferSizeLine, &QLineEdit::editingFinished, this, &MainWindow::updateJackBufferSizeSlider);

    connect(ui->alsaDefaultDeviceLine, &QLineEdit::textChanged, this, &MainWindow::enableApplyButton);
    connect(ui->alsaDefaultCaptureLine, &QLineEdit::textChanged, this, &MainWindow::enableApplyButton);
    connect(ui->alsaResamplerCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);
    connect(ui->alsaMmapCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);

    connect(ui->ossDefaultDeviceLine, &QLineEdit::textChanged, this, &MainWindow::enableApplyButton);
    connect(ui->ossPlaybackPushButton, &QPushButton::clicked, this, &MainWindow::selectOSSPlayback);
    connect(ui->ossDefaultCaptureLine, &QLineEdit::textChanged, this, &MainWindow::enableApplyButton);
    connect(ui->ossCapturePushButton, &QPushButton::clicked, this, &MainWindow::selectOSSCapture);

    connect(ui->solarisDefaultDeviceLine, &QLineEdit::textChanged, this, &MainWindow::enableApplyButton);
    connect(ui->solarisPlaybackPushButton, &QPushButton::clicked, this, &MainWindow::selectSolarisPlayback);

    connect(ui->waveOutputLine, &QLineEdit::textChanged, this, &MainWindow::enableApplyButton);
    connect(ui->waveOutputButton, &QPushButton::clicked, this, &MainWindow::selectWaveOutput);
    connect(ui->waveBFormatCheckBox, &QCheckBox::stateChanged, this, &MainWindow::enableApplyButton);

    ui->backendListWidget->setCurrentRow(0);
    ui->tabWidget->setCurrentIndex(0);

    for(int i = 1;i < ui->backendListWidget->count();i++)
        ui->backendListWidget->setRowHidden(i, true);
    for(size_t i{0};i < backendList.size();++i)
    {
        QList<QListWidgetItem*> items = ui->backendListWidget->findItems(
            std::data(backendList[i].full_string), Qt::MatchFixedString);
        foreach(QListWidgetItem *item, items)
            item->setHidden(false);
    }

    loadConfig(getDefaultConfigName());
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *event)
{
    if(!mNeedsSave)
        event->accept();
    else
    {
        QMessageBox::StandardButton btn = QMessageBox::warning(this,
            tr("Apply changes?"), tr("Save changes before quitting?"),
            QMessageBox::Save | QMessageBox::No | QMessageBox::Cancel);
        if(btn == QMessageBox::Save)
            saveCurrentConfig();
        if(btn == QMessageBox::Cancel)
            event->ignore();
        else
            event->accept();
    }
}

void MainWindow::cancelCloseAction()
{
    mNeedsSave = false;
    close();
}


void MainWindow::showAboutPage()
{
    QMessageBox::information(this, tr("About"),
        tr("OpenAL Soft Configuration Utility.\nBuilt for OpenAL Soft library version ") +
        GetVersionString());
}


QStringList MainWindow::collectHrtfs()
{
    QStringList ret;
    QStringList processed;

    for(int i = 0;i < ui->hrtfFileList->count();i++)
    {
        QDir dir(ui->hrtfFileList->item(i)->text());
        QStringList fnames = dir.entryList(QDir::Files | QDir::Readable, QDir::Name);
        foreach(const QString &fname, fnames)
        {
            if(!fname.endsWith(".mhr", Qt::CaseInsensitive))
                continue;
            QString fullname{dir.absoluteFilePath(fname)};
            if(processed.contains(fullname))
                continue;
            processed.push_back(fullname);

            QString name{fname.left(fname.length()-4)};
            if(!ret.contains(name))
                ret.push_back(name);
            else
            {
                size_t i{2};
                do {
                    QString s = name+" #"+QString::number(i);
                    if(!ret.contains(s))
                    {
                        ret.push_back(s);
                        break;
                    }
                    ++i;
                } while(true);
            }
        }
    }

    if(ui->defaultHrtfPathsCheckBox->isChecked())
    {
        QStringList paths = getAllDataPaths("/openal/hrtf");
        foreach(const QString &name, paths)
        {
            QDir dir{name};
            QStringList fnames{dir.entryList(QDir::Files | QDir::Readable, QDir::Name)};
            foreach(const QString &fname, fnames)
            {
                if(!fname.endsWith(".mhr", Qt::CaseInsensitive))
                    continue;
                QString fullname{dir.absoluteFilePath(fname)};
                if(processed.contains(fullname))
                    continue;
                processed.push_back(fullname);

                QString name{fname.left(fname.length()-4)};
                if(!ret.contains(name))
                    ret.push_back(name);
                else
                {
                    size_t i{2};
                    do {
                        QString s{name+" #"+QString::number(i)};
                        if(!ret.contains(s))
                        {
                            ret.push_back(s);
                            break;
                        }
                        ++i;
                    } while(true);
                }
            }
        }

#ifdef ALSOFT_EMBED_HRTF_DATA
        ret.push_back("Built-In HRTF");
#endif
    }
    return ret;
}


void MainWindow::loadConfigFromFile()
{
    QString fname = QFileDialog::getOpenFileName(this, tr("Select Files"));
    if(fname.isEmpty() == false)
        loadConfig(fname);
}

void MainWindow::loadConfig(const QString &fname)
{
    QSettings settings{fname, QSettings::IniFormat};

    QString sampletype = settings.value("sample-type").toString();
    ui->sampleFormatCombo->setCurrentIndex(0);
    if(sampletype.isEmpty() == false)
    {
        QString str{getNameFromValue(sampleTypeList, sampletype)};
        if(!str.isEmpty())
        {
            const int j{ui->sampleFormatCombo->findText(str)};
            if(j > 0) ui->sampleFormatCombo->setCurrentIndex(j);
        }
    }

    QString channelconfig{settings.value("channels").toString()};
    ui->channelConfigCombo->setCurrentIndex(0);
    if(channelconfig.isEmpty() == false)
    {
        if(channelconfig == "surround51rear")
            channelconfig = "surround51";
        QString str{getNameFromValue(speakerModeList, channelconfig)};
        if(!str.isEmpty())
        {
            const int j{ui->channelConfigCombo->findText(str)};
            if(j > 0) ui->channelConfigCombo->setCurrentIndex(j);
        }
    }

    QString srate{settings.value("frequency").toString()};
    if(srate.isEmpty())
        ui->sampleRateCombo->setCurrentIndex(0);
    else
    {
        ui->sampleRateCombo->lineEdit()->clear();
        ui->sampleRateCombo->lineEdit()->insert(srate);
    }

    ui->srcCountLineEdit->clear();
    ui->srcCountLineEdit->insert(settings.value("sources").toString());
    ui->effectSlotLineEdit->clear();
    ui->effectSlotLineEdit->insert(settings.value("slots").toString());
    ui->srcSendLineEdit->clear();
    ui->srcSendLineEdit->insert(settings.value("sends").toString());

    QString resampler = settings.value("resampler").toString().trimmed();
    ui->resamplerSlider->setValue(2);
    ui->resamplerLabel->setText(std::data(resamplerList[2].name));
    /* The "sinc4" and "sinc8" resamplers are no longer supported. Use "cubic"
     * as a fallback.
     */
    if(resampler == "sinc4" || resampler == "sinc8")
        resampler = "cubic";
    /* The "bsinc" resampler name is an alias for "bsinc12". */
    else if(resampler == "bsinc")
        resampler = "bsinc12";
    for(int i = 0;resamplerList[i].name[0];i++)
    {
        if(resampler == std::data(resamplerList[i].value))
        {
            ui->resamplerSlider->setValue(i);
            ui->resamplerLabel->setText(std::data(resamplerList[i].name));
            break;
        }
    }

    QString stereomode = settings.value("stereo-mode").toString().trimmed();
    ui->stereoModeCombo->setCurrentIndex(0);
    if(stereomode.isEmpty() == false)
    {
        QString str{getNameFromValue(stereoModeList, stereomode)};
        if(!str.isEmpty())
        {
            const int j{ui->stereoModeCombo->findText(str)};
            if(j > 0) ui->stereoModeCombo->setCurrentIndex(j);
        }
    }

    int periodsize{settings.value("period_size").toInt()};
    ui->periodSizeEdit->clear();
    if(periodsize >= 64)
    {
        ui->periodSizeEdit->insert(QString::number(periodsize));
        updatePeriodSizeSlider();
    }

    int periodcount{settings.value("periods").toInt()};
    ui->periodCountEdit->clear();
    if(periodcount >= 2)
    {
        ui->periodCountEdit->insert(QString::number(periodcount));
        updatePeriodCountSlider();
    }

    ui->outputLimiterCheckBox->setCheckState(getCheckState(settings.value("output-limiter")));
    ui->outputDitherCheckBox->setCheckState(getCheckState(settings.value("dither")));

    QString stereopan{settings.value("stereo-encoding").toString()};
    ui->stereoEncodingComboBox->setCurrentIndex(0);
    if(stereopan.isEmpty() == false)
    {
        QString str{getNameFromValue(stereoEncList, stereopan)};
        if(!str.isEmpty())
        {
            const int j{ui->stereoEncodingComboBox->findText(str)};
            if(j > 0) ui->stereoEncodingComboBox->setCurrentIndex(j);
        }
    }

    QString ambiformat{settings.value("ambi-format").toString()};
    ui->ambiFormatComboBox->setCurrentIndex(0);
    if(ambiformat.isEmpty() == false)
    {
        QString str{getNameFromValue(ambiFormatList, ambiformat)};
        if(!str.isEmpty())
        {
            const int j{ui->ambiFormatComboBox->findText(str)};
            if(j > 0) ui->ambiFormatComboBox->setCurrentIndex(j);
        }
    }

    ui->decoderHQModeCheckBox->setChecked(getCheckState(settings.value("decoder/hq-mode")));
    ui->decoderDistCompCheckBox->setCheckState(getCheckState(settings.value("decoder/distance-comp")));
    ui->decoderNFEffectsCheckBox->setCheckState(getCheckState(settings.value("decoder/nfc")));
    double speakerdist{settings.value("decoder/speaker-dist", 1.0).toDouble()};
    ui->decoderSpeakerDistSpinBox->setValue(speakerdist);

    ui->decoderQuadLineEdit->setText(settings.value("decoder/quad").toString());
    ui->decoder51LineEdit->setText(settings.value("decoder/surround51").toString());
    ui->decoder61LineEdit->setText(settings.value("decoder/surround61").toString());
    ui->decoder71LineEdit->setText(settings.value("decoder/surround71").toString());
    ui->decoder3D71LineEdit->setText(settings.value("decoder/surround3d71").toString());

    QStringList disabledCpuExts{settings.value("disable-cpu-exts").toStringList()};
    if(disabledCpuExts.size() == 1)
        disabledCpuExts = disabledCpuExts[0].split(QChar(','));
    for(QString &name : disabledCpuExts)
        name = name.trimmed();
    ui->enableSSECheckBox->setChecked(!disabledCpuExts.contains("sse", Qt::CaseInsensitive));
    ui->enableSSE2CheckBox->setChecked(!disabledCpuExts.contains("sse2", Qt::CaseInsensitive));
    ui->enableSSE3CheckBox->setChecked(!disabledCpuExts.contains("sse3", Qt::CaseInsensitive));
    ui->enableSSE41CheckBox->setChecked(!disabledCpuExts.contains("sse4.1", Qt::CaseInsensitive));
    ui->enableNeonCheckBox->setChecked(!disabledCpuExts.contains("neon", Qt::CaseInsensitive));

    QString hrtfmode{settings.value("hrtf-mode").toString().trimmed()};
    ui->hrtfmodeSlider->setValue(2);
    ui->hrtfmodeLabel->setText(std::data(hrtfModeList[3].name));
    /* The "basic" mode name is no longer supported. Use "ambi2" instead. */
    if(hrtfmode == "basic")
        hrtfmode = "ambi2";
    for(size_t i{0};i < hrtfModeList.size();++i)
    {
        if(hrtfmode == std::data(hrtfModeList[i].value))
        {
            ui->hrtfmodeSlider->setValue(static_cast<int>(i));
            ui->hrtfmodeLabel->setText(std::data(hrtfModeList[i].name));
            break;
        }
    }

    QStringList hrtf_paths{settings.value("hrtf-paths").toStringList()};
    if(hrtf_paths.size() == 1)
        hrtf_paths = hrtf_paths[0].split(QChar(','));
    for(QString &name : hrtf_paths)
        name = name.trimmed();
    if(!hrtf_paths.empty() && !hrtf_paths.back().isEmpty())
        ui->defaultHrtfPathsCheckBox->setCheckState(Qt::Unchecked);
    else
    {
        hrtf_paths.removeAll(QString());
        ui->defaultHrtfPathsCheckBox->setCheckState(Qt::Checked);
    }
    hrtf_paths.removeDuplicates();
    ui->hrtfFileList->clear();
    ui->hrtfFileList->addItems(hrtf_paths);
    updateHrtfRemoveButton();

    ui->preferredHrtfComboBox->clear();
    ui->preferredHrtfComboBox->addItem("- Any -");
    if(ui->defaultHrtfPathsCheckBox->isChecked())
    {
        QStringList hrtfs{collectHrtfs()};
        foreach(const QString &name, hrtfs)
            ui->preferredHrtfComboBox->addItem(name);
    }

    QString defaulthrtf{settings.value("default-hrtf").toString()};
    ui->preferredHrtfComboBox->setCurrentIndex(0);
    if(defaulthrtf.isEmpty() == false)
    {
        int i{ui->preferredHrtfComboBox->findText(defaulthrtf)};
        if(i > 0)
            ui->preferredHrtfComboBox->setCurrentIndex(i);
        else
        {
            i = ui->preferredHrtfComboBox->count();
            ui->preferredHrtfComboBox->addItem(defaulthrtf);
            ui->preferredHrtfComboBox->setCurrentIndex(i);
        }
    }
    ui->preferredHrtfComboBox->adjustSize();

    ui->enabledBackendList->clear();
    ui->disabledBackendList->clear();
    QStringList drivers{settings.value("drivers").toStringList()};
    if(drivers.empty())
        ui->backendCheckBox->setChecked(true);
    else
    {
        if(drivers.size() == 1)
            drivers = drivers[0].split(QChar(','));
        for(QString &name : drivers)
        {
            name = name.trimmed();
            /* Convert "mmdevapi" references to "wasapi" for backwards
             * compatibility.
             */
            if(name == "-mmdevapi")
                name = "-wasapi";
            else if(name == "mmdevapi")
                name = "wasapi";
        }

        bool lastWasEmpty = false;
        foreach(const QString &backend, drivers)
        {
            lastWasEmpty = backend.isEmpty();
            if(lastWasEmpty) continue;

            if(!backend.startsWith(QChar('-')))
            {
                for(size_t j{0};j < backendList.size();++j)
                {
                    if(backend == std::data(backendList[j].backend_name))
                    {
                        ui->enabledBackendList->addItem(std::data(backendList[j].full_string));
                        break;
                    }
                }
            }
            else if(backend.size() > 1)
            {
                QStringRef backendref{backend.rightRef(backend.size()-1)};
                for(size_t j{0};j < backendList.size();++j)
                {
                    if(backendref == std::data(backendList[j].backend_name))
                    {
                        ui->disabledBackendList->addItem(std::data(backendList[j].full_string));
                        break;
                    }
                }
            }
        }
        ui->backendCheckBox->setChecked(lastWasEmpty);
    }

    QString defaultreverb{settings.value("default-reverb").toString().toLower()};
    ui->defaultReverbComboBox->setCurrentIndex(0);
    if(defaultreverb.isEmpty() == false)
    {
        for(int i = 0;i < ui->defaultReverbComboBox->count();i++)
        {
            if(defaultreverb.compare(ui->defaultReverbComboBox->itemText(i).toLower()) == 0)
            {
                ui->defaultReverbComboBox->setCurrentIndex(i);
                break;
            }
        }
    }

    QStringList excludefx{settings.value("excludefx").toStringList()};
    if(excludefx.size() == 1)
        excludefx = excludefx[0].split(QChar(','));
    for(QString &name : excludefx)
        name = name.trimmed();
    ui->enableEaxReverbCheck->setChecked(!excludefx.contains("eaxreverb", Qt::CaseInsensitive));
    ui->enableStdReverbCheck->setChecked(!excludefx.contains("reverb", Qt::CaseInsensitive));
    ui->enableAutowahCheck->setChecked(!excludefx.contains("autowah", Qt::CaseInsensitive));
    ui->enableChorusCheck->setChecked(!excludefx.contains("chorus", Qt::CaseInsensitive));
    ui->enableCompressorCheck->setChecked(!excludefx.contains("compressor", Qt::CaseInsensitive));
    ui->enableDistortionCheck->setChecked(!excludefx.contains("distortion", Qt::CaseInsensitive));
    ui->enableEchoCheck->setChecked(!excludefx.contains("echo", Qt::CaseInsensitive));
    ui->enableEqualizerCheck->setChecked(!excludefx.contains("equalizer", Qt::CaseInsensitive));
    ui->enableFlangerCheck->setChecked(!excludefx.contains("flanger", Qt::CaseInsensitive));
    ui->enableFrequencyShifterCheck->setChecked(!excludefx.contains("fshifter", Qt::CaseInsensitive));
    ui->enableModulatorCheck->setChecked(!excludefx.contains("modulator", Qt::CaseInsensitive));
    ui->enableDedicatedCheck->setChecked(!excludefx.contains("dedicated", Qt::CaseInsensitive));
    ui->enablePitchShifterCheck->setChecked(!excludefx.contains("pshifter", Qt::CaseInsensitive));
    ui->enableVocalMorpherCheck->setChecked(!excludefx.contains("vmorpher", Qt::CaseInsensitive));
    if(ui->enableEaxCheck->isEnabled())
        ui->enableEaxCheck->setChecked(getCheckState(settings.value("eax/enable")) != Qt::Unchecked);

    ui->pulseAutospawnCheckBox->setCheckState(getCheckState(settings.value("pulse/spawn-server")));
    ui->pulseAllowMovesCheckBox->setCheckState(getCheckState(settings.value("pulse/allow-moves")));
    ui->pulseFixRateCheckBox->setCheckState(getCheckState(settings.value("pulse/fix-rate")));
    ui->pulseAdjLatencyCheckBox->setCheckState(getCheckState(settings.value("pulse/adjust-latency")));

    ui->pwireAssumeAudioCheckBox->setCheckState(getCheckState(settings.value("pipewire/assume-audio")));
    ui->pwireRtMixCheckBox->setCheckState(getCheckState(settings.value("pipewire/rt-mix")));

    ui->wasapiResamplerCheckBox->setCheckState(getCheckState(settings.value("wasapi/allow-resampler")));

    ui->jackAutospawnCheckBox->setCheckState(getCheckState(settings.value("jack/spawn-server")));
    ui->jackConnectPortsCheckBox->setCheckState(getCheckState(settings.value("jack/connect-ports")));
    ui->jackRtMixCheckBox->setCheckState(getCheckState(settings.value("jack/rt-mix")));
    ui->jackBufferSizeLine->setText(settings.value("jack/buffer-size", QString()).toString());
    updateJackBufferSizeSlider();

    ui->alsaDefaultDeviceLine->setText(settings.value("alsa/device", QString()).toString());
    ui->alsaDefaultCaptureLine->setText(settings.value("alsa/capture", QString()).toString());
    ui->alsaResamplerCheckBox->setCheckState(getCheckState(settings.value("alsa/allow-resampler")));
    ui->alsaMmapCheckBox->setCheckState(getCheckState(settings.value("alsa/mmap")));

    ui->ossDefaultDeviceLine->setText(settings.value("oss/device", QString()).toString());
    ui->ossDefaultCaptureLine->setText(settings.value("oss/capture", QString()).toString());

    ui->solarisDefaultDeviceLine->setText(settings.value("solaris/device", QString()).toString());

    ui->waveOutputLine->setText(settings.value("wave/file", QString()).toString());
    ui->waveBFormatCheckBox->setChecked(settings.value("wave/bformat", false).toBool());

    ui->applyButton->setEnabled(false);
    ui->closeCancelButton->setText(tr("Close"));
    mNeedsSave = false;
}

void MainWindow::saveCurrentConfig()
{
    saveConfig(getDefaultConfigName());
    ui->applyButton->setEnabled(false);
    ui->closeCancelButton->setText(tr("Close"));
    mNeedsSave = false;
    QMessageBox::information(this, tr("Information"),
        tr("Applications using OpenAL need to be restarted for changes to take effect."));
}

void MainWindow::saveConfigAsFile()
{
    QString fname{QFileDialog::getOpenFileName(this, tr("Select Files"))};
    if(fname.isEmpty() == false)
    {
        saveConfig(fname);
        ui->applyButton->setEnabled(false);
        mNeedsSave = false;
    }
}

void MainWindow::saveConfig(const QString &fname) const
{
    QSettings settings{fname, QSettings::IniFormat};

    /* HACK: Compound any stringlist values into a comma-separated string. */
    QStringList allkeys{settings.allKeys()};
    foreach(const QString &key, allkeys)
    {
        QStringList vals{settings.value(key).toStringList()};
        if(vals.size() > 1)
            settings.setValue(key, vals.join(QChar(',')));
    }

    settings.setValue("sample-type", getValueFromName(sampleTypeList, ui->sampleFormatCombo->currentText()));
    settings.setValue("channels", getValueFromName(speakerModeList, ui->channelConfigCombo->currentText()));

    uint rate{ui->sampleRateCombo->currentText().toUInt()};
    if(rate <= 0)
        settings.setValue("frequency", QString{});
    else
        settings.setValue("frequency", rate);

    settings.setValue("period_size", ui->periodSizeEdit->text());
    settings.setValue("periods", ui->periodCountEdit->text());

    settings.setValue("sources", ui->srcCountLineEdit->text());
    settings.setValue("slots", ui->effectSlotLineEdit->text());

    settings.setValue("resampler", std::data(resamplerList[ui->resamplerSlider->value()].value));

    settings.setValue("stereo-mode", getValueFromName(stereoModeList, ui->stereoModeCombo->currentText()));
    settings.setValue("stereo-encoding", getValueFromName(stereoEncList, ui->stereoEncodingComboBox->currentText()));
    settings.setValue("ambi-format", getValueFromName(ambiFormatList, ui->ambiFormatComboBox->currentText()));

    settings.setValue("output-limiter", getCheckValue(ui->outputLimiterCheckBox));
    settings.setValue("dither", getCheckValue(ui->outputDitherCheckBox));

    settings.setValue("decoder/hq-mode", getCheckValue(ui->decoderHQModeCheckBox));
    settings.setValue("decoder/distance-comp", getCheckValue(ui->decoderDistCompCheckBox));
    settings.setValue("decoder/nfc", getCheckValue(ui->decoderNFEffectsCheckBox));
    double speakerdist{ui->decoderSpeakerDistSpinBox->value()};
    settings.setValue("decoder/speaker-dist",
        (speakerdist != 1.0) ? QString::number(speakerdist) : QString{}
    );

    settings.setValue("decoder/quad", ui->decoderQuadLineEdit->text());
    settings.setValue("decoder/surround51", ui->decoder51LineEdit->text());
    settings.setValue("decoder/surround61", ui->decoder61LineEdit->text());
    settings.setValue("decoder/surround71", ui->decoder71LineEdit->text());
    settings.setValue("decoder/surround3d71", ui->decoder3D71LineEdit->text());

    QStringList strlist;
    if(!ui->enableSSECheckBox->isChecked())
        strlist.append("sse");
    if(!ui->enableSSE2CheckBox->isChecked())
        strlist.append("sse2");
    if(!ui->enableSSE3CheckBox->isChecked())
        strlist.append("sse3");
    if(!ui->enableSSE41CheckBox->isChecked())
        strlist.append("sse4.1");
    if(!ui->enableNeonCheckBox->isChecked())
        strlist.append("neon");
    settings.setValue("disable-cpu-exts", strlist.join(QChar(',')));

    settings.setValue("hrtf-mode", std::data(hrtfModeList[ui->hrtfmodeSlider->value()].value));

    if(ui->preferredHrtfComboBox->currentIndex() == 0)
        settings.setValue("default-hrtf", QString{});
    else
    {
        QString str{ui->preferredHrtfComboBox->currentText()};
        settings.setValue("default-hrtf", str);
    }

    strlist.clear();
    strlist.reserve(ui->hrtfFileList->count());
    for(int i = 0;i < ui->hrtfFileList->count();i++)
        strlist.append(ui->hrtfFileList->item(i)->text());
    if(!strlist.empty() && ui->defaultHrtfPathsCheckBox->isChecked())
        strlist.append(QString{});
    settings.setValue("hrtf-paths", strlist.join(QChar{','}));

    strlist.clear();
    for(int i = 0;i < ui->enabledBackendList->count();i++)
    {
        QString label{ui->enabledBackendList->item(i)->text()};
        for(size_t j{0};j < backendList.size();++j)
        {
            if(label == std::data(backendList[j].full_string))
            {
                strlist.append(std::data(backendList[j].backend_name));
                break;
            }
        }
    }
    for(int i = 0;i < ui->disabledBackendList->count();i++)
    {
        QString label{ui->disabledBackendList->item(i)->text()};
        for(size_t j{0};j < backendList.size();++j)
        {
            if(label == std::data(backendList[j].full_string))
            {
                strlist.append(QChar{'-'}+QString{std::data(backendList[j].backend_name)});
                break;
            }
        }
    }
    if(strlist.empty() && !ui->backendCheckBox->isChecked())
        strlist.append("-all");
    else if(ui->backendCheckBox->isChecked())
        strlist.append(QString{});
    settings.setValue("drivers", strlist.join(QChar(',')));

    // TODO: Remove check when we can properly match global values.
    if(ui->defaultReverbComboBox->currentIndex() == 0)
        settings.setValue("default-reverb", QString{});
    else
    {
        QString str{ui->defaultReverbComboBox->currentText().toLower()};
        settings.setValue("default-reverb", str);
    }

    strlist.clear();
    if(!ui->enableEaxReverbCheck->isChecked())
        strlist.append("eaxreverb");
    if(!ui->enableStdReverbCheck->isChecked())
        strlist.append("reverb");
    if(!ui->enableAutowahCheck->isChecked())
        strlist.append("autowah");
    if(!ui->enableChorusCheck->isChecked())
        strlist.append("chorus");
    if(!ui->enableDistortionCheck->isChecked())
        strlist.append("distortion");
    if(!ui->enableCompressorCheck->isChecked())
        strlist.append("compressor");
    if(!ui->enableEchoCheck->isChecked())
        strlist.append("echo");
    if(!ui->enableEqualizerCheck->isChecked())
        strlist.append("equalizer");
    if(!ui->enableFlangerCheck->isChecked())
        strlist.append("flanger");
    if(!ui->enableFrequencyShifterCheck->isChecked())
        strlist.append("fshifter");
    if(!ui->enableModulatorCheck->isChecked())
        strlist.append("modulator");
    if(!ui->enableDedicatedCheck->isChecked())
        strlist.append("dedicated");
    if(!ui->enablePitchShifterCheck->isChecked())
        strlist.append("pshifter");
    if(!ui->enableVocalMorpherCheck->isChecked())
        strlist.append("vmorpher");
    settings.setValue("excludefx", strlist.join(QChar{','}));
    settings.setValue("eax/enable",
        (!ui->enableEaxCheck->isEnabled() || ui->enableEaxCheck->isChecked())
        ? QString{/*"true"*/} : QString{"false"});

    settings.setValue("pipewire/assume-audio", getCheckValue(ui->pwireAssumeAudioCheckBox));
    settings.setValue("pipewire/rt-mix", getCheckValue(ui->pwireRtMixCheckBox));

    settings.setValue("wasapi/allow-resampler", getCheckValue(ui->wasapiResamplerCheckBox));

    settings.setValue("pulse/spawn-server", getCheckValue(ui->pulseAutospawnCheckBox));
    settings.setValue("pulse/allow-moves", getCheckValue(ui->pulseAllowMovesCheckBox));
    settings.setValue("pulse/fix-rate", getCheckValue(ui->pulseFixRateCheckBox));
    settings.setValue("pulse/adjust-latency", getCheckValue(ui->pulseAdjLatencyCheckBox));

    settings.setValue("jack/spawn-server", getCheckValue(ui->jackAutospawnCheckBox));
    settings.setValue("jack/connect-ports", getCheckValue(ui->jackConnectPortsCheckBox));
    settings.setValue("jack/rt-mix", getCheckValue(ui->jackRtMixCheckBox));
    settings.setValue("jack/buffer-size", ui->jackBufferSizeLine->text());

    settings.setValue("alsa/device", ui->alsaDefaultDeviceLine->text());
    settings.setValue("alsa/capture", ui->alsaDefaultCaptureLine->text());
    settings.setValue("alsa/allow-resampler", getCheckValue(ui->alsaResamplerCheckBox));
    settings.setValue("alsa/mmap", getCheckValue(ui->alsaMmapCheckBox));

    settings.setValue("oss/device", ui->ossDefaultDeviceLine->text());
    settings.setValue("oss/capture", ui->ossDefaultCaptureLine->text());

    settings.setValue("solaris/device", ui->solarisDefaultDeviceLine->text());

    settings.setValue("wave/file", ui->waveOutputLine->text());
    settings.setValue("wave/bformat",
        ui->waveBFormatCheckBox->isChecked() ? QString{"true"} : QString{/*"false"*/}
    );

    /* Remove empty keys
     * FIXME: Should only remove keys whose value matches the globally-specified value.
     */
    allkeys = settings.allKeys();
    foreach(const QString &key, allkeys)
    {
        QString str{settings.value(key).toString()};
        if(str == QString{})
            settings.remove(key);
    }
}


void MainWindow::enableApplyButton()
{
    if(!mNeedsSave)
        ui->applyButton->setEnabled(true);
    mNeedsSave = true;
    ui->closeCancelButton->setText(tr("Cancel"));
}


void MainWindow::updateResamplerLabel(int num)
{
    ui->resamplerLabel->setText(std::data(resamplerList[num].name));
    enableApplyButton();
}


void MainWindow::updatePeriodSizeEdit(int size)
{
    ui->periodSizeEdit->clear();
    if(size >= 64)
        ui->periodSizeEdit->insert(QString::number(size));
    enableApplyButton();
}

void MainWindow::updatePeriodSizeSlider()
{
    int pos = ui->periodSizeEdit->text().toInt();
    if(pos >= 64)
    {
        if(pos > 8192)
            pos = 8192;
        ui->periodSizeSlider->setSliderPosition(pos);
    }
    enableApplyButton();
}

void MainWindow::updatePeriodCountEdit(int count)
{
    ui->periodCountEdit->clear();
    if(count >= 2)
        ui->periodCountEdit->insert(QString::number(count));
    enableApplyButton();
}

void MainWindow::updatePeriodCountSlider()
{
    int pos = ui->periodCountEdit->text().toInt();
    if(pos < 2)
        pos = 0;
    else if(pos > 16)
        pos = 16;
    ui->periodCountSlider->setSliderPosition(pos);
    enableApplyButton();
}


void MainWindow::selectQuadDecoderFile()
{ selectDecoderFile(ui->decoderQuadLineEdit, "Select Quadraphonic Decoder");}
void MainWindow::select51DecoderFile()
{ selectDecoderFile(ui->decoder51LineEdit, "Select 5.1 Surround Decoder");}
void MainWindow::select61DecoderFile()
{ selectDecoderFile(ui->decoder61LineEdit, "Select 6.1 Surround Decoder");}
void MainWindow::select71DecoderFile()
{ selectDecoderFile(ui->decoder71LineEdit, "Select 7.1 Surround Decoder");}
void MainWindow::select3D71DecoderFile()
{ selectDecoderFile(ui->decoder3D71LineEdit, "Select 3D7.1 Surround Decoder");}
void MainWindow::selectDecoderFile(QLineEdit *line, const char *caption)
{
    QString dir{line->text()};
    if(dir.isEmpty() || QDir::isRelativePath(dir))
    {
        QStringList paths{getAllDataPaths("/openal/presets")};
        while(!paths.isEmpty())
        {
            if(QDir{paths.last()}.exists())
            {
                dir = paths.last();
                break;
            }
            paths.removeLast();
        }
    }
    QString fname{QFileDialog::getOpenFileName(this, tr(caption),
        dir, tr("AmbDec Files (*.ambdec);;All Files (*.*)"))};
    if(!fname.isEmpty())
    {
        line->setText(fname);
        enableApplyButton();
    }
}


void MainWindow::updateJackBufferSizeEdit(int size)
{
    ui->jackBufferSizeLine->clear();
    if(size > 0)
        ui->jackBufferSizeLine->insert(QString::number(1<<size));
    enableApplyButton();
}

void MainWindow::updateJackBufferSizeSlider()
{
    int value{ui->jackBufferSizeLine->text().toInt()};
    auto pos = static_cast<int>(floor(log2(value) + 0.5));
    ui->jackBufferSizeSlider->setSliderPosition(pos);
    enableApplyButton();
}


void MainWindow::updateHrtfModeLabel(int num)
{
    ui->hrtfmodeLabel->setText(std::data(hrtfModeList[static_cast<uint>(num)].name));
    enableApplyButton();
}


void MainWindow::addHrtfFile()
{
    QString path{QFileDialog::getExistingDirectory(this, tr("Select HRTF Path"))};
    if(path.isEmpty() == false && !getAllDataPaths("/openal/hrtf").contains(path))
    {
        ui->hrtfFileList->addItem(path);
        enableApplyButton();
    }
}

void MainWindow::removeHrtfFile()
{
    QList<gsl::owner<QListWidgetItem*>> selected{ui->hrtfFileList->selectedItems()};
    if(!selected.isEmpty())
    {
        std::for_each(selected.begin(), selected.end(), std::default_delete<QListWidgetItem>{});
        enableApplyButton();
    }
}

void MainWindow::updateHrtfRemoveButton()
{
    ui->hrtfRemoveButton->setEnabled(!ui->hrtfFileList->selectedItems().empty());
}

void MainWindow::showEnabledBackendMenu(QPoint pt)
{
    QHash<QAction*,QString> actionMap;

    pt = ui->enabledBackendList->mapToGlobal(pt);

    QMenu ctxmenu;
    QAction *removeAction{ctxmenu.addAction(QIcon::fromTheme("list-remove"), "Remove")};
    if(ui->enabledBackendList->selectedItems().empty())
        removeAction->setEnabled(false);
    ctxmenu.addSeparator();
    for(size_t i{0};i < backendList.size();++i)
    {
        QString backend{std::data(backendList[i].full_string)};
        QAction *action{ctxmenu.addAction(QString("Add ")+backend)};
        actionMap[action] = backend;
        if(!ui->enabledBackendList->findItems(backend, Qt::MatchFixedString).empty() ||
           !ui->disabledBackendList->findItems(backend, Qt::MatchFixedString).empty())
            action->setEnabled(false);
    }

    QAction *gotAction{ctxmenu.exec(pt)};
    if(gotAction == removeAction)
    {
        QList<gsl::owner<QListWidgetItem*>> selected{ui->enabledBackendList->selectedItems()};
        std::for_each(selected.begin(), selected.end(), std::default_delete<QListWidgetItem>{});
        enableApplyButton();
    }
    else if(gotAction != nullptr)
    {
        auto iter = actionMap.constFind(gotAction);
        if(iter != actionMap.cend())
            ui->enabledBackendList->addItem(iter.value());
        enableApplyButton();
    }
}

void MainWindow::showDisabledBackendMenu(QPoint pt)
{
    QHash<QAction*,QString> actionMap;

    pt = ui->disabledBackendList->mapToGlobal(pt);

    QMenu ctxmenu;
    QAction *removeAction{ctxmenu.addAction(QIcon::fromTheme("list-remove"), "Remove")};
    if(ui->disabledBackendList->selectedItems().empty())
        removeAction->setEnabled(false);
    ctxmenu.addSeparator();
    for(size_t i{0};i < backendList.size();++i)
    {
        QString backend{std::data(backendList[i].full_string)};
        QAction *action{ctxmenu.addAction(QString("Add ")+backend)};
        actionMap[action] = backend;
        if(!ui->disabledBackendList->findItems(backend, Qt::MatchFixedString).empty() ||
           !ui->enabledBackendList->findItems(backend, Qt::MatchFixedString).empty())
            action->setEnabled(false);
    }

    QAction *gotAction{ctxmenu.exec(pt)};
    if(gotAction == removeAction)
    {
        QList<gsl::owner<QListWidgetItem*>> selected{ui->disabledBackendList->selectedItems()};
        std::for_each(selected.begin(), selected.end(), std::default_delete<QListWidgetItem>{});
        enableApplyButton();
    }
    else if(gotAction != nullptr)
    {
        auto iter = actionMap.constFind(gotAction);
        if(iter != actionMap.cend())
            ui->disabledBackendList->addItem(iter.value());
        enableApplyButton();
    }
}

void MainWindow::selectOSSPlayback()
{
    QString current{ui->ossDefaultDeviceLine->text()};
    if(current.isEmpty()) current = ui->ossDefaultDeviceLine->placeholderText();
    QString fname{QFileDialog::getOpenFileName(this, tr("Select Playback Device"), current)};
    if(!fname.isEmpty())
    {
        ui->ossDefaultDeviceLine->setText(fname);
        enableApplyButton();
    }
}

void MainWindow::selectOSSCapture()
{
    QString current{ui->ossDefaultCaptureLine->text()};
    if(current.isEmpty()) current = ui->ossDefaultCaptureLine->placeholderText();
    QString fname{QFileDialog::getOpenFileName(this, tr("Select Capture Device"), current)};
    if(!fname.isEmpty())
    {
        ui->ossDefaultCaptureLine->setText(fname);
        enableApplyButton();
    }
}

void MainWindow::selectSolarisPlayback()
{
    QString current{ui->solarisDefaultDeviceLine->text()};
    if(current.isEmpty()) current = ui->solarisDefaultDeviceLine->placeholderText();
    QString fname{QFileDialog::getOpenFileName(this, tr("Select Playback Device"), current)};
    if(!fname.isEmpty())
    {
        ui->solarisDefaultDeviceLine->setText(fname);
        enableApplyButton();
    }
}

void MainWindow::selectWaveOutput()
{
    QString fname{QFileDialog::getSaveFileName(this, tr("Select Wave File Output"),
        ui->waveOutputLine->text(), tr("Wave Files (*.wav *.amb);;All Files (*.*)"))};
    if(!fname.isEmpty())
    {
        ui->waveOutputLine->setText(fname);
        enableApplyButton();
    }
}
