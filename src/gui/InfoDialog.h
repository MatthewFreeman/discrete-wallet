// Copyright (c) 2017 The Karbowanec developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <QDialog>
#include <QMenu>
#include <QModelIndex>

namespace Ui {
class InfoDialog;
}

namespace WalletGui {

class InfoDialog : public QDialog {
  Q_OBJECT

public:
  InfoDialog(QWidget* _parent);
  ~InfoDialog();
  QModelIndex m_index;
  void showWalletOutputs();
  bool manualMaintenanceRequested() const;

public slots:
  void onCustomContextMenu(const QPoint &point);

public Q_SLOTS:
  void copyAddressClicked();
  void copyIdClicked();

protected:
  void timerEvent(QTimerEvent* _event) Q_DECL_OVERRIDE;

private:
  void refreshWalletOutputs();
  QScopedPointer<Ui::InfoDialog> m_ui;
  QMenu* m_contextMenu;
  int m_refreshTimerId;
  bool m_manualMaintenanceRequested = false;
};

}
