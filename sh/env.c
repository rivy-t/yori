/**
 * @file sh/env.c
 *
 * Fetches values from the environment including emulated values
 *
 * Copyright (c) 2017 Malcolm J. Smith
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

#include "yori.h"

/**
 Returns TRUE if the specified character is an environment variable marker.

 @param Char The character to check.

 @return TRUE to indicate the character is an environment variable marker,
         FALSE if it is a regular character.
 */
BOOL
YoriShIsEnvironmentVariableChar(
    __in TCHAR Char
    )
{
    if (Char == '%') {
        return TRUE;
    }
    return FALSE;
}

/**
 Obtain the current directory with a trailing slash using the same API
 semantics as GetCurrentDirectory.  This is used because %__CD__% includes a
 trailing slash, has very bad memory bugs on XP, and isn't implemented before
 that, so doing this manually allows us to implement it consistently and
 hopefully correctly.

 @param Size The size of Buffer, in TCHARs.

 @param Buffer A buffer to populate with the current directory on successful
        completion.

 @return The number of characters copied.  If this is less than Size, the
         operation is successful.  If it's greater than Size, the buffer is
         insufficient and the returned number of characters is needed.  If
         it's zero, a failure occurred.  Note that these APIs will count the
         NULL character when indicating a required buffer length, but do not
         count the NULL character when completing successfully.
 */
__success(return != 0 && return < Size)
DWORD
YoriShGetCurrentDirectoryWithTrailingSlash(
    __in DWORD Size,
    __out_ecount_part_opt(Size, return + 1) _When_(Size > 0, __out_ecount_part(Size, return + 1)) LPTSTR Buffer
    )
{
    YORI_STRING CurDir;
    DWORD LengthNeeded;
    DWORD Index;
    BOOLEAN AddSlash;
    LPTSTR EndPtr;

    YoriLibInitEmptyString(&CurDir);
    if (!YoriLibGetCurrentDirectory(&CurDir)) {
        return 0;
    }

    AddSlash = FALSE;
    LengthNeeded = CurDir.LengthInChars;

    if (CurDir.LengthInChars > 0 &&
        !YoriLibIsSep(CurDir.StartOfString[CurDir.LengthInChars - 1])) {

        LengthNeeded++;
        AddSlash = TRUE;
    }

    if (Size > LengthNeeded) {

        //
        //  Manually copy so Prefast can see what we did
        //

        EndPtr = Buffer;
        for (Index = 0; Index < CurDir.LengthInChars; Index++) {
            *EndPtr = CurDir.StartOfString[Index];
            EndPtr++;
        }
        if (AddSlash) {
            *EndPtr = '\\';
            EndPtr++;
        }
        *EndPtr = '\0';
    } else {
        if (Size > 0) {
            *Buffer = '\0';
        }
        LengthNeeded++;
    }

    YoriLibFreeStringContents(&CurDir);
    return LengthNeeded;
}

/**
 Obtain the application directory with a trailing slash using the same API
 semantics as GetCurrentDirectory.  This is used because %__APPDIR__% includes
 a trailing slash, has very bad memory bugs on XP, and isn't implemented before
 that, so doing this manually allows us to implement it consistently and
 hopefully correctly.

 @param Size The size of Buffer, in TCHARs.

 @param Buffer A buffer to populate with the current directory on successful
        completion.

 @return The number of characters copied.  If this is less than Size, the
         operation is successful.  If it's greater than Size, the buffer is
         insufficient and the returned number of characters is needed.  If
         it's zero, a failure occurred.  Note that these APIs will count the
         NULL character when indicating a required buffer length, but do not
         count the NULL character when completing successfully.
 */
