
#include "config.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QtGlobal>
#include "mainwindow.h"
#include "ui_mainwindow.h"

namespace {
static const struct {
    char backend_name[16];
    char menu_string[32];
} backendMenuList[] = {
#ifdef HAVE_JACK
    { "jack", "Add JACK" },
#endif
#ifdef HAVE_PULSEAUDIO
    { "pulse", "Add PulseAudio" },
#endif
#ifdef HAVE_ALSA
    { "alsa", "Add ALSA" },
#endif
#ifdef HAVE_COREAUDIO
    { "core", "Add CoreAudio" },
#endif
#ifdef HAVE_OSS
    { "oss", "Add OSS" },
#endif
#ifdef HAVE_SOLARIS
    { "solaris", "Add Solaris" },
#endif
#ifdef HAVE_SNDIO
    { "sndio", "Add SndIO" },
#endif
#ifdef HAVE_QSA
    { "qsa", "Add QSA" },
#endif
#ifdef HAVE_MMDEVAPI
    { "mmdevapi", "Add MMDevAPI" },
#endif
#ifdef HAVE_DSOUND
    { "dsound", "Add DirectSound" },
#endif
#ifdef HAVE_WINMM
    { "winmm", "Add Windows Multimedia" },
#endif
#ifdef HAVE_PORTAUDIO
    { "port", "Add PortAudio" },
#endif
#ifdef HAVE_OPENSL
    { "opensl", "Add OpenSL" },
#endif

    { "null", "Add Null Output" },
#ifdef HAVE_WAVE
    { "wave", "Add Wave Writer" },
#endif
    { "", "" }
};

static const struct {
    const char name[64];
    const char value[16];
} speakerModeList[] = {
    { "Autodetect", "" },
    { "Mono", "mono" },
    { "Stereo", "stereo" },
    { "Quadrophonic", "quad" },
    { "5.1 Surround (Side)", "surround51" },
    { "5.1 Surround (Rear)", "surround51rear" },
    { "6.1 Surround", "surround61" },
    { "7.1 Surround", "surround71" },

    { "", "" }
}, sampleTypeList[] = {
    { "Autodetect", "" },
    { "8-bit int", "int8" },
    { "8-bit uint", "uint8" },
    { "16-bit int", "int16" },
    { "16-bit uint", "uint16" },
    { "32-bit int", "int32" },
    { "32-bit uint", "uint32" },
    { "32-bit float", "float32" },

    { "", "" }
}, resamplerList[] = {
    { "Default", "" },
    { "Point (low quality, very fast)", "point" },
    { "Linear (basic quality, fast)", "linear" },
    { "4-Point Sinc (good quality)", "sinc4" },
    { "8-Point Sinc (high quality, slow)", "sinc8" },
    { "Band-limited Sinc (very high quality, very slow)", "bsinc" },

    { "", "" }
}, stereoModeList[] = {
    { "Autodetect", "" },
    { "Speakers", "speakers" },
    { "Headphones", "headphones" },

    { "", "" }
};

static QString getDefaultConfigName()
{
#ifdef Q_OS_WIN32
    static const char fname[] = "alsoft.ini";
    QByteArray base = qgetenv("AppData");
#else
    static const char fname[] = "alsoft.conf";
    QByteArray base = qgetenv("XDG_CONFIG_HOME");
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

static QString getBaseDataPath()
{
#ifdef Q_OS_WIN32
    QByteArray base = qgetenv("AppData");
#else
    QByteArray base = qgetenv("XDG_DATA_HOME");
    if(base.isEmpty())
    {
        base = qgetenv("HOME");
        if(!base.isEmpty())
            base += "/.local/share";
    }
#endif
    return base;
}

static QStringList getAllDataPaths(QString append=QString())
{
    QStringList list;
    list.append(getBaseDataPath());
#ifdef Q_OS_WIN32
    // TODO: Common AppData path
#else
    QString paths = qgetenv("XDG_DATA_DIRS");
    if(paths.isEmpty())
        paths = "/usr/local/share/:/usr/share/";
    list += paths.split(QChar(':'), QString::SkipEmptyParts);
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
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    mPeriodSizeValidator(NULL),
    mPeriodCountValidator(NULL),
    mSourceCountValidator(NULL),
    mEffectSlotValidator(NULL),
    mSourceSendValidator(NULL),
    mSampleRateValidator(NULL)
{
    ui->setupUi(this);

    for(int i = 0;speakerModeList[i].name[0];i++)
        ui->channelConfigCombo->addItem(speakerModeList[i].name);
    ui->channelConfigCombo->adjustSize();
    for(int i = 0;sampleTypeList[i].name[0];i++)
        ui->sampleFormatCombo->addItem(sampleTypeList[i].name);
    ui->sampleFormatCombo->adjustSize();
    for(int i = 0;resamplerList[i].name[0];i++)
        ui->resamplerComboBox->addItem(resamplerList[i].name);
    ui->resamplerComboBox->adjustSize();
    for(int i = 0;stereoModeList[i].name[0];i++)
        ui->stereoModeCombo->addItem(stereoModeList[i].name);
    ui->stereoModeCombo->adjustSize();

    ui->hrtfStateComboBox->adjustSize();

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

    mPeriodSizeValidator = new QIntValidator(64, 8192, this);
    ui->periodSizeEdit->setValidator(mPeriodSizeValidator);
    mPeriodCountValidator = new QIntValidator(2, 16, this);
    ui->periodCountEdit->setValidator(mPeriodCountValidator);

    mSourceCountValidator = new QIntValidator(0, 256, this);
    ui->srcCountLineEdit->setValidator(mSourceCountValidator);
    mEffectSlotValidator = new QIntValidator(0, 16, this);
    ui->effectSlotLineEdit->setValidator(mEffectSlotValidator);
    mSourceSendValidator = new QIntValidator(0, 4, this);
    ui->srcSendLineEdit->setValidator(mSourceSendValidator);
    mSampleRateValidator = new QIntValidator(8000, 192000, this);
    ui->sampleRateCombo->lineEdit()->setValidator(mSampleRateValidator);

    connect(ui->actionLoad, SIGNAL(triggered()), this, SLOT(loadConfigFromFile()));
    connect(ui->actionSave_As, SIGNAL(triggered()), this, SLOT(saveConfigAsFile()));

    connect(ui->applyButton, SIGNAL(clicked()), this, SLOT(saveCurrentConfig()));

    connect(ui->periodSizeSlider, SIGNAL(valueChanged(int)), this, SLOT(updatePeriodSizeEdit(int)));
    connect(ui->periodSizeEdit, SIGNAL(editingFinished()), this, SLOT(updatePeriodSizeSlider()));
    connect(ui->periodCountSlider, SIGNAL(valueChanged(int)), this, SLOT(updatePeriodCountEdit(int)));
    connect(ui->periodCountEdit, SIGNAL(editingFinished()), this, SLOT(updatePeriodCountSlider()));

    connect(ui->hrtfAddButton, SIGNAL(clicked()), this, SLOT(addHrtfFile()));
    connect(ui->hrtfRemoveButton, SIGNAL(clicked()), this, SLOT(removeHrtfFile()));
    connect(ui->hrtfFileList, SIGNAL(itemSelectionChanged()), this, SLOT(updateHrtfRemoveButton()));

    ui->enabledBackendList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->enabledBackendList, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showEnabledBackendMenu(QPoint)));

    ui->disabledBackendList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->disabledBackendList, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showDisabledBackendMenu(QPoint)));

    loadConfig(getDefaultConfigName());
}

