#include "infra/rateLimiter.h"
#include "base/util.h"
#include "../test_util.h"

int main()
{
    return RunTestMain([]() {
        infra::TokenBucketRateLimiter limiter(2.0, 2.0);

        REQUIRE(limiter.allow());
        REQUIRE(limiter.allow());
        REQUIRE(!limiter.allow());

        util::SleepMs(600);
        REQUIRE(limiter.allow());

        limiter.reset(10.0, 1.0);
        REQUIRE(limiter.allow());
        REQUIRE(!limiter.allow());
    });
}
