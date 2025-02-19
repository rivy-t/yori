/**
 * @file base64/base64.c
 *
 * Yori shell base64 encode or decode
 *
 * Copyright (c) 2023 Malcolm J. Smith
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

/**
 Help text to display to the user.
 */
const
CHAR strBase64HelpText[] =
        "\n"
        "Base64 encode or decode a file or standard input.\n"
        "\n"
        "HASH [-license] [-d] [<file>]\n"
        "\n"
        "   -d             Decode the file or standard input.  Default is encode.\n";

/**
 Display usage text to the user.
 */
BOOL
Base64Help(VOID)
{
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("Base64 %i.%02i\n"), YORI_VER_MAJOR, YORI_VER_MINOR);
#if YORI_BUILD_ID
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("  Build %i\n"), YORI_BUILD_ID);
#endif
    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%hs"), strBase64HelpText);
    return TRUE;
}

/**
 A buffer for a single data stream.
 */
typedef struct _BASE64_BUFFER {

    /**
     The number of bytes currently allocated to this buffer.
     */
    DWORD BytesAllocated;

    /**
     The number of bytes populated with data in this buffer.
     */
    DWORD BytesPopulated;

    /**
     A handle to a pipe which is the source of data for this buffer.
     */
    HANDLE hSource;

    /**
     The data buffer.
     */
    PCHAR Buffer;

} BASE64_BUFFER, *PBASE64_BUFFER;

/**
 Populate data from stdin into an in memory buffer.

 @param ThisBuffer A pointer to the process buffer set.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
Base64BufferPump(
    __in PBASE64_BUFFER ThisBuffer
    )
{
    DWORD BytesRead;
    BOOL Result = FALSE;

    while (TRUE) {

        if (ReadFile(ThisBuffer->hSource,
                     YoriLibAddToPointer(ThisBuffer->Buffer, ThisBuffer->BytesPopulated),
                     ThisBuffer->BytesAllocated - ThisBuffer->BytesPopulated,
                     &BytesRead,
                     NULL)) {

            if (BytesRead == 0) {
                Result = TRUE;
                break;
            }

            ThisBuffer->BytesPopulated += BytesRead;
            ASSERT(ThisBuffer->BytesPopulated <= ThisBuffer->BytesAllocated);
            if (ThisBuffer->BytesPopulated >= ThisBuffer->BytesAllocated) {
                DWORD NewBytesAllocated;
                PCHAR NewBuffer;

                //
                //  Note this limits the size of the allocation to be 1Gb.
                //  This program depends on having the source buffer in
                //  memory at the same time as the target buffer, so it
                //  depends on ensuring that a string allocation can
                //  coexist with this one, and that allocation will be 2.5x
                //  larger.  In a strict sense this limit could be higher,
                //  but not by much.
                //
                if (ThisBuffer->BytesAllocated >= ((DWORD)-1) / 4) {
                    break;
                }

                NewBytesAllocated = ThisBuffer->BytesAllocated * 4;

                NewBuffer = YoriLibMalloc(NewBytesAllocated);
                if (NewBuffer == NULL) {
                    break;
                }

                memcpy(NewBuffer, ThisBuffer->Buffer, ThisBuffer->BytesAllocated);
                YoriLibFree(ThisBuffer->Buffer);
                ThisBuffer->Buffer = NewBuffer;
                ThisBuffer->BytesAllocated = NewBytesAllocated;
            }
        } else {
            Result = TRUE;
            break;
        }
    }

    return Result;
}

/**
 Allocate and initialize a buffer for an input stream.

 @param Buffer Pointer to the buffer to allocate structures for.

 @return TRUE if the buffer is successfully initialized, FALSE if it is not.
 */
BOOL
Base64AllocateBuffer(
    __out PBASE64_BUFFER Buffer
    )
{
    Buffer->BytesAllocated = 1024;
    Buffer->Buffer = YoriLibMalloc(Buffer->BytesAllocated);
    if (Buffer->Buffer == NULL) {
        return FALSE;
    }

    return TRUE;
}

/**
 Free structures associated with a single input stream.

 @param ThisBuffer Pointer to the single stream's buffers to deallocate.
 */