MainWindow::~MainWindow()
{
    delete ui;
    delete mPeriodSizeValidator;
    delete mPeriodCountValidator;
    delete mSourceCountValidator;
    delete mEffectSlotValidator;
    delete mSourceSendValidator;
    delete mSampleRateValidator;
}

void MainWindow::loadConfigFromFile()
{
    QString fname = QFileDialog::getOpenFileName(this, tr("Select Files"));
    if(fname.isEmpty() == false)
        loadConfig(fname);
}

void MainWindow::loadConfig(const QString &fname)
{
    QSettings settings(fname, QSettings::IniFormat);

    QString sampletype = settings.value("sample-type").toString();
    ui->sampleFormatCombo->setCurrentIndex(0);
    if(sampletype.isEmpty() == false)
    {
        for(int i = 0;sampleTypeList[i].name[i];i++)
        {
            if(sampletype == sampleTypeList[i].value)
            {
                for(int j = 1;j < ui->sampleFormatCombo->count();j++)
                {
                    QString item = ui->sampleFormatCombo->itemText(j);
                    if(item == sampleTypeList[i].name)
                    {
                        ui->sampleFormatCombo->setCurrentIndex(j);
                        break;
                    }
                }
                break;
            }
        }
    }

    QString channelconfig = settings.value("channels").toString();
    ui->channelConfigCombo->setCurrentIndex(0);
    if(channelconfig.isEmpty() == false)
    {
        for(int i = 0;speakerModeList[i].name[i];i++)
        {
            if(channelconfig == speakerModeList[i].value)
            {
                for(int j = 1;j < ui->channelConfigCombo->count();j++)
                {
                    QString item = ui->channelConfigCombo->itemText(j);
                    if(item == speakerModeList[i].name)
                    {
                        ui->channelConfigCombo->setCurrentIndex(j);
                        break;
                    }
                }
                break;
            }
        }
    }

    QString srate = settings.value("frequency").toString();
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
    ui->resamplerComboBox->setCurrentIndex(0);
    if(resampler.isEmpty() == false)
    {
        /* The "cubic" resampler is no longer supported. It's been replaced by
         * "sinc4". */
        if(resampler == "cubic")
            resampler = "sinc4";

        for(int i = 0;resamplerList[i].name[i];i++)
        {
            if(resampler == resamplerList[i].value)
            {
                for(int j = 1;j < ui->resamplerComboBox->count();j++)
                {
                    QString item = ui->resamplerComboBox->itemText(j);
                    if(item == resamplerList[i].name)
                    {
                        ui->resamplerComboBox->setCurrentIndex(j);
                        break;
                    }
                }
                break;
            }
        }
    }

    QString stereomode = settings.value("stereo-mode").toString().trimmed();
    ui->stereoModeCombo->setCurrentIndex(0);
    if(stereomode.isEmpty() == false)
    {
        for(int i = 0;stereoModeList[i].name[i];i++)
        {
            if(stereomode == stereoModeList[i].value)
            {
                for(int j = 1;j < ui->stereoModeCombo->count();j++)
                {
                    QString item = ui->stereoModeCombo->itemText(j);
                    if(item == stereoModeList[i].name)
                    {
                        ui->stereoModeCombo->setCurrentIndex(j);
                        break;
                    }
                }
                break;
            }
        }
    }

    int periodsize = settings.value("period_size").toInt();
    ui->periodSizeEdit->clear();
    if(periodsize >= 64)
    {
        ui->periodSizeEdit->insert(QString::number(periodsize));
        updatePeriodSizeSlider();
    }

    int periodcount = settings.value("periods").toInt();
    ui->periodCountEdit->clear();
    if(periodcount >= 2)
    {
        ui->periodCountEdit->insert(QString::number(periodcount));
        updatePeriodCountSlider();
    }

    QStringList disabledCpuExts = settings.value("disable-cpu-exts").toStringList();
    if(disabledCpuExts.size() == 1)
        disabledCpuExts = disabledCpuExts[0].split(QChar(','));
    std::transform(disabledCpuExts.begin(), disabledCpuExts.end(),
                   disabledCpuExts.begin(), std::mem_fun_ref(&QString::trimmed));
    ui->enableSSECheckBox->setChecked(!disabledCpuExts.contains("sse", Qt::CaseInsensitive));
    ui->enableSSE2CheckBox->setChecked(!disabledCpuExts.contains("sse2", Qt::CaseInsensitive));
    ui->enableSSE3CheckBox->setChecked(!disabledCpuExts.contains("sse3", Qt::CaseInsensitive));
    ui->enableSSE41CheckBox->setChecked(!disabledCpuExts.contains("sse4.1", Qt::CaseInsensitive));
    ui->enableNeonCheckBox->setChecked(!disabledCpuExts.contains("neon", Qt::CaseInsensitive));

    if(settings.value("hrtf").toString() == QString())
        ui->hrtfStateComboBox->setCurrentIndex(0);
    else
    {
        if(settings.value("hrtf", true).toBool())
            ui->hrtfStateComboBox->setCurrentIndex(1);
        else
            ui->hrtfStateComboBox->setCurrentIndex(2);
    }

    QStringList hrtf_tables = settings.value("hrtf_tables").toStringList();
    if(hrtf_tables.size() == 1)
        hrtf_tables = hrtf_tables[0].split(QChar(','));
    std::transform(hrtf_tables.begin(), hrtf_tables.end(),
                   hrtf_tables.begin(), std::mem_fun_ref(&QString::trimmed));
    ui->hrtfFileList->clear();
    ui->hrtfFileList->addItems(hrtf_tables);
    updateHrtfRemoveButton();

    ui->enabledBackendList->clear();
    ui->disabledBackendList->clear();
    QStringList drivers = settings.value("drivers").toStringList();
    if(drivers.size() == 0)
        ui->backendCheckBox->setChecked(true);
    else
    {
        if(drivers.size() == 1)
            drivers = drivers[0].split(QChar(','));
        std::transform(drivers.begin(), drivers.end(),
                       drivers.begin(), std::mem_fun_ref(&QString::trimmed));

        bool lastWasEmpty = false;
        foreach(const QString &backend, drivers)
        {
            lastWasEmpty = backend.isEmpty();
            if(!backend.startsWith(QChar('-')) && !lastWasEmpty)
                ui->enabledBackendList->addItem(backend);
            else if(backend.size() > 1)
                ui->disabledBackendList->addItem(backend.right(backend.size()-1));
        }
        ui->backendCheckBox->setChecked(lastWasEmpty);
    }

    QString defaultreverb = settings.value("default-reverb").toString().toLower();
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

    ui->emulateEaxCheckBox->setChecked(settings.value("reverb/emulate-eax", false).toBool());

    QStringList excludefx = settings.value("excludefx").toStringList();
    if(excludefx.size() == 1)
        excludefx = excludefx[0].split(QChar(','));
    std::transform(excludefx.begin(), excludefx.end(),
                   excludefx.begin(), std::mem_fun_ref(&QString::trimmed));
    ui->enableEaxReverbCheck->setChecked(!excludefx.contains("eaxreverb", Qt::CaseInsensitive));
    ui->enableStdReverbCheck->setChecked(!excludefx.contains("reverb", Qt::CaseInsensitive));
    ui->enableChorusCheck->setChecked(!excludefx.contains("chorus", Qt::CaseInsensitive));
    ui->enableCompressorCheck->setChecked(!excludefx.contains("compressor", Qt::CaseInsensitive));
    ui->enableDistortionCheck->setChecked(!excludefx.contains("distortion", Qt::CaseInsensitive));
    ui->enableEchoCheck->setChecked(!excludefx.contains("echo", Qt::CaseInsensitive));
    ui->enableEqualizerCheck->setChecked(!excludefx.contains("equalizer", Qt::CaseInsensitive));
    ui->enableFlangerCheck->setChecked(!excludefx.contains("flanger", Qt::CaseInsensitive));
    ui->enableModulatorCheck->setChecked(!excludefx.contains("modulator", Qt::CaseInsensitive));
    ui->enableDedicatedCheck->setChecked(!excludefx.contains("dedicated", Qt::CaseInsensitive));
}