__success(return != 0 && return < Size)
DWORD
YoriShGetAppDirectoryWithTrailingSlash(
    __in DWORD Size,
    __out_ecount_part_opt(Size, return + 1) _When_(Size > 0, __out_ecount_part(Size, return + 1)) LPTSTR Buffer
    )
{
    YORI_STRING AppDir;
    DWORD LengthNeeded;
    LPTSTR FinalSlash;
    LPTSTR EndPtr;
    DWORD Index;

    YoriLibInitEmptyString(&AppDir);

    if (!YoriLibAllocateString(&AppDir, 32768)) {
        return 0;
    }

    AppDir.LengthInChars = GetModuleFileName(NULL, AppDir.StartOfString, AppDir.LengthAllocated);
    if (AppDir.LengthInChars == 0) {
        YoriLibFreeStringContents(&AppDir);
        return 0;
    }

    FinalSlash = YoriLibFindRightMostCharacter(&AppDir, '\\');
    if (FinalSlash == NULL) {
        YoriLibFreeStringContents(&AppDir);
        return 0;
    }

    AppDir.LengthInChars = (DWORD)(FinalSlash - AppDir.StartOfString + 1);
    LengthNeeded = AppDir.LengthInChars;

    if (Size > AppDir.LengthInChars) {

        //
        //  Manually copy so Prefast can see what we did
        //

        EndPtr = Buffer;
        for (Index = 0; Index < AppDir.LengthInChars; Index++) {
            *EndPtr = AppDir.StartOfString[Index];
            EndPtr++;
        }
        *EndPtr = '\0';
    } else {
        if (Size > 0) {
            *Buffer = '\0';
        }
        LengthNeeded++;
    }

    YoriLibFreeStringContents(&AppDir);
    return LengthNeeded;
}

//
//  Warning about manipulating the Variable buffer but failing the
//  function.  This function is trying to mimic the behavior of the
//  underlying API, so if it manipulates the buffer and fails, that's
//  desirable behavior.
//

#if defined(_MSC_VER) && (_MSC_VER >= 1500)
#pragma warning(push)
#pragma warning(suppress: 6103)
#endif

/**
 Wrapper around the Win32 GetEnvironmentVariable call, but augmented with
 "magic" things that appear to be variables but aren't, including %CD% and
 %ERRORLEVEL%.

 @param Name The name of the environment variable to get.

 @param Variable Pointer to the buffer to receive the variable's contents.

 @param Size The length of the Variable parameter, in characters.

 @param Generation Optionally points to a location to populate with the
        generation of the environment at the time of the query.

 @return The number of characters copied (without NULL), of if the buffer
         is too small, the number of characters needed (including NULL.)
 */
__success(return != 0)
DWORD
YoriShGetEnvironmentVariableWithoutSubstitution(
    __in LPCTSTR Name,
    __out_opt _When_(Size > 0, __out) LPTSTR Variable,
    __in DWORD Size,
    __out_opt PDWORD Generation
    )
{
    DWORD Length;
    TCHAR NumString[12];

    //
    //  Query the variable and/or length required.
    //

    if (_tcsicmp(Name, _T("__APPDIR__")) == 0) {
        Length = YoriShGetAppDirectoryWithTrailingSlash(Size, Variable);
    } else if (_tcsicmp(Name, _T("CD")) == 0) {
        Length = GetCurrentDirectory(Size, Variable);
    } else if (tcsicmp(Name, _T("__CD__")) == 0) {
        Length = YoriShGetCurrentDirectoryWithTrailingSlash(Size, Variable);
    } else if (tcsicmp(Name, _T("ERRORLEVEL")) == 0) {
        if (Variable != NULL) {
            Length = YoriLibSPrintfS(Variable, Size, _T("%i"), YoriShGlobal.ErrorLevel);
        } else {
            Length = YoriLibSPrintfS(NumString, sizeof(NumString)/sizeof(NumString[0]), _T("%i"), YoriShGlobal.ErrorLevel);
            Length++;
        }
    } else if (tcsicmp(Name, _T("LASTJOB")) == 0) {
        if (Variable != NULL) {
            Length = YoriLibSPrintfS(Variable, Size, _T("%i"), YoriShGlobal.PreviousJobId);
        } else {
            Length = YoriLibSPrintfS(NumString, sizeof(NumString)/sizeof(NumString[0]), _T("%i"), YoriShGlobal.PreviousJobId);
            Length++;
        }
    } else if (tcsicmp(Name, _T("YORIPID")) == 0) {
        if (Variable != NULL) {
            Length = YoriLibSPrintfS(Variable, Size, _T("0x%x"), GetCurrentProcessId());
        } else {
            Length = YoriLibSPrintfS(NumString, sizeof(NumString)/sizeof(NumString[0]), _T("0x%x"), GetCurrentProcessId());
            Length++;
        }
    } else {

        Length = GetEnvironmentVariable(Name, Variable, Size);
    }

    if (Generation != NULL) {
        *Generation = YoriShGlobal.EnvironmentGeneration;
    }

    return Length;
}

#if defined(_MSC_VER) && (_MSC_VER >= 1500)
#pragma warning(pop)
#endif

