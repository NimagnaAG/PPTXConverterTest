#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_PPTXConverterTest.h"
#include "PowerPointConverter.h"

class PPTXConverterTestApp : public QMainWindow
{
  Q_OBJECT

public:
  PPTXConverterTestApp(QWidget* parent = Q_NULLPTR);
  ~PPTXConverterTestApp();

private slots:
  void on_actionOpen_triggered();
  void on_pushButton_clicked();
  void on_pushButtonCloud_clicked();
  void on_pushButtonCloudQt_clicked();

  void onConverterError(const QString& error);
  void onConverterDebug(const QString& debug);
  void onConverterProgress(float value);
  void onConverterStatusChanged(const PowerPointConverter::PowerPointConverterStatus& status);
  void onConverterDone(const QStringList& generatedFiles);

signals:
  void startProcessing(const QString& filepath, const QString& targetpath);

private:

  Ui::PPTXConverterTestUserInterface ui;
  QThread mConverterThread;
  PowerPointConverter* mConverter;
};
