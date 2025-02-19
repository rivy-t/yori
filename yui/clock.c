/**
 * @file yui/clock.c
 *
 * Yori shell taskbar clock
 *
 * Copyright (c) 2019-2023 Malcolm J. Smith
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <yoripch.h>
#include <yorilib.h>
#include "yui.h"

/**
 A list of names for each month.
 */
LPCTSTR YuiMonthNames[12] = {_T("January"),
                             _T("February"),
                             _T("March"),
                             _T("April"),
                             _T("May"),
                             _T("June"),
                             _T("July"),
                             _T("August"),
                             _T("September"),
                             _T("October"),
                             _T("November"),
                             _T("December")};

/**
 A list of names for each day.
 */
LPCTSTR YuiDayNames[7] = {_T("Sunday"),
                          _T("Monday"),
                          _T("Tuesday"),
                          _T("Wednesday"),
                          _T("Thursday"),
                          _T("Friday"),
                          _T("Saturday")};

/**
 Display additional information about the system time.

 @param YuiContext Pointer to the application context.
 */
VOID
YuiClockDisplayInfo(
    __in PYUI_CONTEXT YuiContext
    )
{
    SYSTEMTIME CurrentTime;
    YORI_STRING Text;
    DWORD Hour;
    LPCTSTR Suffix;

    GetLocalTime(&CurrentTime);

    Suffix = _T("AM");
    Hour = CurrentTime.wHour;
    if (Hour >= 12) {
        Hour = Hour - 12;
        Suffix = _T("PM");
    }
    if (Hour == 0) {
        Hour = 12;
    }

    YoriLibInitEmptyString(&Text);
    YoriLibYPrintf(&Text,
                   _T("%s %02i %s %04i\n%02i:%02i:%02i %s"),
                   YuiDayNames[CurrentTime.wDayOfWeek],
                   CurrentTime.wDay,
                   YuiMonthNames[CurrentTime.wMonth - 1],
                   CurrentTime.wYear,
                   Hour,
                   CurrentTime.wMinute,
                   CurrentTime.wSecond,
                   Suffix);
    MessageBox(YuiContext->hWnd, Text.StartOfString, _T("Clock"), MB_ICONINFORMATION);
    YoriLibFreeStringContents(&Text);
}

/**
 Display additional information about battery state.

 @param YuiContext Pointer to the application context.
 */
VOID
YuiClockDisplayBatteryInfo(
    __in PYUI_CONTEXT YuiContext
    )
{
    YORI_SYSTEM_POWER_STATUS PowerStatus;
    YORI_STRING Text;

    DllKernel32.pGetSystemPowerStatus(&PowerStatus);

    YoriLibInitEmptyString(&Text);

    if (PowerStatus.BatteryFlag & YORI_BATTERY_FLAG_NO_BATTERY) {
        YoriLibYPrintf(&Text, _T("No battery found."));
    } else {
        YORI_STRING TimeRemaining;

        LPCTSTR FormatString = _T("Battery remaining: %i%%\n")
                               _T("Power source: %s\n")
                               _T("Battery state: %s\n")
                               _T("%y");

        YoriLibInitEmptyString(&TimeRemaining);
        if (PowerStatus.BatterySecondsRemaining != (DWORD)-1) {
            YoriLibYPrintf(&TimeRemaining,
                           _T("Time remaining: %i hours, %i minutes\n"),
                           PowerStatus.BatterySecondsRemaining / (60 * 60),
                           (PowerStatus.BatterySecondsRemaining / 60) % 60);

        }


        YoriLibYPrintf(&Text,
                       FormatString,
                       PowerStatus.BatteryLifePercent, 
                       (PowerStatus.PowerSource&YORI_POWER_SOURCE_POWERED)?_T("AC power"):_T("Battery"),
                       (PowerStatus.BatteryFlag&YORI_BATTERY_FLAG_CHARGING)?_T("Charging"):_T("Draining"),
                       &TimeRemaining);

        YoriLibFreeStringContents(&TimeRemaining);
    }

    MessageBox(YuiContext->hWnd, Text.StartOfString, _T("Battery"), MB_ICONINFORMATION);
    YoriLibFreeStringContents(&Text);
}

