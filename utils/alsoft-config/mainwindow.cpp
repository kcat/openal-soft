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
#ifdef Q_OS_WIN32
    { "mmdevapi", "Add MMDevAPI" },
    { "dsound", "Add DirectSound" },
    { "winmm", "Add Windows Multimedia" },
#endif
#ifdef Q_OS_MAC
    { "core", "Add CoreAudio" },
#endif
    { "pulse", "Add PulseAudio" },
#ifdef Q_OS_UNIX
    { "alsa", "Add ALSA" },
    { "oss", "Add OSS" },
    { "solaris", "Add Solaris" },
    { "sndio", "Add SndIO" },
    { "qsa", "Add QSA" },
#endif
    { "port", "Add PortAudio" },
    { "opensl", "Add OpenSL" },
    { "null", "Add Null Output" },
    { "wave", "Add Wave Writer" },
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
    mSampleRateValidator(NULL),
    mReverbBoostValidator(NULL)
{
    ui->setupUi(this);

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

    mReverbBoostValidator = new QDoubleValidator(-12.0, +12.0, 1, this);
    ui->reverbBoostEdit->setValidator(mReverbBoostValidator);

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

    connect(ui->reverbBoostSlider, SIGNAL(valueChanged(int)), this, SLOT(updateReverbBoostEdit(int)));
    connect(ui->reverbBoostEdit, SIGNAL(textEdited(QString)), this, SLOT(updateReverbBoostSlider(QString)));

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
    delete mReverbBoostValidator;
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
        for(int i = 1;i < ui->sampleFormatCombo->count();i++)
        {
            QString item = ui->sampleFormatCombo->itemText(i);
            if(item.startsWith(sampletype))
            {
                ui->sampleFormatCombo->setCurrentIndex(i);
                break;
            }
        }
    }

    QString channelconfig = settings.value("channels").toString();
    ui->channelConfigCombo->setCurrentIndex(0);
    if(channelconfig.isEmpty() == false)
    {
        for(int i = 1;i < ui->channelConfigCombo->count();i++)
        {
            QString item = ui->channelConfigCombo->itemText(i);
            if(item.startsWith(channelconfig))
            {
                ui->channelConfigCombo->setCurrentIndex(i);
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
    if(resampler.isEmpty())
        ui->resamplerComboBox->setCurrentIndex(0);
    else
    {
        for(int i = 1;i < ui->resamplerComboBox->count();i++)
        {
            QString item = ui->resamplerComboBox->itemText(i);
            int end = item.indexOf(' ');
            if(end < 0) end = item.size();
            if(resampler.size() == end && resampler.compare(item.leftRef(end), Qt::CaseInsensitive) == 0)
            {
                ui->resamplerComboBox->setCurrentIndex(i);
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
    ui->enableSSE41CheckBox->setChecked(!disabledCpuExts.contains("sse4.1", Qt::CaseInsensitive));
    ui->enableNeonCheckBox->setChecked(!disabledCpuExts.contains("neon", Qt::CaseInsensitive));

    if(settings.value("hrtf").toString() == QString())
        ui->hrtfEnableButton->setChecked(true);
    else
    {
        if(settings.value("hrtf", true).toBool())
            ui->hrtfForceButton->setChecked(true);
        else
            ui->hrtfDisableButton->setChecked(true);
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
    ui->reverbBoostEdit->clear();
    ui->reverbBoostEdit->insert(settings.value("reverb/boost").toString());

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
    str.truncate(str.indexOf('-'));
    settings.setValue("sample-type", str.trimmed());

    str = ui->channelConfigCombo->currentText();
    str.truncate(str.indexOf('-'));
    settings.setValue("channels", str.trimmed());

    uint rate = ui->sampleRateCombo->currentText().toUInt();
    if(rate == 0)
        settings.setValue("frequency", QString());
    else
        settings.setValue("frequency", rate);

    settings.setValue("period_size", ui->periodSizeEdit->text());
    settings.setValue("periods", ui->periodCountEdit->text());

    settings.setValue("sources", ui->srcCountLineEdit->text());
    settings.setValue("slots", ui->effectSlotLineEdit->text());

    if(ui->resamplerComboBox->currentIndex() == 0)
        settings.setValue("resampler", QString());
    else
    {
        str = ui->resamplerComboBox->currentText();
        settings.setValue("resampler", str.split(' ').first().toLower());
    }

    QStringList strlist;
    if(!ui->enableSSECheckBox->isChecked())
        strlist.append("sse");
    if(!ui->enableSSE2CheckBox->isChecked())
        strlist.append("sse2");
    if(!ui->enableSSE41CheckBox->isChecked())
        strlist.append("sse4.1");
    if(!ui->enableNeonCheckBox->isChecked())
        strlist.append("neon");
    settings.setValue("disable-cpu-exts", strlist.join(QChar(',')));

    if(ui->hrtfForceButton->isChecked())
        settings.setValue("hrtf", "true");
    else if(ui->hrtfDisableButton->isChecked())
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

    // TODO: Remove check when we can properly match global values.
    if(ui->reverbBoostSlider->sliderPosition() == 0)
        settings.setValue("reverb/boost", QString());
    else
        settings.setValue("reverb/boost", ui->reverbBoostEdit->text());

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

void MainWindow::updateReverbBoostEdit(int value)
{
    ui->reverbBoostEdit->clear();
    if(value != 0)
        ui->reverbBoostEdit->insert(QString::number(value/10.0, 'f', 1));
}

void MainWindow::updateReverbBoostSlider(QString value)
{
    int pos = int(value.toFloat()*10.0f);
    ui->reverbBoostSlider->setSliderPosition(pos);
}