void MainWindow::saveCurrentConfig()
{
    saveConfig(getDefaultConfigName());
    QMessageBox::information(this, tr("Information"),
                             tr("Applications using OpenAL need to be restarted for changes to take effect."));
}

void MainWindow::saveConfigAsFile()
{
    QString fname = QFileDialog::getOpenFileName(this, tr("Select Files"));
    if(fname.isEmpty() == false)
        saveConfig(fname);
}

void MainWindow::saveConfig(const QString &fname) const
{
    QSettings settings(fname, QSettings::IniFormat);

    /* HACK: Compound any stringlist values into a comma-separated string. */
    QStringList allkeys = settings.allKeys();
    foreach(const QString &key, allkeys)
    {
        QStringList vals = settings.value(key).toStringList();
        if(vals.size() > 1)
            settings.setValue(key, vals.join(QChar(',')));
    }

    QString str = ui->sampleFormatCombo->currentText();
    for(int i = 0;sampleTypeList[i].name[0];i++)
    {
        if(str == sampleTypeList[i].name)
        {
            settings.setValue("sample-type", sampleTypeList[i].value);
            break;
        }
    }

    str = ui->channelConfigCombo->currentText();
    for(int i = 0;speakerModeList[i].name[0];i++)
    {
        if(str == speakerModeList[i].name)
        {
            settings.setValue("channels", speakerModeList[i].value);
            break;
        }
    }

    uint rate = ui->sampleRateCombo->currentText().toUInt();
    if(!(rate > 0))
        settings.setValue("frequency", QString());
    else
        settings.setValue("frequency", rate);

    settings.setValue("period_size", ui->periodSizeEdit->text());
    settings.setValue("periods", ui->periodCountEdit->text());

    settings.setValue("sources", ui->srcCountLineEdit->text());
    settings.setValue("slots", ui->effectSlotLineEdit->text());

    str = ui->resamplerComboBox->currentText();
    for(int i = 0;resamplerList[i].name[0];i++)
    {
        if(str == resamplerList[i].name)
        {
            settings.setValue("resampler", resamplerList[i].value);
            break;
        }
    }

    str = ui->stereoModeCombo->currentText();
    for(int i = 0;stereoModeList[i].name[0];i++)
    {
        if(str == stereoModeList[i].name)
        {
            settings.setValue("stereo-mode", stereoModeList[i].value);
            break;
        }
    }

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

    if(ui->hrtfStateComboBox->currentIndex() == 1)
        settings.setValue("hrtf", "true");
    else if(ui->hrtfStateComboBox->currentIndex() == 2)
        settings.setValue("hrtf", "false");
    else
        settings.setValue("hrtf", QString());

    strlist.clear();
    QList<QListWidgetItem*> items = ui->hrtfFileList->findItems("*", Qt::MatchWildcard);
    foreach(const QListWidgetItem *item, items)
        strlist.append(item->text());
    settings.setValue("hrtf_tables", strlist.join(QChar(',')));

    strlist.clear();
    items = ui->enabledBackendList->findItems("*", Qt::MatchWildcard);
    foreach(const QListWidgetItem *item, items)
        strlist.append(item->text());
    items = ui->disabledBackendList->findItems("*", Qt::MatchWildcard);
    foreach(const QListWidgetItem *item, items)
        strlist.append(QChar('-')+item->text());
    if(strlist.size() == 0 && !ui->backendCheckBox->isChecked())
        strlist.append("-all");
    else if(ui->backendCheckBox->isChecked())
        strlist.append(QString());
    settings.setValue("drivers", strlist.join(QChar(',')));

    // TODO: Remove check when we can properly match global values.
    if(ui->defaultReverbComboBox->currentIndex() == 0)
        settings.setValue("default-reverb", QString());
    else
    {
        str = ui->defaultReverbComboBox->currentText().toLower();
        settings.setValue("default-reverb", str);
    }

    if(ui->emulateEaxCheckBox->isChecked())
        settings.setValue("reverb/emulate-eax", "true");
    else
        settings.setValue("reverb/emulate-eax", QString()/*"false"*/);

    strlist.clear();
    if(!ui->enableEaxReverbCheck->isChecked())
        strlist.append("eaxreverb");
    if(!ui->enableStdReverbCheck->isChecked())
        strlist.append("reverb");
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
    if(!ui->enableModulatorCheck->isChecked())
        strlist.append("modulator");
    if(!ui->enableDedicatedCheck->isChecked())
        strlist.append("dedicated");
    settings.setValue("excludefx", strlist.join(QChar(',')));

    /* Remove empty keys
     * FIXME: Should only remove keys whose value matches the globally-specified value.
     */
    allkeys = settings.allKeys();
    foreach(const QString &key, allkeys)
    {
        str = settings.value(key).toString();
        if(str == QString())
            settings.remove(key);
    }
}