/**
 Wrapper around the Win32 GetEnvironmentVariable call, but augmented with
 "magic" things that appear to be variables but aren't, including %CD% and
 %ERRORLEVEL%.

 @param Name The name of the environment variable to get.

 @param Variable Pointer to the buffer to receive the variable's contents.

 @param Size The length of the Variable parameter, in characters.

 @param ReturnedSize On successful completion, populated with the number of
        characters copied (without NULL), or if the buffer is too small, the
        number of characters needed (including NULL.)

 @param Generation Optionally points to a location to populate with the
        generation of the environment at the time of the query.

 @return TRUE to indicate success, FALSE to indicate failure.  In particular,
         returns FALSE to indicate that the variable was not found.
 */
#if defined(_MSC_VER) && (_MSC_VER >= 1700)
#pragma warning(push)
#pragma warning(disable: 6054) // Can return non-NULL terminated string,
                               // which is detected by the caller as
                               // ReturnedSize > Size
#pragma warning(disable: 6101 6103) // Returning uninitialized memory,
                               // which is detected by the caller as
                               // ReturnedSize > Size
#endif
__success(return)
BOOL
YoriShGetEnvironmentVariable(
    __in LPCTSTR Name,
    __out_ecount_opt(Size) LPTSTR Variable,
    __in DWORD Size,
    __out PDWORD ReturnedSize,
    __out_opt PDWORD Generation
    )
{
    DWORD DataLength;
    LPTSTR DataVariable;
    LPTSTR ColonPtr;
    LPTSTR EqualsPtr;
    LPTSTR RawName;
    DWORD RawNameLength;
    DWORD ProcessedLength;

    __analysis_assume(Size == 0 || Variable != NULL);

    //
    //  Find the colon which is followed by information about the substring
    //  to return.  If there isn't one, this is a simple case that can be
    //  handed to the lower level routine.
    //

    ColonPtr = _tcschr(Name, ':');
    if (ColonPtr == NULL) {
        DataLength = YoriShGetEnvironmentVariableWithoutSubstitution(Name, Variable, Size, Generation);
        if (DataLength == 0) {
            return FALSE;
        }
        *ReturnedSize = DataLength;
        return TRUE;
    }

    //
    //  If one exists, copy the part of the string before the colon so it can
    //  be NULL terminated and we can call into the OS APIs.
    //

    RawNameLength = (DWORD)(ColonPtr - Name);
    RawName = YoriLibMalloc((RawNameLength + 1) * sizeof(TCHAR));
    if (RawName == NULL) {
        return FALSE;
    }
    memcpy(RawName, Name, RawNameLength * sizeof(TCHAR));
    RawName[RawNameLength] = '\0';
    ColonPtr++;


    //
    //  Check what kind of processing we're doing.  It may be substring
    //  fetching (indicated with ~) or may be string substitution (no ~,
    //  but an = somewhere else.)
    //

    EqualsPtr = NULL;
    if (ColonPtr[0] != '~') {
        EqualsPtr = _tcschr(ColonPtr, '=');
    }

    DataVariable = NULL;
    DataLength = YoriShGetEnvironmentVariableWithoutSubstitution(RawName, NULL, 0, Generation);
    if (DataLength == 0) {
        YoriLibFree(RawName);
        return FALSE;
    }

    //
    //  If the request wants to return data, or if we're doing string
    //  substitution, this routine needs to double buffer.
    //

    if (Variable != NULL || EqualsPtr != NULL) {
        DWORD FinalDataLength;
        DataVariable = YoriLibMalloc(DataLength * sizeof(TCHAR));
        if (DataVariable == NULL) {
            YoriLibFree(RawName);
            return FALSE;
        }

        FinalDataLength = YoriShGetEnvironmentVariableWithoutSubstitution(RawName, DataVariable, DataLength, NULL);

        if (FinalDataLength >= DataLength || FinalDataLength == 0) {
            YoriLibFree(RawName);
            YoriLibFree(DataVariable);
            return FALSE;
        }
    }

    ProcessedLength = 0;

    if (ColonPtr[0] == '~') {
        YORI_STRING SubstringString;
        DWORD CharsConsumed;
        LONGLONG RequestedOffset;
        LONGLONG RequestedLength;
        ULONG ActualOffset;
        ULONG ActualLength;

        RequestedOffset = 0;
        RequestedLength = DataLength - 1;

        //
        //  Parse the range that the user requested.
        //

        YoriLibConstantString(&SubstringString, ColonPtr + 1);
        if (!YoriLibStringToNumber(&SubstringString, FALSE, &RequestedOffset, &CharsConsumed)) {
            YoriLibFree(RawName);
            return FALSE;
        }

        if (CharsConsumed < SubstringString.LengthInChars) {
            SubstringString.StartOfString += CharsConsumed;
            SubstringString.LengthInChars -= CharsConsumed;

            if (SubstringString.StartOfString[0] == ',' && SubstringString.LengthInChars > 1) {
                SubstringString.StartOfString++;
                SubstringString.LengthInChars--;

                if (!YoriLibStringToNumber(&SubstringString, FALSE, &RequestedLength, &CharsConsumed)) {
                    YoriLibFree(RawName);
                    return FALSE;
                }
            }
        }

        //
        //  Remove the NULL from the data length.  We'll add it back as needed
        //  below.
        //

        DataLength--;

        //
        //  Check the user request against the known string length making any
        //  adjustments necessary.
        //

        if (RequestedOffset >= 0) {
            if (RequestedOffset < DataLength) {
                ActualOffset = (ULONG)RequestedOffset;
            } else {
                ActualOffset = 0;
                RequestedLength = 0;
            }
        } else {
            RequestedOffset = RequestedOffset * -1;
            if (RequestedOffset > DataLength) {
                ActualOffset = 0;
                RequestedLength = 0;
            } else {
                ActualOffset = (DWORD)(DataLength - RequestedOffset);
            }
        }

        if (RequestedLength < 0) {
            RequestedLength = RequestedLength * -1;
            if (RequestedLength > DataLength) {
                RequestedLength = DataLength;
            }

            RequestedLength = DataLength - RequestedLength;
        }

        if (ActualOffset + RequestedLength < DataLength) {
            ActualLength = (ULONG)RequestedLength;
        } else {
            ActualLength = DataLength - ActualOffset;
        }

        //
        //  If this is a request for data and the buffer is big enough, return
        //  data.  If it's not a request for data or the buffer is too small,
        //  return the actual length plus a NULL terminator.
        //

        if (Variable == NULL) {
            ProcessedLength = ActualLength + 1;
        } else if (Size < ActualLength + 1) {
            ProcessedLength = ActualLength + 1;
        } else {
            memcpy(Variable, DataVariable + ActualOffset, ActualLength * sizeof(TCHAR));
            Variable[ActualLength] = '\0';
            ProcessedLength = ActualLength;
        }

    } else if (EqualsPtr != NULL) {
        YORI_STRING SearchExpr;
        YORI_STRING ReplaceExpr;
        YORI_STRING RawVariable;
        PYORI_STRING FoundMatch;
        DWORD FoundAt;
        DWORD CurrentOffset;

        YoriLibInitEmptyString(&SearchExpr);
        SearchExpr.StartOfString = ColonPtr;
        SearchExpr.LengthInChars = (DWORD)(EqualsPtr - ColonPtr);
        YoriLibConstantString(&ReplaceExpr, EqualsPtr + 1);

        if (SearchExpr.LengthInChars == 0) {
            YoriLibFree(DataVariable);
            YoriLibFree(RawName);
            return FALSE;
        }

        YoriLibConstantString(&RawVariable, DataVariable);
        CurrentOffset = 0;
        FoundMatch = YoriLibFindFirstMatchingSubstring(&RawVariable, 1, &SearchExpr, &FoundAt);
        while (FoundMatch) {
            if (Variable != NULL && CurrentOffset + FoundAt < Size) {
                memcpy(Variable + CurrentOffset, RawVariable.StartOfString, FoundAt * sizeof(TCHAR));
            }
            CurrentOffset += FoundAt;
            if (Variable != NULL && CurrentOffset + ReplaceExpr.LengthInChars < Size) {
                memcpy(Variable + CurrentOffset,
                       ReplaceExpr.StartOfString,
                       ReplaceExpr.LengthInChars * sizeof(TCHAR));
            }
            CurrentOffset += ReplaceExpr.LengthInChars;
            RawVariable.StartOfString += FoundAt + SearchExpr.LengthInChars;
            RawVariable.LengthInChars -= FoundAt + SearchExpr.LengthInChars;
            FoundMatch = YoriLibFindFirstMatchingSubstring(&RawVariable, 1, &SearchExpr, &FoundAt);
        }
        if (Variable != NULL && CurrentOffset + RawVariable.LengthInChars < Size) {
            memcpy(Variable + CurrentOffset,
                   RawVariable.StartOfString,
                   RawVariable.LengthInChars * sizeof(TCHAR));
        }

        CurrentOffset += RawVariable.LengthInChars;
        if (Variable != NULL && CurrentOffset < Size) {
            Variable[CurrentOffset] = '\0';
            ProcessedLength = CurrentOffset;
        } else {
            ProcessedLength = CurrentOffset + 1;
        }
    } else {
        ProcessedLength = YoriShGetEnvironmentVariableWithoutSubstitution(Name, Variable, Size, NULL);
    }

    if (DataVariable != NULL) {
        YoriLibFree(DataVariable);
    }
    YoriLibFree(RawName);

    *ReturnedSize = ProcessedLength;
    return TRUE;
}
#if defined(_MSC_VER) && (_MSC_VER >= 1700)
#pragma warning(pop)
#endif

