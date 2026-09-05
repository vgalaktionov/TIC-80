// MIT License

// Copyright (c) 2017 Vadim Grigoruk @nesbox // grigoruk@gmail.com

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "fat_datetime.h"

#include <stdio.h>

#define FAT_DATE(year, month, day) ((u16)((((year) - 1980) << 9) | ((month) << 5) | (day)))
#define FAT_TIME(hour, minute, second) ((u16)(((hour) << 11) | ((minute) << 5) | ((second) / 2)))

static bool expect(const char* name, u64 actual, u64 expected)
{
    if(actual == expected)
        return true;

    fprintf(stderr, "%s: expected %llu, got %llu\n", name,
        (unsigned long long)expected, (unsigned long long)actual);
    return false;
}

int main(void)
{
    bool success = true;

    success &= expect("FAT epoch",
        fat_datetime_to_unix(FAT_DATE(1980, 1, 1), FAT_TIME(0, 0, 0)), 315532800);
    success &= expect("leap day before 2000",
        fat_datetime_to_unix(FAT_DATE(1980, 2, 29), FAT_TIME(23, 59, 58)), 320716798);
    success &= expect("leap day in 2000",
        fat_datetime_to_unix(FAT_DATE(2000, 2, 29), FAT_TIME(12, 34, 56)), 951827696);
    success &= expect("latest FAT date",
        fat_datetime_to_unix(FAT_DATE(2107, 12, 31), FAT_TIME(23, 59, 58)), 4354819198ULL);
    success &= expect("two-second precision",
        fat_datetime_to_unix(FAT_DATE(1980, 1, 1), FAT_TIME(0, 0, 2)), 315532802);
    success &= expect("zero timestamp", fat_datetime_to_unix(0, 0), 0);
    success &= expect("non-leap February",
        fat_datetime_to_unix(FAT_DATE(2001, 2, 29), 0), 0);
    success &= expect("invalid month",
        fat_datetime_to_unix(FAT_DATE(2000, 13, 1), 0), 0);
    success &= expect("invalid time",
        fat_datetime_to_unix(FAT_DATE(2000, 1, 1), (u16)(24 << 11)), 0);

    return success ? 0 : 1;
}