void MainWindow::updatePeriodSizeEdit(int size)
{
    ui->periodSizeEdit->clear();
    if(size >= 64)
    {
        size = (size+32)&~0x3f;
        ui->periodSizeEdit->insert(QString::number(size));
    }
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
}

void MainWindow::updatePeriodCountEdit(int count)
{
    ui->periodCountEdit->clear();
    if(count >= 2)
        ui->periodCountEdit->insert(QString::number(count));
}

void MainWindow::updatePeriodCountSlider()
{
    int pos = ui->periodCountEdit->text().toInt();
    if(pos < 2)
        pos = 0;
    else if(pos > 16)
        pos = 16;
    ui->periodCountSlider->setSliderPosition(pos);
}


void MainWindow::addHrtfFile()
{
    const QStringList datapaths = getAllDataPaths("/openal/hrtf");
    QStringList fnames = QFileDialog::getOpenFileNames(this, tr("Select Files"),
                                                       datapaths.empty() ? QString() : datapaths[0],
                                                       "HRTF Datasets(*.mhr);;All Files(*.*)");
    if(fnames.isEmpty() == false)
    {
        for(QStringList::iterator iter = fnames.begin();iter != fnames.end();iter++)
        {
            QStringList::const_iterator path = datapaths.constBegin();
            for(;path != datapaths.constEnd();path++)
            {
                QDir hrtfdir(*path);
                if(!hrtfdir.isAbsolute())
                    continue;

                const QString relname = hrtfdir.relativeFilePath(*iter);
                if(!relname.startsWith(".."))
                {
                    // If filename is within this path, use the relative pathname
                    ui->hrtfFileList->addItem(relname);
                    break;
                }
            }
            if(path == datapaths.constEnd())
            {
                // Filename is not within any data path, use the absolute pathname
                ui->hrtfFileList->addItem(*iter);
            }
        }
    }
}

