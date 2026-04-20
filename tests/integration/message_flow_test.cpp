#include <memory>
#include <string>

#include "codec/protocol.h"
#include "service/messageService.h"
#include "storage/messageStore.h"
#include "../test_util.h"

int main()
{
    return RunTestMain([]() {
        auto store = std::make_shared<storage::InMemoryMessageStore>();
        std::string initError;
        REQUIRE(store->init(initError));
        REQUIRE(initError.empty());

        MessageService service;
        service.setMessageStore(store);

        uint64_t msgId = 0;
        uint64_t serverSeq = 0;
        std::string resultMsg;
        REQUIRE(service.sendReliableP2PMessage("alice",
                                               "bob",
                                               "client-1",
                                               "hello",
                                               msgId,
                                               serverSeq,
                                               resultMsg));
        REQUIRE(msgId > 0);
        REQUIRE(serverSeq == 1);

        uint64_t nextBeginSeq = 0;
        auto offline = service.pullOfflineMessages("bob", 0, 10, nextBeginSeq);
        REQUIRE(offline.size() == 1);
        REQUIRE(offline.front().content == "hello");
        REQUIRE(offline.front().msgId == msgId);

        uint64_t lastAckedSeq = 0;
        std::string ackError;
        REQUIRE(service.processAck("bob",
                                   msgId,
                                   serverSeq,
                                   protocol::AckReceived,
                                   lastAckedSeq,
                                   ackError));
        REQUIRE(lastAckedSeq == serverSeq);
        REQUIRE(ackError.empty());

        nextBeginSeq = 0;
        offline = service.pullOfflineMessages("bob", lastAckedSeq, 10, nextBeginSeq);
        REQUIRE(offline.empty());
    });
}
