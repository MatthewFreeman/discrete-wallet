// Copyright (c) 2017 The Karbowanec developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "InfoDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFont>
#include <QLocale>
#include <QPushButton>
#include <QTabWidget>

#include "NodeAdapter.h"
#include "CryptoNoteWrapper.h"
#include "CurrencyAdapter.h"
#include "ConnectionsModel.h"
#include "WalletAdapter.h"
#include "gui/PqOutputStatus.h"

#include "ui_infodialog.h"

namespace WalletGui {

InfoDialog::InfoDialog(QWidget* _parent) : QDialog(_parent), m_ui(new Ui::InfoDialog), m_refreshTimerId(-1) {
  m_ui->setupUi(this);
  QFont countFont = m_ui->m_walletOutputCount->font();
  countFont.setPointSize(20);
  countFont.setBold(true);
  m_ui->m_walletOutputCount->setFont(countFont);
  connect(m_ui->m_runOneMaintenanceButton, &QPushButton::clicked,
          this, [this]() {
            m_manualMaintenanceRequested = true;
            accept();
          });
  connect(&WalletAdapter::instance(), &WalletAdapter::walletPqOutputStateUpdatedSignal,
          this, [this]() { refreshWalletOutputs(); }, Qt::QueuedConnection);
  refreshWalletOutputs();
  m_refreshTimerId = startTimer(1000);
  m_ui->m_connectionsView->setModel(&ConnectionsModel::instance());
  m_ui->m_connectionsView->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_ui->m_connectionsView->setSortingEnabled(true);
  m_ui->m_connectionsView->sortByColumn(0, Qt::AscendingOrder);
  m_ui->m_connectionsView->header()->resizeSection(ConnectionsModel::COLUMN_STATE, 80);
  m_ui->m_connectionsView->header()->resizeSection(ConnectionsModel::COLUMN_ID, 90);
  m_ui->m_connectionsView->header()->resizeSection(ConnectionsModel::COLUMN_PORT, 45);
  m_ui->m_connectionsView->header()->resizeSection(ConnectionsModel::COLUMN_IS_INCOMING, 70);
  m_ui->m_connectionsView->header()->resizeSection(ConnectionsModel::COLUMN_HEIGHT, 50);
  m_ui->m_connectionsView->header()->resizeSection(ConnectionsModel::COLUMN_LAST_RESPONSE_HEIGHT, 50);
  m_ui->m_connectionsView->header()->resizeSection(ConnectionsModel::COLUMN_VERSION, 45);
  m_ui->m_connectionsView->setRootIsDecorated(false);

  m_ui->m_connectionsView->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_ui->m_connectionsView, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(onCustomContextMenu(const QPoint &)));

  m_contextMenu = new QMenu();
  m_contextMenu->addAction(QString(tr("Copy &address")), this, SLOT(copyAddressClicked()));
  m_contextMenu->addAction(QString(tr("Copy &Id")), this, SLOT(copyIdClicked()));

  ConnectionsModel::instance().refreshConnections();
}

InfoDialog::~InfoDialog() {
  killTimer(m_refreshTimerId);
  m_refreshTimerId = -1;
}

void InfoDialog::showWalletOutputs() {
  m_ui->tabWidget->setCurrentWidget(m_ui->m_walletTab);
}

bool InfoDialog::manualMaintenanceRequested() const {
  return m_manualMaintenanceRequested;
}

void InfoDialog::refreshWalletOutputs() {
  const PqOutputSnapshot snapshot = WalletAdapter::instance().pqOutputSnapshot();
  const QString ticker = CurrencyAdapter::instance().getCurrencyTicker().toUpper();
  const PqOutputStatusPresentation presentation = makePqOutputStatusPresentation(
      snapshot.ready, snapshot.availableOutputs, snapshot.inputLimit,
      snapshot.selectedInputs, snapshot.resultingOutputs,
      CurrencyAdapter::instance().formatAmount(snapshot.fee), ticker,
      snapshot.pending);

  m_ui->m_walletOutputCount->setText(
      snapshot.ready && !snapshot.pending
          ? QString::number(snapshot.availableOutputs)
          : tr("—"));
  m_ui->m_walletInputLimit->setText(
      tr("A transaction can use at most %1 inputs.").arg(snapshot.inputLimit));
  m_ui->m_walletOutputState->setText(presentation.stateText);
  if (snapshot.pending) {
    m_ui->m_walletMaintenancePlan->setText(
        tr("Wait for the pending wallet transaction to confirm before another step."));
  } else if (snapshot.ready && snapshot.useful) {
    m_ui->m_walletMaintenancePlan->setText(
        tr("Next step: %1 input(s) → %2 output(s), each at most 10,000 XDS. Fee: %3 %4.")
            .arg(snapshot.selectedInputs)
            .arg(snapshot.resultingOutputs)
            .arg(CurrencyAdapter::instance().formatAmount(snapshot.fee), ticker));
  } else {
    m_ui->m_walletMaintenancePlan->setText(
        snapshot.ready ? tr("No useful maintenance step is available right now.")
                       : tr("Output details appear after wallet synchronization."));
  }
  m_ui->m_runOneMaintenanceButton->setEnabled(canRunManualMaintenance(
      snapshot.ready, snapshot.useful, snapshot.pending,
      snapshot.inProgress));
}