void MainWindow::removeHrtfFile()
{
    QList<QListWidgetItem*> selected = ui->hrtfFileList->selectedItems();
    foreach(QListWidgetItem *item, selected)
        delete item;
}

void MainWindow::updateHrtfRemoveButton()
{
    ui->hrtfRemoveButton->setEnabled(ui->hrtfFileList->selectedItems().size() != 0);
}

void MainWindow::showEnabledBackendMenu(QPoint pt)
{
    QMap<QAction*,QString> actionMap;

    pt = ui->enabledBackendList->mapToGlobal(pt);

    QMenu ctxmenu;
    QAction *removeAction = ctxmenu.addAction(QIcon::fromTheme("list-remove"), "Remove");
    if(ui->enabledBackendList->selectedItems().size() == 0)
        removeAction->setEnabled(false);
    ctxmenu.addSeparator();
    for(size_t i = 0;backendMenuList[i].backend_name[0];i++)
    {
        QAction *action = ctxmenu.addAction(backendMenuList[i].menu_string);
        actionMap[action] = backendMenuList[i].backend_name;
        if(ui->enabledBackendList->findItems(backendMenuList[i].backend_name, Qt::MatchFixedString).size() != 0 ||
           ui->disabledBackendList->findItems(backendMenuList[i].backend_name, Qt::MatchFixedString).size() != 0)
            action->setEnabled(false);
    }

    QAction *gotAction = ctxmenu.exec(pt);
    if(gotAction == removeAction)
    {
        QList<QListWidgetItem*> selected = ui->enabledBackendList->selectedItems();
        foreach(QListWidgetItem *item, selected)
            delete item;
    }
    else if(gotAction != NULL)
    {
        QMap<QAction*,QString>::const_iterator iter = actionMap.find(gotAction);
        if(iter != actionMap.end())
            ui->enabledBackendList->addItem(iter.value());
    }
}

