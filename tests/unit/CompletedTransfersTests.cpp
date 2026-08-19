/**
 * (c) 2013 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of MEGAcmd.
 *
 * MEGAcmd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "listeners.h"

using mega::MegaHandle;
using mega::MegaTransfer;
using megacmd::MegaCmdGlobalTransferListener;

namespace {

class FakeTransfer : public MegaTransfer
{
public:
    MegaTransfer *copy() override { return new FakeTransfer(); }

    int getType() const override { return MegaTransfer::TYPE_UPLOAD; }
    bool isSyncTransfer() const override { return false; }
    MegaHandle getNodeHandle() const override { return mega::INVALID_HANDLE; }
};

// Adds transfers until the buffer stops growing, and returns its capacity.
size_t fillCompletedBuffer(MegaCmdGlobalTransferListener &listener)
{
    size_t capacity = 0;
    while (true)
    {
        listener.addCompletedTransfer(std::make_unique<FakeTransfer>(), std::nullopt);

        const size_t count = listener.getCompletedTransfersCount();
        if (count == capacity)
        {
            return capacity;
        }
        capacity = count;
    }
}

}

// "transfers --show-completed" collects the transfers to print under the lock and
// prints them after releasing it. Any transfer finishing meanwhile (from the SDK
// transfer thread; done inline here) evicts and deletes the oldest ones, which are
// the very ones still waiting to be printed.
TEST(CompletedTransfers, readerGetsValidTransfersWhileOldOnesAreEvicted)
{
    constexpr int evictions = 100;

    MegaCmdGlobalTransferListener listener(nullptr, nullptr);
    const size_t capacity = fillCompletedBuffer(listener);
    ASSERT_GT(capacity, evictions);

    auto toPrint = listener.getCompletedTransfers(capacity, [](const MegaTransfer &) { return true; });

    for (int i = 0; i < evictions; ++i)
    {
        listener.addCompletedTransfer(std::make_unique<FakeTransfer>(), std::nullopt);
    }

    for (const auto &transfer : toPrint)
    {
        EXPECT_EQ(transfer->getType(), MegaTransfer::TYPE_UPLOAD);
    }
}
