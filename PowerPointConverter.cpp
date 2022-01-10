#include "PowerPointConverter.h"

#include <QUrlQuery>
#include <QDir>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>

void PowerPointConverter::convertPowerpointFile(const QString& filepath, const QString& targetpath)
{
  // main entrypoint from the outside
  if (mCurrentStatus != PowerPointConverterStatus::kNone && mCurrentStatus != PowerPointConverterStatus::kFailure && mCurrentStatus != PowerPointConverterStatus::kFinishedConversion) {
    // conversion in progress
    emit("Conversion already in progress... Please wait!");
    return;
  }

  mCurrentStatus = PowerPointConverterStatus::kNone;

  if (!mNetworkAccessManager) {
    // needs to be created here to have it in the right thread
    mNetworkAccessManager = std::make_unique<QNetworkAccessManager>(this);
  }

  // set file and target path
  setPowerpointFile(filepath);
  setTargetPath(targetpath);

  // check
  if (mCurrentStatus != PowerPointConverterStatus::kNone) {
    emit error(QString("Start processing: Status is wrong: %1").arg(static_cast<int>(mCurrentStatus)));
    return;
  }

  // first step: upload file (will update authentication token automatically)
  uploadPresentation();
}

void PowerPointConverter::convertPowerpointFile2(const QString& filepath, const QString& targetpath)
{
  // main entrypoint from the outside
  if (mCurrentStatus != PowerPointConverterStatus::kNone && mCurrentStatus != PowerPointConverterStatus::kFailure && mCurrentStatus != PowerPointConverterStatus::kFinishedConversion) {
    // conversion in progress
    emit("Conversion already in progress... Please wait!");
    return;
  }

  mCurrentStatus = PowerPointConverterStatus::kNone;

  if (!mNetworkAccessManager) {
    // needs to be created here to have it in the right thread
    mNetworkAccessManager = std::make_unique<QNetworkAccessManager>(this);
  }

  // set file and target path
  setPowerpointFile(filepath);
  setTargetPath(targetpath);

  // check
  if (mCurrentStatus != PowerPointConverterStatus::kNone) {
    emit error(QString("Start processing: Status is wrong: %1").arg(static_cast<int>(mCurrentStatus)));
    return;
  }

  // upload and convert
  uploadAndConvert();
}

void PowerPointConverter::setPowerpointFile(const QString& filepath)
{
  mLocalFilename = "";
  mLocalFilepath = "";
  if (!QFile::exists(filepath)) {
    stopOnFailure(QString("File %1 does not exist").arg(filepath));
    return;
  }

  auto fileInfo = QFileInfo(filepath);
  auto fileSize = fileInfo.size();
  if (fileSize == 0) {
    stopOnFailure("File size = 0");
    return;
  }
  if (fileSize > 35 * 1024 * 1024) {
    // exceeding service limits of 35 MB
    stopOnFailure("File too big. Must be < 35MB");
    return;
  }

  mPresentationFile = std::make_unique<QFile>(filepath);
  if (!mPresentationFile->open(QIODevice::ReadOnly)) {
    mPresentationFile.release();
    emit error(QString("Presentation file '%1' can't be opened").arg(filepath));
    return;
  }

  // check presentation
  if (!mPresentationFile->isOpen() || !mPresentationFile->isReadable()) {
    stopOnFailure(QString("Presentation file '%1' is not set/open/readable").arg(mLocalFilename));
    return;
  }

  // file exists and can be opened
  mLocalFilename = fileInfo.fileName();
  mLocalFilepath = fileInfo.filePath();
}

void PowerPointConverter::setTargetPath(const QString& targetpath)
{
  QDir dir;
  if (!dir.exists(targetpath)) {
    if (!dir.mkpath(targetpath)) {
      emit error(QString("Target path '%1' does not exist and can't be created").arg(targetpath));
      return;
    }
  }
  mTargetPath = targetpath;
}

void PowerPointConverter::setStatus(PowerPointConverterStatus status)
{
  mCurrentStatus = status;
  emit statusChanged(mCurrentStatus);
}

void PowerPointConverter::stopOnFailure(const QString& message)
{
  emit error(message);
  setStatus(PowerPointConverterStatus::kFailure);
}