void MainWindow::showDisabledBackendMenu(QPoint pt)
{
    QMap<QAction*,QString> actionMap;

    pt = ui->disabledBackendList->mapToGlobal(pt);

    QMenu ctxmenu;
    QAction *removeAction = ctxmenu.addAction(QIcon::fromTheme("list-remove"), "Remove");
    if(ui->disabledBackendList->selectedItems().size() == 0)
        removeAction->setEnabled(false);
    ctxmenu.addSeparator();
    for(size_t i = 0;backendMenuList[i].backend_name[0];i++)
    {
        QAction *action = ctxmenu.addAction(backendMenuList[i].menu_string);
        actionMap[action] = backendMenuList[i].backend_name;
        if(ui->disabledBackendList->findItems(backendMenuList[i].backend_name, Qt::MatchFixedString).size() != 0 ||
           ui->enabledBackendList->findItems(backendMenuList[i].backend_name, Qt::MatchFixedString).size() != 0)
            action->setEnabled(false);
    }

    QAction *gotAction = ctxmenu.exec(pt);
    if(gotAction == removeAction)
    {
        QList<QListWidgetItem*> selected = ui->disabledBackendList->selectedItems();
        foreach(QListWidgetItem *item, selected)
            delete item;
    }
    else if(gotAction != NULL)
    {
        QMap<QAction*,QString>::const_iterator iter = actionMap.find(gotAction);
        if(iter != actionMap.end())
            ui->disabledBackendList->addItem(iter.value());
    }
}
