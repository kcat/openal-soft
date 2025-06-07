
#include "config.h"
#include "config_backends.h"
#include "config_simd.h"

#include "mainwindow.h"

#include <array>
#include <cmath>
#include <memory>
#include <ranges>
#include <span>

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

namespace {

struct BackendNamePair {
    QString backend_name;
    QString full_string;
};
const std::array backendList{
#if HAVE_PIPEWIRE
    BackendNamePair{ QStringLiteral("pipewire"), QStringLiteral("PipeWire") },
#endif
#if HAVE_PULSEAUDIO
    BackendNamePair{ QStringLiteral("pulse"), QStringLiteral("PulseAudio") },
#endif
#if HAVE_WASAPI
    BackendNamePair{ QStringLiteral("wasapi"), QStringLiteral("WASAPI") },
#endif
#if HAVE_COREAUDIO
    BackendNamePair{ QStringLiteral("core"), QStringLiteral("CoreAudio") },
#endif
#if HAVE_OPENSL
    BackendNamePair{ QStringLiteral("opensl"), QStringLiteral("OpenSL") },
#endif
#if HAVE_ALSA
    BackendNamePair{ QStringLiteral("alsa"), QStringLiteral("ALSA") },
#endif
#if HAVE_SOLARIS
    BackendNamePair{ QStringLiteral("solaris"), QStringLiteral("Solaris") },
#endif
#if HAVE_SNDIO
    BackendNamePair{ QStringLiteral("sndio"), QStringLiteral("SndIO") },
#endif
#if HAVE_OSS
    BackendNamePair{ QStringLiteral("oss"), QStringLiteral("OSS") },
#endif
#if HAVE_DSOUND
    BackendNamePair{ QStringLiteral("dsound"), QStringLiteral("DirectSound") },
#endif
#if HAVE_WINMM
    BackendNamePair{ QStringLiteral("winmm"), QStringLiteral("Windows Multimedia") },
#endif
#if HAVE_PORTAUDIO
    BackendNamePair{ QStringLiteral("port"), QStringLiteral("PortAudio") },
#endif
#if HAVE_JACK
    BackendNamePair{ QStringLiteral("jack"), QStringLiteral("JACK") },
#endif

    BackendNamePair{ QStringLiteral("null"), QStringLiteral("Null Output") },
#if HAVE_WAVE
    BackendNamePair{ QStringLiteral("wave"), QStringLiteral("Wave Writer") },
#endif
};

struct NameValuePair {
    QString name;
    QString value;
};
const std::array speakerModeList{
    NameValuePair{ QStringLiteral("Autodetect"), QStringLiteral("") },
    NameValuePair{ QStringLiteral("Mono"), QStringLiteral("mono") },
    NameValuePair{ QStringLiteral("Stereo"), QStringLiteral("stereo") },
    NameValuePair{ QStringLiteral("Quadraphonic"), QStringLiteral("quad") },
    NameValuePair{ QStringLiteral("5.1 Surround"), QStringLiteral("surround51") },
    NameValuePair{ QStringLiteral("6.1 Surround"), QStringLiteral("surround61") },
    NameValuePair{ QStringLiteral("7.1 Surround"), QStringLiteral("surround71") },
    NameValuePair{ QStringLiteral("3D7.1 Surround"), QStringLiteral("surround3d71") },

    NameValuePair{ QStringLiteral("Ambisonic, 1st Order"), QStringLiteral("ambi1") },
    NameValuePair{ QStringLiteral("Ambisonic, 2nd Order"), QStringLiteral("ambi2") },
    NameValuePair{ QStringLiteral("Ambisonic, 3rd Order"), QStringLiteral("ambi3") },
    NameValuePair{ QStringLiteral("Ambisonic, 4th Order"), QStringLiteral("ambi4") },
};
const std::array sampleTypeList{
    NameValuePair{ QStringLiteral("Autodetect"), QStringLiteral("") },
    NameValuePair{ QStringLiteral("8-bit int"), QStringLiteral("int8") },
    NameValuePair{ QStringLiteral("8-bit uint"), QStringLiteral("uint8") },
    NameValuePair{ QStringLiteral("16-bit int"), QStringLiteral("int16") },
    NameValuePair{ QStringLiteral("16-bit uint"), QStringLiteral("uint16") },
    NameValuePair{ QStringLiteral("32-bit int"), QStringLiteral("int32") },
    NameValuePair{ QStringLiteral("32-bit uint"), QStringLiteral("uint32") },
    NameValuePair{ QStringLiteral("32-bit float"), QStringLiteral("float32") },
};
const std::array resamplerList{
    NameValuePair{ QStringLiteral("Point"), QStringLiteral("point") },
    NameValuePair{ QStringLiteral("Linear"), QStringLiteral("linear") },
    NameValuePair{ QStringLiteral("Cubic Spline"), QStringLiteral("spline") },
    NameValuePair{ QStringLiteral("Default (Cubic Spline)"), QStringLiteral("") },
    NameValuePair{ QStringLiteral("4-point Gaussian"), QStringLiteral("gaussian") },
    NameValuePair{ QStringLiteral("11th order Sinc (fast)"), QStringLiteral("fast_bsinc12") },
    NameValuePair{ QStringLiteral("11th order Sinc"), QStringLiteral("bsinc12") },
    NameValuePair{ QStringLiteral("23rd order Sinc (fast)"), QStringLiteral("fast_bsinc24") },
    NameValuePair{ QStringLiteral("23rd order Sinc"), QStringLiteral("bsinc24") },
    NameValuePair{ QStringLiteral("47th order Sinc (fast)"), QStringLiteral("fast_bsinc48") },
    NameValuePair{ QStringLiteral("47th order Sinc"), QStringLiteral("bsinc48") },
};
const std::array stereoModeList{
    NameValuePair{ QStringLiteral("Autodetect"), QStringLiteral("") },
    NameValuePair{ QStringLiteral("Speakers"), QStringLiteral("speakers") },
    NameValuePair{ QStringLiteral("Headphones"), QStringLiteral("headphones") },
};
const std::array stereoEncList{
    NameValuePair{ QStringLiteral("Default"), QStringLiteral("") },
    NameValuePair{ QStringLiteral("Basic"), QStringLiteral("panpot") },
    NameValuePair{ QStringLiteral("UHJ"), QStringLiteral("uhj") },
    NameValuePair{ QStringLiteral("Binaural"), QStringLiteral("hrtf") },
};
const std::array ambiFormatList{
    NameValuePair{ QStringLiteral("Default"), QStringLiteral("") },
    NameValuePair{ QStringLiteral("AmbiX (ACN, SN3D)"), QStringLiteral("ambix") },
    NameValuePair{ QStringLiteral("Furse-Malham"), QStringLiteral("fuma") },
    NameValuePair{ QStringLiteral("ACN, N3D"), QStringLiteral("acn+n3d") },
    NameValuePair{ QStringLiteral("ACN, FuMa"), QStringLiteral("acn+fuma") },
};
const std::array hrtfModeList{
    NameValuePair{ QStringLiteral("1st Order Ambisonic"), QStringLiteral("ambi1") },
    NameValuePair{ QStringLiteral("2nd Order Ambisonic"), QStringLiteral("ambi2") },
    NameValuePair{ QStringLiteral("3rd Order Ambisonic"), QStringLiteral("ambi3") },
    NameValuePair{ QStringLiteral("Default (Full)"), QStringLiteral("") },
    NameValuePair{ QStringLiteral("Full"), QStringLiteral("full") },
};

auto GetDefaultIndex(const std::span<const NameValuePair> list) -> uint8_t
{
    auto iter = std::ranges::find(list, QStringLiteral(""), &NameValuePair::value);
    if(iter != list.end())
        return static_cast<uint8_t>(std::distance(list.begin(), iter));
    throw std::runtime_error{"Failed to find default entry"};
}

#ifdef Q_OS_WIN32
struct CoTaskMemDeleter {
    void operator()(void *buffer) { CoTaskMemFree(buffer); }
};
/* NOLINTNEXTLINE(*-avoid-c-arrays) */
using WCharBufferPtr = std::unique_ptr<WCHAR[],CoTaskMemDeleter>;
#endif

QString getDefaultConfigName()
{
#ifdef Q_OS_WIN32
    const char *fname{"alsoft.ini"};
    static constexpr auto get_appdata_path = []() -> QString
    {
        auto buffer = WCharBufferPtr{};
        if(const HRESULT hr{SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DONT_UNEXPAND,
            nullptr, al::out_ptr(buffer))}; SUCCEEDED(hr))
            return QString::fromWCharArray(buffer.get());
        return QString{};
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
    static constexpr auto get_appdata_path = []() -> QString
    {
        auto buffer = WCharBufferPtr{};
        if(const HRESULT hr{SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DONT_UNEXPAND,
            nullptr, al::out_ptr(buffer))}; SUCCEEDED(hr))
            return QString::fromWCharArray(buffer.get());
        return QString{};
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
    list += paths.split(QChar(':'), Qt::SkipEmptyParts);
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

auto getValueFromName(const std::span<const NameValuePair> list, const QString &str) -> QString
{
    auto iter = std::ranges::find(list, str, &NameValuePair::name);
    if(iter != list.end())
        return iter->value;
    return QString{};
}

auto getNameFromValue(const std::span<const NameValuePair> list, const QString &str) -> QString
{
    auto iter = std::ranges::find(list, str, &NameValuePair::value);
    if(iter != list.end())
        return iter->name;
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
        return QStringLiteral("true");
    if(state == Qt::Unchecked)
        return QStringLiteral("false");
    return QString{};
}

}

MainWindow::MainWindow(QWidget *parent) : QMainWindow{parent}
    , ui{std::make_unique<Ui::MainWindow>()}
{
    ui->setupUi(this);

    for(auto &item : speakerModeList)
        ui->channelConfigCombo->addItem(item.name);
    ui->channelConfigCombo->adjustSize();
    for(auto &item : sampleTypeList)
        ui->sampleFormatCombo->addItem(item.name);
    ui->sampleFormatCombo->adjustSize();
    for(auto &item : stereoModeList)
        ui->stereoModeCombo->addItem(item.name);
    ui->stereoModeCombo->adjustSize();
    for(auto &item : stereoEncList)
        ui->stereoEncodingComboBox->addItem(item.name);
    ui->stereoEncodingComboBox->adjustSize();
    for(auto &item : ambiFormatList)
        ui->ambiFormatComboBox->addItem(item.name);
    ui->ambiFormatComboBox->adjustSize();

    ui->resamplerSlider->setRange(0, resamplerList.size()-1);
    ui->hrtfmodeSlider->setRange(0, hrtfModeList.size()-1);

#if !HAVE_NEON && !HAVE_SSE
    ui->cpuExtDisabledLabel->move(ui->cpuExtDisabledLabel->x(), ui->cpuExtDisabledLabel->y() - 60);
#else
    ui->cpuExtDisabledLabel->setVisible(false);
#endif

#if !HAVE_NEON

#if !HAVE_SSE4_1
#if !HAVE_SSE3
#if !HAVE_SSE2
#if !HAVE_SSE
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

#if !HAVE_SSE4_1
#if !HAVE_SSE3
#if !HAVE_SSE2
#if !HAVE_SSE
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

#if !ALSOFT_EAX
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

    /* QCheckBox::checkStateChanged was added in Qt 6.7, and
     * QCheckBox::stateChanged causes a deprecation warning since 6.9. Pick
     * whichever one we have.
     */
    const auto qcb_checkstatechanged = std::invoke([]<typename T=QCheckBox>
    {
        if constexpr(requires { &T::checkStateChanged; })
            return &T::checkStateChanged;
        else
            return &T::stateChanged;
    });
    connect(ui->stereoEncodingComboBox, qcb_cicint, this, &MainWindow::enableApplyButton);
    connect(ui->ambiFormatComboBox, qcb_cicint, this, &MainWindow::enableApplyButton);
    connect(ui->outputLimiterCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->outputDitherCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);

    connect(ui->decoderHQModeCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->decoderDistCompCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->decoderNFEffectsCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
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
    connect(ui->defaultHrtfPathsCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);

    connect(ui->srcCountLineEdit, &QLineEdit::editingFinished, this, &MainWindow::enableApplyButton);
    connect(ui->srcSendLineEdit, &QLineEdit::editingFinished, this, &MainWindow::enableApplyButton);
    connect(ui->effectSlotLineEdit, &QLineEdit::editingFinished, this, &MainWindow::enableApplyButton);

    connect(ui->enableSSECheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableSSE2CheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableSSE3CheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableSSE41CheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableNeonCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);

    ui->enabledBackendList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->enabledBackendList, &QListWidget::customContextMenuRequested, this, &MainWindow::showEnabledBackendMenu);

    ui->disabledBackendList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->disabledBackendList, &QListWidget::customContextMenuRequested, this, &MainWindow::showDisabledBackendMenu);
    connect(ui->backendCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);