/**
 Update the value displayed in the clock and battery indicators in the
 taskbar.

 @param YuiContext Pointer to the application context.
 */
VOID
YuiClockUpdate(
    __in PYUI_CONTEXT YuiContext
    )
{
    YORI_STRING DisplayTime;
    TCHAR DisplayTimeBuffer[16];
    YORI_STRING BatteryString;
    TCHAR BatteryStringBuffer[16];
    SYSTEMTIME CurrentLocalTime;
    WORD DisplayHour;
    YORI_SYSTEM_POWER_STATUS PowerStatus;

    YoriLibInitEmptyString(&DisplayTime);
    DisplayTime.StartOfString = DisplayTimeBuffer;
    DisplayTime.LengthAllocated = sizeof(DisplayTimeBuffer)/sizeof(DisplayTimeBuffer[0]);

    YoriLibInitEmptyString(&BatteryString);
    BatteryString.StartOfString = BatteryStringBuffer;
    BatteryString.LengthAllocated = sizeof(BatteryStringBuffer)/sizeof(BatteryStringBuffer[0]);

    GetLocalTime(&CurrentLocalTime);

    DisplayHour = (WORD)(CurrentLocalTime.wHour % 12);
    if (DisplayHour == 0) {
        DisplayHour = 12;
    }

    YoriLibYPrintf(&DisplayTime, _T("%i:%02i %s"), DisplayHour, CurrentLocalTime.wMinute, (CurrentLocalTime.wHour >= 12)?_T("PM"):_T("AM"));

    if (YoriLibCompareString(&DisplayTime, &YuiContext->ClockDisplayedValue) != 0) {
        if (DisplayTime.LengthInChars < YuiContext->ClockDisplayedValue.LengthAllocated) {
            memcpy(YuiContext->ClockDisplayedValue.StartOfString,
                   DisplayTime.StartOfString,
                   DisplayTime.LengthInChars * sizeof(TCHAR));

            YuiContext->ClockDisplayedValue.LengthInChars = DisplayTime.LengthInChars;
        }

        //
        //  YoriLibYPrintf will NULL terminate, but that is hard to express
        //  given that yori strings are not always NULL terminated
        //
#if defined(_MSC_VER) && (_MSC_VER >= 1700)
#pragma warning(suppress: 6054)
#endif
        DllUser32.pSetWindowTextW(YuiContext->hWndClock, DisplayTime.StartOfString);
    }
    YoriLibFreeStringContents(&DisplayTime);

    if (YuiContext->DisplayBattery) {
        DllKernel32.pGetSystemPowerStatus(&PowerStatus);

        YoriLibYPrintf(&BatteryString, _T("%i%%"), PowerStatus.BatteryLifePercent);

        if (YoriLibCompareString(&BatteryString, &YuiContext->BatteryDisplayedValue) != 0) {
            if (BatteryString.LengthInChars < YuiContext->BatteryDisplayedValue.LengthAllocated) {
                memcpy(YuiContext->BatteryDisplayedValue.StartOfString,
                       BatteryString.StartOfString,
                       BatteryString.LengthInChars * sizeof(TCHAR));

                YuiContext->BatteryDisplayedValue.LengthInChars = BatteryString.LengthInChars;
            }

            //
            //  YoriLibYPrintf will NULL terminate, but that is hard to express
            //  given that yori strings are not always NULL terminated
            //
#if defined(_MSC_VER) && (_MSC_VER >= 1700)
#pragma warning(suppress: 6054)
#endif
            DllUser32.pSetWindowTextW(YuiContext->hWndBattery, BatteryString.StartOfString);
        }

        YoriLibFreeStringContents(&BatteryString);
    }
}

// vim:sw=4:ts=4:et:
