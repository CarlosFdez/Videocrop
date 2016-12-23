#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <iostream>
#include <memory>
#include <sstream>

#include <QString>
#include <QtDebug>
#include <QDropEvent>
#include <QMimeData>
#include <QMessageBox>
#include <QStringList>
#include <QProgressDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <math.h>
#include <QtAV_Global.h>

#include <util.h>
#include <videoexportprocessor.h>

using namespace std;

// todo: move somewhere else
const qint64 SEEK_JUMP = 2000;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    exportProcessor(this)
{
    ui->setupUi(this);
    this->setAcceptDrops(true);

    videoOutput = make_shared<QtAV::VideoOutput>(this);
    if (!videoOutput->widget()) {
        QMessageBox::warning(0, QString::fromLatin1("QtAV error"), tr("Can not create video renderer"));
        return;
    }

    videoPlayer = make_shared<QtAV::AVPlayer>();
    videoPlayer->setRenderer(videoOutput.get());
    videoPlayer->setSeekType(QtAV::SeekType::KeyFrameSeek);
    videoPlayer->setMediaEndAction(QtAV::MediaEndActionFlag::MediaEndAction_Pause);

    // set seek bar
    seekbar = new VideoSeekBar();
    QVBoxLayout *seekLayout = new QVBoxLayout();
    seekLayout->setContentsMargins(0, 0, 0, 0);
    seekLayout->addWidget(seekbar);
    ui->seekContainer->setLayout(seekLayout);

    // set video widget
    QVBoxLayout* layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(videoOutput->widget());
    ui->videoWidget->setLayout(layout);

    //this->setFocusPolicy(Qt::StrongFocus);
    //ui->rangeInput->installEventFilter(this);

    // default icon states
    ui->togglePlayButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    ui->speedDecreaseButton->setIcon(style()->standardIcon(QStyle::SP_MediaSeekBackward));
    ui->speedIncreaseButton->setIcon(style()->standardIcon(QStyle::SP_MediaSeekForward));
    ui->trimLeftButton->setText("[");
    ui->splitMiddleButton->setText("|");
    ui->trimRightButton->setText("]");

    // wire up events
    connect(videoPlayer.get(), SIGNAL(loaded()), SLOT(on_playerLoaded()));
    connect(videoPlayer.get(), SIGNAL(positionChanged(qint64)), SLOT(on_playerPositionChanged(qint64)));
    connect(videoPlayer.get(), SIGNAL(stateChanged(QtAV::AVPlayer::State)), SLOT(on_playerStateChanged(QtAV::AVPlayer::State)));
    connect(seekbar, SIGNAL(positionSeeked(qint64)), SLOT(on_seek(qint64)));
    connect(&exportProcessor, SIGNAL(progress(int)), SLOT(on_exportProgress(int)));
    connect(&exportProcessor, SIGNAL(finishedItem(int)), SLOT(on_exportedItem(int)));
    connect(&exportProcessor, SIGNAL(finishedAll()), SLOT(on_exportFinished()));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *evt)
{
    const QMimeData* mimeData = evt->mimeData();
    if (mimeData->hasUrls())
    {
        QUrl firstUrl = mimeData->urls().first();
        this->openFile(firstUrl.toLocalFile());
    }
}

void MainWindow::keyPressEvent(QKeyEvent *evt)
{
    switch (evt->key())
    {
    case Qt::Key_Space:
        this->videoPlayer->togglePause();
        break;

    case Qt::Key_Left:
        // perform keyframe seek if default ever changes to accurate seek
        videoPlayer->seek(videoPlayer->position() - SEEK_JUMP);
        break;

    case Qt::Key_Right:
        // perform keyframe seek if default ever changes to accurate seek
        videoPlayer->seek(videoPlayer->position() + SEEK_JUMP);
        break;

    default:
        QMainWindow::keyPressEvent(evt);
    }
}

void MainWindow::syncRangesToText()
{
    ui->rangeInput->property("plainText").toString();
    QString newText = "";
    for (pair<qint64, qint64> range : ranges)
    {
        QString first = millisecondsToTimestamp(range.first, false);
        QString second = millisecondsToTimestamp(range.second, false);
        newText += first + "," + second + "\n";
    }
    qDebug() << newText;
    ui->rangeInput->setPlainText(newText);
}

void MainWindow::openFile(const QString& filename)
{
    qDebug() << "opening " << filename;
    this->filename = filename;

    if (videoPlayer->isPlaying())
        videoPlayer->stop();

    videoPlayer->setFile(filename);
    videoPlayer->load();
}

void MainWindow::on_playerLoaded()
{
    qDebug() << "video loaded";
    this->setWindowTitle(this->filename);
    videoPlayer->play(); // temp

    seekbar->setVideoLength(videoPlayer->duration());

    QString endTime = millisecondsToTimestamp(videoPlayer->duration(), false);
    auto rangeTxt = QString("%1, %2").arg("00:00", endTime);
    ui->rangeInput->setPlainText(rangeTxt);
}

void MainWindow::on_playerPositionChanged(qint64 position)
{
    bool hasHours = testHasHours(videoPlayer->duration());
    QString positionTime = millisecondsToTimestamp(position, hasHours);
    QString totalTime = millisecondsToTimestamp(videoPlayer->duration(), hasHours);
    qreal speed = videoPlayer->speed();

    QString label = QString("(x%1) %2 / %3").arg(speed).arg(positionTime, totalTime);
    ui->progressLabel->setText(label);

    seekbar->setPosition(position);
}