/**
 Capture the value from an environment variable, allocating a Yori string of
 appropriate size to contain the contents.

 @param Name Pointer to the name of the variable to obtain.

 @param Value On successful completion, populated with a newly allocated
        string containing the environment variable's contents.

 @param Generation Optionally points to a location to populate with the
        generation of the environment at the time of the query.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOL
YoriShAllocateAndGetEnvironmentVariable(
    __in LPCTSTR Name,
    __out PYORI_STRING Value,
    __out_opt PDWORD Generation
    )
{
    DWORD LengthNeeded;

    if (Generation != NULL) {
        *Generation = YoriShGlobal.EnvironmentGeneration;
    }

    if (!YoriShGetEnvironmentVariable(Name, NULL, 0, &LengthNeeded, NULL)) {
        YoriLibInitEmptyString(Value);
        return TRUE;
    }

    if (!YoriLibAllocateString(Value, LengthNeeded)) {
        return FALSE;
    }

    if (!YoriShGetEnvironmentVariable(Name, Value->StartOfString, Value->LengthAllocated, &Value->LengthInChars, NULL) ||
        Value->LengthInChars >= Value->LengthAllocated) {

        YoriLibFreeStringContents(Value);
        return FALSE;
    }

    return TRUE;
}

/**
 Get an environment variable from a variable named as a Yori string.

 @param VariableName Pointer to the name of the variable to obtain.

 @param Value On successful completion, populated with a newly allocated
        string containing the environment variable's contents.

 @return TRUE to indicate success, FALSE to indicate failure.  In particular,
         returns FALSE to indicate that the variable was not found.
 */
