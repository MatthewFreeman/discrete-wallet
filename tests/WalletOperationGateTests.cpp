// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QCoreApplication>

#include <iostream>

#include "security/WalletOperationGate.h"

namespace {

bool require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  WalletGui::WalletOperationGate gate;

  if (!require(!gate.consumeSaveCompletion() &&
                   !gate.consumeSendCompletion() && gate.available() == 1,
               "unowned callbacks changed the gate")) {
    return 1;
  }

  gate.acquire();
  gate.expectSaveCompletion();
  if (!require(gate.available() == 0 && !gate.tryAcquire(),
               "a second operation entered an owned gate") ||
      !require(gate.consumeSaveCompletion(),
               "owned save completion was not recognized")) {
    return 1;
  }
  gate.release();
  if (!require(!gate.consumeSaveCompletion() && gate.available() == 1,
               "duplicate save completion inflated the gate")) {
    return 1;
  }

  // An embedded RPC mutation must either own the same permit or fail fast. It
  // cannot overlap a GUI save and deliver an indistinguishable callback while
  // the GUI's completion flag is armed.
  gate.acquire();
  gate.expectSaveCompletion();
  if (!require(!gate.tryAcquire(),
               "an RPC mutation overlapped an owned GUI save") ||
      !require(gate.consumeSaveCompletion(),
               "GUI save ownership was lost while RPC was excluded")) {
    return 1;
  }
  gate.release();

  if (!require(gate.tryAcquire(), "RPC mutation could not acquire an idle gate") ||
      !require(!gate.consumeSaveCompletion() &&
                   !gate.consumeSendCompletion(),
               "RPC mutation armed a GUI completion flag")) {
    return 1;
  }
  gate.release();

  gate.acquire();
  gate.expectSendCompletion();
  if (!require(gate.consumeSendCompletion(),
               "owned send completion was not recognized")) {
    return 1;
  }
  gate.release();
  for (int i = 0; i < 100; ++i) {
    if (!require(!gate.consumeSendCompletion(),
                 "unowned send completion was accepted")) {
      return 1;
    }
  }
  if (!require(gate.available() == 1,
               "repeated RPC callbacks inflated the gate")) {
    return 1;
  }

  std::cout << "WalletOperationGateTests: all checks passed\n";
  return 0;
}
