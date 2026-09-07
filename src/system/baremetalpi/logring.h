#pragma once

#include <cstring>

template <unsigned Capacity>
class Tic80LogRing
{
public:
    Tic80LogRing()
        : mStart(0), mLength(0), mDroppedBytes(0), mWriteCalls(0)
    {
    }

    void Write(const void* data, unsigned length)
    {
        if (!data || length == 0) return;

        const char* bytes = static_cast<const char*>(data);
        mWriteCalls = SaturatingAdd(mWriteCalls, 1);

        if (length >= Capacity)
        {
            mDroppedBytes = SaturatingAdd(mDroppedBytes, mLength);
            mDroppedBytes = SaturatingAdd(mDroppedBytes, length - Capacity);
            memcpy(mBuffer, bytes + length - Capacity, Capacity);
            mStart = 0;
            mLength = Capacity;
            return;
        }

        const unsigned overflow = mLength + length > Capacity
                                      ? mLength + length - Capacity
                                      : 0;
        mStart = (mStart + overflow) % Capacity;
        mLength -= overflow;
        mDroppedBytes = SaturatingAdd(mDroppedBytes, overflow);

        const unsigned end = (mStart + mLength) % Capacity;
        const unsigned first = length < Capacity - end ? length : Capacity - end;
        memcpy(mBuffer + end, bytes, first);
        memcpy(mBuffer, bytes + first, length - first);
        mLength += length;
    }

    unsigned Snapshot(char* output, unsigned capacity,
                      unsigned* droppedBytes, unsigned* writeCalls) const
    {
        if (!output || capacity == 0) return 0;

        const unsigned length = mLength < capacity ? mLength : capacity;
        const unsigned start = (mStart + mLength - length) % Capacity;
        const unsigned first = length < Capacity - start ? length : Capacity - start;
        memcpy(output, mBuffer + start, first);
        memcpy(output + first, mBuffer, length - first);
        if (droppedBytes)
            *droppedBytes = SaturatingAdd(mDroppedBytes, mLength - length);
        if (writeCalls) *writeCalls = mWriteCalls;
        return length;
    }

private:
    static unsigned SaturatingAdd(unsigned left, unsigned right)
    {
        return right > ~0u - left ? ~0u : left + right;
    }

    char mBuffer[Capacity];
    unsigned mStart;
    unsigned mLength;
    unsigned mDroppedBytes;
    unsigned mWriteCalls;
};