__success(return)
BOOL
YoriShGetEnvironmentVariableYS(
    __in PYORI_STRING VariableName,
    __out PYORI_STRING Value
    )
{
    LPTSTR NullTerminatedVariable;
    BOOL AllocatedVariable;
    DWORD LengthNeeded;

    if (YoriLibIsStringNullTerminated(VariableName)) {
        NullTerminatedVariable = VariableName->StartOfString;
        AllocatedVariable = FALSE;
    } else {
        NullTerminatedVariable = YoriLibCStringFromYoriString(VariableName);
        if (NullTerminatedVariable == NULL) {
            return FALSE;
        }
        AllocatedVariable = TRUE;
    }

    if (!YoriShGetEnvironmentVariable(NullTerminatedVariable, NULL, 0, &LengthNeeded, NULL)) {
        YoriLibInitEmptyString(Value);
        if (AllocatedVariable) {
            YoriLibDereference(NullTerminatedVariable);
        }
        return FALSE;
    }

    if (!YoriLibAllocateString(Value, LengthNeeded)) {
        if (AllocatedVariable) {
            YoriLibDereference(NullTerminatedVariable);
        }
        return FALSE;
    }

    if (!YoriShGetEnvironmentVariable(NullTerminatedVariable, Value->StartOfString, Value->LengthAllocated, &Value->LengthInChars, NULL) ||
        Value->LengthInChars >= Value->LengthAllocated) {

        if (AllocatedVariable) {
            YoriLibDereference(NullTerminatedVariable);
        }

        YoriLibFreeStringContents(Value);
        return FALSE;
    }

    if (AllocatedVariable) {
        YoriLibDereference(NullTerminatedVariable);
    }

    return TRUE;
}


