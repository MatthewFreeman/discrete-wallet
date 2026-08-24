// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <QSemaphore>

#include <atomic>

namespace WalletGui {

// Serializes GUI-owned wallet operations while ensuring that observer
// callbacks produced by the embedded RPC server cannot add semaphore permits.
// WalletLegacy notifies every observer, including WalletAdapter, regardless of
// which caller started a save/send operation.
class WalletOperationGate {
public:
  WalletOperationGate() : m_semaphore(1) {}

  void acquire() { m_semaphore.acquire(); }
  bool tryAcquire() { return m_semaphore.tryAcquire(); }
  void release() { m_semaphore.release(); }

  void expectSaveCompletion() {
    m_saveCompletionExpected.store(true, std::memory_order_release);
  }
  bool consumeSaveCompletion() {
    return m_saveCompletionExpected.exchange(false,
                                              std::memory_order_acq_rel);
  }

  void expectSendCompletion() {
    m_sendCompletionExpected.store(true, std::memory_order_release);
  }
  bool consumeSendCompletion() {
    return m_sendCompletionExpected.exchange(false,
                                              std::memory_order_acq_rel);
  }

  int available() const { return m_semaphore.available(); }

private:
  QSemaphore m_semaphore;
  std::atomic<bool> m_saveCompletionExpected{false};
  std::atomic<bool> m_sendCompletionExpected{false};
};

}  // namespace WalletGui