    connect(ui->defaultReverbComboBox, qcb_cicint, this, &MainWindow::enableApplyButton);
    connect(ui->enableEaxReverbCheck, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableStdReverbCheck, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableAutowahCheck, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableChorusCheck, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableCompressorCheck, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableDistortionCheck, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableEchoCheck, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableEqualizerCheck, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableFlangerCheck, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableFrequencyShifterCheck, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableModulatorCheck, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableDedicatedCheck, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->enablePitchShifterCheck, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableVocalMorpherCheck, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->enableEaxCheck, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);

    connect(ui->pulseAutospawnCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->pulseAllowMovesCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->pulseFixRateCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->pulseAdjLatencyCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);

    connect(ui->pwireAssumeAudioCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->pwireRtMixCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);

    connect(ui->wasapiResamplerCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);

    connect(ui->jackAutospawnCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->jackConnectPortsCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->jackRtMixCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->jackBufferSizeSlider, &QSlider::valueChanged, this, &MainWindow::updateJackBufferSizeEdit);
    connect(ui->jackBufferSizeLine, &QLineEdit::editingFinished, this, &MainWindow::updateJackBufferSizeSlider);

    connect(ui->alsaDefaultDeviceLine, &QLineEdit::textChanged, this, &MainWindow::enableApplyButton);
    connect(ui->alsaDefaultCaptureLine, &QLineEdit::textChanged, this, &MainWindow::enableApplyButton);
    connect(ui->alsaResamplerCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);
    connect(ui->alsaMmapCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);