/**
 Returns the expanded form of an environment variable.  For variables that are
 not defined, the expanded form is the name of the variable itself, keeping
 the seperators in place.

 @param Name Pointer to a string specifying the environment variable name.
        Note this is not NULL terminated.

 @param Seperator The seperator character to use when the variable is not
        found.

 @param Result Optionally points to a buffer to receive the result.  If not
        specified, only the length required is returned.

 @param ReturnedSize On successful completion, populated with the number of
        characters copied (without NULL), or if the buffer is too small, the
        number of characters needed (including NULL.)

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOL
YoriShGetEnvironmentExpandedText(
    __in PYORI_STRING Name,
    __in TCHAR Seperator,
    __inout PYORI_STRING Result,
    __out PDWORD ReturnedSize
    )
{
    DWORD EnvVarCopied;
    LPTSTR EnvVarName;
    DWORD ReturnValue;

    EnvVarName = YoriLibCStringFromYoriString(Name);
    if (EnvVarName == NULL) {
        return FALSE;
    }

    if (!YoriShGetEnvironmentVariable(EnvVarName, Result->StartOfString, Result->LengthAllocated, &EnvVarCopied, NULL)) {

        if (Result->LengthAllocated > 2 + Name->LengthInChars) {
            Result->LengthInChars = YoriLibSPrintf(Result->StartOfString, _T("%c%y%c"), Seperator, Name, Seperator);
            ReturnValue = Result->LengthInChars;
        } else {
            ReturnValue = Name->LengthInChars + 2 + 1;
        }

    } else {

        ReturnValue = EnvVarCopied;
        if (Result->LengthAllocated > EnvVarCopied) {
            Result->LengthInChars = EnvVarCopied;
        }
    }

    YoriLibDereference(EnvVarName);

    *ReturnedSize = ReturnValue;
    return TRUE;
}

/**
 Expand the environment variables in a string and return the result.

 @param Expression Pointer to the string which may contain variables to
        expand.

 @param ResultingExpression On successful completion, updated to point to
        a string containing the expanded form.  This may be a pointer to
        the same string as Expression; the caller should call
        @ref YoriLibDereference on this value if it is different to
        Expression.

 @param CurrentOffset Optionally specifies the offset of the cursor within
        the source expression.  If specified, on output, this value is
        updated to contain the cursor position after environment expansion.

 @return TRUE to indicate variables were successfully expanded, or FALSE to
         indicate a failure to expand.
 */