VOID
Base64FreeBuffer(
    __in PBASE64_BUFFER ThisBuffer
    )
{
    if (ThisBuffer->Buffer != NULL) {
        YoriLibFree(ThisBuffer->Buffer);
        ThisBuffer->Buffer = NULL;
    }

    ThisBuffer->BytesAllocated = 0;
    ThisBuffer->BytesPopulated = 0;
}

/**
 Perform base64 encode and output to the requested device.

 @param ThisBuffer Pointer to the buffer to output.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
Base64Encode(
    __inout PBASE64_BUFFER ThisBuffer
    )
{
    DWORD CharsRequired;
    YORI_STRING Buffer;
    DWORD Err;
    LPTSTR ErrText;

    //
    //  Calculate the buffer size needed
    //

    if (!DllCrypt32.pCryptBinaryToStringW(ThisBuffer->Buffer, ThisBuffer->BytesPopulated, CRYPT_STRING_BASE64, NULL, &CharsRequired)) {
        Err = GetLastError();
        ErrText = YoriLibGetWinErrorText(Err);
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("base64: failure to calculate buffer length in CryptBinaryToString: %s"), ErrText);
        YoriLibFreeWinErrorText(ErrText);
        return FALSE;
    }

    //
    //  Check if the buffer size would overflow, and fail if so
    //

    if (CharsRequired >= ((DWORD)-1) / 2) {
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("base64: supplied data too large\n"));
        return FALSE;
    }

    //
    //  Allocate buffer
    //

    if (!YoriLibAllocateString(&Buffer, CharsRequired)) {
        Err = GetLastError();
        ErrText = YoriLibGetWinErrorText(Err);
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("base64: allocation failure: %s"), ErrText);
        YoriLibFreeWinErrorText(ErrText);
        return FALSE;
    }

    //
    //  Perform the encode
    //

    Buffer.LengthInChars = CharsRequired;
    if (!DllCrypt32.pCryptBinaryToStringW(ThisBuffer->Buffer, ThisBuffer->BytesPopulated, CRYPT_STRING_BASE64, Buffer.StartOfString, &Buffer.LengthInChars)) {
        Err = GetLastError();
        ErrText = YoriLibGetWinErrorText(Err);
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("base64: failure to encode in CryptBinaryToString: %s"), ErrText);
        YoriLibFreeWinErrorText(ErrText);
        YoriLibFreeStringContents(&Buffer);
        return FALSE;
    }

    //
    //  Free the source buffer.  We're done with it by this point, and
    //  output may need to double buffer for encoding.
    //

    Base64FreeBuffer(ThisBuffer);

    //
    //  Output the encoded form.
    //

    YoriLibOutput(YORI_LIB_OUTPUT_STDOUT, _T("%y"), &Buffer);
    YoriLibFreeStringContents(&Buffer);

    return TRUE;
}

/**
 Perform base64 decode and output to the requested device.

 @param ThisBuffer Pointer to the buffer to output.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOL
Base64Decode(
    __inout PBASE64_BUFFER ThisBuffer
    )
{
    DWORD CharsRequired;
    DWORD BytesRequired;
    YORI_STRING Buffer;
    PUCHAR BinaryBuffer;
    DWORD Skip;
    DWORD Flags;
    DWORD BytesSent;
    BOOL Result;
    HANDLE hTarget;
    DWORD Err;
    LPTSTR ErrText;

    //
    //  Convert the input buffer into a UTF16 string.
    //

    CharsRequired = YoriLibGetMultibyteInputSizeNeeded(ThisBuffer->Buffer, ThisBuffer->BytesPopulated);
    if (!YoriLibAllocateString(&Buffer, CharsRequired + 1)) {
        Err = GetLastError();
        ErrText = YoriLibGetWinErrorText(Err);
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("base64: allocation failure: %s"), ErrText);
        YoriLibFreeWinErrorText(ErrText);
        return FALSE;
    }

    YoriLibMultibyteInput(ThisBuffer->Buffer, ThisBuffer->BytesPopulated, Buffer.StartOfString, CharsRequired);
    Buffer.LengthInChars = CharsRequired;
    Buffer.StartOfString[CharsRequired] = '\0';

    //
    //  Free the source buffer.  We're done with it by this point, and
    //  output may need to double buffer for encoding.
    //

    Base64FreeBuffer(ThisBuffer);

    //
    //  Calculate the buffer size needed
    //

    if (!DllCrypt32.pCryptStringToBinaryW(Buffer.StartOfString, Buffer.LengthInChars, CRYPT_STRING_BASE64, NULL, &BytesRequired, NULL, NULL)) {
        Err = GetLastError();
        ErrText = YoriLibGetWinErrorText(Err);
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("base64: failure to calculate buffer length in CryptStringToBinary: %s"), ErrText);
        YoriLibFreeWinErrorText(ErrText);
        YoriLibFreeStringContents(&Buffer);
        return FALSE;
    }

    //
    //  Allocate binary buffer
    //

    BinaryBuffer = YoriLibMalloc(BytesRequired);
    if (BinaryBuffer == NULL) {
        Err = GetLastError();
        ErrText = YoriLibGetWinErrorText(Err);
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("base64: allocation failure: %s"), ErrText);
        YoriLibFreeWinErrorText(ErrText);
        YoriLibFreeStringContents(&Buffer);
        return FALSE;
    }

    //
    //  Perform the decode
    //

    if (!DllCrypt32.pCryptStringToBinaryW(Buffer.StartOfString, Buffer.LengthInChars, CRYPT_STRING_BASE64, BinaryBuffer, &BytesRequired, &Skip, &Flags)) {
        Err = GetLastError();
        ErrText = YoriLibGetWinErrorText(Err);
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("base64: failure to encode in CryptBinaryToString: %s"), ErrText);
        YoriLibFreeWinErrorText(ErrText);
        YoriLibFreeStringContents(&Buffer);
        YoriLibFree(BinaryBuffer);
        return FALSE;
    }

    //
    //  We're done with the string form of the source, deallocate it.
    //

    YoriLibFreeStringContents(&Buffer);

    //
    //  Output the decoded form.
    //
    BytesSent = 0;
    Result = TRUE;
    hTarget = GetStdHandle(STD_OUTPUT_HANDLE);

    while (BytesSent < BytesRequired) {
        DWORD BytesToWrite;
        DWORD BytesWritten;
        BytesToWrite = 4096;
        if (BytesSent + BytesToWrite > BytesRequired) {
            BytesToWrite = BytesRequired - BytesSent;
        }

        if (WriteFile(hTarget,
                      YoriLibAddToPointer(BinaryBuffer, BytesSent),
                      BytesToWrite,
                      &BytesWritten,
                      NULL)) {

            BytesSent += BytesWritten;
        } else {
            Err = GetLastError();
            ErrText = YoriLibGetWinErrorText(Err);
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("base64: failure to write to output: %s"), ErrText);
            YoriLibFreeWinErrorText(ErrText);
            Result = FALSE;
        }

        ASSERT(BytesSent <= BytesRequired);
    }
    YoriLibFree(BinaryBuffer);

    return Result;
}

#ifdef YORI_BUILTIN
/**
 The main entrypoint for the base64 builtin command.
 */