    connect(ui->ossDefaultDeviceLine, &QLineEdit::textChanged, this, &MainWindow::enableApplyButton);
    connect(ui->ossPlaybackPushButton, &QPushButton::clicked, this, &MainWindow::selectOSSPlayback);
    connect(ui->ossDefaultCaptureLine, &QLineEdit::textChanged, this, &MainWindow::enableApplyButton);
    connect(ui->ossCapturePushButton, &QPushButton::clicked, this, &MainWindow::selectOSSCapture);

    connect(ui->solarisDefaultDeviceLine, &QLineEdit::textChanged, this, &MainWindow::enableApplyButton);
    connect(ui->solarisPlaybackPushButton, &QPushButton::clicked, this, &MainWindow::selectSolarisPlayback);

    connect(ui->waveOutputLine, &QLineEdit::textChanged, this, &MainWindow::enableApplyButton);
    connect(ui->waveOutputButton, &QPushButton::clicked, this, &MainWindow::selectWaveOutput);
    connect(ui->waveBFormatCheckBox, qcb_checkstatechanged, this, &MainWindow::enableApplyButton);

    ui->backendListWidget->setCurrentRow(0);
    ui->tabWidget->setCurrentIndex(0);

    std::ranges::for_each(std::views::iota(1, ui->backendListWidget->count()),
        [widget=ui->backendListWidget](int idx) { widget->setRowHidden(idx, true); });
    for(size_t i{0};i < backendList.size();++i)
    {
        auto items = ui->backendListWidget->findItems(backendList[i].full_string,
            Qt::MatchFixedString);
        Q_FOREACH(QListWidgetItem *item, items)
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
        const auto btn = QMessageBox::warning(this, tr("Apply changes?"),
            tr("Save changes before quitting?"),
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
        const auto dir = QDir(ui->hrtfFileList->item(i)->text());
        const auto fnames = dir.entryList(QDir::Files | QDir::Readable, QDir::Name);
        Q_FOREACH(const QString &fname, fnames)
        {
            if(!fname.endsWith(QStringLiteral(".mhr"), Qt::CaseInsensitive))
                continue;
            const auto fullname = dir.absoluteFilePath(fname);
            if(processed.contains(fullname))
                continue;
            processed.push_back(fullname);

            const auto name = fname.left(fname.length()-4);
            if(!ret.contains(name))
                ret.push_back(name);
            else
            {
                size_t i{2};
                do {
                    const auto s = name+" #"+QString::number(i);
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
        const auto paths = getAllDataPaths(QStringLiteral("/openal/hrtf"));
        Q_FOREACH(const QString &name, paths)
        {
            const auto dir = QDir{name};
            const auto fnames = dir.entryList(QDir::Files | QDir::Readable, QDir::Name);
            Q_FOREACH(const QString &fname, fnames)
            {
                if(!fname.endsWith(QStringLiteral(".mhr"), Qt::CaseInsensitive))
                    continue;
                const auto fullname = dir.absoluteFilePath(fname);
                if(processed.contains(fullname))
                    continue;
                processed.push_back(fullname);

                const auto name = fname.left(fname.length()-4);
                if(!ret.contains(name))
                    ret.push_back(name);
                else
                {
                    size_t i{2};
                    do {
                        const auto s = name+" #"+QString::number(i);
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
        ret.push_back(QStringLiteral("Built-In HRTF"));
#endif
    }
    return ret;
}


void MainWindow::loadConfigFromFile()
{
    const auto fname = QFileDialog::getOpenFileName(this, tr("Select Files"));
    if(fname.isEmpty() == false)
        loadConfig(fname);
}

void MainWindow::loadConfig(const QString &fname)
{
    const auto settings = QSettings{fname, QSettings::IniFormat};

    const auto sampletype = settings.value(QStringLiteral("sample-type")).toString();
    ui->sampleFormatCombo->setCurrentIndex(0);
    if(sampletype.isEmpty() == false)
    {
        const auto str = getNameFromValue(sampleTypeList, sampletype);
        if(!str.isEmpty())
        {
            const int j{ui->sampleFormatCombo->findText(str)};
            if(j > 0) ui->sampleFormatCombo->setCurrentIndex(j);
        }
    }

    auto channelconfig = settings.value(QStringLiteral("channels")).toString();
    ui->channelConfigCombo->setCurrentIndex(0);
    if(channelconfig.isEmpty() == false)
    {
        if(channelconfig == QStringLiteral("surround51rear"))
            channelconfig = QStringLiteral("surround51");
        const auto str = getNameFromValue(speakerModeList, channelconfig);
        if(!str.isEmpty())
        {
            const int j{ui->channelConfigCombo->findText(str)};
            if(j > 0) ui->channelConfigCombo->setCurrentIndex(j);
        }
    }

    const auto srate = settings.value(QStringLiteral("frequency")).toString();
    if(srate.isEmpty())
        ui->sampleRateCombo->setCurrentIndex(0);
    else
    {
        ui->sampleRateCombo->lineEdit()->clear();
        ui->sampleRateCombo->lineEdit()->insert(srate);
    }

    ui->srcCountLineEdit->clear();
    ui->srcCountLineEdit->insert(settings.value(QStringLiteral("sources")).toString());
    ui->effectSlotLineEdit->clear();
    ui->effectSlotLineEdit->insert(settings.value(QStringLiteral("slots")).toString());
    ui->srcSendLineEdit->clear();
    ui->srcSendLineEdit->insert(settings.value(QStringLiteral("sends")).toString());

    auto resampler = settings.value(QStringLiteral("resampler")).toString().trimmed();
    const auto defaultResamplerIndex = GetDefaultIndex(resamplerList);
    ui->resamplerSlider->setValue(defaultResamplerIndex);
    ui->resamplerLabel->setText(resamplerList[defaultResamplerIndex].name);
    /* "Cubic" is an alias for the 4-point spline resampler. The "sinc4" and
     * "sinc8" resamplers are unsupported, use "gaussian" as a fallback.
     */
    if(resampler == QLatin1String{"cubic"})
        resampler = QStringLiteral("spline");
    else if(resampler == QLatin1String{"sinc4"} || resampler == QLatin1String{"sinc8"})
        resampler = QStringLiteral("gaussian");
    /* The "bsinc" resampler name is an alias for "bsinc12". */
    else if(resampler == QLatin1String{"bsinc"})
        resampler = QStringLiteral("bsinc12");
    for(int i = 0;i < std::ssize(resamplerList);i++)
    {
        if(resampler == resamplerList[i].value)
        {
            ui->resamplerSlider->setValue(i);
            ui->resamplerLabel->setText(resamplerList[i].name);
            break;
        }
    }

    const auto stereomode = settings.value(QStringLiteral("stereo-mode")).toString().trimmed();
    ui->stereoModeCombo->setCurrentIndex(0);
    if(stereomode.isEmpty() == false)
    {
        if(const auto str = getNameFromValue(stereoModeList, stereomode); !str.isEmpty())
        {
            if(const auto j = ui->stereoModeCombo->findText(str); j > 0)
                ui->stereoModeCombo->setCurrentIndex(j);
        }
    }

    ui->periodSizeEdit->clear();
    if(const auto periodsize = settings.value("period_size").toInt(); periodsize >= 64)
    {
        ui->periodSizeEdit->insert(QString::number(periodsize));
        updatePeriodSizeSlider();
    }

    ui->periodCountEdit->clear();
    if(const auto periodcount = settings.value("periods").toInt(); periodcount >= 2)
    {
        ui->periodCountEdit->insert(QString::number(periodcount));
        updatePeriodCountSlider();
    }

    ui->outputLimiterCheckBox->setCheckState(getCheckState(settings.value(QStringLiteral("output-limiter"))));
    ui->outputDitherCheckBox->setCheckState(getCheckState(settings.value(QStringLiteral("dither"))));

    ui->stereoEncodingComboBox->setCurrentIndex(0);
    if(const auto stereopan = settings.value(QStringLiteral("stereo-encoding")).toString();
        stereopan.isEmpty() == false)
    {
        const auto str = getNameFromValue(stereoEncList, stereopan);
        if(!str.isEmpty())
        {
            const int j{ui->stereoEncodingComboBox->findText(str)};
            if(j > 0) ui->stereoEncodingComboBox->setCurrentIndex(j);
        }
    }

    ui->ambiFormatComboBox->setCurrentIndex(0);
    if(const auto ambiformat = settings.value(QStringLiteral("ambi-format")).toString();
        ambiformat.isEmpty() == false)
    {
        const auto str = getNameFromValue(ambiFormatList, ambiformat);
        if(!str.isEmpty())
        {
            const int j{ui->ambiFormatComboBox->findText(str)};
            if(j > 0) ui->ambiFormatComboBox->setCurrentIndex(j);
        }
    }

    ui->decoderHQModeCheckBox->setChecked(getCheckState(settings.value(QStringLiteral("decoder/hq-mode"))));
    ui->decoderDistCompCheckBox->setCheckState(getCheckState(settings.value(QStringLiteral("decoder/distance-comp"))));
    ui->decoderNFEffectsCheckBox->setCheckState(getCheckState(settings.value(QStringLiteral("decoder/nfc"))));
    ui->decoderSpeakerDistSpinBox->setValue(settings.value(QStringLiteral("decoder/speaker-dist"), 1.0).toDouble());

    ui->decoderQuadLineEdit->setText(settings.value(QStringLiteral("decoder/quad")).toString());
    ui->decoder51LineEdit->setText(settings.value(QStringLiteral("decoder/surround51")).toString());
    ui->decoder61LineEdit->setText(settings.value(QStringLiteral("decoder/surround61")).toString());
    ui->decoder71LineEdit->setText(settings.value(QStringLiteral("decoder/surround71")).toString());
    ui->decoder3D71LineEdit->setText(settings.value(QStringLiteral("decoder/surround3d71")).toString());

    QStringList disabledCpuExts{settings.value(QStringLiteral("disable-cpu-exts")).toStringList()};
    if(disabledCpuExts.size() == 1)
        disabledCpuExts = disabledCpuExts[0].split(QChar(','));
    for(QString &name : disabledCpuExts)
        name = name.trimmed();
    ui->enableSSECheckBox->setChecked(!disabledCpuExts.contains(QStringLiteral("sse"), Qt::CaseInsensitive));
    ui->enableSSE2CheckBox->setChecked(!disabledCpuExts.contains(QStringLiteral("sse2"), Qt::CaseInsensitive));
    ui->enableSSE3CheckBox->setChecked(!disabledCpuExts.contains(QStringLiteral("sse3"), Qt::CaseInsensitive));
    ui->enableSSE41CheckBox->setChecked(!disabledCpuExts.contains(QStringLiteral("sse4.1"), Qt::CaseInsensitive));
    ui->enableNeonCheckBox->setChecked(!disabledCpuExts.contains(QStringLiteral("neon"), Qt::CaseInsensitive));

    auto hrtfmode = settings.value(QStringLiteral("hrtf-mode")).toString().trimmed();
    const auto defaultHrtfModeIndex = GetDefaultIndex(hrtfModeList);
    ui->hrtfmodeSlider->setValue(defaultHrtfModeIndex);
    ui->hrtfmodeLabel->setText(hrtfModeList[defaultHrtfModeIndex].name);
    /* The "basic" mode name is no longer supported. Use "ambi2" instead. */
    if(hrtfmode == QLatin1String{"basic"})
        hrtfmode = QStringLiteral("ambi2");
    for(size_t i{0};i < hrtfModeList.size();++i)
    {
        if(hrtfmode == hrtfModeList[i].value)
        {
            ui->hrtfmodeSlider->setValue(static_cast<int>(i));
            ui->hrtfmodeLabel->setText(hrtfModeList[i].name);
            break;
        }
    }

    QStringList hrtf_paths{settings.value(QStringLiteral("hrtf-paths")).toStringList()};
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
    ui->preferredHrtfComboBox->addItem(QStringLiteral("- Any -"));
    if(ui->defaultHrtfPathsCheckBox->isChecked())
    {
        const auto hrtfs = collectHrtfs();
        Q_FOREACH(const QString &name, hrtfs)
            ui->preferredHrtfComboBox->addItem(name);
    }

    const auto defaulthrtf = settings.value(QStringLiteral("default-hrtf")).toString();
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
    QStringList drivers{settings.value(QStringLiteral("drivers")).toStringList()};
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
            if(name == QLatin1String{"-mmdevapi"})
                name = QStringLiteral("-wasapi");
            else if(name == QLatin1String{"mmdevapi"})
                name = QStringLiteral("wasapi");
        }

        bool lastWasEmpty{false};
        Q_FOREACH(const QString &backend, drivers)
        {
            lastWasEmpty = backend.isEmpty();
            if(lastWasEmpty) continue;

            if(!backend.startsWith(QChar('-')))
            {
                std::ranges::for_each(backendList
                    | std::views::filter([&backend](const BackendNamePair &names)
                        { return backend == names.backend_name; }),
                    [uilist=ui->enabledBackendList](const BackendNamePair &names)
                    { uilist->addItem(names.full_string); });
            }
            else if(backend.size() > 1)
            {
                const auto backendref = QStringView{backend}.right(backend.size()-1);
                std::ranges::for_each(backendList
                    | std::views::filter([backendref](const BackendNamePair &names)
                       { return backendref == names.backend_name; }),
                [uilist=ui->disabledBackendList](const BackendNamePair &names)
                { uilist->addItem(names.full_string); });
            }
        }
        ui->backendCheckBox->setChecked(lastWasEmpty);
    }

    const auto defaultreverb = settings.value(QStringLiteral("default-reverb")).toString()
        .toLower();
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

    QStringList excludefx{settings.value(QStringLiteral("excludefx")).toStringList()};
    if(excludefx.size() == 1)
        excludefx = excludefx[0].split(QChar(','));
    for(QString &name : excludefx)
        name = name.trimmed();
    ui->enableEaxReverbCheck->setChecked(!excludefx.contains(QStringLiteral("eaxreverb"), Qt::CaseInsensitive));
    ui->enableStdReverbCheck->setChecked(!excludefx.contains(QStringLiteral("reverb"), Qt::CaseInsensitive));
    ui->enableAutowahCheck->setChecked(!excludefx.contains(QStringLiteral("autowah"), Qt::CaseInsensitive));
    ui->enableChorusCheck->setChecked(!excludefx.contains(QStringLiteral("chorus"), Qt::CaseInsensitive));
    ui->enableCompressorCheck->setChecked(!excludefx.contains(QStringLiteral("compressor"), Qt::CaseInsensitive));
    ui->enableDistortionCheck->setChecked(!excludefx.contains(QStringLiteral("distortion"), Qt::CaseInsensitive));
    ui->enableEchoCheck->setChecked(!excludefx.contains(QStringLiteral("echo"), Qt::CaseInsensitive));
    ui->enableEqualizerCheck->setChecked(!excludefx.contains(QStringLiteral("equalizer"), Qt::CaseInsensitive));
    ui->enableFlangerCheck->setChecked(!excludefx.contains(QStringLiteral("flanger"), Qt::CaseInsensitive));
    ui->enableFrequencyShifterCheck->setChecked(!excludefx.contains(QStringLiteral("fshifter"), Qt::CaseInsensitive));
    ui->enableModulatorCheck->setChecked(!excludefx.contains(QStringLiteral("modulator"), Qt::CaseInsensitive));
    ui->enableDedicatedCheck->setChecked(!excludefx.contains(QStringLiteral("dedicated"), Qt::CaseInsensitive));
    ui->enablePitchShifterCheck->setChecked(!excludefx.contains(QStringLiteral("pshifter"), Qt::CaseInsensitive));
    ui->enableVocalMorpherCheck->setChecked(!excludefx.contains(QStringLiteral("vmorpher"), Qt::CaseInsensitive));
    if(ui->enableEaxCheck->isEnabled())
        ui->enableEaxCheck->setChecked(getCheckState(settings.value(QStringLiteral("eax/enable"))) != Qt::Unchecked);

    ui->pulseAutospawnCheckBox->setCheckState(getCheckState(settings.value(QStringLiteral("pulse/spawn-server"))));
    ui->pulseAllowMovesCheckBox->setCheckState(getCheckState(settings.value(QStringLiteral("pulse/allow-moves"))));
    ui->pulseFixRateCheckBox->setCheckState(getCheckState(settings.value(QStringLiteral("pulse/fix-rate"))));
    ui->pulseAdjLatencyCheckBox->setCheckState(getCheckState(settings.value(QStringLiteral("pulse/adjust-latency"))));

    ui->pwireAssumeAudioCheckBox->setCheckState(getCheckState(settings.value(QStringLiteral("pipewire/assume-audio"))));
    ui->pwireRtMixCheckBox->setCheckState(getCheckState(settings.value(QStringLiteral("pipewire/rt-mix"))));

    ui->wasapiResamplerCheckBox->setCheckState(getCheckState(settings.value(QStringLiteral("wasapi/allow-resampler"))));

    ui->jackAutospawnCheckBox->setCheckState(getCheckState(settings.value(QStringLiteral("jack/spawn-server"))));
    ui->jackConnectPortsCheckBox->setCheckState(getCheckState(settings.value(QStringLiteral("jack/connect-ports"))));
    ui->jackRtMixCheckBox->setCheckState(getCheckState(settings.value(QStringLiteral("jack/rt-mix"))));
    ui->jackBufferSizeLine->setText(settings.value(QStringLiteral("jack/buffer-size"), QString()).toString());
    updateJackBufferSizeSlider();

    ui->alsaDefaultDeviceLine->setText(settings.value(QStringLiteral("alsa/device"), QString()).toString());
    ui->alsaDefaultCaptureLine->setText(settings.value(QStringLiteral("alsa/capture"), QString()).toString());
    ui->alsaResamplerCheckBox->setCheckState(getCheckState(settings.value(QStringLiteral("alsa/allow-resampler"))));
    ui->alsaMmapCheckBox->setCheckState(getCheckState(settings.value(QStringLiteral("alsa/mmap"))));

    ui->ossDefaultDeviceLine->setText(settings.value(QStringLiteral("oss/device"), QString()).toString());
    ui->ossDefaultCaptureLine->setText(settings.value(QStringLiteral("oss/capture"), QString()).toString());

    ui->solarisDefaultDeviceLine->setText(settings.value(QStringLiteral("solaris/device"), QString()).toString());

    ui->waveOutputLine->setText(settings.value(QStringLiteral("wave/file"), QString()).toString());
    ui->waveBFormatCheckBox->setChecked(settings.value(QStringLiteral("wave/bformat"), false).toBool());

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
    const auto fname = QFileDialog::getOpenFileName(this, tr("Select Files"));
    if(fname.isEmpty() == false)
    {
        saveConfig(fname);
        ui->applyButton->setEnabled(false);
        mNeedsSave = false;
    }
}

void MainWindow::saveConfig(const QString &fname) const
{
    auto settings = QSettings{fname, QSettings::IniFormat};

    /* HACK: Compound any stringlist values into a comma-separated string. */
    auto allkeys = settings.allKeys();
    Q_FOREACH(const QString &key, allkeys)
    {
        const auto vals = settings.value(key).toStringList();
        if(vals.size() > 1)
            settings.setValue(key, vals.join(QChar(',')));
    }

    settings.setValue(QStringLiteral("sample-type"), getValueFromName(sampleTypeList, ui->sampleFormatCombo->currentText()));
    settings.setValue(QStringLiteral("channels"), getValueFromName(speakerModeList, ui->channelConfigCombo->currentText()));

    const auto rate = ui->sampleRateCombo->currentText().toUInt();
    if(rate <= 0)
        settings.setValue(QStringLiteral("frequency"), QString{});
    else
        settings.setValue(QStringLiteral("frequency"), rate);

    settings.setValue(QStringLiteral("period_size"), ui->periodSizeEdit->text());
    settings.setValue(QStringLiteral("periods"), ui->periodCountEdit->text());

    settings.setValue(QStringLiteral("sources"), ui->srcCountLineEdit->text());
    settings.setValue(QStringLiteral("slots"), ui->effectSlotLineEdit->text());

    settings.setValue(QStringLiteral("resampler"), resamplerList[ui->resamplerSlider->value()].value);

    settings.setValue(QStringLiteral("stereo-mode"), getValueFromName(stereoModeList, ui->stereoModeCombo->currentText()));
    settings.setValue(QStringLiteral("stereo-encoding"), getValueFromName(stereoEncList, ui->stereoEncodingComboBox->currentText()));
    settings.setValue(QStringLiteral("ambi-format"), getValueFromName(ambiFormatList, ui->ambiFormatComboBox->currentText()));

    settings.setValue(QStringLiteral("output-limiter"), getCheckValue(ui->outputLimiterCheckBox));
    settings.setValue(QStringLiteral("dither"), getCheckValue(ui->outputDitherCheckBox));

    settings.setValue(QStringLiteral("decoder/hq-mode"), getCheckValue(ui->decoderHQModeCheckBox));
    settings.setValue(QStringLiteral("decoder/distance-comp"), getCheckValue(ui->decoderDistCompCheckBox));
    settings.setValue(QStringLiteral("decoder/nfc"), getCheckValue(ui->decoderNFEffectsCheckBox));
    const auto speakerdist = ui->decoderSpeakerDistSpinBox->value();
    settings.setValue(QStringLiteral("decoder/speaker-dist"),
        (speakerdist != 1.0) ? QString::number(speakerdist) : QString{}
    );

    settings.setValue(QStringLiteral("decoder/quad"), ui->decoderQuadLineEdit->text());
    settings.setValue(QStringLiteral("decoder/surround51"), ui->decoder51LineEdit->text());
    settings.setValue(QStringLiteral("decoder/surround61"), ui->decoder61LineEdit->text());
    settings.setValue(QStringLiteral("decoder/surround71"), ui->decoder71LineEdit->text());
    settings.setValue(QStringLiteral("decoder/surround3d71"), ui->decoder3D71LineEdit->text());

    QStringList strlist;
    if(!ui->enableSSECheckBox->isChecked())
        strlist.append(QStringLiteral("sse"));
    if(!ui->enableSSE2CheckBox->isChecked())
        strlist.append(QStringLiteral("sse2"));
    if(!ui->enableSSE3CheckBox->isChecked())
        strlist.append(QStringLiteral("sse3"));
    if(!ui->enableSSE41CheckBox->isChecked())
        strlist.append(QStringLiteral("sse4.1"));
    if(!ui->enableNeonCheckBox->isChecked())
        strlist.append(QStringLiteral("neon"));
    settings.setValue(QStringLiteral("disable-cpu-exts"), strlist.join(QChar(',')));

    settings.setValue(QStringLiteral("hrtf-mode"), hrtfModeList[ui->hrtfmodeSlider->value()].value);

    if(ui->preferredHrtfComboBox->currentIndex() == 0)
        settings.setValue(QStringLiteral("default-hrtf"), QString{});
    else
    {
        const auto str = ui->preferredHrtfComboBox->currentText();
        settings.setValue(QStringLiteral("default-hrtf"), str);
    }

    strlist.clear();
    strlist.reserve(ui->hrtfFileList->count());
    for(int i = 0;i < ui->hrtfFileList->count();i++)
        strlist.append(ui->hrtfFileList->item(i)->text());
    if(!strlist.empty() && ui->defaultHrtfPathsCheckBox->isChecked())
        strlist.append(QString{});
    settings.setValue(QStringLiteral("hrtf-paths"), strlist.join(QChar{','}));

    strlist.clear();
    std::ranges::for_each(std::views::iota(0, ui->enabledBackendList->count()),
        [&strlist,list=ui->enabledBackendList](int idx)
    {
        const auto label = list->item(idx)->text();
        std::ranges::for_each(backendList
            | std::views::filter([&label](const BackendNamePair &names)
                { return label == names.full_string; }),
            [&strlist](const BackendNamePair &names)
            { strlist.append(names.backend_name); });
    });
    std::ranges::for_each(std::views::iota(0, ui->disabledBackendList->count()),
        [&strlist,list=ui->disabledBackendList](int idx)
    {
        const auto label = list->item(idx)->text();
        std::ranges::for_each(backendList
            | std::views::filter([&label](const BackendNamePair &names)
                { return label == names.full_string; }),
            [&strlist](const BackendNamePair &names)
            { strlist.append(QChar{'-'}+names.backend_name); });
    });
    if(strlist.empty() && !ui->backendCheckBox->isChecked())
        strlist.append(QStringLiteral("-all"));
    else if(ui->backendCheckBox->isChecked())
        strlist.append(QString{});
    settings.setValue(QStringLiteral("drivers"), strlist.join(QChar(',')));

    // TODO: Remove check when we can properly match global values.
    if(ui->defaultReverbComboBox->currentIndex() == 0)
        settings.setValue(QStringLiteral("default-reverb"), QString{});
    else
    {
        const auto str = ui->defaultReverbComboBox->currentText().toLower();
        settings.setValue(QStringLiteral("default-reverb"), str);
    }

    strlist.clear();
    if(!ui->enableEaxReverbCheck->isChecked())
        strlist.append(QStringLiteral("eaxreverb"));
    if(!ui->enableStdReverbCheck->isChecked())
        strlist.append(QStringLiteral("reverb"));
    if(!ui->enableAutowahCheck->isChecked())
        strlist.append(QStringLiteral("autowah"));
    if(!ui->enableChorusCheck->isChecked())
        strlist.append(QStringLiteral("chorus"));
    if(!ui->enableDistortionCheck->isChecked())
        strlist.append(QStringLiteral("distortion"));
    if(!ui->enableCompressorCheck->isChecked())
        strlist.append(QStringLiteral("compressor"));
    if(!ui->enableEchoCheck->isChecked())
        strlist.append(QStringLiteral("echo"));
    if(!ui->enableEqualizerCheck->isChecked())
        strlist.append(QStringLiteral("equalizer"));
    if(!ui->enableFlangerCheck->isChecked())
        strlist.append(QStringLiteral("flanger"));
    if(!ui->enableFrequencyShifterCheck->isChecked())
        strlist.append(QStringLiteral("fshifter"));
    if(!ui->enableModulatorCheck->isChecked())
        strlist.append(QStringLiteral("modulator"));
    if(!ui->enableDedicatedCheck->isChecked())
        strlist.append(QStringLiteral("dedicated"));
    if(!ui->enablePitchShifterCheck->isChecked())
        strlist.append(QStringLiteral("pshifter"));
    if(!ui->enableVocalMorpherCheck->isChecked())
        strlist.append(QStringLiteral("vmorpher"));
    settings.setValue(QStringLiteral("excludefx"), strlist.join(QChar{','}));
    settings.setValue(QStringLiteral("eax/enable"),
        (!ui->enableEaxCheck->isEnabled() || ui->enableEaxCheck->isChecked())
            ? QString{/*"true"*/} : QStringLiteral("false"));

    settings.setValue(QStringLiteral("pipewire/assume-audio"), getCheckValue(ui->pwireAssumeAudioCheckBox));
    settings.setValue(QStringLiteral("pipewire/rt-mix"), getCheckValue(ui->pwireRtMixCheckBox));

    settings.setValue(QStringLiteral("wasapi/allow-resampler"), getCheckValue(ui->wasapiResamplerCheckBox));

    settings.setValue(QStringLiteral("pulse/spawn-server"), getCheckValue(ui->pulseAutospawnCheckBox));
    settings.setValue(QStringLiteral("pulse/allow-moves"), getCheckValue(ui->pulseAllowMovesCheckBox));
    settings.setValue(QStringLiteral("pulse/fix-rate"), getCheckValue(ui->pulseFixRateCheckBox));
    settings.setValue(QStringLiteral("pulse/adjust-latency"), getCheckValue(ui->pulseAdjLatencyCheckBox));

    settings.setValue(QStringLiteral("jack/spawn-server"), getCheckValue(ui->jackAutospawnCheckBox));
    settings.setValue(QStringLiteral("jack/connect-ports"), getCheckValue(ui->jackConnectPortsCheckBox));
    settings.setValue(QStringLiteral("jack/rt-mix"), getCheckValue(ui->jackRtMixCheckBox));
    settings.setValue(QStringLiteral("jack/buffer-size"), ui->jackBufferSizeLine->text());

    settings.setValue(QStringLiteral("alsa/device"), ui->alsaDefaultDeviceLine->text());
    settings.setValue(QStringLiteral("alsa/capture"), ui->alsaDefaultCaptureLine->text());
    settings.setValue(QStringLiteral("alsa/allow-resampler"), getCheckValue(ui->alsaResamplerCheckBox));
    settings.setValue(QStringLiteral("alsa/mmap"), getCheckValue(ui->alsaMmapCheckBox));

    settings.setValue(QStringLiteral("oss/device"), ui->ossDefaultDeviceLine->text());
    settings.setValue(QStringLiteral("oss/capture"), ui->ossDefaultCaptureLine->text());

    settings.setValue(QStringLiteral("solaris/device"), ui->solarisDefaultDeviceLine->text());

    settings.setValue(QStringLiteral("wave/file"), ui->waveOutputLine->text());
    settings.setValue(QStringLiteral("wave/bformat"),
        ui->waveBFormatCheckBox->isChecked() ? QStringLiteral("true") : QString{/*"false"*/}
    );

    /* Remove empty keys
     * FIXME: Should only remove keys whose value matches the globally-specified value.
     */
    allkeys = settings.allKeys();
    Q_FOREACH(const QString &key, allkeys)
    {
        const auto str = settings.value(key).toString();
        if(str.isEmpty())
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
    ui->resamplerLabel->setText(resamplerList[num].name);
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
    const auto pos = ui->periodSizeEdit->text().toInt();
    if(pos >= 64)
        ui->periodSizeSlider->setSliderPosition(std::min(pos, 8192));
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
    const auto fname = QFileDialog::getOpenFileName(this, tr(caption), dir,
        tr("AmbDec Files (*.ambdec);;All Files (*.*)"));
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
    const auto value = ui->jackBufferSizeLine->text().toInt();
    const auto pos = static_cast<int>(floor(log2(value) + 0.5));
    ui->jackBufferSizeSlider->setSliderPosition(pos);
    enableApplyButton();
}


void MainWindow::updateHrtfModeLabel(int num)
{
    ui->hrtfmodeLabel->setText(hrtfModeList[static_cast<uint>(num)].name);
    enableApplyButton();
}


void MainWindow::addHrtfFile()
{
    const auto path = QFileDialog::getExistingDirectory(this, tr("Select HRTF Path"));
    if(path.isEmpty() == false && !getAllDataPaths(QStringLiteral("/openal/hrtf")).contains(path))
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
    auto actionMap = QHash<QAction*,QString>{};

    pt = ui->enabledBackendList->mapToGlobal(pt);

    auto ctxmenu = QMenu{};
    auto *removeAction = ctxmenu.addAction(QIcon::fromTheme("list-remove"), "Remove");
    if(ui->enabledBackendList->selectedItems().empty())
        removeAction->setEnabled(false);
    ctxmenu.addSeparator();
    for(size_t i{0};i < backendList.size();++i)
    {
        const auto &backend = backendList[i].full_string;
        auto *action = ctxmenu.addAction(QString("Add ")+backend);
        actionMap[action] = backend;
        if(!ui->enabledBackendList->findItems(backend, Qt::MatchFixedString).empty() ||
           !ui->disabledBackendList->findItems(backend, Qt::MatchFixedString).empty())
            action->setEnabled(false);
    }

    auto *gotAction = ctxmenu.exec(pt);
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
    auto actionMap = QHash<QAction*,QString>{};

    pt = ui->disabledBackendList->mapToGlobal(pt);

    auto ctxmenu = QMenu{};
    auto *removeAction = ctxmenu.addAction(QIcon::fromTheme("list-remove"), "Remove");
    if(ui->disabledBackendList->selectedItems().empty())
        removeAction->setEnabled(false);
    ctxmenu.addSeparator();
    for(size_t i{0};i < backendList.size();++i)
    {
        const auto &backend = backendList[i].full_string;
        auto *action = ctxmenu.addAction(QString("Add ")+backend);
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
    auto current = ui->ossDefaultDeviceLine->text();
    if(current.isEmpty()) current = ui->ossDefaultDeviceLine->placeholderText();
    const auto fname = QFileDialog::getOpenFileName(this, tr("Select Playback Device"), current);
    if(!fname.isEmpty())
    {
        ui->ossDefaultDeviceLine->setText(fname);
        enableApplyButton();
    }
}

void MainWindow::selectOSSCapture()
{
    auto current = ui->ossDefaultCaptureLine->text();
    if(current.isEmpty()) current = ui->ossDefaultCaptureLine->placeholderText();
    const auto fname = QFileDialog::getOpenFileName(this, tr("Select Capture Device"), current);
    if(!fname.isEmpty())
    {
        ui->ossDefaultCaptureLine->setText(fname);
        enableApplyButton();
    }
}

void MainWindow::selectSolarisPlayback()
{
    auto current = ui->solarisDefaultDeviceLine->text();
    if(current.isEmpty()) current = ui->solarisDefaultDeviceLine->placeholderText();
    const auto fname = QFileDialog::getOpenFileName(this, tr("Select Playback Device"), current);
    if(!fname.isEmpty())
    {
        ui->solarisDefaultDeviceLine->setText(fname);
        enableApplyButton();
    }
}

void MainWindow::selectWaveOutput()
{
    const auto fname = QFileDialog::getSaveFileName(this, tr("Select Wave File Output"),
        ui->waveOutputLine->text(), tr("Wave Files (*.wav *.amb);;All Files (*.*)"));
    if(!fname.isEmpty())
    {
        ui->waveOutputLine->setText(fname);
        enableApplyButton();
    }
}
