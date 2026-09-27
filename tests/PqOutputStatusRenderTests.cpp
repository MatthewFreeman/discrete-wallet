#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFrame>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>

#include "ui_accountframe.h"
#include "ui_infodialog.h"

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << std::endl;
    std::exit(1);
  }
}

QString outputPath(int argc, char** argv, int index, const char* fallback) {
  return argc > index ? QString::fromLocal8Bit(argv[index])
                      : QDir::current().filePath(QString::fromLatin1(fallback));
}

void render(QWidget& widget, const QString& path) {
  QPixmap image(widget.size());
  image.fill(Qt::transparent);
  widget.render(&image);
  require(image.save(path, "PNG"), "failed to save maintenance UI render");
}

}  // namespace

int main(int argc, char** argv) {
  QApplication application(argc, argv);
#ifdef Q_OS_WIN
  const int fontId = QFontDatabase::addApplicationFont(
      QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
  if (fontId >= 0) application.setFont(QFont(QStringLiteral("Segoe UI"), 9));
#endif

  QFrame frame;
  Ui::AccountFrame account;
  account.setupUi(&frame);
  account.m_pqOutputSeparator->hide();
  account.m_pqMaintenanceButton->hide();
  account.m_accountBalances->setMinimumHeight(118);
  account.m_actualBalanceLabel->setText(QStringLiteral("AVAILABLE<br>2719.22 XDS"));
  account.m_pendingBalanceLabel->setText(QStringLiteral("Locked 1999.98"));
  account.m_totalBalanceLabel->setText(QStringLiteral("Total 4719.20"));
  frame.resize(226, 300);
  frame.show();
  application.processEvents();
  require(!account.m_pqMaintenanceButton->isVisible(),
          "healthy balance card must hide maintenance details");
  require(frame.findChild<QLabel*>(QStringLiteral("m_pqOutputCountLabel")) == nullptr,
          "balance card must not contain the technical output count");
  render(frame, outputPath(argc, argv, 1, "PqOutputStatusRender.png"));

  account.m_pqOutputSeparator->show();
  account.m_pqMaintenanceButton->setText(QStringLiteral("Review output maintenance"));
  account.m_pqMaintenanceButton->show();
  account.m_accountBalances->setMinimumHeight(164);
  frame.resize(226, 346);
  application.processEvents();
  frame.layout()->activate();
  require(account.m_pqMaintenanceButton->isVisible(),
          "attention state must expose the maintenance entry point");
  require(account.m_pqMaintenanceButton->geometry().bottom() <=
              account.m_accountBalances->contentsRect().bottom(),
          "maintenance button must fit inside the balance card");
  require(QFontMetrics(account.m_pqMaintenanceButton->font()).horizontalAdvance(
              account.m_pqMaintenanceButton->text()) <=
              account.m_pqMaintenanceButton->contentsRect().width(),
          "maintenance button text must not be clipped in the real sidebar");
  render(frame, outputPath(argc, argv, 2, "PqOutputAttentionRender.png"));

  QDialog dialog;
  Ui::InfoDialog info;
  info.setupUi(&dialog);
  info.tabWidget->setCurrentWidget(info.m_walletTab);
  info.m_walletOutputCount->setText(QStringLiteral("38"));
  info.m_walletInputLimit->setText(
      QStringLiteral("A transaction can use at most 32 inputs."));
  info.m_walletOutputState->setText(QStringLiteral("Consolidation recommended"));
  info.m_walletMaintenancePlan->setText(QStringLiteral(
      "Next step: 32 input(s) -> 5 output(s), each at most 10,000 XDS. Fee: 0.01 XDS."));
  info.m_runOneMaintenanceButton->setEnabled(true);
  dialog.show();
  application.processEvents();
  require(info.m_runOneMaintenanceButton->isVisible(),
          "manual one-step action must be available from Information");
  require(info.m_runOneMaintenanceButton->geometry().bottom() <=
              info.m_walletTab->contentsRect().bottom(),
          "manual action must fit inside Information's Wallet outputs tab");
  render(dialog, outputPath(argc, argv, 3, "PqOutputInformationRender.png"));

  info.m_walletOutputCount->setText(QStringLiteral("—"));
  info.m_walletOutputState->setText(
      QStringLiteral("Recalculating after confirmation"));
  info.m_runOneMaintenanceButton->setEnabled(false);
  require(!info.m_runOneMaintenanceButton->isEnabled(),
          "pending maintenance must disable another manual step");
  return 0;
}