#define ENTRYPOINT YoriCmd_YBASE64
#else
/**
 The main entrypoint for the base64 standalone application.
 */
#define ENTRYPOINT ymain
#endif

/**
 The main entrypoint for the base64 cmdlet.

 @param ArgC The number of arguments.

 @param ArgV An array of arguments.

 @return Exit code of the process, typically zero for success and nonzero
         for failure.
 */
DWORD
ENTRYPOINT(
    __in DWORD ArgC,
    __in YORI_STRING ArgV[]
    )
{
    BOOL ArgumentUnderstood;
    DWORD i;
    DWORD StartArg = 0;
    YORI_STRING Arg;
    BOOLEAN Decode = FALSE;
    BASE64_BUFFER Base64Buffer;
    YORI_STRING FullFilePath;
    DWORD Err;
    LPTSTR ErrText;

    ZeroMemory(&Base64Buffer, sizeof(Base64Buffer));
    YoriLibInitEmptyString(&FullFilePath);

    for (i = 1; i < ArgC; i++) {

        ArgumentUnderstood = FALSE;
        ASSERT(YoriLibIsStringNullTerminated(&ArgV[i]));

        if (YoriLibIsCommandLineOption(&ArgV[i], &Arg)) {

            if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("?")) == 0) {
                Base64Help();
                return EXIT_SUCCESS;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("license")) == 0) {
                YoriLibDisplayMitLicense(_T("2023"));
                return EXIT_SUCCESS;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("d")) == 0) {
                Decode = TRUE;
                ArgumentUnderstood = TRUE;
            } else if (YoriLibCompareStringWithLiteralInsensitive(&Arg, _T("-")) == 0) {
                StartArg = i + 1;
                ArgumentUnderstood = TRUE;
                break;
            }
        } else {
            ArgumentUnderstood = TRUE;
            StartArg = i;
            break;
        }

        if (!ArgumentUnderstood) {
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("Argument not understood, ignored: %y\n"), &ArgV[i]);
        }
    }

    YoriLibLoadCrypt32Functions();
    if (DllCrypt32.pCryptBinaryToStringW == NULL ||
        DllCrypt32.pCryptStringToBinaryW == NULL) {

        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("base64: operating system support not present\n"));
        return EXIT_FAILURE;
    }

