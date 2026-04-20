#include <string>

#include "codec/buffer.h"
#include "../test_util.h"

int main()
{
    return RunTestMain([]() {
        Buffer buffer;
        buffer.append("hello");
        REQUIRE(buffer.readableBytes() == 5);
        REQUIRE(buffer.retrieveAsString(2) == "he");
        REQUIRE(buffer.readableBytes() == 3);

        std::string large(Buffer::kInitialSize + 128, 'x');
        buffer.append(large);
        REQUIRE(buffer.readableBytes() == 3 + large.size());

        buffer.retrieveAll();
        REQUIRE(buffer.readableBytes() == 0);

        buffer.append("abc");
        REQUIRE(buffer.retrieveAllAsString() == "abc");
        REQUIRE(buffer.readableBytes() == 0);
    });
}