void MainWindow::on_playerStateChanged(QtAV::AVPlayer::State state)
{
    QIcon playIcon;

    if (state == QtAV::AVPlayer::State::PlayingState)
        playIcon = style()->standardIcon(QStyle::SP_MediaPause);
    else
        playIcon = style()->standardIcon(QStyle::SP_MediaPlay);
    ui->togglePlayButton->setIcon(playIcon);
}

void MainWindow::on_togglePlayButton_clicked(bool)
{
    qDebug() << "clicked play button; was playing" << videoPlayer->isPlaying();
    videoPlayer->togglePause();
}

void MainWindow::on_seek(qint64 position)
{
    videoPlayer->seek(position);
}

void MainWindow::on_rangeInput_textChanged()
{
    ranges.clear();

    QString text = ui->rangeInput->property("plainText").toString();
    QStringList lines = text.split('\n');
    for (QString line : lines)
    {
        QStringList sections = line.split(',');
        if (sections.count() != 2)
            continue;

        qint64 first = timestampToMilliseconds(sections[0].trimmed());
        qint64 second = timestampToMilliseconds(sections[1].trimmed());

        if (first < 0 || second < 0)
            continue;

        qDebug() << "times " << first << " - " << second;
        ranges.push_back(pair<qint64, qint64>(first, second));
    }

    seekbar->setRanges(ranges);
}

void MainWindow::on_exportButton_clicked()
{
    // todo: show what files conflict, allow overwriting

    QFileInfo fileToExport(this->filename);
    if (!fileToExport.exists() || !fileToExport.isFile() )
    {
        QMessageBox message;
        message.setText("Source file does not exist");
        message.exec();
        return;
    }

    QString parentDir = fileToExport.dir().absolutePath();

    videoPlayer->pause();

    QString outputFilename = QFileDialog::getSaveFileName(
                this, "Save File", parentDir, "Video Files (*.mp4)");

    if (outputFilename == "")
    {
        qDebug() << "cancelled export";
        return;
    }

    if (!outputFilename.toLower().endsWith(".mp4"))
        outputFilename += ".mp4";

    qDebug() << "output filename " << outputFilename;

    exportProcessor.setFilename(this->filename);
    for (pair<qint64, qint64> range : ranges)
    {
        exportProcessor.addRange(range.first, range.second);
    }

    vector<QString> generatedFiles = exportProcessor.getFilenames(outputFilename);
    vector<QString> existingFiles = checkExistingFiles(generatedFiles);

    if (existingFiles.size() > 0)
    {
        QMessageBox message;
        message.setText("This will override an existing file");
        message.exec();
        return;
    }

    exportDialog = make_shared<QProgressDialog>(this);
    exportDialog->setSizeGripEnabled(false);
    exportDialog->setWindowModality(Qt::WindowModal);
    exportDialog->setLabelText("Exporting files...");
    exportDialog->setRange(0, 100);
    connect(exportDialog.get(), SIGNAL(canceled()), SLOT(on_exportCancelled()));

    exportDialog->open();

    exportProcessor.process(outputFilename);
}

void MainWindow::on_exportCancelled()
{
    qDebug() << "attempted cancel";
    // todo: implement
}

void MainWindow::on_exportedItem(int itemIdx)
{
    qDebug() << "Exported file number " << itemIdx;
    // todo: update label
}

void MainWindow::on_exportFinished()
{
    qDebug() << "Export complete" << endl;
    exportDialog->close();
}

void MainWindow::on_exportProgress(int progress)
{
    exportDialog->setValue(progress);
}

void MainWindow::on_speedDecreaseButton_clicked()
{
    qreal newSpeed = videoPlayer->speed() - 0.5;
    newSpeed = max(newSpeed, 1.0);
    videoPlayer->setSpeed(newSpeed);
}

void MainWindow::on_speedIncreaseButton_clicked()
{
    qreal newSpeed = videoPlayer->speed() + 0.5;
    newSpeed = min(newSpeed, 6.0);
    videoPlayer->setSpeed(newSpeed);
}

void MainWindow::on_trimLeftButton_clicked()
{
    // todo: replace with something that also can extend the range into empty space
    // helper function that gets current intersection or before would help
    qint64 position = videoPlayer->position();
    for (pair<qint64, qint64> &range : ranges)
    {
        if (range.first < position && range.second > position)
            range.first = position;
    }

    // if we're after all ranges, then create a new one at the end
    if (ranges.empty() || ranges.back().second < position)
    {
        ranges.push_back(pair<qint64, qint64>(position, videoPlayer->duration()));
    }

    syncRangesToText();
    qDebug() << "performed trim left";
}

void MainWindow::on_splitMiddleButton_clicked()
{
    vector<pair<qint64, qint64>> oldRanges = ranges;
    ranges.clear();

    qint64 position = videoPlayer->position();
    for (pair<qint64, qint64> &range : oldRanges)
    {
        if (range.first < position && range.second > position)
        {
            ranges.push_back(pair<qint64, qint64>(range.first, position));
            ranges.push_back(pair<qint64, qint64>(position, range.second));
        }
        else
        {
            ranges.push_back(range);
        }
    }

    syncRangesToText();
    qDebug() << "performed split";
}

void MainWindow::on_trimRightButton_clicked()
{
    // todo: replace with something that also can extend the range into empty space
    // helper function that gets current intersection or before would help
    qint64 position = videoPlayer->position();
    for (pair<qint64, qint64> &range : ranges)
    {
        if (range.first < position && range.second > position)
            range.second = position;
    }

    // if we're before all ranges, then create a new one at the start
    if (ranges.empty() || ranges.front().first > position)
    {
        ranges.insert(ranges.begin(), pair<qint64, qint64>(0, position));
    }

    syncRangesToText();
    qDebug() << "performed trim right";
}