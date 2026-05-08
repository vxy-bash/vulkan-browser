#include <gtest/gtest.h>
#include "ipc/IpcChannel.h"
#include <QCoreApplication>

// Sanity-check that IpcChannel header sizes and message types are stable.

TEST(IpcChannelTest, HeaderSizeIs5Bytes)
{
    static_assert(sizeof(vkb::IpcHeader) == 5,
                  "IpcHeader must be exactly 5 bytes (1 type + 4 length)");
    SUCCEED();
}

TEST(IpcChannelTest, MsgTypeValuesDoNotOverlap)
{
    // Verify enum discriminants stay distinct (would break if someone adds a duplicate).
    EXPECT_NE(static_cast<uint8_t>(vkb::IpcMsgType::Navigate),
              static_cast<uint8_t>(vkb::IpcMsgType::PaintReady));
    EXPECT_NE(static_cast<uint8_t>(vkb::IpcMsgType::Shutdown),
              static_cast<uint8_t>(vkb::IpcMsgType::Navigate));
}

TEST(IpcChannelTest, ConstructionDoesNotCrash)
{
    int    argc = 0;
    QCoreApplication app(argc, nullptr);
    vkb::IpcChannel ch;
    SUCCEED();
}