__success(return)
BOOL
YoriShExpandEnvironmentVariables(
    __in PYORI_STRING Expression,
    __out PYORI_STRING ResultingExpression,
    __inout_opt PDWORD CurrentOffset
    )
{
    DWORD SrcIndex;
    DWORD EndVarIndex;
    DWORD DestIndex;
    DWORD ExpandResult;
    DWORD LocalCurrentOffset;
    BOOLEAN CurrentOffsetFound = FALSE;
    BOOLEAN VariableExpanded;
    BOOLEAN AnyVariableExpanded = FALSE;
    YORI_STRING VariableName;
    YORI_STRING ExpandedVariable;

    LocalCurrentOffset = 0;
    if (CurrentOffset != NULL) {
        LocalCurrentOffset = *CurrentOffset;
    }

    //
    //  First, scan through looking for environment variables to expand, and
    //  count the size needed to perform expansion.
    //

    YoriLibInitEmptyString(&ExpandedVariable);
    YoriLibInitEmptyString(&VariableName);

    for (SrcIndex = 0, DestIndex = 0; SrcIndex < Expression->LengthInChars; SrcIndex++) {

        if (YoriLibIsEscapeChar(Expression->StartOfString[SrcIndex])) {
            SrcIndex++;
            DestIndex++;
            if (SrcIndex >= Expression->LengthInChars) {
                break;
            }
            DestIndex++;
            continue;
        }

        if (YoriShIsEnvironmentVariableChar(Expression->StartOfString[SrcIndex])) {
            VariableExpanded = FALSE;
            VariableName.StartOfString = &Expression->StartOfString[SrcIndex + 1];

            for (EndVarIndex = SrcIndex + 1; EndVarIndex < Expression->LengthInChars; EndVarIndex++) {
                if (YoriLibIsEscapeChar(Expression->StartOfString[EndVarIndex])) {
                    EndVarIndex++;
                    if (EndVarIndex >= Expression->LengthInChars) {
                        break;
                    }
                    continue;
                }

                if (YoriShIsEnvironmentVariableChar(Expression->StartOfString[EndVarIndex])) {

                    VariableName.LengthInChars = EndVarIndex - SrcIndex - 1;
                    if (!YoriShGetEnvironmentExpandedText(&VariableName,
                                                          Expression->StartOfString[SrcIndex],
                                                          &ExpandedVariable,
                                                          &ExpandResult) ||
                        ExpandResult == 0) {
                        return FALSE;
                    }

                    DestIndex += ExpandResult;
                    SrcIndex = EndVarIndex;
                    VariableExpanded = TRUE;
                    AnyVariableExpanded = TRUE;
                    break;
                }
            }

            if (!VariableExpanded) {
                DestIndex += (EndVarIndex - SrcIndex);
                SrcIndex = EndVarIndex;
                if (SrcIndex >= Expression->LengthInChars) {
                    break;
                }
            }
        } else {
            DestIndex++;
        }
    }

    //
    //  If no environment variables were found, we're done.
    //

    if (!AnyVariableExpanded) {
        memcpy(ResultingExpression, Expression, sizeof(YORI_STRING));
        return TRUE;
    }

    //
    //  If they were found, allocate a buffer and apply the same algorithm as
    //  before, this time populating the buffer.
    //

    DestIndex++;
    if (!YoriLibAllocateString(ResultingExpression, DestIndex)) {
        return FALSE;
    }

    for (SrcIndex = 0, DestIndex = 0; SrcIndex < Expression->LengthInChars; SrcIndex++) {

        if (YoriLibIsEscapeChar(Expression->StartOfString[SrcIndex])) {

            if (!CurrentOffsetFound &&
                LocalCurrentOffset == SrcIndex) {

                LocalCurrentOffset = DestIndex;
                CurrentOffsetFound = FALSE;
            }

            ResultingExpression->StartOfString[DestIndex] = Expression->StartOfString[SrcIndex];
            SrcIndex++;
            DestIndex++;
            if (SrcIndex >= Expression->LengthInChars) {
                break;
            }

            if (!CurrentOffsetFound &&
                LocalCurrentOffset == SrcIndex) {

                LocalCurrentOffset = DestIndex;
                CurrentOffsetFound = FALSE;
            }

            ResultingExpression->StartOfString[DestIndex] = Expression->StartOfString[SrcIndex];
            DestIndex++;
            continue;
        }

        if (YoriShIsEnvironmentVariableChar(Expression->StartOfString[SrcIndex])) {
            VariableExpanded = FALSE;
            VariableName.StartOfString = &Expression->StartOfString[SrcIndex + 1];
            for (EndVarIndex = SrcIndex + 1; EndVarIndex < Expression->LengthInChars; EndVarIndex++) {
                if (YoriLibIsEscapeChar(Expression->StartOfString[EndVarIndex])) {
                    EndVarIndex++;
                    if (EndVarIndex >= Expression->LengthInChars) {
                        break;
                    }
                    continue;
                }

                if (YoriShIsEnvironmentVariableChar(Expression->StartOfString[EndVarIndex])) {
                    VariableName.LengthInChars = EndVarIndex - SrcIndex - 1;
                    ExpandedVariable.StartOfString = &ResultingExpression->StartOfString[DestIndex];
                    ExpandedVariable.LengthAllocated = ResultingExpression->LengthAllocated - DestIndex;
                    if (!YoriShGetEnvironmentExpandedText(&VariableName,
                                                          Expression->StartOfString[SrcIndex],
                                                          &ExpandedVariable,
                                                          &ExpandResult)) {
                        YoriLibFreeStringContents(ResultingExpression);
                        return FALSE;
                    }

                    if (!CurrentOffsetFound &&
                        LocalCurrentOffset >= SrcIndex &&
                        LocalCurrentOffset <= EndVarIndex) {

                        LocalCurrentOffset = DestIndex + ExpandResult;
                        CurrentOffsetFound = FALSE;
                    }

                    SrcIndex = EndVarIndex;
                    DestIndex += ExpandResult;
                    VariableExpanded = TRUE;
                    break;
                }
            }

            if (!VariableExpanded) {
                if (!CurrentOffsetFound &&
                    LocalCurrentOffset >= SrcIndex &&
                    LocalCurrentOffset <= EndVarIndex) {

                    LocalCurrentOffset = DestIndex + (EndVarIndex - SrcIndex);
                    CurrentOffsetFound = FALSE;
                }

                memcpy(&ResultingExpression->StartOfString[DestIndex], &Expression->StartOfString[SrcIndex], (EndVarIndex - SrcIndex) * sizeof(TCHAR));
                DestIndex += (EndVarIndex - SrcIndex);
                SrcIndex = EndVarIndex;
                if (SrcIndex >= Expression->LengthInChars) {
                    break;
                }
            }
        } else {

            if (!CurrentOffsetFound &&
                LocalCurrentOffset == SrcIndex) {

                LocalCurrentOffset = DestIndex;
                CurrentOffsetFound = FALSE;
            }

            ResultingExpression->StartOfString[DestIndex] = Expression->StartOfString[SrcIndex];
            DestIndex++;
        }
    }

    if (!CurrentOffsetFound) {
        LocalCurrentOffset = DestIndex;
        CurrentOffsetFound = FALSE;
        if (LocalCurrentOffset > 0) {
            LocalCurrentOffset--;
        }
    }

    if (CurrentOffset != NULL) {
        *CurrentOffset = LocalCurrentOffset;
    }

    ResultingExpression->StartOfString[DestIndex] = '\0';
    ResultingExpression->LengthInChars = DestIndex;
    return TRUE;
}