void PowerPointConverter::updateBearerToken(PowerPointConverterStatus nextStage)
{
  setStatus(PowerPointConverterStatus::kUpdateBearerToken);
  emit debug("Start Update Bearer Token");

  // keep next stage once token is successfully updated
  mStageAfterTokenUpdate = nextStage;

  QNetworkRequest request;
  request.setUrl(QUrl("https://api.aspose.cloud/connect/token"));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
  request.setRawHeader("Accept", "application/json");

  QUrlQuery postData;
  postData.addQueryItem("grant_type", "client_credentials");
  postData.addQueryItem("client_id", kClientId);
  postData.addQueryItem("client_secret", kClientSecret);
  emit debug(QString(">> Post Data: '%1'").arg(postData.toString()));

  registerNetworkReply(mNetworkAccessManager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8()), PowerPointConverterStatus::kUpdateBearerToken);
}

void PowerPointConverter::handleBearerReply(QNetworkReply* reply)
{
  if (mCurrentStatus != PowerPointConverterStatus::kUpdateBearerToken) {
    stopOnFailure(QString("Bearer reply: wrong status: %1").arg(static_cast<int>(mCurrentStatus)));
    return;
  }

  emit debug("Handle Bearer Reply");

  QJsonDocument document;
  if (!getJsonFromNetworkReply(reply, document)) return;

  QJsonObject object = document.object();
  if (!object.contains("access_token") || !object["access_token"].isString()) {
    stopOnFailure("Bearer response has no access_token field!");
    return;
  }

  mBearerToken = object.value("access_token").toString("");
  emit debug("Updated Bearer token");
  emit debug(QString("Bearer token: ") + mBearerToken);

  // potentially continue with previous stage
  if (mStageAfterTokenUpdate != PowerPointConverterStatus::kNone && mFullyAutomatic) {
    switch (mStageAfterTokenUpdate)
    {
    case PowerPointConverter::PowerPointConverterStatus::kUploadFile:
      uploadPresentation();
      break;
    case PowerPointConverter::PowerPointConverterStatus::kSplitAndConvert:
      splitPresentationAndCreatePNGs();
      break;
    case PowerPointConverter::PowerPointConverterStatus::kDownloadSlides:
      downloadQueuedSlides();
      break;
    case PowerPointConverterStatus::kUploadAndConvert:
      uploadAndConvert();
      break;
    default:
      stopOnFailure("Updated Bearer token has no next stage...");
      break;
    }
  }
}

void PowerPointConverter::uploadPresentation()
{
  if (mBearerToken.isEmpty()) {
    updateBearerToken(PowerPointConverterStatus::kUploadFile);
    return;
  }

  emit debug(QString("Start uploading presentation '%1'").arg(mLocalFilename));
  emit progress(0.0f);
  setStatus(PowerPointConverterStatus::kUploadFile);

  // a unique upload path
  mServerpathAfterUpload = QUuid::createUuid().toString();
  mServerfileAfterUpload = mLocalFilename; // assuming filename remains the same

  QNetworkRequest request;
  request.setUrl(QUrl(QString("https://api.aspose.cloud/v3.0/slides/storage/file/%1/%2").arg(mServerpathAfterUpload).arg(mServerfileAfterUpload)));
  request.setRawHeader("Authorization", QString("Bearer %1").arg(mBearerToken).toUtf8());
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
  request.setRawHeader("Accept", "application/json");

  registerNetworkReply(mNetworkAccessManager->put(request, mPresentationFile.get()), PowerPointConverterStatus::kUploadFile);
}

void PowerPointConverter::handleUploadReply(QNetworkReply* reply)
{
  if (mCurrentStatus != PowerPointConverterStatus::kUploadFile) {
    stopOnFailure(QString("Upload reply: wrong status: %1").arg(static_cast<int>(mCurrentStatus)));
    return;
  }

  emit debug("Handle Upload Reply");

  // verify answer to ensure upload was successful
  QJsonDocument document;
  if (!getJsonFromNetworkReply(reply, document)) return;

  QJsonObject object = document.object();
  if (!object.contains("uploaded") || !object["uploaded"].isArray()) {
    stopOnFailure("Upload reply does not contain an 'uploaded' array!");
    return;
  }
  auto uploadedArray = object["uploaded"].toArray();
  if (uploadedArray.count() != 1) {
    stopOnFailure("Uploaded array contains more than one value!");
    return;
  }
  if (!uploadedArray.at(0).isString()) {
    stopOnFailure("Uploaded array does not contain a string!");
    return;
  }
  auto uploadedValue = uploadedArray.at(0).toString();
  if (uploadedValue != mServerfileAfterUpload) {
    emit debug(QString("Uploaded filename differs from expected name! Using returned value %1 instead of %2").arg(uploadedValue).arg(mServerfileAfterUpload));
    mServerfileAfterUpload = uploadedValue;
  }

  if (mFullyAutomatic) splitPresentationAndCreatePNGs();
}

void PowerPointConverter::splitPresentationAndCreatePNGs()
{
  if (mBearerToken.isEmpty()) {
    updateBearerToken(PowerPointConverterStatus::kSplitAndConvert);
    return;
  }
  if (mServerfileAfterUpload.isEmpty()) {
    stopOnFailure("Split/Convert: Server filename is empty.");
    return;
  }

  emit debug("Start splitting presentation and create PNGs");
  setStatus(PowerPointConverterStatus::kSplitAndConvert);
  emit progress(0.33f);

  QUrl url;
  url.setHost("api.aspose.cloud");
  url.setScheme("https");
  url.setPath(QString("/v3.0/slides/%1/split").arg(mServerfileAfterUpload));
  QUrlQuery query;
  query.addQueryItem("folder", mServerpathAfterUpload);
  query.addQueryItem("format", "png");
  query.addQueryItem("height", "1080");
  query.addQueryItem("width", "1920");
  query.addQueryItem("destFolder", mServerpathAfterUpload + "/split");
  query.addQueryItem("fontsFolder", "fonts");
  url.setQuery(query);

  QNetworkRequest request(url);
  request.setRawHeader("Authorization", QString("Bearer %1").arg(mBearerToken).toUtf8());
  request.setRawHeader("Accept", "application/json");
  emit debug(QString(">> Split/Convert URL: '%1'").arg(url.toString()));

  mSplitAndConvertTimer = std::make_unique<QTimer>();
  connect(mSplitAndConvertTimer.get(), &QTimer::timeout, this, &PowerPointConverter::onSplitAndConvertTimerTimout);
  mSplitAndConvertTimer->start(500);
  mSplitAndConvertTimoutCounter = 0;

  registerNetworkReply(mNetworkAccessManager->post(request, QByteArray()), PowerPointConverterStatus::kSplitAndConvert);
}

void PowerPointConverter::onSplitAndConvertTimerTimout()
{
  if (mCurrentStatus == PowerPointConverterStatus::kSplitAndConvert) {
    mSplitAndConvertTimoutCounter++;
    // we assume 15 seconds (=30 timeouts of 500 ms)
    emit progress(0.33 + (static_cast<float>(mSplitAndConvertTimoutCounter) / 30));
  }
}

void PowerPointConverter::handleSplitReply(QNetworkReply* reply)
{
  if (mSplitAndConvertTimer) mSplitAndConvertTimer->stop();
  if (mCurrentStatus != PowerPointConverterStatus::kSplitAndConvert) {
    stopOnFailure(QString("Split reply: wrong status: %1").arg(static_cast<int>(mCurrentStatus)));
    return;
  }

  emit debug("Handle Split Reply");

  QJsonDocument document;
  if (!getJsonFromNetworkReply(reply, document)) return;
  QJsonObject object = document.object();
  if (!object.contains("slides") || !object["slides"].isArray()) {
    stopOnFailure("Split reply has no 'slides' array...");
    return;
  }

  QJsonArray slidesArray = object["slides"].toArray();
  mDownloadQueue.clear();
  mConvertedFiles.clear();
  for (auto element : slidesArray) {
    if (!element.isObject()) continue;
    auto elementObject = element.toObject();
    if (!elementObject.contains("href")) continue;
    auto href = elementObject["href"].toString();
    emit debug(QString("Split reply: Add to download queue %1...").arg(href));
    mDownloadQueue.push_back(href);
  }

  if (mFullyAutomatic) downloadQueuedSlides();
}

void PowerPointConverter::downloadQueuedSlides()
{
  if (mBearerToken.isEmpty()) {
    updateBearerToken(PowerPointConverterStatus::kDownloadSlides);
    return;
  }

  if (mDownloadQueue.isEmpty()) {
    stopOnFailure("Download queue is empty...");
    return;
  }

  emit debug(QString("Start downloading %1 PNGs").arg(mDownloadQueue.count()));
  setStatus(PowerPointConverterStatus::kDownloadSlides);
  emit progress(0.66f);

  for (const auto& url : mDownloadQueue) {
    downloadSlidePng(url);
  }
}

void PowerPointConverter::downloadSlidePng(const QString& url)
{
  if (mCurrentStatus != PowerPointConverterStatus::kDownloadSlides) {
    stopOnFailure(QString("Download: wrong status: %1").arg(static_cast<int>(mCurrentStatus)));
    return;
  }
  if (mBearerToken.isEmpty()) {
    stopOnFailure(("Download slide: Bearer token must be updated before..."));
    return;
  }

  emit debug(QString("Download a PNG from URL %1").arg(url));

  QNetworkRequest request;
  request.setUrl(url);
  request.setRawHeader("Authorization", QString("Bearer %1").arg(mBearerToken).toUtf8());
  request.setRawHeader("Accept", "application/json");

  registerNetworkReply(mNetworkAccessManager->get(request), PowerPointConverterStatus::kDownloadSlides);
}

void PowerPointConverter::handleDownloadReply(QNetworkReply* reply)
{
  if (mCurrentStatus != PowerPointConverterStatus::kDownloadSlides) {
    stopOnFailure(QString("Download reply: wrong status: %1").arg(static_cast<int>(mCurrentStatus)));
    return;
  }

  emit debug("Handle Download Reply");

  // extract filename from content-disposition header
  QString saveFilename;
  auto contentDispositionHeader = reply->header(QNetworkRequest::ContentDispositionHeader).toString();
  auto contentDispositionHeaderElements = contentDispositionHeader.split(";");
  for (const auto& contentDispositionHeaderElement : contentDispositionHeaderElements) {
    auto candidate = contentDispositionHeaderElement.trimmed();
    if (candidate.startsWith("filename=")) {
      saveFilename = candidate.replace("filename=", "");
    }
  }
  if (saveFilename.isEmpty()) {
    stopOnFailure(QString("Download reply: Failed to extract filename from header '%1'").arg(contentDispositionHeader));
    return;
  }

  mDownloadSyncMutex.lock();

  QString targetFile = mTargetPath + QDir::separator() + saveFilename;
  QFile file(targetFile);
  file.open(QIODevice::WriteOnly);
  file.write(reply->readAll());
  file.close();

  mConvertedFiles << targetFile;
  // progress from 0.66 -> 1.0
  emit progress(0.66f + (static_cast<float>(mConvertedFiles.count()) / mDownloadQueue.count()) * 0.33);

  emit debug(QString(">> Saved PNG as %1 into %2").arg(saveFilename).arg(mTargetPath));
  emit debug(QString(">> %1 / %2").arg(mConvertedFiles.count()).arg(mDownloadQueue.count()));

  if (mConvertedFiles.count() == mDownloadQueue.count()) {
    mDownloadQueue.clear();
    // all slides downloaded
    emit progress(1.0f);
    setStatus(PowerPointConverterStatus::kFinishedConversion);
    emit processingDone(mConvertedFiles);
  }
  mDownloadSyncMutex.unlock();
}

void PowerPointConverter::uploadAndConvert()
{
  if (mBearerToken.isEmpty()) {
    updateBearerToken(PowerPointConverterStatus::kUploadAndConvert);
    return;
  }

  emit debug(QString("Start uploading/converting presentation '%1'").arg(mLocalFilename));
  emit progress(0.0f);
  setStatus(PowerPointConverterStatus::kUploadAndConvert);


  QUrl url;
  url.setHost("api.aspose.cloud");
  url.setScheme("https");
  url.setPath(QString("/v3.0/slides/convert/Png"));
  QUrlQuery query;
  query.addQueryItem("fontsFolder", "fonts");
  //query.addQueryItem("width", "1920");
  //query.addQueryItem("height", "1080");
  //query.addQueryItem("outPath", "output");
  url.setQuery(query);

  QHttpMultiPart* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

  QHttpPart jsonPart;
  jsonPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("text/json"));
  jsonPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"data\""));
  QJsonObject jsonObj;
  jsonObj["Height"] = 1080;
  jsonObj["Width"] = 1920;
  QJsonDocument jsonDoc(jsonObj);
  QByteArray jsonData = jsonDoc.toJson();
  jsonPart.setBody(jsonData);

  QHttpPart presentationPart;
  presentationPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/octet-stream"));
  presentationPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant(QString("form-data; name=\"file0\"; filename=\"%1\"").arg(mLocalFilename)));
  presentationPart.setBodyDevice(mPresentationFile.get());

  multiPart->append(jsonPart);
  multiPart->append(presentationPart);

  QNetworkRequest request(url);
  request.setRawHeader("Authorization", QString("Bearer %1").arg(mBearerToken).toUtf8());

  auto* reply = registerNetworkReply(mNetworkAccessManager->post(request, multiPart /* mPresentationFile.get() */), PowerPointConverterStatus::kUploadAndConvert);
  multiPart->setParent(reply); // delete the multiPart with the reply
}

void PowerPointConverter::onUploadAndConvertTimerTimout()
{
  const int kExpectedConversionTimeInSecs = 15.0;
  if (mCurrentStatus == PowerPointConverterStatus::kUploadAndConvert && mSplitAndConvertTimoutCounter<= kExpectedConversionTimeInSecs*2) {
    mSplitAndConvertTimoutCounter++;
    // we assume kExpectedConversionTimeInSecs seconds and timer timeouts every 0.5 second
    emit progress(0.33 + 0.33*(static_cast<float>(mSplitAndConvertTimoutCounter) / (kExpectedConversionTimeInSecs*2)));
  }
}

void PowerPointConverter::handleUploadAndConvertReply(QNetworkReply* reply)
{
  if (mCurrentStatus != PowerPointConverterStatus::kUploadAndConvert) {
    stopOnFailure(QString("DownloadAndConvert reply: wrong status: %1").arg(static_cast<int>(mCurrentStatus)));
    return;
  }

  emit debug("Handle DownloadAndConvert Reply");

  // extract filename from content-disposition header
  QString saveFilename;
  auto contentDispositionHeader = reply->header(QNetworkRequest::ContentDispositionHeader).toString();
  auto contentDispositionHeaderElements = contentDispositionHeader.split(";");
  for (const auto& contentDispositionHeaderElement : contentDispositionHeaderElements) {
    auto candidate = contentDispositionHeaderElement.trimmed();
    if (candidate.startsWith("filename=")) {
      saveFilename = candidate.replace("filename=", "");
    }
  }
  if (saveFilename.isEmpty()) {
    emit debug(QString("Download reply: Failed to extract filename from header '%1'").arg(contentDispositionHeader));
    saveFilename = "result.zip";
  }

  QString targetFile = mTargetPath + QDir::separator() + saveFilename;
  QFile file(targetFile);
  file.open(QIODevice::WriteOnly);
  file.write(reply->readAll());
  file.close();

  mConvertedFiles << targetFile;

  emit debug(QString(">> Saved result as %1 into %2").arg(saveFilename).arg(mTargetPath));

  // all slides downloaded
  emit progress(1.0f);
  setStatus(PowerPointConverterStatus::kFinishedConversion);
  emit processingDone(mConvertedFiles);
}

QNetworkReply* PowerPointConverter::registerNetworkReply(QNetworkReply* reply, PowerPointConverterStatus stage)
{
  emit debug(QString(">> Register network reply: Stage %1, Url: %2").arg(static_cast<int>(stage)).arg(reply->request().url().toString(QUrl::PrettyDecoded)));
  // keep in map and connect signals
  mNetworkReplies[reply] = stage;
  connect(reply, &QNetworkReply::downloadProgress, this, &PowerPointConverter::onDownloadProgress);
  connect(reply, &QNetworkReply::encrypted, this, &PowerPointConverter::onEncrypted);
  connect(reply, &QNetworkReply::errorOccurred, this, &PowerPointConverter::onErrorOccurred);
  connect(reply, &QNetworkReply::sslErrors, this, &PowerPointConverter::onSslErrors);
  connect(reply, &QNetworkReply::uploadProgress, this, &PowerPointConverter::onUploadProgress);
  connect(reply, &QNetworkReply::finished, this, &PowerPointConverter::onReplyFinished);
  return reply;
}

bool PowerPointConverter::getJsonFromNetworkReply(QNetworkReply* reply, QJsonDocument& document)
{
  if (!reply->header(QNetworkRequest::ContentTypeHeader).toString().contains("application/json")) {
    emit debug("Extract Json reply: No JSON content type!");
    emit debug(QString("Content type received: %1").arg(reply->header(QNetworkRequest::ContentTypeHeader).toString()));
    emit debug(reply->readAll());
    return false;
  }

  document = QJsonDocument::fromJson(reply->readAll());
  if (document.isNull() || document.isEmpty()) {
    emit debug("Failed to read JSON from reply!");
    return false;
  }

  emit debug(QString("Json response: %1").arg(QString(document.toJson(QJsonDocument::Compact))));

  QJsonObject object = document.object();
  if (object.contains("error")) {
    auto errorObject = object["error"].toObject();
    if (errorObject.contains("code")) {
      emit debug("JSON reply contains error code!");
    }
  }

  return true;
}

void PowerPointConverter::onRequestFinished(QNetworkReply* reply)
{
  emit debug("Request finished");
  reply->deleteLater();
}

void PowerPointConverter::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
  emit debug(QString("Download progress: %1/%2").arg(bytesReceived).arg(bytesTotal));
  if (mCurrentStatus == PowerPointConverterStatus::kUploadAndConvert) {
    emit progress(0.66 + bytesReceived / static_cast<float>(bytesTotal));
  }
}

void PowerPointConverter::onErrorOccurred(QNetworkReply::NetworkError code)
{
  QString error;
  switch (code)
  {
  case QNetworkReply::NoError:
    error = "No Error";
    break;
  case QNetworkReply::ConnectionRefusedError:
    error = "Connection refused";
    break;
  case QNetworkReply::RemoteHostClosedError:
    error = "Remote host closed";
    break;
  case QNetworkReply::HostNotFoundError:
    error = "Host not found";
    break;
  case QNetworkReply::TimeoutError:
    error = "Timeout";
    break;
  case QNetworkReply::OperationCanceledError:
    error = "Operation canceled";
    break;
  case QNetworkReply::SslHandshakeFailedError:
    error = "Ssh handshake failed";
    break;
  case QNetworkReply::TemporaryNetworkFailureError:
    error = "Temporary network failure";
    break;
  case QNetworkReply::NetworkSessionFailedError:
    error = "Network session failed";
    break;
  case QNetworkReply::BackgroundRequestNotAllowedError:
    error = "Background request not allowed";
    break;
  case QNetworkReply::TooManyRedirectsError:
    error = "Too many redirects";
    break;
  case QNetworkReply::InsecureRedirectError:
    error = "Insecure redirect";
    break;
  case QNetworkReply::UnknownNetworkError:
    error = "Unknown";
    break;
  case QNetworkReply::ProxyConnectionRefusedError:
    error = "Proxy connection refused";
    break;
  case QNetworkReply::ProxyConnectionClosedError:
    error = "Proxy connection closed";
    break;
  case QNetworkReply::ProxyNotFoundError:
    error = "Proxy not found";
    break;
  case QNetworkReply::ProxyTimeoutError:
    error = "Proxy timeout";
    break;
  case QNetworkReply::ProxyAuthenticationRequiredError:
    error = "Proxy authentication required";
    break;
  case QNetworkReply::UnknownProxyError:
    error = "Unknown proxy error";
    break;
  case QNetworkReply::ContentAccessDenied:
    error = "Content access denied";
    break;
  case QNetworkReply::ContentOperationNotPermittedError:
    error = "Content operation not permitted";
    break;
  case QNetworkReply::ContentNotFoundError:
    error = "Content not found";
    break;
  case QNetworkReply::AuthenticationRequiredError:
    error = "Authentication required";
    break;
  case QNetworkReply::ContentReSendError:
    error = "Content resend error";
    break;
  case QNetworkReply::ContentConflictError:
    error = "Content conflict error";
    break;
  case QNetworkReply::ContentGoneError:
    error = "Content gone error";
    break;
  case QNetworkReply::UnknownContentError:
    error = "Unknown content error";
    break;
  case QNetworkReply::ProtocolUnknownError:
    error = "Protocol unknown";
    break;
  case QNetworkReply::ProtocolInvalidOperationError:
    error = "Protocol invalid operation";
    break;
  case QNetworkReply::ProtocolFailure:
    error = "Protocol failure";
    break;
  case QNetworkReply::InternalServerError:
    error = "Internal server error";
    break;
  case QNetworkReply::OperationNotImplementedError:
    error = "Operation not implemented";
    break;
  case QNetworkReply::ServiceUnavailableError:
    error = "Service unavailable";
    break;
  case QNetworkReply::UnknownServerError:
    error = "Unknown server error";
    break;
  default:
    break;
  }
  stopOnFailure(QString("Error: %1").arg(error));
}

void PowerPointConverter::onSslErrors(const QList<QSslError>& errors)
{
  for (const auto& error : errors) {
    emit debug(QString("SSL Error: %1").arg(error.errorString()));
  }
  stopOnFailure("SSL errors occurred");
}

void PowerPointConverter::onUploadProgress(qint64 bytesSent, qint64 bytesTotal)
{
  emit debug(QString("Upload progress: %1/%2").arg(bytesSent).arg(bytesTotal));
  if (mCurrentStatus == PowerPointConverterStatus::kUploadFile) {
    // upload is from 0->0.33
    emit progress(0.33 * bytesSent / static_cast<float>(bytesTotal));
  }
  if (mCurrentStatus == PowerPointConverterStatus::kUploadAndConvert) {
    emit progress(0.33 * bytesSent / static_cast<float>(bytesTotal));
    if (bytesSent == bytesTotal) {
      mSplitAndConvertTimer = std::make_unique<QTimer>();
      connect(mSplitAndConvertTimer.get(), &QTimer::timeout, this, &PowerPointConverter::onUploadAndConvertTimerTimout);
      mSplitAndConvertTimer->start(500);
      mSplitAndConvertTimoutCounter = 0;

    }
  }
}

void PowerPointConverter::onReplyFinished()
{
  // get reply and find in reply map
  auto reply = static_cast<QNetworkReply*>(sender());
  auto iter = mNetworkReplies.find(reply);
  if (iter != mNetworkReplies.end()) {
    PowerPointConverterStatus stage = iter->second;
    emit debug(QString("Reply finished stage %1").arg(static_cast<int>(stage)));
    // remove from map
    mNetworkReplies.erase(iter);
    // get headers
    for (const auto& pair : reply->rawHeaderPairs()) {
      emit debug(QString(">> Reply header: %1: %2").arg(QString::fromUtf8(pair.first)).arg(QString::fromUtf8(pair.second)));
    }

    if (mCurrentStatus == PowerPointConverterStatus::kFailure) {
      // probably, an error occurred, the answer might be JSON and contains an error message
      QJsonDocument document;
      getJsonFromNetworkReply(reply, document);
    }
    else {
      // handle reply depending on stage
      switch (stage)
      {
      case PowerPointConverterStatus::kUpdateBearerToken:
        handleBearerReply(reply);
        break;
      case PowerPointConverterStatus::kUploadFile:
        handleUploadReply(reply);
        break;
      case PowerPointConverterStatus::kSplitAndConvert:
        handleSplitReply(reply);
        break;
      case PowerPointConverterStatus::kDownloadSlides:
        handleDownloadReply(reply);
        break;
      case PowerPointConverterStatus::kUploadAndConvert:
        handleUploadAndConvertReply(reply);
        break;
      case PowerPointConverterStatus::kNone:
      case PowerPointConverterStatus::kFailure:
      default:
        stopOnFailure(QString("Reply finished: Unknown stage !!!"));
        break;
      }
    }
  }
  else {
    stopOnFailure(QString("A reply finished but it is not in the map. Url: %1").arg(reply->request().url().toString(QUrl::PrettyDecoded)));
  }
}

void PowerPointConverter::onEncrypted()
{
  emit debug("Transmission is encrypted...");
}