void InfoDialog::onCustomContextMenu(const QPoint &point) {
  m_index = m_ui->m_connectionsView->indexAt(point);
  if (!m_index.isValid())
     return;
  m_contextMenu->exec(m_ui->m_connectionsView->mapToGlobal(point));
}

void InfoDialog::timerEvent(QTimerEvent* _event) {
  if (_event->timerId() == m_refreshTimerId) {
    refreshWalletOutputs();

    quint64 Connections = NodeAdapter::instance().getPeerCount(); // NodeAdapter::instance().getConnectionsCount();
    quint64 Outgoing =    NodeAdapter::instance().getOutgoingConnectionsCount();
    quint64 Incoming =    NodeAdapter::instance().getIncomingConnectionsCount();
    m_ui->m_connections->setText(QString(tr("%1 (Outgoing: %2, Incoming: %3)")).arg(Connections).arg(Outgoing).arg(Incoming));

    quint64 whitePeerList = NodeAdapter::instance().getWhitePeerlistSize();
    quint64 greyPeerList = NodeAdapter::instance().getGreyPeerlistSize();
    m_ui->m_peerList->setText(QString(tr("White: %1, Grey: %2")).arg(whitePeerList).arg(greyPeerList));

    quint64 lastKnownBlockHeight = NodeAdapter::instance().getLastKnownBlockHeight();
    quint64 lastLocalBlockHeight = NodeAdapter::instance().getLastLocalBlockHeight();
    m_ui->m_height->setText(QString(tr("Known: %1, Local: %2")).arg(lastKnownBlockHeight).arg(lastLocalBlockHeight));

    const QDateTime blockTime = NodeAdapter::instance().getLastLocalBlockTimestamp();
    m_ui->m_blockTime->setText(QString(tr("%1")).arg(QLocale(QLocale::English).toString(blockTime, "dd.MM.yyyy, HH:mm:ss UTC")));

    quint64 difficulty = NodeAdapter::instance().getDifficulty();
    m_ui->m_difficulty->setText(QString(tr("%1")).arg(difficulty));

    quint64 txCount = NodeAdapter::instance().getTxCount();
    m_ui->m_txCount->setText(QString(tr("%1")).arg(txCount));

    quint64 txPoolSize = NodeAdapter::instance().getTxPoolSize();
    m_ui->m_txPoolSize->setText(QString(tr("%1")).arg(txPoolSize));

    quint64 altBlocks = NodeAdapter::instance().getAltBlocksCount();
    m_ui->m_altBlocksCount->setText(QString(tr("%1")).arg(altBlocks));

    quint64 coinsInCirculation = NodeAdapter::instance().getAlreadyGeneratedCoins();
    m_ui->m_alreadyGeneratedCoins->setText(QString(tr("%1 %2")).arg(CurrencyAdapter::instance().formatAmount(coinsInCirculation)).arg(CurrencyAdapter::instance().getCurrencyTicker()));

    return;
  }

  QDialog::timerEvent(_event);
}

void InfoDialog::copyAddressClicked() {
  QApplication::clipboard()->setText(QString("%1:%2").arg(m_ui->m_connectionsView->currentIndex().data(ConnectionsModel::ROLE_HOST).toString()).arg(m_ui->m_connectionsView->currentIndex().data(ConnectionsModel::ROLE_PORT).toString()));
}

void InfoDialog::copyIdClicked() {
  QApplication::clipboard()->setText(m_ui->m_connectionsView->currentIndex().data(ConnectionsModel::ROLE_ID).toString());
}

}
