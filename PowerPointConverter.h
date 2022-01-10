#pragma once
#include <QObject>
#include <QThread>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QFile>
#include <QMutex>
#include <QTimer>
#include <deque>

// Convert a Powerpoint file using Aspose cloud service

class PowerPointConverter : public QObject
{
  Q_OBJECT

public:
  // the stages of conversion
  enum class PowerPointConverterStatus {
    kNone,
    kFailure,
    kUpdateBearerToken,
    kUploadFile,
    kSplitAndConvert,
    kDownloadSlides,
    kFinishedConversion,
    kUploadAndConvert,
  };

public slots:
  void convertPowerpointFile(const QString& filepath, const QString& targetpath);
  void convertPowerpointFile2(const QString& filepath, const QString& targetpath);

signals:
  void processingDone(const QStringList& createdPngs);
  void error(const QString&);
  void progress(float);
  void statusChanged(const PowerPointConverterStatus& newStatus);
  void debug(const QString&);

private slots:
  // network access manager signal handling
  void onRequestFinished(QNetworkReply* reply);

  // network reply signal handling
  void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
  void onErrorOccurred(QNetworkReply::NetworkError code);
  void onSslErrors(const QList<QSslError>& errors);
  void onUploadProgress(qint64 bytesSent, qint64 bytesTotal);
  void onReplyFinished();
  void onEncrypted();

private:

  void setPowerpointFile(const QString& filepath);
  void setTargetPath(const QString& targetpath);

  void setStatus(PowerPointConverterStatus status);
  void stopOnFailure(const QString& message);

  // update the authentication token
  void updateBearerToken(PowerPointConverterStatus nextStage);
  void handleBearerReply(QNetworkReply* reply);
  // upload the presentation
  void uploadPresentation();
  void handleUploadReply(QNetworkReply* reply);
  // split the presentation into PNGs
  void splitPresentationAndCreatePNGs();
  void onSplitAndConvertTimerTimout();
  void handleSplitReply(QNetworkReply* reply);
  // queue up the slide downloads
  void downloadQueuedSlides();
  // download a single slide png
  void downloadSlidePng(const QString& url);
  void handleDownloadReply(QNetworkReply* reply);

  void uploadAndConvert();
  void onUploadAndConvertTimerTimout();
  void handleUploadAndConvertReply(QNetworkReply* reply);

  // service authentication information
  const QString kClientId = "af708600-5a90-4fd7-a119-0c644fa4fe1c";
  const QString kClientSecret = "adac2fcf6871ad12711270c0cde60e2b";
  QString mBearerToken;
  // network handling  
  std::unique_ptr<QNetworkAccessManager> mNetworkAccessManager;
  std::map<QNetworkReply*, PowerPointConverterStatus> mNetworkReplies;
  QNetworkReply* registerNetworkReply(QNetworkReply* reply, PowerPointConverterStatus stage);
  bool getJsonFromNetworkReply(QNetworkReply* reply, QJsonDocument& document);

  // status
  PowerPointConverterStatus mCurrentStatus = PowerPointConverterStatus::kNone;
  PowerPointConverterStatus mStageAfterTokenUpdate = PowerPointConverterStatus::kNone;

  // the local file to convert
  std::unique_ptr<QFile> mPresentationFile;
  QString mLocalFilename;
  QString mLocalFilepath;

  // server information
  QString mServerpathAfterUpload = "folder";
  QString mServerfileAfterUpload;

  // the split/convert timer to indicate some progress
  std::unique_ptr<QTimer> mSplitAndConvertTimer;
  int mSplitAndConvertTimoutCounter;

  // download information
  QString mTargetPath = ".";
  QStringList mDownloadQueue;
  bool mFullyAutomatic = true;
  QMutex mDownloadSyncMutex;

  // the output
  QStringList mConvertedFiles;
};
