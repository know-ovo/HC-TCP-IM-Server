#include "service/overloadProtectService.h"
#include "base/util.h"
#include "../test_util.h"

int main()
{
    return RunTestMain([]() {
        OverloadProtectService service(2, 100);
        REQUIRE(service.canAcceptConnection());
        service.onConnectionAccepted();
        service.onConnectionAccepted();
        REQUIRE(!service.canAcceptConnection());
        service.onConnectionClosed();
        REQUIRE(service.canAcceptConnection());

        service.setRateLimitEnabled(true);
        service.configureIpConnectRate(1.0);
        REQUIRE(service.canAcceptConnection("127.0.0.1"));
        REQUIRE(!service.canAcceptConnection("127.0.0.1"));

        util::SleepMs(1100);
        REQUIRE(service.canAcceptConnection("127.0.0.1"));

        service.configureUserSendRate(1.0);
        REQUIRE(service.canSendMessage("alice"));
        REQUIRE(!service.canSendMessage("alice"));
    });
}