#if YORI_BUILTIN
    YoriLibCancelEnable(FALSE);
#endif

    //
    //  If no file name is specified, use stdin; otherwise open
    //  the file and use that
    //

    YoriLibInitEmptyString(&FullFilePath);
    Base64Buffer.hSource = GetStdHandle(STD_INPUT_HANDLE);
    if (StartArg == 0 || StartArg == ArgC) {
        if (YoriLibIsStdInConsole()) {
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("base64: no file or pipe for input\n"));
            return EXIT_FAILURE;
        }
    } else {
        if (!YoriLibUserStringToSingleFilePath(&ArgV[StartArg], TRUE, &FullFilePath)) {
            Err = GetLastError();
            ErrText = YoriLibGetWinErrorText(Err);
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("base64: resolving path failed: %s"), ErrText);
            YoriLibFreeWinErrorText(ErrText);
            return EXIT_FAILURE;
        }

        Base64Buffer.hSource = CreateFile(FullFilePath.StartOfString, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (Base64Buffer.hSource == INVALID_HANDLE_VALUE) {
            Err = GetLastError();
            ErrText = YoriLibGetWinErrorText(Err);
            YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("base64: opening file failed: %s"), ErrText);
            YoriLibFreeWinErrorText(ErrText);
            YoriLibFreeStringContents(&FullFilePath);
            return EXIT_FAILURE;
        }
    }

    if (!Base64AllocateBuffer(&Base64Buffer)) {
        Err = GetLastError();
        ErrText = YoriLibGetWinErrorText(Err);
        YoriLibOutput(YORI_LIB_OUTPUT_STDERR, _T("base64: allocating buffer failed: %s"), ErrText);
        YoriLibFreeWinErrorText(ErrText);
        if (FullFilePath.LengthInChars > 0) {
            CloseHandle(Base64Buffer.hSource);
        }
        YoriLibFreeStringContents(&FullFilePath);
        return EXIT_FAILURE;
    }

    if (!Base64BufferPump(&Base64Buffer)) {
        if (FullFilePath.LengthInChars > 0) {
            CloseHandle(Base64Buffer.hSource);
        }
        YoriLibFreeStringContents(&FullFilePath);
        Base64FreeBuffer(&Base64Buffer);
        return EXIT_FAILURE;
    }

    if (!Decode) {
        if (!Base64Encode(&Base64Buffer)) {
            if (FullFilePath.LengthInChars > 0) {
                CloseHandle(Base64Buffer.hSource);
            }
            YoriLibFreeStringContents(&FullFilePath);
            Base64FreeBuffer(&Base64Buffer);
            return EXIT_FAILURE;
        }
    } else {
        if (!Base64Decode(&Base64Buffer)) {
            if (FullFilePath.LengthInChars > 0) {
                CloseHandle(Base64Buffer.hSource);
            }
            YoriLibFreeStringContents(&FullFilePath);
            Base64FreeBuffer(&Base64Buffer);
            return EXIT_FAILURE;
        }
    }

    if (FullFilePath.LengthInChars > 0) {
        CloseHandle(Base64Buffer.hSource);
    }
    YoriLibFreeStringContents(&FullFilePath);
    Base64FreeBuffer(&Base64Buffer);

    return EXIT_SUCCESS;
}

// vim:sw=4:ts=4:et:
