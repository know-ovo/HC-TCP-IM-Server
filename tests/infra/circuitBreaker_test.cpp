#include "infra/circuitBreaker.h"
#include "base/util.h"
#include "../test_util.h"

int main()
{
    return RunTestMain([]() {
        infra::CircuitBreaker breaker(true, 2, 50);

        REQUIRE(breaker.allowRequest());
        breaker.recordFailure();
        REQUIRE(breaker.allowRequest());

        breaker.recordFailure();
        REQUIRE(breaker.isOpen());
        REQUIRE(!breaker.allowRequest());

        util::SleepMs(60);
        REQUIRE(breaker.allowRequest());

        breaker.recordSuccess();
        REQUIRE(!breaker.isOpen());
        REQUIRE(breaker.allowRequest());
    });
}
