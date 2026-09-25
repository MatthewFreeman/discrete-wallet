#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPixmap>

#include "gui/PqOutputStatus.h"
#include "ui_accountframe.h"

namespace {

void require(bool _condition, const char* _message) {
  if (!_condition) {
    std::cerr << _message << std::endl;
    std::exit(1);
  }
}

QString accentFor(WalletGui::PqOutputStatusTone _tone) {
  switch (_tone) {
    case WalletGui::PqOutputStatusTone::Ready:
      return QStringLiteral("#5FE29F");
    case WalletGui::PqOutputStatusTone::Attention:
      return QStringLiteral("#F4C95D");
    case WalletGui::PqOutputStatusTone::Pending:
      return QStringLiteral("#71B7FF");
    case WalletGui::PqOutputStatusTone::Unavailable:
      return QStringLiteral("#7F8B94");
  }

  return QStringLiteral("#7F8B94");
}

}

int main(int argc, char** argv) {
  QApplication application(argc, argv);

#ifdef Q_OS_WIN
  const int uiFontId = QFontDatabase::addApplicationFont(
      QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
  if (uiFontId >= 0) {
    application.setFont(QFont(QStringLiteral("Segoe UI"), 9));
  }
#endif

  QFrame frame;
  Ui::AccountFrame ui;
  ui.setupUi(&frame);

  frame.setStyleSheet(QStringLiteral("QFrame#AccountFrame { background-color:#10171D; }"));
  const QString cardCss = QStringLiteral(
      "QFrame#%1 { background-color:#182029; border:1px solid #2A343D; border-radius:12px; }");
  ui.m_accountNumberPanel->setStyleSheet(cardCss.arg(QStringLiteral("m_accountNumberPanel")));
  ui.m_accountBalances->setStyleSheet(cardCss.arg(QStringLiteral("m_accountBalances")));
  const QString captionCss = QStringLiteral(
      "color:#7f8b94; font-size:10px; letter-spacing:1px;");
  ui.m_accountNumberTitleLabel->setStyleSheet(captionCss);
  ui.label->setStyleSheet(captionCss);
  ui.m_pqOutputTitleLabel->setStyleSheet(captionCss);
  ui.m_pqOutputSeparator->setStyleSheet(QStringLiteral(
      "QFrame { background-color:#2A343D; border:0; min-height:1px; max-height:1px; }"));

  ui.m_copyAccountNumberButton->hide();
  ui.m_accountNumberQrButton->hide();
  ui.m_registerAccountButton->hide();
  ui.m_copyButton->hide();
  ui.m_accountNumberLabel->setText(QStringLiteral("8772-1-NEVK-2"));
  ui.m_accountNumberLabel->setStyleSheet(QStringLiteral(
      "color:#5FE29F; font-size:23px; font-weight:600;"));
  ui.m_addressLabel->setText(QStringLiteral("disc1q8dch...r6gekzzktr"));
  ui.m_addressLabel->setStyleSheet(QStringLiteral(
      "color:#9AA7B2; font-size:15px; font-weight:600;"));
  ui.m_actualBalanceLabel->setText(QStringLiteral(
      "<div style=\"line-height:1.15;\"><span style=\"font-size:10px; color:#7f8b94; "
      "letter-spacing:1px;\">AVAILABLE</span><br><span style=\"font-size:22px; "
      "font-weight:600; color:#F5F7F8;\">2719.22</span><span style=\"font-size:12px; "
      "color:#7f8b94;\"> XDS</span></div>"));
  ui.m_pendingBalanceLabel->setText(QStringLiteral(
      "<span style=\"font-size:12px; color:#8a95a0;\">Locked </span>"
      "<span style=\"font-size:12px; color:#d9e0e5;\">1999.98</span>"));
  ui.m_totalBalanceLabel->setText(QStringLiteral(
      "<span style=\"font-size:12px; color:#8a95a0;\">Total </span>"
      "<span style=\"font-size:12px; color:#d9e0e5;\">4719.20</span>"));

  const WalletGui::PqOutputStatusPresentation presentation =
      WalletGui::makePqOutputStatusPresentation(
          true, 38, 32, 32, 5, QStringLiteral("0.01"),
          QStringLiteral("XDS"), false);
  const QString accent = accentFor(presentation.tone);
  ui.m_pqOutputCountLabel->setText(presentation.countText);
  ui.m_pqOutputCountLabel->setStyleSheet(QStringLiteral(
      "color:%1; font-size:14px; font-weight:600;").arg(accent));
  ui.m_pqOutputStateLabel->setText(presentation.stateText);
  ui.m_pqOutputStateLabel->setStyleSheet(QStringLiteral(
      "color:%1; font-size:11px;").arg(accent));
  ui.m_pqOutputPanel->setToolTip(presentation.toolTip);

  // MainWindow's 250 px sidebar has 12 px margins on both sides, so the
  // account frame is rendered at 226 px in the real application.
  frame.resize(226, 346);
  frame.show();
  application.processEvents();
  frame.layout()->activate();

  require(ui.m_accountBalances->height() >= 164,
          "balance card must reserve room for the PQ output status");
  require(ui.m_pqOutputPanel->geometry().bottom() <=
              ui.m_accountBalances->contentsRect().bottom(),
          "PQ output panel must remain inside the balance card");
  require(QFontMetrics(ui.m_pqOutputTitleLabel->font()).horizontalAdvance(
              ui.m_pqOutputTitleLabel->text()) <=
              ui.m_pqOutputTitleLabel->contentsRect().width(),
          "PQ output title must not be clipped");
  require(QFontMetrics(ui.m_pqOutputCountLabel->font()).horizontalAdvance(
              ui.m_pqOutputCountLabel->text()) <=
              ui.m_pqOutputCountLabel->contentsRect().width(),
          "PQ output count must not be clipped");
  require(QFontMetrics(ui.m_pqOutputStateLabel->font()).horizontalAdvance(
              ui.m_pqOutputStateLabel->text()) <=
              ui.m_pqOutputStateLabel->contentsRect().width(),
          "PQ output state must not be clipped");

  const QString outputPath = argc > 1
      ? QString::fromLocal8Bit(argv[1])
      : QDir::current().filePath(QStringLiteral("PqOutputStatusRender.png"));
  QPixmap image(frame.size());
  image.fill(Qt::transparent);
  frame.render(&image);
  require(image.save(outputPath, "PNG"), "failed to save PQ output status render");

  const WalletGui::PqOutputStatusPresentation pendingPresentation =
      WalletGui::makePqOutputStatusPresentation(
          true, 6, 32, 6, 2, QStringLiteral("0.01"),
          QStringLiteral("XDS"), true);
  const QString pendingAccent = accentFor(pendingPresentation.tone);
  ui.m_pqOutputCountLabel->setText(pendingPresentation.countText);
  ui.m_pqOutputCountLabel->setStyleSheet(QStringLiteral(
      "color:%1; font-size:14px; font-weight:600;").arg(pendingAccent));
  ui.m_pqOutputStateLabel->setText(pendingPresentation.stateText);
  ui.m_pqOutputStateLabel->setStyleSheet(QStringLiteral(
      "color:%1; font-size:11px;").arg(pendingAccent));
  application.processEvents();
  frame.layout()->activate();
  const QString pendingOutputPath = argc > 2
      ? QString::fromLocal8Bit(argv[2])
      : QDir::current().filePath(QStringLiteral("PqOutputStatusPendingRender.png"));
  QPixmap pendingImage(frame.size());
  pendingImage.fill(Qt::transparent);
  frame.render(&pendingImage);
  require(pendingImage.save(pendingOutputPath, "PNG"),
          "failed to save pending PQ output status render");

  ui.m_pqOutputCountLabel->setText(QStringLiteral("12345 / 32"));
  application.processEvents();
  frame.layout()->activate();
  require(QFontMetrics(ui.m_pqOutputTitleLabel->font()).horizontalAdvance(
              ui.m_pqOutputTitleLabel->text()) <=
              ui.m_pqOutputTitleLabel->contentsRect().width(),
          "PQ output title must fit beside a five-digit output count");
  require(QFontMetrics(ui.m_pqOutputCountLabel->font()).horizontalAdvance(
              ui.m_pqOutputCountLabel->text()) <=
              ui.m_pqOutputCountLabel->contentsRect().width(),
          "five-digit PQ output count must not be clipped");

  const QString stateTexts[] = {
      QStringLiteral("Available after synchronization"),
      QStringLiteral("Within the transaction limit"),
      QStringLiteral("Recalculating after confirmation"),
      QStringLiteral("Above limit; no reducing batch")};
  for (const QString& stateText : stateTexts) {
    ui.m_pqOutputStateLabel->setText(stateText);
    application.processEvents();
    frame.layout()->activate();
    require(QFontMetrics(ui.m_pqOutputStateLabel->font()).horizontalAdvance(
                ui.m_pqOutputStateLabel->text()) <=
                ui.m_pqOutputStateLabel->contentsRect().width(),
            "PQ output state must fit the real sidebar width");
  }

  return 0;
}