/**
 Set an environment variable in the Yori shell process.

 @param VariableName The variable name to set.

 @param Value Pointer to the value to set.  If NULL, the variable is deleted.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOL
YoriShSetEnvironmentVariable(
    __in PYORI_STRING VariableName,
    __in_opt PYORI_STRING Value
    )
{
    LPTSTR NullTerminatedVariable;
    LPTSTR NullTerminatedValue;
    BOOL AllocatedVariable;
    BOOL AllocatedValue;
    BOOL Result;

    if (YoriLibIsStringNullTerminated(VariableName)) {
        NullTerminatedVariable = VariableName->StartOfString;
        AllocatedVariable = FALSE;
    } else {
        NullTerminatedVariable = YoriLibCStringFromYoriString(VariableName);
        if (NullTerminatedVariable == NULL) {
            return FALSE;
        }
        AllocatedVariable = TRUE;
    }

    if (Value == NULL) {
        NullTerminatedValue = NULL;
        AllocatedValue = FALSE;
    } else if (YoriLibIsStringNullTerminated(Value)) {
        NullTerminatedValue = Value->StartOfString;
        AllocatedValue = FALSE;
    } else {
        NullTerminatedValue = YoriLibCStringFromYoriString(Value);
        if (NullTerminatedValue == NULL) {
            if (AllocatedVariable) {
                YoriLibDereference(NullTerminatedVariable);
            }
            return FALSE;
        }
        AllocatedValue = TRUE;
    }

    ASSERT(!AllocatedVariable && !AllocatedValue);

    Result = SetEnvironmentVariable(NullTerminatedVariable, NullTerminatedValue);
    YoriShGlobal.EnvironmentGeneration++;

    if (AllocatedVariable) {
        YoriLibDereference(NullTerminatedVariable);
    }
    if (AllocatedValue) {
        YoriLibDereference(NullTerminatedValue);
    }

    return Result;
}

/**
 Apply an environment block into the running process.  Variables not explicitly
 included in this block are discarded.

 @param NewEnv Pointer to the new environment block to apply.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOL
YoriShSetEnvironmentStrings(
    __in PYORI_STRING NewEnv
    )
{
    YORI_STRING CurrentEnvironment;
    LPTSTR ThisVar;
    LPTSTR ThisValue;
    DWORD VarLen;

    //
    //  Query the current environment and delete everything in it.
    //

    if (!YoriLibGetEnvironmentStrings(&CurrentEnvironment)) {
        return FALSE;
    }
    ThisVar = CurrentEnvironment.StartOfString;
    while (*ThisVar != '\0') {
        VarLen = _tcslen(ThisVar);

        //
        //  We know there's at least one char.  Skip it if it's equals since
        //  that's how drive current directories are recorded.
        //

        ThisValue = _tcschr(&ThisVar[1], '=');
        if (ThisValue != NULL) {
            ThisValue[0] = '\0';
            SetEnvironmentVariable(ThisVar, NULL);
        }

        ThisVar += VarLen;
        ThisVar++;
    }
    YoriLibFreeStringContents(&CurrentEnvironment);

    //
    //  Now load the new environment.
    //

    ThisVar = NewEnv->StartOfString;
    while (*ThisVar != '\0') {
        VarLen = _tcslen(ThisVar);

        //
        //  We know there's at least one char.  Skip it if it's equals since
        //  that's how drive current directories are recorded.
        //

        ThisValue = _tcschr(&ThisVar[1], '=');
        if (ThisValue != NULL) {
            ThisValue[0] = '\0';
            ThisValue++;
            SetEnvironmentVariable(ThisVar, ThisValue);
        }

        ThisVar += VarLen;
        ThisVar++;
    }

    YoriShGlobal.EnvironmentGeneration++;

    return TRUE;
}


// vim:sw=4:ts=4:et:
