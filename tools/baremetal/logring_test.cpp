#include <cassert>
#include <cstring>

#include "../../src/system/baremetalpi/logring.h"

static void expectSnapshot(Tic80LogRing<8>& ring, unsigned capacity,
                           const char* expected, unsigned expectedDropped,
                           unsigned expectedWrites)
{
    char output[8] = {};
    unsigned dropped = 0;
    unsigned writes = 0;
    const unsigned length = ring.Snapshot(output, capacity, &dropped, &writes);
    assert(length == strlen(expected));
    assert(!memcmp(output, expected, length));
    assert(dropped == expectedDropped);
    assert(writes == expectedWrites);
}

int main()
{
    Tic80LogRing<8> ring;
    ring.Write("abc", 3);
    expectSnapshot(ring, 8, "abc", 0, 1);

    ring.Write("defghi", 6);
    expectSnapshot(ring, 8, "bcdefghi", 1, 2);
    expectSnapshot(ring, 4, "fghi", 5, 2);

    ring.Write("0123456789", 10);
    expectSnapshot(ring, 8, "23456789", 11, 3);

    ring.Write(nullptr, 4);
    ring.Write("", 0);
    expectSnapshot(ring, 8, "23456789", 11, 3);

    char binary[8] = {'a', '\0', 'b', 'c', 'd', 'e', 'f', 'g'};
    ring.Write(binary, sizeof binary);
    char output[8] = {};
    unsigned dropped = 0;
    unsigned writes = 0;
    assert(ring.Snapshot(output, sizeof output, &dropped, &writes) == sizeof output);
    assert(!memcmp(output, binary, sizeof binary));
    assert(dropped == 19);
    assert(writes == 4);
    assert(ring.Snapshot(nullptr, 8, nullptr, nullptr) == 0);
    assert(ring.Snapshot(output, 0, nullptr, nullptr) == 0);
    return 0;
}
