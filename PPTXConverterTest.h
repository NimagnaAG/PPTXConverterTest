#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_PPTXConverterTest.h"

class PPTXConverterTestApp : public QMainWindow
{
  Q_OBJECT

public:
  PPTXConverterTestApp(QWidget* parent = Q_NULLPTR);

private slots:
  void on_actionOpen_triggered();
  void on_pushButton_clicked();

private:
  Ui::PPTXConverterTestUserInterface ui;
};
