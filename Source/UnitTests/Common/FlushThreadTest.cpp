// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include <atomic>

#include "Common/CommonTypes.h"
#include "Common/FlushThread.h"

TEST(FlushThread, Simple)
{
  Common::FlushThread ft;

  std::atomic<int> value = 0;

  ft.Reset("flush", [&] { ++value; });

  // No flush on start.
  EXPECT_EQ(value.load(), 0);

  ft.SetDirty();
  ft.WaitForCompletion();

  // One flush.
  EXPECT_EQ(value.load(), 1);

  ft.Reset("flush", [&] { ++value; });

  // No change after reset.
  EXPECT_EQ(value.load(), 1);

  ft.Shutdown();
  ft.SetDirty();
  ft.WaitForCompletion();

  // No change because shutdown.
  EXPECT_EQ(value.load(), 1);

  ft.Reset("flush", [&] {
    ++value;
    value.notify_one();
  });
  ft.WaitForCompletion();

  // Dirty state persits on reset.
  EXPECT_EQ(value.load(), 2);

  value = 0;

  ft.SetFlushDelay(std::chrono::milliseconds{999999});
  ft.SetDirty();
  ft.SetDirty();
  ft.SetDirty();

  // Not using EXPECT_ here because the tests are technically racey.

  // Probably no flush yet, because of the delay.
  GTEST_LOG_(INFO) << "Ideally 0: " << value.load();

  const auto start = std::chrono::steady_clock::now();
  ft.WaitForCompletion();
  const auto end = std::chrono::steady_clock::now();

  GTEST_LOG_(INFO) << "Ideally 0: "
                   << duration_cast<std::chrono::milliseconds>(end - start).count();

  // At least one flush happened. Probably just one.
  EXPECT_GT(value.load(), 0);
  GTEST_LOG_(INFO) << "Ideally 1: " << value.load();

  value = 0;

  ft.SetDirty();
  ft.Reset("flush", [] {});

  // Reset first causes a shutdown, so we have an additional immediate flush.
  EXPECT_EQ(value.load(), 1);
}
