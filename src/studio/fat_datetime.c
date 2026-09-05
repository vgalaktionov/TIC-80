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

static bool isLeapYear(u32 year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static u32 daysInMonth(u32 year, u32 month)
{
    static const u8 Days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return Days[month - 1] + (month == 2 && isLeapYear(year));
}

u64 fat_datetime_to_unix(u16 date, u16 time)
{
    const u32 year = 1980 + (date >> 9);
    const u32 month = (date >> 5) & 0x0f;
    const u32 day = date & 0x1f;
    const u32 hour = time >> 11;
    const u32 minute = (time >> 5) & 0x3f;
    const u32 fatSecond = time & 0x1f;

    if(month < 1 || month > 12 || day < 1 || day > daysInMonth(year, month)
        || hour > 23 || minute > 59 || fatSecond > 29)
        return 0;

    u64 days = 0;

    for(u32 current = 1970; current < year; current++)
        days += isLeapYear(current) ? 366 : 365;

    for(u32 current = 1; current < month; current++)
        days += daysInMonth(year, current);

    days += day - 1;

    return days * 24 * 60 * 60 + hour * 60 * 60 + minute * 60 + fatSecond * 2;
}
