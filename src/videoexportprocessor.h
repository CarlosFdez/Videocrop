#ifndef VIDEOCUTTER_H
#define VIDEOCUTTER_H

#include <QString>
#include <QProcess>
#include <vector>
#include <queue>
#include <memory>

#include <singlevideoprocessor.h>

using namespace std;

class VideoExportProcessor : public QObject
{
    Q_OBJECT

public:
    explicit VideoExportProcessor(QObject *parent);

    void setInputFilename(const QString& filename);

    /// Clears the ranges
    void clearRanges();

    /// adds a range in milliseconds
    void addRange(qint64 start, qint64 end);

    /// Calculates export filepaths using the export path.
    /// If there is only one item, its unchanged.
    /// If there is more than one, its base-{idx}.{extension}
    /// Assumes that the exportPath ends with an extension
    vector<QString> getFilenames(const QString& exportPath);

    /// Begins processing to export to the given export path, writing
    /// to the files that would be returned in getFilenames().
    /// The processing will begin firing signals
    void process(const QString& exportPath);

signals:
    void progress(int progress);
    void finishedItem(int idx);
    void finishedAll();

protected:
    void processNext();

protected slots:
    void on_finishedItem();
    void on_itemProgress(int progress);

private:
    QString filename;
    vector<pair<qint64, qint64>> ranges;

    SingleVideoProcessor processor;

    int currentItem;
    vector<QString> filesToProcess;
    shared_ptr<queue<int>> processQueue;
    qint64 processedDuration;
    qint64 totalDuration;
};

#endif // VIDEOCUTTER_H
