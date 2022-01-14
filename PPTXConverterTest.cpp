#include "PPTXConverterTest.h"

#include <QFileDialog>
#include <QTime>
#include <QBitmap>
#include <QProcess>

#include <Export/SaveFormat.h>
#include <DOM/Presentation.h>
#include <DOM/ISlideCollection.h>
#include <DOM/ISlide.h>
#include <DOM/ISlidesize.h>
#include <drawing/imaging/image_format.h>
#include <drawing/image.h>
#include <system/string.h>
#include <system/io/stream.h>
#include <system/io/memory_stream.h>
#include <system/io/file_stream.h>
#include <drawing/bitmap.h>
#include <system/io/directory.h>

//using namespace Aspose::Slides;
//using System::Drawing::Imaging::ImageFormat;
//using System::IO::Path;

PPTXConverterTestApp::PPTXConverterTestApp(QWidget* parent)
  : QMainWindow(parent)
{
  ui.setupUi(this);
  ui.progressBar->setValue(0);
  ui.scrollArea->setBackgroundRole(QPalette::Dark);

  mConverter = new PowerPointConverter();
  mConverter->moveToThread(&mConverterThread);

  connect(mConverter, &PowerPointConverter::processingDone, this, &PPTXConverterTestApp::onConverterDone);
  connect(mConverter, &PowerPointConverter::error, this, &PPTXConverterTestApp::onConverterError);
  connect(mConverter, &PowerPointConverter::progress, this, &PPTXConverterTestApp::onConverterProgress);
  connect(mConverter, &PowerPointConverter::statusChanged, this, &PPTXConverterTestApp::onConverterStatusChanged);
  connect(mConverter, &PowerPointConverter::debug, this, &PPTXConverterTestApp::onConverterDebug);
  connect(this, &PPTXConverterTestApp::startProcessing, mConverter, &PowerPointConverter::convertPowerpointFile2);
  mConverterThread.start();
}

PPTXConverterTestApp::~PPTXConverterTestApp()
{
  mConverter->deleteLater();
  mConverterThread.quit();
  mConverterThread.wait();
}

void PPTXConverterTestApp::on_actionOpen_triggered()
{
  QString filename = QFileDialog::getOpenFileName(this, "Open");
  ui.lineEdit->setText(filename);
}

void PPTXConverterTestApp::on_pushButton_clicked()
{
  auto filename = ui.lineEdit->text();
  if (filename.isEmpty()) return;

  ui.plainTextEdit->clear();
  while (ui.scrollAreaWidgetContents->layout()->count() > 0) {
    auto* item = ui.scrollAreaWidgetContents->layout()->itemAt(0);
    ui.scrollAreaWidgetContents->layout()->removeItem(item);
    delete item->widget();
    delete item;
  }

  QTime time;
  System::String input(filename.toStdU16String());

  time.start();
  auto pres = System::MakeObject<Aspose::Slides::Presentation>(input);
  ui.plainTextEdit->appendPlainText(QString("Opening %1 took %2 ms...").arg(filename).arg(time.elapsed()));

  auto count = pres->get_Slides()->get_Count();
  auto size = pres->get_SlideSize()->get_Size();
  auto sizeW = size.get_Width();
  auto sizeH = size.get_Height();
  ui.plainTextEdit->appendPlainText(QString("> %1 pages").arg(count));
  ui.plainTextEdit->appendPlainText(QString("> Slide size: w=%1, h=%2").arg(sizeW).arg(sizeH));
  ui.progressBar->setMaximum(count - 1);
  ui.progressBar->setValue(0);
  QApplication::processEvents();

  int desiredW = ui.spinBoxX->value();
  int desiredH = ui.spinBoxY->value();
  float ScaleX = (float)(1.0 / sizeW) * desiredW;
  float ScaleY = (float)(1.0 / sizeH) * desiredH;
  auto PngScale = ui.doubleSpinBox->value();


  ui.plainTextEdit->appendPlainText(QString("\nStarting conversion to SVG").arg(filename));
  for (int i = 0; i < count; ++i)
  {
    auto slide = pres->get_Slides()->idx_get(i);
    ui.plainTextEdit->appendPlainText(QString("> Page %1/%2").arg(i + 1).arg(count));

    // save as SVG
    time.start();
    System::String outputSlideNameSvg = System::IO::Path::GetFileNameWithoutExtension(input) + u"_" + System::ObjectExt::ToString(i) + u".svg";
    auto fileStream = System::MakeObject<System::IO::FileStream>(outputSlideNameSvg, System::IO::FileMode::Create, System::IO::FileAccess::Write);
    slide->WriteAsSvg(fileStream);
    ui.plainTextEdit->appendPlainText(QString("> SVG %1/%2 : %3ms").arg(sizeW * PngScale).arg(sizeH * PngScale).arg(time.elapsed()));

    // get thumbnail bitmap
    time.start();
    auto fullres = slide->GetThumbnail(PngScale, PngScale);
    auto thumbnail = slide->GetThumbnail(ScaleX, ScaleY);
    ui.plainTextEdit->appendPlainText(QString("> GetThumbnails : %3ms").arg(time.elapsed()));

    // save to PNG
    time.start();
    System::String outputSlideNamePng = System::IO::Path::GetFileNameWithoutExtension(input) + u"_" + System::ObjectExt::ToString(i) + u".png";
    slide->GetThumbnail(PngScale, PngScale)->Save(outputSlideNamePng, System::Drawing::Imaging::ImageFormat::get_Png());
    ui.plainTextEdit->appendPlainText(QString("> PNG %1/%2 : %3ms").arg(sizeW * PngScale).arg(sizeH * PngScale).arg(time.elapsed()));

    // save to memory BMP full res
    time.start();
    auto iostream = System::MakeObject<System::IO::MemoryStream>();
    fullres->Save(iostream.dynamic_pointer_cast<System::IO::Stream>(), System::Drawing::Imaging::ImageFormat::get_MemoryBmp());
    auto buffer = iostream->GetBuffer();
    auto dataptr = buffer->data_ptr();
    ui.plainTextEdit->appendPlainText(QString("> Memory bitmap %1/%2 : %3ms").arg(sizeW * PngScale).arg(sizeH * PngScale).arg(time.elapsed()));

    //ui.plainTextEdit->appendPlainText(QString("> Thumb: %1/%2: %3ms").arg(sizeW * ScaleX).arg(sizeH * ScaleY).arg(time.elapsed()));

    // qt stuff
    ui.progressBar->setValue(i);

    // create label to show thumbnail
    auto* imageLabel = new QLabel;
    imageLabel->setStyleSheet("border: 1px solid black");
    QImage image;
    image.loadFromData(dataptr, buffer->get_Length());
    imageLabel->setPixmap(QPixmap::fromImage(image));
    ui.scrollAreaWidgetContents->layout()->addWidget(imageLabel);

    // update ui
    QApplication::processEvents();
  }
}

#include <asposeslidescloud/api/SlidesApi.h>
#include <asposeslidescloud/model/ExportOptions.h>

void PPTXConverterTestApp::on_pushButtonCloud_clicked()
{
  auto filename = ui.lineEdit->text();
  ui.plainTextEdit->clear();
  ui.plainTextEdit->appendPlainText(QString("Convert %1 on the cloud").arg(filename));

  auto input = utility::conversions::to_string_t((filename.toUtf8().constData()));

  auto api = std::make_shared<asposeslidescloud::api::SlidesApi>(utility::conversions::to_string_t("71ac8316-49ca-4e4a-84dd-7d5941ccaafc"), utility::conversions::to_string_t("8dc30cfec1501a6b574b08de50cdca4c"));
  auto httpRequest = std::make_shared<asposeslidescloud::api::HttpContent>();

  auto inputfilestream = std::make_shared<std::ifstream>(filename.toUtf8().constData(), std::ios::binary);
  ui.plainTextEdit->appendPlainText(QString("> Filesize: %1 bytes").arg(inputfilestream->gcount()));
  httpRequest->setData(inputfilestream);

  auto format = utility::conversions::to_string_t("Png");
  auto password = utility::string_t();
  auto storage = utility::string_t();
  auto fontsFolder = utility::conversions::to_string_t("fonts");
  auto slides = std::vector<int32_t>();
  auto exportOptions = std::make_shared<asposeslidescloud::model::ExportOptions>();
  exportOptions->setHeight(1080);
  exportOptions->setWidth(1920);
  exportOptions->setDefaultRegularFont(utility::conversions::to_string_t("Arial"));

  QTime time;
  time.start();
  const bool saveToCloud = false;
  if (saveToCloud) {
    // save to cloud storage
    auto outpath = utility::conversions::to_string_t("test234");
    api->convertAndSave(httpRequest, format, outpath).get();
    // saved as test234.zip
  }
  else {
    // save locally
    std::ofstream fs("output.zip", std::ios::binary);
    auto response = api->convert(httpRequest, format, password, storage, fontsFolder, slides, exportOptions).get();
    ui.plainTextEdit->appendPlainText(QString("> Upload, convert, and download took %1 ms").arg(time.elapsed()));
    time.start();
    response.writeTo(fs);
    fs.close();
    ui.plainTextEdit->appendPlainText(QString("> Saving locally took %1 ms").arg(time.elapsed()));
  }
}

void PPTXConverterTestApp::on_pushButtonCloudQt_clicked()
{
  ui.pushButtonCloudQt->setDisabled(true);
  ui.progressBar->setMaximum(100);
  ui.progressBar->setValue(0);
  emit startProcessing(ui.lineEdit->text(), "download");
}

void PPTXConverterTestApp::on_actionExtract_triggered()
{
  QProcess process;
  process.setWorkingDirectory("download");
  process.setProgram("tar");
  process.setArguments(QStringList() << "-x" << "-f" << "converted.zip");
  process.start();
  //process.execute(QString("tar -x -f %1").arg("converted.zip"));
}

void PPTXConverterTestApp::onConverterError(const QString& error)
{
  ui.plainTextEdit->appendPlainText(QString("Converter Error: %1").arg(error));
  ui.pushButtonCloudQt->setDisabled(false);
}

void PPTXConverterTestApp::onConverterDebug(const QString& debug)
{
  ui.plainTextEdit->appendPlainText(QString("Converter Debug: %1").arg(debug));
}

void PPTXConverterTestApp::onConverterProgress(float value)
{
  ui.progressBar->setValue(value * 100);
}

void PPTXConverterTestApp::onConverterStatusChanged(const PowerPointConverter::PowerPointConverterStatus& status)
{
  ui.plainTextEdit->appendPlainText(QString("Converter Status Change : %1 ---------------------").arg(static_cast<int>(status)));
}

void PPTXConverterTestApp::onConverterDone(const QStringList& generatedFiles)
{
  ui.plainTextEdit->appendPlainText("Converter Status Done!");
  for (const auto& file : generatedFiles) {
    ui.plainTextEdit->appendPlainText(QString("> %1").arg(file));
  }
  ui.pushButtonCloudQt->setDisabled(false);
}
