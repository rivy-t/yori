/**
 * @file libwin/hexedit.c
 *
 * Yori window hexadecimal edit control
 *
 * Copyright (c) 2020-2023 Malcolm J. Smith
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

#include "yoripch.h"
#include "yorilib.h"
#include "yoriwin.h"
#include "winpriv.h"

/**
 Information about the selection region within a hex edit control.
 */
typedef struct _YORI_WIN_HEX_EDIT_SELECT {

    /**
     Indicates if a selection is currently active, and if so, what caused the
     activation.
     */
    enum {
        YoriWinHexEditSelectNotActive = 0,
        YoriWinHexEditSelectActive = 1
    } Active;


    /**
     The first byte of the selection range.  This byte is included in the
     selection.
     */
    DWORDLONG FirstByteOffset;

    /**
     The last byte of the selection range.  This byte is included in the
     selection.
     */
    DWORDLONG LastByteOffset;

} YORI_WIN_HEX_EDIT_SELECT, *PYORI_WIN_HEX_EDIT_SELECT;

/**
 A structure describing the contents of a hex edit control.
 */
typedef struct _YORI_WIN_CTRL_HEX_EDIT {

    /**
     A common header for all controls
     */
    YORI_WIN_CTRL Ctrl;

    /**
     Pointer to the vertical scroll bar associated with the hex edit.
     */
    PYORI_WIN_CTRL VScrollCtrl;

    /**
     Optional pointer to a callback to invoke when the cursor moves.
     */
    PYORI_WIN_NOTIFY_HEX_EDIT_CURSOR_MOVE CursorMoveCallback;

    /**
     The caption to display above the edit control.
     */
    YORI_STRING Caption;

    /**
     Pointer to the data buffer to display.
     */
    PUCHAR Buffer;

    /**
     The length of the data buffer allocation in bytes.
     */
    DWORDLONG BufferAllocated;

    /**
     The number of bytes within the data allocation that contain meaningful
     data.
     */
    DWORDLONG BufferValid;

    /**
     The number of bytes that will be displayed in a single line of the
     control.
     */
    DWORD BytesPerLine;

    /**
     Specifies the number of bytes per word.  This code will currently only
     work with 1 byte per word, but this value is here to ease the transition
     to supporting 2 byte, 4 byte and 8 byte words later.
     */
    DWORD BytesPerWord;

    /**
     The index within LineArray that is displayed at the top of the control.
     */
    DWORD ViewportTop;

    /**
     The horizontal offset within each line to display.
     */
    DWORD ViewportLeft;

    /**
     The index within LineArray that the cursor is located at.
     */
    DWORD CursorLine;

    /**
     The horizontal offset of the cursor in terms of the offset within the
     line buffer.
     */
    DWORD CursorOffset;

    /**
     The first line, in cursor coordinates, that requires redrawing.  Lines
     between this and the last line below (inclusive) will be redrawn on
     paint.  If this value is greater than the last line, no redrawing
     occurs.  This is a fairly common scenario when the cursor is moved,
     where a repaint is needed but no data changes are occurring.
     */
    DWORD FirstDirtyLine;

    /**
     The last line, in cursor coordinates, that requires redrawing.  Lines
     between the first line above and this line (inclusive) will be redrawn
     on paint.
     */
    DWORD LastDirtyLine;

    /**
     Specifies the selection state of text within the multiline edit control.
     This is encapsulated into a structure purely for readability.
     */
    YORI_WIN_HEX_EDIT_SELECT Selection;

    /**
     Records the last observed mouse location when a mouse selection is
     active.  This is repeatedly used via a timer when the mouse moves off
     the control area.  Once the mouse returns to the control area or the
     button is released (completing the selection) this value is undefined.
     */
    YORI_WIN_BOUNDED_COORD LastMousePos;

    /**
     A timer that is used to indicate the previous mouse position should be
     repeated to facilitate scroll.  This can be NULL if auto scroll is not
     in effect.
     */
    PYORI_WIN_CTRL_HANDLE Timer;

    /**
     When inputting a character by value, the current value that has been
     accumulated (since this requires multiple key events.)
     */
    DWORD NumericKeyValue;

    /**
     Indicates how to interpret the NumericKeyValue.  Ascii uses CP_OEMCP,
     Ansi uses CP_ACP, Unicode is direct.  Also note that Unicode takes
     input in hexadecimal to match the normal U+xxxx specification.
     */
    YORI_LIB_NUMERIC_KEY_TYPE NumericKeyType;

    /**
     The attributes to display text in.
     */
    WORD TextAttributes;

    /**
     The attributes to display selected text in.
     */
    WORD SelectedAttributes;

    /**
     The attributes to display the caption in.
     */
    WORD CaptionAttributes;

    /**
     Specifies the number of bits to use for the buffer offset.  Currently
     supported values are 0, 32 and 64.
     */
    UCHAR OffsetWidth;

    /**
     0 if the cursor is currently not visible.  20 for insert mode, 50 for
     overwrite mode.  Paint calculates the desired value and based on
     comparing the new value with the current value decides on the action
     to take.
     */
    UCHAR PercentCursorVisibleLastPaint;

    /**
     If TRUE, new characters are inserted at the cursor position.  If FALSE,
     new characters overwrite existing characters.
     */
    BOOLEAN InsertMode;

    /**
     If TRUE, the edit control should not support editing.  If FALSE, it is
     a regular, editable edit control.
     */
    BOOLEAN ReadOnly;

    /**
     TRUE if the control currently has focus, FALSE if another control has
     focus.
     */
    BOOLEAN HasFocus;

    /**
     TRUE if the contents of the control have been modified by user input.
     FALSE if the contents have not changed since this value was last reset.
     */
    BOOLEAN UserModified;

} YORI_WIN_CTRL_HEX_EDIT, *PYORI_WIN_CTRL_HEX_EDIT;

/**
 The hex edit should display a vertical scroll bar.
 */
#define YORI_WIN_HEX_EDIT_STYLE_VSCROLLBAR  (0x0001)

/**
 The hex edit should be read only.
 */
#define YORI_WIN_HEX_EDIT_STYLE_READ_ONLY   (0x0002)

/**
 A list of possible meanings behind each displayed cell.
 */
typedef enum _YORI_WIN_HEX_EDIT_CELL_TYPE {
    YoriWinHexEditCellTypeOffset = 0,
    YoriWinHexEditCellTypeWhitespace = 1,
    YoriWinHexEditCellTypeHexDigit = 2,
    YoriWinHexEditCellTypeCharValue = 3
} YORI_WIN_HEX_EDIT_CELL_TYPE;

/**
 Return the number of lines which this control can contain to display the
 data buffer.

 @param HexEdit Pointer to the hex edit control containing the data buffer.

 @return The number of lines that will need to be displayed.
 */
DWORD
YoriWinHexEditLinesPopulated(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    DWORD LineCount;

    //
    //  Calculate the number of lines, rounding up if any partial lines
    //  exist.
    //

    LineCount = (DWORD)((HexEdit->BufferValid + HexEdit->BytesPerLine - 1)/HexEdit->BytesPerLine);
    return LineCount;
}

/**
 Returns the number of cells used to display the offset at the beginning of
 each line.

 @param HexEdit Pointer to the hex edit control.

 @return The number of cells used to display the offset at the beginning of
         each line.
 */
DWORD
YoriWinHexEditOffsetSizeInCells(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    DWORD OffsetInChars;
    OffsetInChars = 0;
    if (HexEdit->OffsetWidth == 64) {
        OffsetInChars = sizeof("01234567`01234567:") - 1;
    } else if (HexEdit->OffsetWidth == 32) {
        OffsetInChars = sizeof("01234567:") - 1;
    }
    return OffsetInChars;
}

/**
 Return the number of display cells needed for each word in the current
 configuration.

 @param HexEdit Pointer to the hex edit control, which contains the number of
        bytes per word.

 @return The number of display cells required.
 */
DWORD
YoriWinHexEditGetCellsPerWord(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    DWORD CellsPerWord;

    CellsPerWord = HexEdit->BytesPerWord * 2 + 1;
    if (HexEdit->BytesPerWord == 8) {
        CellsPerWord = CellsPerWord + 1;
    }

    return CellsPerWord;
}

/**
 Return the offset in cell indexes for the specified bit shift.  Note that
 the offset is from the right (low bits), so a bit shift of zero returns
 zero, which is the right most cell.

 @param HexEdit Pointer to the hex edit control, which contains the number of
        bytes per word.

 @param BitShift The number of bits to shift by.

 @return The cell offset to apply for the specified bit shift value.
 */
DWORD
YoriWinHexEditGetCellIndexForBitShift(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in DWORD BitShift
    )
{
    DWORD CellIndex;

    UNREFERENCED_PARAMETER(HexEdit);

    ASSERT((BitShift % 4) == 0);
    CellIndex = BitShift / 4;
    if (BitShift >= 32) {
        ASSERT(HexEdit->BytesPerWord == 8);
        CellIndex = CellIndex + 1;
    }

    return CellIndex;
}


/**
 Obtain the meaning of a specific display cell.

 @param HexEdit Pointer to the hex edit control.

 @param LineIndex Specifies the line to obtain the meaning for.

 @param CellOffset Specifies the display offset to obtain meaning for within
        the line.

 @param ByteOffset On successful completion, updated to point to the offset
        of the value within the line.

 @param BitShift On successful completion, updated to indicate the number of
        bits to shift to indicate a nibble within a larger word.

 @param BeyondBufferEnd On successful completion, updated to indicate if the
        ByteOffset value is beyond the currently allocated buffer length.

 @return The meaning of the display cell.
 */
YORI_WIN_HEX_EDIT_CELL_TYPE
YoriWinHexEditCellType(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in DWORD LineIndex,
    __in DWORD CellOffset,
    __out_opt PDWORD ByteOffset,
    __out_opt PDWORD BitShift,
    __out_opt PBOOLEAN BeyondBufferEnd
    )
{
    DWORD LocalByteOffset;
    DWORD ModValue;
    DWORD DataOffset;
    DWORD BytesThisLine;
    DWORD LinesPopulated;
    DWORD CellsPerWord;
    DWORD OffsetInChars;
    DWORD WordsPerLine;
    DWORD LocalBitShift;

    if (ByteOffset != NULL) {
        *ByteOffset = 0;
    }
    if (BitShift != NULL) {
        *BitShift = 0;
    }
    if (BeyondBufferEnd != NULL) {
        *BeyondBufferEnd = FALSE;
    }

    LinesPopulated = YoriWinHexEditLinesPopulated(HexEdit);
    BytesThisLine = HexEdit->BytesPerLine;
    if (LineIndex + 1 == LinesPopulated) {
        DWORDLONG BytesInFullLines;

        BytesInFullLines = LineIndex;
        BytesInFullLines = BytesInFullLines * HexEdit->BytesPerLine;
        BytesThisLine = (DWORD)(HexEdit->BufferValid - BytesInFullLines);
    }

    OffsetInChars = YoriWinHexEditOffsetSizeInCells(HexEdit);
    if (OffsetInChars > 0) {
        if (CellOffset < OffsetInChars) {
            return YoriWinHexEditCellTypeOffset;
        } else if (CellOffset == OffsetInChars) {
            return YoriWinHexEditCellTypeWhitespace;
        }
    }

    CellsPerWord = YoriWinHexEditGetCellsPerWord(HexEdit);
    WordsPerLine = HexEdit->BytesPerLine / HexEdit->BytesPerWord;

    DataOffset = CellOffset - OffsetInChars;

    //
    //  If there's an offset, the first cell is a seperator.  With no offset,
    //  bias the data offset by one so the first cell is a hex digit.
    //

    if (OffsetInChars == 0) {
        DataOffset = DataOffset + 1;
    }
    if (DataOffset < WordsPerLine * CellsPerWord) {
        ModValue = DataOffset % CellsPerWord;
        LocalByteOffset = (DataOffset / CellsPerWord) * HexEdit->BytesPerWord;
        LocalBitShift = 0;
        if (ModValue == 0) {
            return YoriWinHexEditCellTypeWhitespace;
        } else {
            ModValue = CellsPerWord - 1 - ModValue;
            if (ModValue == 8) {
                ASSERT(HexEdit->BytesPerWord == 8);
                return YoriWinHexEditCellTypeWhitespace;
            } else if (ModValue > 8) {
                ModValue = ModValue - 1;
            }
            LocalBitShift = 4 * ModValue;
            if (ByteOffset != NULL) {
                *ByteOffset = LocalByteOffset;
            }
            if (BitShift != NULL) {
                *BitShift = LocalBitShift;
            }
        }
        if (BeyondBufferEnd != NULL &&
            (LineIndex >= LinesPopulated || LocalByteOffset + (LocalBitShift / 8) >= BytesThisLine)) {

            *BeyondBufferEnd = TRUE;
        }
        return YoriWinHexEditCellTypeHexDigit;
    }

    DataOffset = DataOffset - WordsPerLine * CellsPerWord;
    if (DataOffset < 2) {
        return YoriWinHexEditCellTypeWhitespace;
    }

    DataOffset = DataOffset - 2;
    if (DataOffset >= HexEdit->BytesPerLine) {
        return YoriWinHexEditCellTypeWhitespace;
    }
    if (BeyondBufferEnd != NULL &&
        (LineIndex >= LinesPopulated || DataOffset >= BytesThisLine)) {

        *BeyondBufferEnd = TRUE;
    }

    if (ByteOffset != NULL) {
        *ByteOffset = DataOffset;
    }
    return YoriWinHexEditCellTypeCharValue;
}

/**
 Determine the visual location on screen in the character area for a specified
 buffer location.

 @param HexEdit Pointer to the hex edit control.

 @param BufferOffset Indicates the byte offset within the buffer.

 @param EndLine On successful completion, updated to indicate the new cursor
        line.

 @param EndCharOffset On successful completion, updated to indicate the new
        cursor offset.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinHexEditCellFromCharBufferOffset(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in DWORDLONG BufferOffset,
    __out PDWORD EndLine,
    __out PDWORD EndCharOffset
    )
{
    DWORD CellsPerWord;
    DWORD LineByteOffset;
    DWORD OffsetInChars;
    DWORD WordsPerLine;

    OffsetInChars = YoriWinHexEditOffsetSizeInCells(HexEdit);

    //
    //  If there is an offset, there's an extra cell of margin between the
    //  offset and hex digits.  There's also an extra cell between hex and
    //  char versions of the buffer.
    //

    if (OffsetInChars > 0) {
        OffsetInChars++;
    }

    CellsPerWord = YoriWinHexEditGetCellsPerWord(HexEdit);
    WordsPerLine = HexEdit->BytesPerLine / HexEdit->BytesPerWord;

    *EndLine = (DWORD)(BufferOffset / HexEdit->BytesPerLine);
    LineByteOffset = (DWORD)(BufferOffset % HexEdit->BytesPerLine);
    *EndCharOffset = OffsetInChars + WordsPerLine * CellsPerWord + 1 + LineByteOffset;
    return TRUE;

}

/**
 Determine the visual location on screen in the hex area for a specified
 buffer location.

 @param HexEdit Pointer to the hex edit control.

 @param BufferOffset Indicates the byte offset within the buffer.

 @param BitShift Indicates the number of bits that should be shifted for the
        cursor location.

 @param EndLine On successful completion, updated to indicate the new cursor
        line.

 @param EndCharOffset On successful completion, updated to indicate the new
        cursor offset.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinHexEditCellFromHexBufferOffset(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in DWORDLONG BufferOffset,
    __in DWORD BitShift,
    __out PDWORD EndLine,
    __out PDWORD EndCharOffset
    )
{
    DWORD CellsPerWord;
    DWORD LineByteOffset;
    DWORD LineCellOffset;
    DWORD OffsetInChars;
    DWORD BitShiftCellIndex;
    DWORD MarginToRemove;

    MarginToRemove = 0;
    OffsetInChars = YoriWinHexEditOffsetSizeInCells(HexEdit);

    //
    //  The math below includes a space for each word.  If there's no offset,
    //  remove the first space.
    //

    if (OffsetInChars == 0) {
        MarginToRemove = 1;
    }

    ASSERT((BufferOffset % HexEdit->BytesPerWord) == 0);

    CellsPerWord = YoriWinHexEditGetCellsPerWord(HexEdit);

    *EndLine = (DWORD)(BufferOffset / HexEdit->BytesPerLine);
    LineByteOffset = (DWORD)(BufferOffset % HexEdit->BytesPerLine);
    LineCellOffset = (LineByteOffset + HexEdit->BytesPerWord - 1) / HexEdit->BytesPerWord;

    BitShiftCellIndex = YoriWinHexEditGetCellIndexForBitShift(HexEdit, BitShift);
    *EndCharOffset = OffsetInChars + ((LineCellOffset + 1) * CellsPerWord) - BitShiftCellIndex - MarginToRemove - 1;
    return TRUE;
}

/**
 Determine whether the cursor should be located before the current location.
 Unlike regular editors, this means keystrokes move across hex digits in hex
 mode or characters in character mode.

 @param HexEdit Pointer to the hex edit control.

 @param CellType Indicates the type of the cell that is being traversed.

 @param BufferOffset Indicates the offset within the buffer of the current
        cursor location.

 @param BitShift Indicates the number of bits that should be shifted for the
        current cursor location.

 @param EndLine On successful completion, updated to indicate the new cursor
        line.

 @param EndCharOffset On successful completion, updated to indicate the new
        cursor offset.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinHexEditPreviousCellSameType(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in YORI_WIN_HEX_EDIT_CELL_TYPE CellType,
    __in DWORDLONG BufferOffset,
    __in DWORD BitShift,
    __out PDWORD EndLine,
    __out PDWORD EndCharOffset
    )
{
    DWORD NewBitShift;
    DWORD Unaligned;
    DWORDLONG NewBufferOffset;

    if (CellType != YoriWinHexEditCellTypeHexDigit &&
        CellType != YoriWinHexEditCellTypeCharValue) {

        return FALSE;
    }

    NewBufferOffset = BufferOffset;
    NewBitShift = BitShift;

    if (CellType == YoriWinHexEditCellTypeCharValue) {
        if (BufferOffset > 0) {
            NewBufferOffset = BufferOffset - 1;
        }
        return YoriWinHexEditCellFromCharBufferOffset(HexEdit, NewBufferOffset, EndLine, EndCharOffset);
    }

    //
    //  If the caller doesn't guarantee this, this function will need to
    //  adjust BitShift to compensate
    //

    Unaligned = (DWORD)(NewBufferOffset % HexEdit->BytesPerWord);
    ASSERT(Unaligned == 0);
    if (Unaligned != 0) {
        NewBufferOffset = (DWORD)(NewBufferOffset - Unaligned);
        NewBitShift = NewBitShift + 8 * Unaligned;
    }

    if (NewBitShift < HexEdit->BytesPerWord * 8 - 4) {
        NewBitShift = NewBitShift + 4;
    } else if (NewBufferOffset > 0) {
        ASSERT(NewBufferOffset >= HexEdit->BytesPerWord);
        NewBufferOffset = NewBufferOffset - HexEdit->BytesPerWord;
        NewBitShift = 0;
    }

    return YoriWinHexEditCellFromHexBufferOffset(HexEdit, NewBufferOffset, NewBitShift, EndLine, EndCharOffset);
}

/**
 Determine whether the cursor should be located after the current location.
 Unlike regular editors, this means keystrokes move across hex digits in hex
 mode or characters in character mode.

 @param HexEdit Pointer to the hex edit control.

 @param CellType Indicates the type of the cell that is being traversed.

 @param BufferOffset Indicates the offset within the buffer of the current
        cursor location.

 @param BitShift Indicates the number of bits that should be shifted for the
        current cursor location.

 @param EndLine On successful completion, updated to indicate the new cursor
        line.

 @param EndCharOffset On successful completion, updated to indicate the new
        cursor offset.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinHexEditNextCellSameType(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in YORI_WIN_HEX_EDIT_CELL_TYPE CellType,
    __in DWORDLONG BufferOffset,
    __in DWORD BitShift,
    __out PDWORD EndLine,
    __out PDWORD EndCharOffset
    )
{
    DWORD NewBitShift;
    DWORD Unaligned;
    DWORDLONG NewBufferOffset;

    if (CellType != YoriWinHexEditCellTypeHexDigit &&
        CellType != YoriWinHexEditCellTypeCharValue) {

        return FALSE;
    }

    if (CellType == YoriWinHexEditCellTypeCharValue) {
        NewBufferOffset = BufferOffset + 1;
        return YoriWinHexEditCellFromCharBufferOffset(HexEdit, NewBufferOffset, EndLine, EndCharOffset);
    }

    NewBufferOffset = BufferOffset;
    NewBitShift = BitShift;

    //
    //  If the caller doesn't guarantee this, this function will need to
    //  adjust BitShift to compensate
    //

    Unaligned = (DWORD)(NewBufferOffset % HexEdit->BytesPerWord);
    ASSERT(Unaligned == 0);
    if (Unaligned != 0) {
        NewBufferOffset = NewBufferOffset - Unaligned;
        NewBitShift = NewBitShift + 8 * Unaligned;
    }

    if (NewBitShift >= 4) {
        NewBitShift = NewBitShift - 4;
    } else {
        NewBufferOffset = BufferOffset + HexEdit->BytesPerWord;
        NewBitShift = 8 * HexEdit->BytesPerWord - 4;
    }

    return YoriWinHexEditCellFromHexBufferOffset(HexEdit, NewBufferOffset, NewBitShift, EndLine, EndCharOffset);
}

//
//  =========================================
//  DISPLAY FUNCTIONS
//  =========================================
//

/**
 Return a color for the cell, based on whether the cell is within a selection
 range.

 @param HexEdit Pointer to the hex edit control.

 @param Offset The offset within the buffer that the cell describes.

 @param PaddingAfter If TRUE, the cell is visually after Offset but before
        Offset + 1.  This is used to extend the highlight across whitespace
        between two selected words.

 @return The color to use to display the cell.
 */
WORD
YoriWinHexEditSelectionColor(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in DWORDLONG Offset,
    __in BOOLEAN PaddingAfter
    )
{
    WORD Attributes;
    Attributes = HexEdit->TextAttributes;
    if (HexEdit->Selection.Active == YoriWinHexEditSelectNotActive) {
        return Attributes;
    }

    if (Offset >= HexEdit->Selection.FirstByteOffset &&
        Offset <= HexEdit->Selection.LastByteOffset) {

        if (PaddingAfter && Offset == HexEdit->Selection.LastByteOffset) {
            return Attributes;
        }

        Attributes = (WORD)((Attributes & 0xFF00) |
                            ((Attributes & 0xF0) >> 4) |
                            ((Attributes & 0x0F) << 4));
    }

    return Attributes;
}


/**
 Generate a line in units of one UCHAR.

 @param HexEdit Pointer to the hex edit control.

 @param Output Pointer to a buffer to populate with the result.

 @param OutputSize The length of the output buffer, in bytes.

 @param Offset The offset within the hex edit control's buffer to display
        data from.

 @param BytesToDisplay Number of bytes to display.

 @return The number of elements written to the Output buffer.
 */
DWORD
YoriWinHexEditByteLine(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __out_ecount(OutputSize) PCHAR_INFO Output,
    __in DWORD OutputSize,
    __in DWORDLONG Offset,
    __in DWORD BytesToDisplay
    )
{
    UCHAR WordToDisplay = 0;
    DWORD WordIndex;
    BOOL DisplayWord;
    DWORD ByteIndex;
    DWORD OutputIndex = 0;
    DWORD WordCount;
    UCHAR CONST * Buffer;

    ASSERT(BytesToDisplay <= HexEdit->BytesPerLine);
    if (BytesToDisplay > HexEdit->BytesPerLine) {
        return 0;
    }

    WordCount = HexEdit->BytesPerLine / sizeof(WordToDisplay);
    ASSERT(WordCount * 2 * sizeof(WordToDisplay) + 1 <= OutputSize);
    if (WordCount * 2 * sizeof(WordToDisplay) + 1 > OutputSize) {
        return 0;
    }

    Buffer = &HexEdit->Buffer[Offset];

    for (WordIndex = 0; WordIndex < WordCount; WordIndex++) {

        WordToDisplay = 0;
        DisplayWord = FALSE;

        for (ByteIndex = 0; ByteIndex < sizeof(WordToDisplay); ByteIndex++) {
            if (WordIndex * sizeof(WordToDisplay) + ByteIndex < BytesToDisplay) {
                DisplayWord = TRUE;
                WordToDisplay = (UCHAR)(WordToDisplay + (Buffer[WordIndex * sizeof(WordToDisplay) + ByteIndex] << (ByteIndex * 8)));
            }
        }

        if (DisplayWord) {
            PCHAR_INFO Subset;
            Subset = &Output[OutputIndex];
            Subset[0].Char.UnicodeChar = YoriLibHexDigitFromValue(WordToDisplay >> 4);
            Subset[0].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex, FALSE);
            Subset[1].Char.UnicodeChar = YoriLibHexDigitFromValue(WordToDisplay & 0x0f);
            Subset[1].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex, FALSE);
            Subset[2].Char.UnicodeChar = ' ';
            Subset[2].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex, TRUE);
            OutputIndex = OutputIndex + 3;
        } else {
            for (ByteIndex = 0;
                 OutputIndex < OutputSize && ByteIndex < (sizeof(WordToDisplay) * 2 + 1);
                 ByteIndex++) {

                Output[OutputIndex].Char.UnicodeChar = ' ';
                Output[OutputIndex].Attributes = HexEdit->TextAttributes;
                OutputIndex++;
            }
        }
    }

    return OutputIndex;
}

/**
 Generate a line in units of one WORD.

 @param HexEdit Pointer to the hex edit control.

 @param Output Pointer to a buffer to populate with the result.

 @param OutputSize The length of the output buffer, in bytes.

 @param Offset The offset within the hex edit control's buffer to display
        data from.

 @param BytesToDisplay Number of bytes to display.

 @return The number of elements written to the Output buffer.
 */
DWORD
YoriWinHexEditWordLine(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __out_ecount(OutputSize) PCHAR_INFO Output,
    __in DWORD OutputSize,
    __in DWORDLONG Offset,
    __in DWORD BytesToDisplay
    )
{
    WORD WordToDisplay = 0;
    DWORD WordIndex;
    BOOL DisplayWord;
    DWORD ByteIndex;
    DWORD OutputIndex = 0;
    DWORD WordCount;
    UCHAR CONST * Buffer;

    ASSERT(BytesToDisplay <= HexEdit->BytesPerLine);
    if (BytesToDisplay > HexEdit->BytesPerLine) {
        return 0;
    }

    WordCount = HexEdit->BytesPerLine / sizeof(WordToDisplay);
    if (WordCount * 2 * sizeof(WordToDisplay) + 1 > OutputSize) {
        return 0;
    }

    Buffer = &HexEdit->Buffer[Offset];

    for (WordIndex = 0; WordIndex < WordCount; WordIndex++) {

        WordToDisplay = 0;
        DisplayWord = FALSE;

        for (ByteIndex = 0; ByteIndex < sizeof(WordToDisplay); ByteIndex++) {
            if (WordIndex * sizeof(WordToDisplay) + ByteIndex < BytesToDisplay) {
                DisplayWord = TRUE;
                WordToDisplay = (WORD)(WordToDisplay + (Buffer[WordIndex * sizeof(WordToDisplay) + ByteIndex] << (ByteIndex * 8)));
            }
        }

        if (DisplayWord) {
            PCHAR_INFO Subset;
            Subset = &Output[OutputIndex];
            Subset[0].Char.UnicodeChar = YoriLibHexDigitFromValue(WordToDisplay >> 12);
            Subset[0].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 1, FALSE);
            Subset[1].Char.UnicodeChar = YoriLibHexDigitFromValue(WordToDisplay >> 8);
            Subset[1].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 1, FALSE);
            Subset[2].Char.UnicodeChar = YoriLibHexDigitFromValue(WordToDisplay >> 4);
            Subset[2].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay), FALSE);
            Subset[3].Char.UnicodeChar = YoriLibHexDigitFromValue(WordToDisplay);
            Subset[3].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay), FALSE);
            Subset[4].Char.UnicodeChar = ' ';
            Subset[4].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 1, TRUE);
            OutputIndex = OutputIndex + 5;
        } else {
            for (ByteIndex = 0;
                 OutputIndex < OutputSize && ByteIndex < (sizeof(WordToDisplay) * 2 + 1);
                 ByteIndex++) {

                Output[OutputIndex].Char.UnicodeChar = ' ';
                Output[OutputIndex].Attributes = HexEdit->TextAttributes;
                OutputIndex++;
            }
        }

    }

    return OutputIndex;
}

/**
 Generate a line in units of one DWORD.

 @param HexEdit Pointer to the hex edit control.

 @param Output Pointer to a buffer to populate with the result.

 @param OutputSize The length of the output buffer, in bytes.

 @param Offset The offset within the hex edit control's buffer to display
        data from.

 @param BytesToDisplay Number of bytes to display.

 @return The number of elements written to the Output buffer.
 */
DWORD
YoriWinHexEditDWordLine(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __out_ecount(OutputSize) PCHAR_INFO Output,
    __in DWORD OutputSize,
    __in DWORDLONG Offset,
    __in DWORD BytesToDisplay
    )
{
    DWORD WordToDisplay = 0;
    DWORD WordIndex;
    BOOL DisplayWord;
    DWORD ByteIndex;
    DWORD OutputIndex = 0;
    DWORD WordCount;
    UCHAR CONST * Buffer;

    ASSERT(BytesToDisplay <= HexEdit->BytesPerLine);
    if (BytesToDisplay > HexEdit->BytesPerLine) {
        return 0;
    }

    WordCount = HexEdit->BytesPerLine / sizeof(WordToDisplay);
    if (WordCount * 2 * sizeof(WordToDisplay) + 1 > OutputSize) {
        return 0;
    }

    Buffer = &HexEdit->Buffer[Offset];

    for (WordIndex = 0; WordIndex < WordCount; WordIndex++) {

        WordToDisplay = 0;
        DisplayWord = FALSE;

        for (ByteIndex = 0; ByteIndex < sizeof(WordToDisplay); ByteIndex++) {
            if (WordIndex * sizeof(WordToDisplay) + ByteIndex < BytesToDisplay) {
                DisplayWord = TRUE;
                WordToDisplay = WordToDisplay + ((DWORD)Buffer[WordIndex * sizeof(WordToDisplay) + ByteIndex] << (ByteIndex * 8));
            }
        }

        if (DisplayWord) {
            PCHAR_INFO Subset;
            Subset = &Output[OutputIndex];
            Subset[0].Char.UnicodeChar = YoriLibHexDigitFromValue(WordToDisplay >> 28);
            Subset[0].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 3, FALSE);
            Subset[1].Char.UnicodeChar = YoriLibHexDigitFromValue(WordToDisplay >> 24);
            Subset[1].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 3, FALSE);
            Subset[2].Char.UnicodeChar = YoriLibHexDigitFromValue(WordToDisplay >> 20);
            Subset[2].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 2, FALSE);
            Subset[3].Char.UnicodeChar = YoriLibHexDigitFromValue(WordToDisplay >> 16);
            Subset[3].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 2, FALSE);
            Subset[4].Char.UnicodeChar = YoriLibHexDigitFromValue(WordToDisplay >> 12);
            Subset[4].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 1, FALSE);
            Subset[5].Char.UnicodeChar = YoriLibHexDigitFromValue(WordToDisplay >> 8);
            Subset[5].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 1, FALSE);
            Subset[6].Char.UnicodeChar = YoriLibHexDigitFromValue(WordToDisplay >> 4);
            Subset[6].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay), FALSE);
            Subset[7].Char.UnicodeChar = YoriLibHexDigitFromValue(WordToDisplay);
            Subset[7].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay), FALSE);
            Subset[8].Char.UnicodeChar = ' ';
            Subset[8].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 3, TRUE);
            OutputIndex = OutputIndex + 9;
        } else {
            for (ByteIndex = 0;
                 OutputIndex < OutputSize && ByteIndex < (sizeof(WordToDisplay) * 2 + 1);
                 ByteIndex++) {

                Output[OutputIndex].Char.UnicodeChar = ' ';
                Output[OutputIndex].Attributes = HexEdit->TextAttributes;
                OutputIndex++;
            }
        }
    }

    return OutputIndex;
}

/**
 Generate a line in units of one DWORDLONG.

 @param HexEdit Pointer to the hex edit control.

 @param Output Pointer to a buffer to populate with the result.

 @param OutputSize The length of the output buffer, in bytes.

 @param Offset The offset within the hex edit control's buffer to display
        data from.

 @param BytesToDisplay Number of bytes to display.

 @return The number of elements written to the Output buffer.
 */
DWORD
YoriWinHexEditDWordLongLine(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __out_ecount(OutputSize) PCHAR_INFO Output,
    __in DWORD OutputSize,
    __in DWORDLONG Offset,
    __in DWORD BytesToDisplay
    )
{
    DWORDLONG WordToDisplay = 0;
    DWORD WordIndex;
    BOOL DisplayWord;
    DWORD ByteIndex;
    DWORD OutputIndex = 0;
    DWORD WordCount;
    UCHAR CONST * Buffer;

    ASSERT(BytesToDisplay <= HexEdit->BytesPerLine);
    if (BytesToDisplay > HexEdit->BytesPerLine) {
        return 0;
    }

    WordCount = HexEdit->BytesPerLine / sizeof(WordToDisplay);
    if (WordCount * 2 * sizeof(WordToDisplay) + 1 > OutputSize) {
        return 0;
    }

    Buffer = &HexEdit->Buffer[Offset];

    for (WordIndex = 0; WordIndex < WordCount; WordIndex++) {

        WordToDisplay = 0;
        DisplayWord = FALSE;

        for (ByteIndex = 0; ByteIndex < sizeof(WordToDisplay); ByteIndex++) {
            if (WordIndex * sizeof(WordToDisplay) + ByteIndex < BytesToDisplay) {
                DisplayWord = TRUE;
                WordToDisplay = WordToDisplay + ((DWORDLONG)Buffer[WordIndex * sizeof(WordToDisplay) + ByteIndex] << (ByteIndex * 8));
            }
        }

        if (DisplayWord) {
            LARGE_INTEGER DisplayValue;
            PCHAR_INFO Subset;
            DWORD ValToDisplay;
            DisplayValue.QuadPart = WordToDisplay;
            ValToDisplay = DisplayValue.HighPart;
            Subset = &Output[OutputIndex];
            Subset[0].Char.UnicodeChar = YoriLibHexDigitFromValue(ValToDisplay >> 28);
            Subset[0].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 7, FALSE);
            Subset[1].Char.UnicodeChar = YoriLibHexDigitFromValue(ValToDisplay >> 24);
            Subset[1].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 7, FALSE);
            Subset[2].Char.UnicodeChar = YoriLibHexDigitFromValue(ValToDisplay >> 20);
            Subset[2].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 6, FALSE);
            Subset[3].Char.UnicodeChar = YoriLibHexDigitFromValue(ValToDisplay >> 16);
            Subset[3].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 6, FALSE);
            Subset[4].Char.UnicodeChar = YoriLibHexDigitFromValue(ValToDisplay >> 12);
            Subset[4].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 5, FALSE);
            Subset[5].Char.UnicodeChar = YoriLibHexDigitFromValue(ValToDisplay >> 8);
            Subset[5].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 5, FALSE);
            Subset[6].Char.UnicodeChar = YoriLibHexDigitFromValue(ValToDisplay >> 4);
            Subset[6].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 4, FALSE);
            Subset[7].Char.UnicodeChar = YoriLibHexDigitFromValue(ValToDisplay);
            Subset[7].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 4, FALSE);
            Subset[8].Char.UnicodeChar = '`';
            Subset[8].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 4, TRUE);
            ValToDisplay = DisplayValue.LowPart;
            Subset[9].Char.UnicodeChar = YoriLibHexDigitFromValue(ValToDisplay >> 28);
            Subset[9].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 3, FALSE);
            Subset[10].Char.UnicodeChar = YoriLibHexDigitFromValue(ValToDisplay >> 24);
            Subset[10].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 3, FALSE);
            Subset[11].Char.UnicodeChar = YoriLibHexDigitFromValue(ValToDisplay >> 20);
            Subset[11].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 2, FALSE);
            Subset[12].Char.UnicodeChar = YoriLibHexDigitFromValue(ValToDisplay >> 16);
            Subset[12].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 2, FALSE);
            Subset[13].Char.UnicodeChar = YoriLibHexDigitFromValue(ValToDisplay >> 12);
            Subset[13].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 1, FALSE);
            Subset[14].Char.UnicodeChar = YoriLibHexDigitFromValue(ValToDisplay >> 8);
            Subset[14].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 1, FALSE);
            Subset[15].Char.UnicodeChar = YoriLibHexDigitFromValue(ValToDisplay >> 4);
            Subset[15].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay), FALSE);
            Subset[16].Char.UnicodeChar = YoriLibHexDigitFromValue(ValToDisplay);
            Subset[16].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay), FALSE);
            Subset[17].Char.UnicodeChar = ' ';
            Subset[17].Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset + WordIndex * sizeof(WordToDisplay) + 7, TRUE);
            OutputIndex = OutputIndex + 18;
        } else {
            for (ByteIndex = 0;
                 OutputIndex < OutputSize && ByteIndex < (sizeof(WordToDisplay) * 2 + 1);
                 ByteIndex++) {

                Output[OutputIndex].Char.UnicodeChar = ' ';
                Output[OutputIndex].Attributes = HexEdit->TextAttributes;
                OutputIndex++;
            }
        }
    }

    return OutputIndex;
}

/**
 Given a cursor offset expressed in terms of the display location of the
 cursor, find the offset within the string buffer.  These are typically the
 same but tab expansion means they are not guaranteed to be identical.

 @param HexEdit Pointer to the hex edit control.

 @param LineIndex Specifies the line to evaluate against.

 @param DisplayChar Specifies the location in terms of the number of cells
        from the left of the line.

 @param CursorChar On completion, populated with the offset in the line buffer
        corresponding to the display offset.
 */
VOID
YoriWinHexEditFindCursorCharFromDisplayChar(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in DWORD LineIndex,
    __in DWORD DisplayChar,
    __out PDWORD CursorChar
    )
{
    UNREFERENCED_PARAMETER(HexEdit);
    UNREFERENCED_PARAMETER(LineIndex);
    *CursorChar = DisplayChar;
}

/**
 Given a cursor offset expressed in terms of the buffer offset of the cursor,
 find the offset within the display.  These are typically the same but tab
 expansion means they are not guaranteed to be identical.

 @param HexEdit Pointer to the hex edit control.

 @param LineIndex Specifies the line to evaluate against.

 @param CursorChar Specifies the location in terms of the line buffer offset.

 @param DisplayChar On completion, populated with the offset from the left of
        the display.
 */
VOID
YoriWinHexEditFindDisplayCharFromCursorChar(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in DWORD LineIndex,
    __in DWORD CursorChar,
    __out PDWORD DisplayChar
    )
{
    UNREFERENCED_PARAMETER(HexEdit);
    UNREFERENCED_PARAMETER(LineIndex);
    *DisplayChar = CursorChar;
}

/**
 Translate coordinates relative to the control's client area into
 cursor coordinates, being offsets to the line and character within the
 buffers being edited.

 @param HexEdit Pointer to the hex edit control.

 @param ViewportLeftOffset Offset from the left of the client area.

 @param ViewportTopOffset Offset from the top of the client area.

 @param LineIndex Populated with the cursor index to the line.

 @param CursorChar Populated with the offset within the line of the cursor.
 */
VOID
YoriWinHexEditTranslateViewportCoordinatesToCursorCoordinates(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in DWORD ViewportLeftOffset,
    __in DWORD ViewportTopOffset,
    __out PDWORD LineIndex,
    __out PDWORD CursorChar
    )
{
    DWORD LineOffset;
    DWORD DisplayOffset;

    LineOffset = ViewportTopOffset + HexEdit->ViewportTop;

    DisplayOffset = ViewportLeftOffset + HexEdit->ViewportLeft;

    YoriWinHexEditFindCursorCharFromDisplayChar(HexEdit, LineOffset, DisplayOffset, CursorChar);
    *LineIndex = LineOffset;
}

/**
 Draw the scroll bar with current information about the location and contents
 of the viewport.

 @param HexEdit Pointer to the hex edit to draw.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinHexEditRepaintScrollBar(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    if (HexEdit->VScrollCtrl) {
        DWORD MaximumTopValue;
        DWORD LinesPopulated;
        COORD ClientSize;

        YoriWinGetControlClientSize(&HexEdit->Ctrl, &ClientSize);

        LinesPopulated = YoriWinHexEditLinesPopulated(HexEdit);

        if (LinesPopulated > (DWORD)ClientSize.Y) {
            MaximumTopValue = LinesPopulated - ClientSize.Y;
        } else {
            MaximumTopValue = 0;
        }

        YoriWinScrollBarSetPosition(HexEdit->VScrollCtrl, HexEdit->ViewportTop, ClientSize.Y, MaximumTopValue);
    }

    return TRUE;
}

/**
 Draw the border, caption and scroll bars on the control.

 @param HexEdit Pointer to the hex edit to draw.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinHexEditPaintNonClient(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    SMALL_RECT BorderLocation;
    WORD BorderFlags;
    WORD WindowAttributes;
    WORD ColumnIndex;

    BorderLocation.Left = 0;
    BorderLocation.Top = 0;
    BorderLocation.Right = (SHORT)(HexEdit->Ctrl.FullRect.Right - HexEdit->Ctrl.FullRect.Left);
    BorderLocation.Bottom = (SHORT)(HexEdit->Ctrl.FullRect.Bottom - HexEdit->Ctrl.FullRect.Top);

    BorderFlags = YORI_WIN_BORDER_TYPE_SUNKEN | YORI_WIN_BORDER_TYPE_SINGLE;

    WindowAttributes = HexEdit->TextAttributes;
    YoriWinDrawBorderOnControl(&HexEdit->Ctrl, &BorderLocation, WindowAttributes, BorderFlags);

    if (HexEdit->Caption.LengthInChars > 0) {
        DWORD CaptionCharsToDisplay;
        DWORD StartOffset;
        COORD ClientSize;

        YoriWinGetControlClientSize(&HexEdit->Ctrl, &ClientSize);

        CaptionCharsToDisplay = HexEdit->Caption.LengthInChars;
        if (CaptionCharsToDisplay > (WORD)ClientSize.X) {
            CaptionCharsToDisplay = ClientSize.X;
        }

        StartOffset = (ClientSize.X - CaptionCharsToDisplay) / 2;
        for (ColumnIndex = 0; ColumnIndex < CaptionCharsToDisplay; ColumnIndex++) {
            YoriWinSetControlNonClientCell(&HexEdit->Ctrl, (WORD)(ColumnIndex + StartOffset), 0, HexEdit->Caption.StartOfString[ColumnIndex], HexEdit->CaptionAttributes);
        }
    }

    //
    //  Repaint the scroll bar after the border is drawn
    //

    YoriWinHexEditRepaintScrollBar(HexEdit);
    return TRUE;
}

/**
 Draw a single line of text within the client area of a hex edit
 control.

 @param HexEdit Pointer to the hex edit control.

 @param ClientSize Pointer to the dimensions of the client area of the
        control.

 @param LineIndex Specifies the index of the line to draw, in cursor
        coordinates.
 */
VOID
YoriWinHexEditPaintSingleLine(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in PCOORD ClientSize,
    __in DWORD LineIndex
    )
{
    WORD ColumnIndex;
    WORD WindowAttributes;
    WORD TextAttributes;
    WORD RowIndex;
    YORI_STRING String;
    DWORD LinesPopulated;
    TCHAR StringBuffer[sizeof("01234567`89abcdef: ")];
    CHAR_INFO CharInfoBuffer[YORI_LIB_HEXDUMP_BYTES_PER_LINE * 4 + 32];
    DWORD CharInfoBufferAllocated;
    DWORD CharInfoBufferPopulated;
    ULARGE_INTEGER Offset;
    DWORD LineLength;
    PUCHAR SourceBuffer;
    DWORD WordIndex;
    UCHAR CharToDisplay;
    PCHAR_INFO Cell;

    ColumnIndex = 0;
    RowIndex = (WORD)(LineIndex - HexEdit->ViewportTop);
    WindowAttributes = HexEdit->TextAttributes;
    TextAttributes = WindowAttributes;

    LinesPopulated = YoriWinHexEditLinesPopulated(HexEdit);

    YoriLibInitEmptyString(&String);
    String.StartOfString = StringBuffer;
    String.LengthAllocated = sizeof(StringBuffer)/sizeof(StringBuffer[0]);
    CharInfoBufferAllocated = sizeof(CharInfoBuffer)/sizeof(CharInfoBuffer[0]);
    CharInfoBufferPopulated = 0;

    if (LineIndex == 0 || LineIndex < LinesPopulated) {

        Offset.HighPart = 0;
        Offset.LowPart = LineIndex;
        Offset.QuadPart = Offset.QuadPart * HexEdit->BytesPerLine;

        ASSERT(Offset.QuadPart <= HexEdit->BufferValid);

        SourceBuffer = &HexEdit->Buffer[Offset.QuadPart];
        if (HexEdit->BufferValid - Offset.QuadPart < HexEdit->BytesPerLine) {
            LineLength = (DWORD)(HexEdit->BufferValid - Offset.QuadPart);
        } else {
            LineLength = HexEdit->BytesPerLine;
        }

        String.LengthInChars = 0;

        //
        //  If the caller requested to display the buffer offset for each
        //  line, display it
        //

        if (HexEdit->OffsetWidth == 64) {
            String.LengthInChars = YoriLibSPrintfS(String.StartOfString, String.LengthAllocated, _T("%08x`%08x: "), Offset.HighPart, Offset.LowPart);
        } else if (HexEdit->OffsetWidth == 32) {
            String.LengthInChars = YoriLibSPrintfS(String.StartOfString, String.LengthAllocated, _T("%08x: "), Offset.LowPart);
        }

        for (ColumnIndex = 0; ColumnIndex < String.LengthInChars; ColumnIndex++) {
            CharInfoBuffer[ColumnIndex].Char.UnicodeChar = String.StartOfString[ColumnIndex];
            CharInfoBuffer[ColumnIndex].Attributes = TextAttributes;

        }
        CharInfoBufferPopulated += String.LengthInChars;

        //
        //  Depending on the requested display format, generate the data.
        //

        if (HexEdit->BytesPerWord == 1) {
            CharInfoBufferPopulated += YoriWinHexEditByteLine(HexEdit, &CharInfoBuffer[CharInfoBufferPopulated], CharInfoBufferAllocated - CharInfoBufferPopulated, Offset.QuadPart, LineLength);
        } else if (HexEdit->BytesPerWord == 2) {
            CharInfoBufferPopulated += YoriWinHexEditWordLine(HexEdit, &CharInfoBuffer[CharInfoBufferPopulated], CharInfoBufferAllocated - CharInfoBufferPopulated, Offset.QuadPart, LineLength);
        } else if (HexEdit->BytesPerWord == 4) {
            CharInfoBufferPopulated += YoriWinHexEditDWordLine(HexEdit, &CharInfoBuffer[CharInfoBufferPopulated], CharInfoBufferAllocated - CharInfoBufferPopulated, Offset.QuadPart, LineLength);
        } else if (HexEdit->BytesPerWord == 8) {
            CharInfoBufferPopulated += YoriWinHexEditDWordLongLine(HexEdit, &CharInfoBuffer[CharInfoBufferPopulated], CharInfoBufferAllocated - CharInfoBufferPopulated, Offset.QuadPart, LineLength);
        }

        //
        //  Generate character output.
        //

        if (CharInfoBufferPopulated < CharInfoBufferAllocated) {
            Cell = &CharInfoBuffer[CharInfoBufferPopulated];
            Cell->Char.UnicodeChar = ' ';
            Cell->Attributes = TextAttributes;
            CharInfoBufferPopulated++;

            for (WordIndex = 0;
                 WordIndex < HexEdit->BytesPerLine && CharInfoBufferPopulated < CharInfoBufferAllocated;
                 WordIndex++, CharInfoBufferPopulated++) {
                if (WordIndex < LineLength) {
                    CharToDisplay = SourceBuffer[WordIndex];
                    if (!YoriLibIsCharPrintable(CharToDisplay)) {
                        CharToDisplay = '.';
                    }
                } else {
                    CharToDisplay = ' ';
                }

                ASSERT(CharToDisplay != '\0');
                Cell = &CharInfoBuffer[CharInfoBufferPopulated];
                Cell->Char.UnicodeChar = CharToDisplay;
                Cell->Attributes = YoriWinHexEditSelectionColor(HexEdit, Offset.QuadPart + WordIndex, FALSE);
            }
        }

        TextAttributes = WindowAttributes;
        for (ColumnIndex = 0; ColumnIndex < ClientSize->X && ColumnIndex + HexEdit->ViewportLeft < CharInfoBufferPopulated; ColumnIndex++) {

            Cell = &CharInfoBuffer[ColumnIndex + HexEdit->ViewportLeft];
            YoriWinSetControlClientCell(&HexEdit->Ctrl,
                                        ColumnIndex,
                                        RowIndex,
                                        Cell->Char.UnicodeChar,
                                        Cell->Attributes);
        }
    }
    for (; ColumnIndex < ClientSize->X; ColumnIndex++) {
        YoriWinSetControlClientCell(&HexEdit->Ctrl, ColumnIndex, RowIndex, ' ', WindowAttributes);
    }

    YoriLibFreeStringContents(&String);
}

/**
 Draw the edit with its current state applied.

 @param HexEdit Pointer to the hex edit to draw.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinHexEditPaint(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    WORD RowIndex;
    DWORD LineIndex;
    COORD ClientSize;

    YoriWinGetControlClientSize(&HexEdit->Ctrl, &ClientSize);

    if (HexEdit->FirstDirtyLine <= HexEdit->LastDirtyLine) {

        for (RowIndex = 0; RowIndex < ClientSize.Y; RowIndex++) {
            LineIndex = HexEdit->ViewportTop + RowIndex;

            //
            //  If the line in the viewport actually has a line in the buffer.
            //  Lines after the end of the buffer still need to be rendered in
            //  the viewport, even if it's trivial.
            //

            if (LineIndex >= HexEdit->FirstDirtyLine &&
                LineIndex <= HexEdit->LastDirtyLine) {

                YoriWinHexEditPaintSingleLine(HexEdit, &ClientSize, LineIndex);
            }
        }

        HexEdit->FirstDirtyLine = (DWORD)-1;
        HexEdit->LastDirtyLine = 0;
    }

    {
        WORD CursorLineWithinDisplay = 0;
        WORD CursorColumnWithinDisplay = 0;
        UCHAR NewPercentCursorVisible = 0;

        //
        //  If the control has focus, check based on insert state which
        //  type of cursor to display.
        //

        if (HexEdit->HasFocus) {
            if (HexEdit->InsertMode) {
                NewPercentCursorVisible = 20;
            } else {
                NewPercentCursorVisible = 50;
            }
        }

        //
        //  If the cursor is off the display, make it invisible.  If not,
        //  find the offset relative to the display.
        //

        if (HexEdit->CursorLine < HexEdit->ViewportTop ||
            HexEdit->CursorLine >= HexEdit->ViewportTop + ClientSize.Y) {

            NewPercentCursorVisible = 0;
        } else {
            CursorLineWithinDisplay = (WORD)(HexEdit->CursorLine - HexEdit->ViewportTop);
        }

        if (HexEdit->CursorOffset < HexEdit->ViewportLeft ||
            HexEdit->CursorOffset >= HexEdit->ViewportLeft + ClientSize.X) {

            NewPercentCursorVisible = 0;
        } else {
            CursorColumnWithinDisplay = (WORD)(HexEdit->CursorOffset - HexEdit->ViewportLeft);
        }

        //
        //  If the cursor is now invisible and previously wasn't, hide the
        //  cursor.  If it should be visible and previously was some other
        //  state, make it visible in the correct percentage.  If it should
        //  be visible now, position it regardless of state.  Note that the
        //  Windows API expects a nonzero percentage even when hiding the
        //  cursor, so we give it a fairly meaningless value.
        //

        if (NewPercentCursorVisible == 0)  {
            if (HexEdit->PercentCursorVisibleLastPaint != 0) {
                YoriWinSetControlCursorState(&HexEdit->Ctrl, FALSE, 25);
            }
        } else {
            if (HexEdit->PercentCursorVisibleLastPaint != NewPercentCursorVisible) {
                YoriWinSetControlCursorState(&HexEdit->Ctrl, TRUE, NewPercentCursorVisible);
            }

            YoriWinSetControlClientCursorLocation(&HexEdit->Ctrl, CursorColumnWithinDisplay, CursorLineWithinDisplay);
        }

        HexEdit->PercentCursorVisibleLastPaint = NewPercentCursorVisible;
    }

    return TRUE;
}

/**
 Set the range of the hex edit control that requires redrawing.  This
 range can only be shrunk by actual drawing, so use any new lines to extend
 but not contract the range.

 @param HexEdit Pointer to the hex edit control.

 @param NewFirstDirtyLine Specifies the first line that needs to be redrawn.

 @param NewLastDirtyLine Specifies the last line that needs to be redrawn.
 */
VOID
YoriWinHexEditExpandDirtyRange(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in DWORD NewFirstDirtyLine,
    __in DWORD NewLastDirtyLine
    )
{
    if (NewFirstDirtyLine < HexEdit->FirstDirtyLine) {
        HexEdit->FirstDirtyLine = NewFirstDirtyLine;
    }

    if (NewLastDirtyLine > HexEdit->LastDirtyLine) {
        HexEdit->LastDirtyLine = NewLastDirtyLine;
    }
}

/**
 Modify the cursor location within the hex edit control.

 @param HexEdit Pointer to the hex edit control.

 @param NewCursorOffset The offset of the cursor from the beginning of the
        line, in buffer coordinates.

 @param NewCursorLine The buffer line that the cursor is located on.
 */
VOID
YoriWinHexEditSetCursorLocationInternal(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in DWORD NewCursorOffset,
    __in DWORD NewCursorLine
    )
{
    YORI_WIN_HEX_EDIT_CELL_TYPE CellType;
    DWORD ByteOffset;
    DWORD BitShift;
    BOOLEAN BeyondBufferEnd;
    DWORDLONG BufferOffset;

    if (NewCursorOffset == HexEdit->CursorOffset &&
        NewCursorLine == HexEdit->CursorLine) {

        return;
    }

    ASSERT(NewCursorLine <= YoriWinHexEditLinesPopulated(HexEdit));

    if (HexEdit->CursorMoveCallback != NULL) {

        CellType = YoriWinHexEditCellType(HexEdit, NewCursorLine, NewCursorOffset, &ByteOffset, &BitShift, &BeyondBufferEnd);
        ASSERT (CellType == YoriWinHexEditCellTypeHexDigit || CellType == YoriWinHexEditCellTypeCharValue);
        if (CellType == YoriWinHexEditCellTypeHexDigit ||
            CellType == YoriWinHexEditCellTypeCharValue) {

            BufferOffset = NewCursorLine;
            BufferOffset = BufferOffset * HexEdit->BytesPerLine + ByteOffset;

            HexEdit->CursorMoveCallback(&HexEdit->Ctrl, BufferOffset, BitShift);
        }
    }

    HexEdit->CursorOffset = NewCursorOffset;
    HexEdit->CursorLine = NewCursorLine;
}

/**
 Adjust the first character to display in the control to ensure the current
 user cursor is visible somewhere within the control.

 @param HexEdit Pointer to the hex edit control.
 */
VOID
YoriWinHexEditEnsureCursorVisible(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    COORD ClientSize;
    DWORD NewViewportLeft;
    DWORD NewViewportTop;

    NewViewportLeft = HexEdit->ViewportLeft;
    NewViewportTop = HexEdit->ViewportTop;

    YoriWinGetControlClientSize(&HexEdit->Ctrl, &ClientSize);

    if (HexEdit->CursorOffset < NewViewportLeft) {
        NewViewportLeft = HexEdit->CursorOffset;
    } else if (HexEdit->CursorOffset >= NewViewportLeft + ClientSize.X) {
        NewViewportLeft = HexEdit->CursorOffset - ClientSize.X + 1;
    }

    if (HexEdit->CursorLine < NewViewportTop) {
        NewViewportTop = HexEdit->CursorLine;
    } else if (HexEdit->CursorLine >= NewViewportTop + ClientSize.Y) {
        NewViewportTop = HexEdit->CursorLine - ClientSize.Y + 1;
    }

    if (NewViewportTop != HexEdit->ViewportTop) {
        HexEdit->ViewportTop = NewViewportTop;
        YoriWinHexEditExpandDirtyRange(HexEdit, NewViewportTop, (DWORD)-1);
        YoriWinHexEditRepaintScrollBar(HexEdit);
    }

    if (NewViewportLeft != HexEdit->ViewportLeft) {
        HexEdit->ViewportLeft = NewViewportLeft;
        YoriWinHexEditExpandDirtyRange(HexEdit, NewViewportTop, (DWORD)-1);
    }
}

/**
 Set the cursor to a specific point, expressed in terms of a buffer offset
 and bit shift.  Bit shift is only meaningful when the cell type refers to
 hex digit, so a cursor has multiple positions per buffer offset.

 @param HexEdit Pointer to the hex edit control.

 @param CellType Specifies the type of cell that the cursor should reside
        on.

 @param BufferOffset Specifies the offset within the buffer.

 @param BitShift Specifies the bits within the buffer offset.

 @return TRUE to indicate the cursor was moved, FALSE if it did not.
 */
__success(return)
BOOLEAN
YoriWinHexEditSetCursorToBufferLocation(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in YORI_WIN_HEX_EDIT_CELL_TYPE CellType,
    __in DWORDLONG BufferOffset,
    __in DWORD BitShift
    )
{
    DWORD NewCursorLine;
    DWORD NewCursorOffset;

    ASSERT (CellType == YoriWinHexEditCellTypeHexDigit || CellType == YoriWinHexEditCellTypeCharValue);

    NewCursorLine = 0;
    NewCursorOffset = 0;
    if (CellType == YoriWinHexEditCellTypeHexDigit) {
        YoriWinHexEditCellFromHexBufferOffset(HexEdit, BufferOffset, BitShift, &NewCursorLine, &NewCursorOffset);
    } else {
        YoriWinHexEditCellFromCharBufferOffset(HexEdit, BufferOffset, &NewCursorLine, &NewCursorOffset);
    }

    if (NewCursorLine != HexEdit->CursorLine || NewCursorOffset != HexEdit->CursorOffset) {
        YoriWinHexEditSetCursorLocationInternal(HexEdit, NewCursorOffset, NewCursorLine);
        YoriWinHexEditEnsureCursorVisible(HexEdit);
        YoriWinHexEditPaint(HexEdit);
        return TRUE;
    }
    return FALSE;
}

/**
 Set the cursor location to the beginning of the buffer.

 @param HexEdit Pointer to the hex edit control.

 @return TRUE if the cursor was moved, FALSE if it was not.
 */
BOOLEAN
YoriWinHexEditSetCursorLocationToZero(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    return YoriWinHexEditSetCursorToBufferLocation(HexEdit,
                                                   YoriWinHexEditCellTypeHexDigit,
                                                   0,
                                                   HexEdit->BytesPerWord * 8 - 4);
}

/**
 Toggle the insert state of the control.  If new keystrokes would previously
 insert new characters, future characters overwrite existing characters, and
 vice versa.  The cursor shape will be updated to reflect the new state.

 @param HexEdit Pointer to the hex edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinHexEditToggleInsert(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    if (HexEdit->InsertMode) {
        HexEdit->InsertMode = FALSE;
    } else {
        HexEdit->InsertMode = TRUE;
    }
    return TRUE;
}

//
//  =========================================
//  BUFFER MANIPULATION FUNCTIONS
//  =========================================
//

/**
 Convert a UTF16 input character into a byte to write into the buffer.  This
 might end up with more sophisticated encoding conversion one day.

 @param Char Specifies the input character to convert.

 @return The byte to populate into the object being edited.
 */
UCHAR
YoriWinHexEditInputCharToByte(
    __in TCHAR Char
    )
{
    return (UCHAR)Char;
}

/**
 Delete a single cell.

 @param HexEdit Pointer to the hex edit control containing the
        contents of the buffer.

 @param FirstLine Specifies the line that contains the first character to
        remove.

 @param FirstCharOffset Specifies the offset within FirstLine of the first
        character to remove.

 @param LastLine Specifies the line that contains the last character to
        remove.

 @param LastCharOffset Specifies the offset beyond the last character to
        remove.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinHexEditDeleteCell(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in DWORD FirstLine,
    __in DWORD FirstCharOffset,
    __out PDWORD LastLine,
    __out PDWORD LastCharOffset
    )
{
    YORI_WIN_HEX_EDIT_CELL_TYPE CellType;
    DWORD ByteOffset;
    DWORDLONG BufferOffset;
    DWORD BitShift;
    DWORD CurrentLine;
    DWORD CurrentCharOffset;
    DWORD DirtyLastLine;
    UCHAR BitMask;
    UCHAR InputChar;
    PUCHAR Cell;
    DWORDLONG BytesToCopy;
    BOOLEAN BeyondBufferEnd;

    CurrentLine = FirstLine;
    CurrentCharOffset = FirstCharOffset;
    DirtyLastLine = FirstLine;

    CellType = YoriWinHexEditCellType(HexEdit, CurrentLine, CurrentCharOffset, &ByteOffset, &BitShift, &BeyondBufferEnd);

    if (BeyondBufferEnd) {
        *LastLine = CurrentLine;
        *LastCharOffset = CurrentCharOffset;
        return TRUE;
    }

    BufferOffset = CurrentLine;
    BufferOffset = BufferOffset * HexEdit->BytesPerLine + ByteOffset;
    ASSERT(BufferOffset < HexEdit->BufferValid);

    //
    //  MSFIX: This wants to be the whole word.  The shifts in HexDigit
    //  are assuming operation on nibbles in a word
    //

    Cell = YoriLibAddToPointer(HexEdit->Buffer, BufferOffset);

    switch(CellType) {
        case YoriWinHexEditCellTypeOffset:
            break;
        case YoriWinHexEditCellTypeWhitespace:
            break;
        case YoriWinHexEditCellTypeHexDigit:
            if (BitShift == 0) {
                if (BufferOffset < HexEdit->BufferValid) {
                    BytesToCopy = HexEdit->BufferValid - BufferOffset;
                    if (BytesToCopy > HexEdit->BytesPerWord) {
                        BytesToCopy = BytesToCopy - HexEdit->BytesPerWord;
                        memmove(Cell,
                                &Cell[HexEdit->BytesPerWord],
                                (DWORD)BytesToCopy);

                        HexEdit->BufferValid = HexEdit->BufferValid - HexEdit->BytesPerWord;
                    } else {
                        HexEdit->BufferValid = HexEdit->BufferValid - BytesToCopy;
                    }
                }

                //
                //  Move to the highest offset in the existing word
                //

                BitShift = HexEdit->BytesPerWord * 8 - 4;
                YoriWinHexEditCellFromHexBufferOffset(HexEdit, BufferOffset, BitShift, &CurrentLine, &CurrentCharOffset);
                DirtyLastLine = (DWORD)-1;
            } else {
                BitMask = (UCHAR)(0xF << BitShift);

                InputChar = *Cell;
                InputChar = (UCHAR)(InputChar & ~(BitMask));
                *Cell = InputChar;

                YoriWinHexEditNextCellSameType(HexEdit, CellType, BufferOffset, BitShift, &CurrentLine, &CurrentCharOffset);
            }
            HexEdit->UserModified = TRUE;
            break;
        case YoriWinHexEditCellTypeCharValue:
            if (BufferOffset < HexEdit->BufferValid) {
                BytesToCopy = HexEdit->BufferValid - BufferOffset;
                if (BytesToCopy > 1) {
                    BytesToCopy = BytesToCopy - 1;
                    memmove(Cell,
                            &Cell[1],
                            (DWORD)BytesToCopy);
                }
                HexEdit->BufferValid = HexEdit->BufferValid - 1;
                DirtyLastLine = (DWORD)-1;
                HexEdit->UserModified = TRUE;
            }
            break;
    }

    YoriWinHexEditExpandDirtyRange(HexEdit, FirstLine, DirtyLastLine);
    *LastLine = CurrentLine;
    *LastCharOffset = CurrentCharOffset;

    return TRUE;
}

/**
 Ensure the buffer has enough space for a specified buffer size.  This may
 reallocate the buffer if required.

 @param HexEdit Pointer to the hex edit control.

 @param NewBufferLength The new number of bytes required in the control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinHexEditEnsureBufferLength(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in DWORDLONG NewBufferLength
    )
{
    DWORDLONG PaddedBufferLength;
    PUCHAR NewBuffer;

    if (HexEdit->BufferAllocated >= NewBufferLength) {
        return TRUE;
    }

    if (NewBufferLength < HexEdit->BufferValid) {
        ASSERT(NewBufferLength >= HexEdit->BufferValid);
        return FALSE;
    }

    //
    //  If the buffer wasn't large enough, assume this won't be the only
    //  insert operation, so grow the buffer by a chunk.
    //

    PaddedBufferLength = NewBufferLength + 16384;

    if (PaddedBufferLength >= (DWORD)-1) {
        return FALSE;
    }

    NewBuffer = YoriLibReferencedMalloc((DWORD)PaddedBufferLength);
    if (NewBuffer == NULL) {
        return FALSE;
    }

    if (HexEdit->BufferValid > 0) {
        memcpy(NewBuffer, HexEdit->Buffer, (DWORD)HexEdit->BufferValid);
    }

    if (HexEdit->Buffer != NULL) {
        YoriLibDereference(HexEdit->Buffer);
    }
    HexEdit->Buffer = NewBuffer;
    HexEdit->BufferAllocated = PaddedBufferLength;

    return TRUE;
}

/**
 Ensure the buffer is valid up to a specified size.  This may reallocate the
 buffer if required, and will zero any new bytes and mark them valid.

 @param HexEdit Pointer to the hex edit control.

 @param NewBufferLength The new number of bytes required in the control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinHexEditEnsureBufferValid(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in DWORDLONG NewBufferLength
    )
{
    ASSERT(NewBufferLength > HexEdit->BufferValid);
    if (NewBufferLength <= HexEdit->BufferValid) {
        return TRUE;
    }
    if (!YoriWinHexEditEnsureBufferLength(HexEdit, NewBufferLength)) {
        return FALSE;
    }
    ZeroMemory(YoriLibAddToPointer(HexEdit->Buffer, HexEdit->BufferValid), (DWORD)(NewBufferLength - HexEdit->BufferValid));
    HexEdit->BufferValid = NewBufferLength;
    return TRUE;
}

/**
 Move the data to add space for newly inserted bytes.  This may reallocate
 the buffer.

 @param HexEdit Pointer to the hex edit control.

 @param BufferOffset The offset within the buffer to insert bytes.

 @param BytesToInsert The number of bytes to insert.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinHexEditInsertSpaceInBuffer(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in DWORDLONG BufferOffset,
    __in DWORD BytesToInsert
    )
{
    DWORDLONG BytesToMove;

    ASSERT(BufferOffset <= HexEdit->BufferValid);
    if (BufferOffset > HexEdit->BufferValid) {
        return FALSE;
    }

    if (!YoriWinHexEditEnsureBufferLength(HexEdit, HexEdit->BufferValid + BytesToInsert)) {
        return FALSE;
    }

    BytesToMove = HexEdit->BufferValid - BufferOffset;
    if (BytesToMove > (DWORD)-1) {
        return FALSE;
    }
    if (BytesToMove > 0) {
        memmove(&HexEdit->Buffer[BufferOffset + BytesToInsert], &HexEdit->Buffer[BufferOffset], (DWORD)BytesToMove);
    }

    ZeroMemory(&HexEdit->Buffer[BufferOffset], BytesToInsert);
    HexEdit->BufferValid = HexEdit->BufferValid + BytesToInsert;
    ASSERT(HexEdit->BufferValid <= HexEdit->BufferAllocated);

    return TRUE;
}

/**
 Insert a block of text, which may contain newlines, into the control at the
 specified position.  Currently, this happens in three scenarios: user input,
 clipboard paste, or undo.

 @param HexEdit Pointer to the hex edit control.

 @param FirstLine Specifies the line in the buffer where text should be
        inserted.

 @param FirstCharOffset Specifies the offset in the line where text should be
        inserted.

 @param Char Specifies the character to insert.

 @param LastLine On successful completion, populated with the line containing
        the end of the newly inserted text.

 @param LastCharOffset On successful completion, populated with the offset
        beyond the newly inserted text.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinHexEditInsertCell(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in DWORD FirstLine,
    __in DWORD FirstCharOffset,
    __in TCHAR Char,
    __out PDWORD LastLine,
    __out PDWORD LastCharOffset
    )
{
    YORI_WIN_HEX_EDIT_CELL_TYPE CellType;
    DWORD ByteOffset;
    DWORDLONG BufferOffset;
    DWORD BitShift;
    DWORD CurrentLine;
    DWORD CurrentCharOffset;
    DWORD DirtyLastLine;
    UCHAR BitMask;
    UCHAR NewNibble;
    UCHAR InputChar;
    PUCHAR Cell;
    BOOLEAN CellUpdated;
    BOOLEAN BeyondBufferEnd;
    DWORDLONG EditBufferOffset;
    DWORD EditBitShift;

    CurrentLine = FirstLine;
    CurrentCharOffset = FirstCharOffset;
    DirtyLastLine = FirstLine;

    CellType = YoriWinHexEditCellType(HexEdit, CurrentLine, CurrentCharOffset, &ByteOffset, &BitShift, &BeyondBufferEnd);
    BufferOffset = CurrentLine;
    BufferOffset = BufferOffset * HexEdit->BytesPerLine + ByteOffset;
    if (BeyondBufferEnd) {
        if (BufferOffset > HexEdit->BufferValid &&
            !YoriWinHexEditEnsureBufferValid(HexEdit, BufferOffset)) {

            *LastLine = CurrentLine;
            *LastCharOffset = CurrentCharOffset;
            return TRUE;
        }
        DirtyLastLine = (DWORD)-1;
    }

    //
    //  Convert everything into bytes as opposed to words
    //

    EditBufferOffset = BufferOffset;
    EditBitShift = BitShift;
    if (EditBitShift >= 8) {
        EditBufferOffset = EditBufferOffset + EditBitShift / 8;
        EditBitShift = EditBitShift % 8;
    }

    Cell = YoriLibAddToPointer(HexEdit->Buffer, EditBufferOffset);
    CellUpdated = FALSE;

    InputChar = YoriWinHexEditInputCharToByte(Char);

    switch(CellType) {
        case YoriWinHexEditCellTypeOffset:
            break;
        case YoriWinHexEditCellTypeWhitespace:
            break;
        case YoriWinHexEditCellTypeHexDigit:
            InputChar = (UCHAR)YoriLibUpcaseChar(InputChar);
            if (InputChar >= '0' && InputChar <= '9') {
                NewNibble = (UCHAR)(InputChar - '0');
            } else if (InputChar >= 'A' && InputChar <= 'F') {
                NewNibble = (UCHAR)(InputChar - 'A' + 10);
            } else {
                break;
            }

            if (BitShift == (HexEdit->BytesPerWord * 8 - 4)) {
                if (!YoriWinHexEditInsertSpaceInBuffer(HexEdit, BufferOffset, HexEdit->BytesPerWord)) {
                    break;
                }
                DirtyLastLine = (DWORD)-1;
                Cell = YoriLibAddToPointer(HexEdit->Buffer, EditBufferOffset);
            }

            BitMask = (UCHAR)(0xF << EditBitShift);
            InputChar = *Cell;
            InputChar = (UCHAR)(InputChar & ~(BitMask));
            InputChar = (UCHAR)(InputChar | (NewNibble << EditBitShift));
            *Cell = InputChar;
            CellUpdated = TRUE;

            break;
        case YoriWinHexEditCellTypeCharValue:
            if (!YoriWinHexEditInsertSpaceInBuffer(HexEdit, EditBufferOffset, 1)) {
                break;
            }
            DirtyLastLine = (DWORD)-1;
            Cell = YoriLibAddToPointer(HexEdit->Buffer, EditBufferOffset);
            *Cell = InputChar;
            CellUpdated = TRUE;
            break;
    }

    if (CellUpdated) {
        ASSERT(CellType == YoriWinHexEditCellTypeHexDigit || CellType == YoriWinHexEditCellTypeCharValue);
        YoriWinHexEditNextCellSameType(HexEdit, CellType, BufferOffset, BitShift, &CurrentLine, &CurrentCharOffset);
        HexEdit->UserModified = TRUE;
    }

    YoriWinHexEditExpandDirtyRange(HexEdit, FirstLine, DirtyLastLine);
    *LastLine = CurrentLine;
    *LastCharOffset = CurrentCharOffset;

    return TRUE;
}

/**
 Overwrite a single character, which may refer to hex digits or character
 output.

 @param HexEdit Pointer to the hex edit control.

 @param FirstLine Specifies the line in the buffer where text should be
        added.

 @param FirstCharOffset Specifies the offset in the line where text should be
        added.

 @param Char Specifies the character to overwrite.

 @param LastLine On successful completion, populated with the line containing
        the end of the newly added text.

 @param LastCharOffset On successful completion, populated with the offset
        beyond the newly added text.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinHexEditOverwriteCell(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in DWORD FirstLine,
    __in DWORD FirstCharOffset,
    __in TCHAR Char,
    __out PDWORD LastLine,
    __out PDWORD LastCharOffset
    )
{
    YORI_WIN_HEX_EDIT_CELL_TYPE CellType;
    DWORD ByteOffset;
    DWORDLONG BufferOffset;
    DWORD BitShift;
    DWORD CurrentLine;
    DWORD CurrentCharOffset;
    UCHAR BitMask;
    UCHAR NewNibble;
    UCHAR InputChar;
    PUCHAR Cell;
    BOOLEAN CellUpdated;
    BOOLEAN BeyondBufferEnd;
    DWORDLONG EditBufferOffset;
    DWORD EditBitShift;

    CurrentLine = FirstLine;
    CurrentCharOffset = FirstCharOffset;
    CellUpdated = FALSE;

    CellType = YoriWinHexEditCellType(HexEdit, CurrentLine, CurrentCharOffset, &ByteOffset, &BitShift, &BeyondBufferEnd);
    BufferOffset = CurrentLine;
    BufferOffset = BufferOffset * HexEdit->BytesPerLine + ByteOffset;

    //
    //  Convert everything into bytes as opposed to words
    //

    EditBufferOffset = BufferOffset;
    EditBitShift = BitShift;
    if (EditBitShift >= 8) {
        EditBufferOffset = EditBufferOffset + EditBitShift / 8;
        EditBitShift = EditBitShift % 8;
    }

    InputChar = YoriWinHexEditInputCharToByte(Char);

    switch(CellType) {
        case YoriWinHexEditCellTypeOffset:
            break;
        case YoriWinHexEditCellTypeWhitespace:
            break;
        case YoriWinHexEditCellTypeHexDigit:
            BitMask = (UCHAR)(0xF << EditBitShift);
            InputChar = (UCHAR)YoriLibUpcaseChar(InputChar);
            if (InputChar >= '0' && InputChar <= '9') {
                NewNibble = (UCHAR)(InputChar - '0');
            } else if (InputChar >= 'A' && InputChar <= 'F') {
                NewNibble = (UCHAR)(InputChar - 'A' + 10);
            } else {
                break;
            }

            if (BeyondBufferEnd) {
                if (!YoriWinHexEditEnsureBufferValid(HexEdit, EditBufferOffset + 1)) {
                    *LastLine = CurrentLine;
                    *LastCharOffset = CurrentCharOffset;
                    return TRUE;
                }
            }

            Cell = YoriLibAddToPointer(HexEdit->Buffer, EditBufferOffset);
            InputChar = *Cell;
            InputChar = (UCHAR)(InputChar & ~(BitMask));
            InputChar = (UCHAR)(InputChar | (NewNibble << EditBitShift));
            *Cell = InputChar;
            CellUpdated = TRUE;

            break;
        case YoriWinHexEditCellTypeCharValue:
            if (BeyondBufferEnd) {
                if (!YoriWinHexEditEnsureBufferValid(HexEdit, EditBufferOffset + 1)) {
                    *LastLine = CurrentLine;
                    *LastCharOffset = CurrentCharOffset;
                    return TRUE;
                }
            }
            Cell = YoriLibAddToPointer(HexEdit->Buffer, EditBufferOffset);
            *Cell = InputChar;
            CellUpdated = TRUE;
            break;
    }

    if (CellUpdated) {
        ASSERT(CellType == YoriWinHexEditCellTypeHexDigit || CellType == YoriWinHexEditCellTypeCharValue);
        YoriWinHexEditNextCellSameType(HexEdit, CellType, BufferOffset, BitShift, &CurrentLine, &CurrentCharOffset);
        YoriWinHexEditExpandDirtyRange(HexEdit, FirstLine, CurrentLine);
        HexEdit->UserModified = TRUE;
    }

    *LastLine = CurrentLine;
    *LastCharOffset = CurrentCharOffset;

    return TRUE;
}

/**
 Assign a currently allocated buffer to a hex edit control.  This function
 assumes the caller allocated the buffer with @ref YoriLibReferencedMalloc .

 @param CtrlHandle Pointer to the hex edit control.

 @param NewBuffer Pointer to a buffer allocated with
        @ref YoriLibReferencedMalloc .

 @param NewBufferAllocated Specifies the number of bytes allocated to the
        NewBuffer allocation.

 @param NewBufferValid Specifies the number of bytes valid in the NewBuffer
        allocation.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinHexEditSetDataNoCopy(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __in PUCHAR NewBuffer,
    __in DWORDLONG NewBufferAllocated,
    __in DWORDLONG NewBufferValid
    )
{
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    PYORI_WIN_CTRL Ctrl;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    if (HexEdit->Buffer != NULL) {
        YoriLibDereference(HexEdit->Buffer);
    }

    YoriLibReference(NewBuffer);
    HexEdit->Buffer = NewBuffer;
    HexEdit->BufferAllocated = NewBufferAllocated;
    HexEdit->BufferValid = NewBufferValid;

    //
    //  Mark the whole range as dirty.  We didn't bother to count how many
    //  lines were populated before freeing, so don't know exactly how many
    //  lines need to be redisplayed.
    //

    YoriWinHexEditExpandDirtyRange(HexEdit, 0, (DWORD)-1);
    YoriWinHexEditPaint(HexEdit);

    return TRUE;
}

/**
 Obtain a referenced buffer to the data underlying the control.  Note that
 this buffer can be subsequently modified by the control, so this data is
 only stable until events are processed.

 @param CtrlHandle Pointer to the hex edit control.

 @param Buffer On successful completion, updated to point to the data behind
        the hex edit control.  Note this pointer will be referenced by this
        routine and the caller is expected to release with
        @ref YoriLibDereference .

 @param BufferLength On successful completion, updated to point to the number
        of bytes in the Buffer allocation.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinHexEditGetDataNoCopy(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __out PUCHAR *Buffer,
    __out PDWORDLONG BufferLength
    )
{
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    PYORI_WIN_CTRL Ctrl;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    if (HexEdit->Buffer) {
        YoriLibReference(HexEdit->Buffer);
    }
    *Buffer = HexEdit->Buffer;
    *BufferLength = HexEdit->BufferValid;

    return TRUE;
}

//
//  =========================================
//  GENERAL EXPORTED API FUNCTIONS
//  =========================================
//

/**
 Indicate that no range is selected in a hex edit control.

 @param CtrlHandle Pointer to the hex edit control.
 */
VOID
YoriWinHexEditClearSelection(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle
    )
{
    DWORD FirstDirtyLine;
    DWORD LastDirtyLine;

    PYORI_WIN_CTRL Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    if (HexEdit->Selection.Active == YoriWinHexEditSelectNotActive) {
        return;
    }

    FirstDirtyLine = (DWORD)(HexEdit->Selection.FirstByteOffset / HexEdit->BytesPerLine);
    LastDirtyLine = (DWORD)(HexEdit->Selection.LastByteOffset / HexEdit->BytesPerLine);
    HexEdit->Selection.Active = YoriWinHexEditSelectNotActive;

    YoriWinHexEditExpandDirtyRange(HexEdit, FirstDirtyLine, LastDirtyLine);
}

/**
 Return a copy of the selected data in the control.  The buffer is allocated
 within this routine and should be freed by the caller with
 @ref YoriLibDereference.  If no data is selected, this routine returns
 FALSE.

 @param CtrlHandle Pointer to the hex edit control.

 @param Data On successful completion, updated to point to a newly allocated
        buffer containing the selected data.

 @param DataLength On successful completion, updated to point to the length
        of the data.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinHexEditGetSelectedData(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __out PVOID * Data,
    __out PDWORDLONG DataLength
    )
{
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    PYORI_WIN_CTRL Ctrl;
    ULARGE_INTEGER LocalDataLength;
    PUCHAR Buffer;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    if (HexEdit->Selection.Active == YoriWinHexEditSelectNotActive) {
        return FALSE;
    }

    LocalDataLength.QuadPart = HexEdit->Selection.LastByteOffset - HexEdit->Selection.FirstByteOffset + 1;
    if (LocalDataLength.HighPart != 0) {
        return FALSE;
    }

    Buffer = YoriLibReferencedMalloc(LocalDataLength.LowPart);
    if (Buffer == NULL) {
        return FALSE;
    }

    memcpy(Buffer, &HexEdit->Buffer[HexEdit->Selection.FirstByteOffset], LocalDataLength.LowPart);
    *Data = Buffer;
    *DataLength = LocalDataLength.QuadPart;

    return TRUE;
}

/**
 Set the color attributes of the hex edit control.

 @param CtrlHandle Pointer to the hex edit control.

 @param Attributes Specifies the foreground and background color for the
        hex edit control to use.

 @param SelectedAttributes Specifies the foreground and background color
        to use for selected text within the hex edit control.
 */
VOID
YoriWinHexEditSetColor(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __in WORD Attributes,
    __in WORD SelectedAttributes
    )
{
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    PYORI_WIN_CTRL Ctrl;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    HexEdit->TextAttributes = Attributes;
    HexEdit->SelectedAttributes = SelectedAttributes;
    YoriWinHexEditExpandDirtyRange(HexEdit, 0, (DWORD)-1);
    YoriWinHexEditPaintNonClient(HexEdit);
    YoriWinHexEditPaint(HexEdit);
}

/**
 Return the current viewport location within a hex edit control.

 @param CtrlHandle Pointer to the hex edit control.

 @param ViewportLeft On successful completion, populated with the first
        character displayed in the control.

 @param ViewportTop On successful completion, populated with the first line
        displayed in the control.
 */
VOID
YoriWinHexEditGetViewportLocation(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __out PDWORD ViewportLeft,
    __out PDWORD ViewportTop
    )
{
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;

    HexEdit = (PYORI_WIN_CTRL_HEX_EDIT)CtrlHandle;

    *ViewportLeft = HexEdit->ViewportLeft;
    *ViewportTop = HexEdit->ViewportTop;
}

/**
 Modify the viewport location within the hex edit control.

 @param CtrlHandle Pointer to the hex edit control.

 @param NewViewportLeft The display offset of the first character to display
        on the left of the control.

 @param NewViewportTop The display offset of the first character to display
        on the top of the control.
 */
VOID
YoriWinHexEditSetViewportLocation(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __in DWORD NewViewportLeft,
    __in DWORD NewViewportTop
    )
{
    COORD ClientSize;
    PYORI_WIN_CTRL Ctrl;
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    DWORD EffectiveNewViewportTop;
    DWORD LinesPopulated;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    YoriWinGetControlClientSize(&HexEdit->Ctrl, &ClientSize);
    LinesPopulated = YoriWinHexEditLinesPopulated(HexEdit);

    EffectiveNewViewportTop = NewViewportTop;

    if (EffectiveNewViewportTop > LinesPopulated) {
        if (LinesPopulated > 0) {
            EffectiveNewViewportTop = LinesPopulated - 1;
        } else {
            EffectiveNewViewportTop = 0;
        }
    }

    //
    //  Normally we'd call YoriWinHexEditEnsureCursorVisible,
    //  but this series of routines allow the viewport to move where the
    //  cursor isn't.
    //

    if (EffectiveNewViewportTop != HexEdit->ViewportTop) {
        YoriWinHexEditExpandDirtyRange(HexEdit, EffectiveNewViewportTop, (DWORD)-1);
        HexEdit->ViewportTop = EffectiveNewViewportTop;
        YoriWinHexEditRepaintScrollBar(HexEdit);
    }

    if (NewViewportLeft != HexEdit->ViewportLeft) {
        YoriWinHexEditExpandDirtyRange(HexEdit, EffectiveNewViewportTop, (DWORD)-1);
        HexEdit->ViewportLeft = NewViewportLeft;
    }
    YoriWinHexEditPaint(HexEdit);
}

/**
 Clear all of the contents of a hex edit control.

 @param CtrlHandle Pointer to the hex edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinHexEditClear(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORI_WIN_CTRL Ctrl;
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    if (HexEdit->Buffer != NULL) {
        YoriLibDereference(HexEdit->Buffer);
        HexEdit->Buffer = NULL;
    }
    HexEdit->BufferAllocated = 0;
    HexEdit->BufferValid = 0;

    HexEdit->ViewportTop = 0;
    HexEdit->ViewportLeft = 0;

    YoriWinHexEditExpandDirtyRange(HexEdit, HexEdit->ViewportTop, (DWORD)-1);
    YoriWinHexEditSetCursorLocationToZero(HexEdit);

    YoriWinHexEditPaint(HexEdit);
    return TRUE;
}

/**
 Set the title to display on the top of a hex edit control.

 @param CtrlHandle Pointer to the hex edit control.

 @param Caption Pointer to the caption to display on the top of the hex
        edit control.  This can point to an empty string to indicate no
        caption should be displayed.

 @return TRUE to indicate the caption was successfully updated, or FALSE on
         failure.
 */
BOOLEAN
YoriWinHexEditSetCaption(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __in PYORI_STRING Caption
    )
{
    PYORI_WIN_CTRL Ctrl;
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    YORI_STRING NewCaption;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    if (HexEdit->Caption.LengthAllocated < Caption->LengthInChars) {
        if (!YoriLibAllocateString(&NewCaption, Caption->LengthInChars)) {
            return FALSE;
        }

        YoriLibFreeStringContents(&HexEdit->Caption);
        memcpy(&HexEdit->Caption, &NewCaption, sizeof(YORI_STRING));
    }

    if (Caption->LengthInChars > 0) {
        memcpy(HexEdit->Caption.StartOfString, Caption->StartOfString, Caption->LengthInChars * sizeof(TCHAR));
    }
    HexEdit->Caption.LengthInChars = Caption->LengthInChars;
    YoriWinHexEditPaintNonClient(HexEdit);
    return TRUE;
}

/**
 Indicates whether the hex edit control has been modified by the user.
 This is typically used after some external event indicates that the buffer
 should be considered unchanged, eg., a file is successfully saved.

 @param CtrlHandle Pointer to the hex edit contorl.

 @param ModifyState TRUE if the control should consider itself modified by
        the user, FALSE if it should not.

 @return TRUE if the control was previously modified by the user, FALSE if it
         was not.
 */
BOOLEAN
YoriWinHexEditSetModifyState(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __in BOOLEAN ModifyState
    )
{
    PYORI_WIN_CTRL Ctrl;
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    BOOLEAN PreviousValue;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    PreviousValue = HexEdit->UserModified;
    HexEdit->UserModified = ModifyState;
    return PreviousValue;
}

/**
 Returns TRUE if the hex edit control has been modified by the user
 since the last time @ref YoriWinHexEditSetModifyState indicated that
 no user modification has occurred.

 @param CtrlHandle Pointer to the hex edit contorl.

 @return TRUE if the control has been modified by the user, FALSE if it has
         not.
 */
BOOLEAN
YoriWinHexEditGetModifyState(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORI_WIN_CTRL Ctrl;
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    return HexEdit->UserModified;
}

/**
 Set a function to call when the cursor location changes.

 @param CtrlHandle Pointer to the hex edit control.

 @param NotifyCallback Pointer to a function to invoke when the cursor
        moves.

 @return TRUE to indicate the callback function was successfully updated,
         FALSE to indicate another callback function was already present.
 */
BOOLEAN
YoriWinHexEditSetCursorMoveNotifyCallback(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __in PYORI_WIN_NOTIFY_HEX_EDIT_CURSOR_MOVE NotifyCallback
    )
{
    PYORI_WIN_CTRL Ctrl;
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    if (HexEdit->CursorMoveCallback != NULL) {
        return FALSE;
    }

    HexEdit->CursorMoveCallback = NotifyCallback;

    return TRUE;
}

/**
 Get the number of bytes per word in the hex edit control.

 @param CtrlHandle Pointer to the hex edit control.

 @return The number of bytes per word.
 */
DWORD
YoriWinHexEditGetBytesPerWord(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORI_WIN_CTRL Ctrl;
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    return HexEdit->BytesPerWord;
}

/**
 Set the number of bytes per word in the hex edit control.

 @param CtrlHandle Pointer to the hex edit control.

 @param BytesPerWord The number of bytes per word.  Valid values are 1,
        2, 4, and 8.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinHexEditSetBytesPerWord(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __in DWORD BytesPerWord
    )
{
    PYORI_WIN_CTRL Ctrl;
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    YORI_WIN_HEX_EDIT_CELL_TYPE CellType;
    DWORD ByteOffset;
    DWORD BitShift;
    BOOLEAN BeyondBufferEnd;
    DWORDLONG BufferOffset;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    if (BytesPerWord != 1 &&
        BytesPerWord != 2 &&
        BytesPerWord != 4 &&
        BytesPerWord != 8) {

        return FALSE;
    }

    CellType = YoriWinHexEditCellType(HexEdit, HexEdit->CursorLine, HexEdit->CursorOffset, &ByteOffset, &BitShift, &BeyondBufferEnd);
    BufferOffset = HexEdit->CursorLine;
    BufferOffset = BufferOffset * HexEdit->BytesPerLine + ByteOffset;

    if (CellType == YoriWinHexEditCellTypeHexDigit) {
        DWORD Unaligned;
        if (BitShift >= BytesPerWord * 8) {
            Unaligned = BitShift / 8;
            BufferOffset = BufferOffset + Unaligned;
            BitShift = BitShift - Unaligned * 8;
        } else if ((BufferOffset % BytesPerWord) != 0) {
            Unaligned = (DWORD)(BufferOffset % BytesPerWord);
            BufferOffset = BufferOffset - Unaligned;
            BitShift = BitShift + 8 * Unaligned;
        }
    }

    HexEdit->BytesPerWord = BytesPerWord;

    YoriWinHexEditSetCursorToBufferLocation(HexEdit, CellType, BufferOffset, BitShift);

    YoriWinHexEditExpandDirtyRange(HexEdit, HexEdit->ViewportTop, (DWORD)-1);

    YoriWinHexEditEnsureCursorVisible(HexEdit);
    YoriWinHexEditPaint(HexEdit);

    return TRUE;
}

/**
 Set the style of the hex edit control.

 @param CtrlHandle Pointer to the hex edit control.

 @param NewStyle The new style to apply to the hex edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinHexEditSetStyle(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __in DWORD NewStyle
    )
{
    PYORI_WIN_CTRL Ctrl;
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    YORI_WIN_HEX_EDIT_CELL_TYPE CellType;
    DWORD ByteOffset;
    DWORD BitShift;
    BOOLEAN BeyondBufferEnd;
    DWORDLONG BufferOffset;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    if (NewStyle & ~(YORI_WIN_HEX_EDIT_STYLE_OFFSET | YORI_WIN_HEX_EDIT_STYLE_LARGE_OFFSET)) {
        return FALSE;
    }

    if ((NewStyle & (YORI_WIN_HEX_EDIT_STYLE_OFFSET | YORI_WIN_HEX_EDIT_STYLE_LARGE_OFFSET)) == (YORI_WIN_HEX_EDIT_STYLE_OFFSET | YORI_WIN_HEX_EDIT_STYLE_LARGE_OFFSET)) {
        return FALSE;
    }

    CellType = YoriWinHexEditCellType(HexEdit, HexEdit->CursorLine, HexEdit->CursorOffset, &ByteOffset, &BitShift, &BeyondBufferEnd);
    BufferOffset = HexEdit->CursorLine;
    BufferOffset = BufferOffset * HexEdit->BytesPerLine + ByteOffset;

    HexEdit->OffsetWidth = 0;
    if (NewStyle & YORI_WIN_HEX_EDIT_STYLE_OFFSET) {
        HexEdit->OffsetWidth = 32;
    } else if (NewStyle & YORI_WIN_HEX_EDIT_STYLE_LARGE_OFFSET) {
        HexEdit->OffsetWidth = 64;
    }

    YoriWinHexEditSetCursorToBufferLocation(HexEdit, CellType, BufferOffset, BitShift);

    YoriWinHexEditExpandDirtyRange(HexEdit, HexEdit->ViewportTop, (DWORD)-1);

    YoriWinHexEditEnsureCursorVisible(HexEdit);
    YoriWinHexEditPaint(HexEdit);
    return TRUE;
}

/**
 Return the cursor offset, expressed in terms of a buffer offset and bit
 shift.  Bit shift is only meaningful when the cell type refers to hex digit,
 so a cursor has multiple positions per buffer offset.

 @param CtrlHandle Pointer to the hex edit control.

 @param AsChar On successful completion, set to TRUE to indicate the cursor is
        within the character representation of the offset.  If FALSE, the
        cursor is within the hex representation of the offset.

 @param BufferOffset Specifies the offset within the buffer.

 @param BitShift Specifies the bits within the buffer offset.

 @return TRUE to indicate successful completion, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinHexEditGetCursorLocation(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __out PBOOLEAN AsChar,
    __out PDWORDLONG BufferOffset,
    __out PDWORD BitShift
    )
{
    PYORI_WIN_CTRL Ctrl;
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    YORI_WIN_HEX_EDIT_CELL_TYPE CellType;
    DWORD ByteOffset;
    BOOLEAN BeyondBufferEnd;
    DWORDLONG LocalBufferOffset;
    DWORD LocalBitShift;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    CellType = YoriWinHexEditCellType(HexEdit, HexEdit->CursorLine, HexEdit->CursorOffset, &ByteOffset, &LocalBitShift, &BeyondBufferEnd);
    ASSERT (CellType == YoriWinHexEditCellTypeHexDigit || CellType == YoriWinHexEditCellTypeCharValue);
    if (CellType == YoriWinHexEditCellTypeHexDigit ||
        CellType == YoriWinHexEditCellTypeCharValue) {

        LocalBufferOffset = HexEdit->CursorLine;
        LocalBufferOffset = LocalBufferOffset * HexEdit->BytesPerLine + ByteOffset;

        *BufferOffset = LocalBufferOffset;
        *BitShift = LocalBitShift;

        if (CellType == YoriWinHexEditCellTypeCharValue) {
            *AsChar = TRUE;
        } else {
            *AsChar = FALSE;
        }

        return TRUE;
    }

    return FALSE;
}

/**
 Return the cursor offset, expressed in terms of the location within the
 control in horizontal and vertical offsets.

 @param CtrlHandle Pointer to the hex edit control.

 @param CursorOffset On successful completion, updated to refer to the offset
        within the control.

 @param CursorLine On successful completion, updated to refer to the line
        within the control.

 @return TRUE to indicate successful completion, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinHexEditGetVisualCursorLocation(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __out PDWORD CursorOffset,
    __out PDWORD CursorLine
    )
{
    PYORI_WIN_CTRL Ctrl;
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    *CursorOffset = HexEdit->CursorOffset;
    *CursorLine = HexEdit->CursorLine;

    return TRUE;
}

/**
 Remove a range of data from a hex edit control.

 @param CtrlHandle Pointer to the hex edit control.

 @param DataOffset The offset of the first byte to remove within the hex edit
        control.

 @param Length The number of bytes to remove from the hex edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinHexEditDeleteData(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __in DWORDLONG DataOffset,
    __in DWORDLONG Length
    )
{
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    PYORI_WIN_CTRL Ctrl;
    DWORDLONG LengthToRemove;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    if (DataOffset >= HexEdit->BufferValid) {
        return FALSE;
    }

    LengthToRemove = Length;
    if (DataOffset + LengthToRemove > HexEdit->BufferValid) {
        LengthToRemove = HexEdit->BufferValid - DataOffset;
    }

    if (HexEdit->BufferValid > DataOffset + LengthToRemove) {
        memmove(&HexEdit->Buffer[DataOffset],
                &HexEdit->Buffer[DataOffset + LengthToRemove],
                (DWORD)(HexEdit->BufferValid - DataOffset - LengthToRemove));
    }

    HexEdit->BufferValid = HexEdit->BufferValid - LengthToRemove;
    YoriWinHexEditExpandDirtyRange(HexEdit, (DWORD)(DataOffset / HexEdit->BytesPerLine), (DWORD)-1);
    return TRUE;
}

/**
 Insert a range of data into a hex edit control.

 @param CtrlHandle Pointer to the hex edit control.

 @param DataOffset The offset of the first byte to be updated with new data.

 @param Data Pointer to the new data to insert.

 @param Length The number of bytes to insert into the hex edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinHexEditInsertData(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __in DWORDLONG DataOffset,
    __in PVOID Data,
    __in DWORDLONG Length
    )
{
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    PYORI_WIN_CTRL Ctrl;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    if (DataOffset >= HexEdit->BufferValid) {
        return FALSE;
    }

    if (!YoriWinHexEditInsertSpaceInBuffer(HexEdit, DataOffset, (DWORD)Length)) {
        return FALSE;
    }

    memmove(&HexEdit->Buffer[DataOffset],
            Data,
            (DWORD)Length);

    YoriWinHexEditExpandDirtyRange(HexEdit, (DWORD)(DataOffset / HexEdit->BytesPerLine), (DWORD)-1);
    return TRUE;
}

/**
 Replace a range of data in a hex edit control.

 @param CtrlHandle Pointer to the hex edit control.

 @param DataOffset The offset of the first byte to be updated with new data.

 @param Data Pointer to the new data to insert.

 @param Length The number of bytes to insert into the hex edit control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
__success(return)
BOOLEAN
YoriWinHexEditReplaceData(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __in DWORDLONG DataOffset,
    __in PVOID Data,
    __in DWORDLONG Length
    )
{
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    PYORI_WIN_CTRL Ctrl;
    DWORD FirstDirtyLine;
    DWORD LastDirtyLine;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    if (DataOffset + Length > HexEdit->BufferValid) {
        return FALSE;
    }

    memmove(&HexEdit->Buffer[DataOffset],
            Data,
            (DWORD)Length);

    FirstDirtyLine = (DWORD)(DataOffset / HexEdit->BytesPerLine);
    LastDirtyLine = (DWORD)((DataOffset + Length) / HexEdit->BytesPerLine);
    YoriWinHexEditExpandDirtyRange(HexEdit, FirstDirtyLine, LastDirtyLine);
    return TRUE;
}

/**
 Return TRUE if a selection region is active, or FALSE if no selection is
 currently active.

 @param CtrlHandle Pointer to the hex edit control.

 @return TRUE if a selection region is active, or FALSE if no selection is
         currently active.
 */
BOOLEAN
YoriWinHexEditSelectionActive(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle
    )
{
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    PYORI_WIN_CTRL Ctrl;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    if (HexEdit->Selection.Active == YoriWinHexEditSelectNotActive) {
        return FALSE;
    }
    return TRUE;
}

//
//  =========================================
//  INPUT HANDLING FUNCTIONS
//  =========================================
//

/**
 Delete the character at the cursor and move later characters into position.

 @param HexEdit Pointer to the hex edit control, indicating the
        current cursor location.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinHexEditDelete(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    DWORD FirstLine;
    DWORD FirstCharOffset;
    DWORD LastLine;
    DWORD LastCharOffset;

    FirstLine = HexEdit->CursorLine;
    FirstCharOffset = HexEdit->CursorOffset;

    if (!YoriWinHexEditDeleteCell(HexEdit, FirstLine, FirstCharOffset, &LastLine, &LastCharOffset)) {
        return FALSE;
    }

    YoriWinHexEditSetCursorLocationInternal(HexEdit, LastCharOffset, LastLine);

    return TRUE;
}

/**
 Move the viewport up by one screenful and move the cursor to match.
 If we're at the top of the range, do nothing.  The somewhat strange
 logic here is patterned after the original edit.

 @param HexEdit Pointer to the hex edit control specifying the
        viewport location and cursor location.  On completion these may be
        adjusted.

 @return TRUE to indicate the display was moved, FALSE if it was not.
 */
BOOLEAN
YoriWinHexEditPageUp(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    COORD ClientSize;
    DWORD ViewportHeight;
    DWORD NewCursorLine;
    DWORD NewCursorOffset;

    YoriWinGetControlClientSize(&HexEdit->Ctrl, &ClientSize);
    ViewportHeight = ClientSize.Y;

    if (HexEdit->CursorLine > 0) {
        if (HexEdit->CursorLine >= ViewportHeight) {
            NewCursorLine = HexEdit->CursorLine - ViewportHeight;
        } else {
            NewCursorLine = 0;
        }

        if (HexEdit->ViewportTop >= ViewportHeight) {
            HexEdit->ViewportTop = HexEdit->ViewportTop - ViewportHeight;
        } else {
            HexEdit->ViewportTop = 0;
        }

        YoriWinHexEditExpandDirtyRange(HexEdit, HexEdit->ViewportTop, (DWORD)-1);

        NewCursorOffset = HexEdit->CursorOffset;
        YoriWinHexEditSetCursorLocationInternal(HexEdit, NewCursorOffset, NewCursorLine);
        YoriWinHexEditRepaintScrollBar(HexEdit);
        return TRUE;
    }

    return FALSE;
}

/**
 Move the viewport down by one screenful and move the cursor to match.
 If we're at the bottom of the range, do nothing.  The somewhat strange
 logic here is patterned after the original edit.

 @param HexEdit Pointer to the hex edit control specifying the
        viewport location and cursor location.  On completion these may be
        adjusted.

 @return TRUE to indicate the display was moved, FALSE if it was not.
 */
BOOLEAN
YoriWinHexEditPageDown(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    COORD ClientSize;
    DWORD ViewportHeight;
    DWORD NewCursorLine;
    DWORD NewCursorOffset;
    DWORD LinesPopulated;

    YoriWinGetControlClientSize(&HexEdit->Ctrl, &ClientSize);
    ViewportHeight = ClientSize.Y;
    LinesPopulated = YoriWinHexEditLinesPopulated(HexEdit);

    if (HexEdit->ViewportTop + ViewportHeight < LinesPopulated) {
        HexEdit->ViewportTop = HexEdit->ViewportTop + ViewportHeight;
        YoriWinHexEditExpandDirtyRange(HexEdit, HexEdit->ViewportTop, (DWORD)-1);
        NewCursorLine = HexEdit->CursorLine;
        if (HexEdit->CursorLine + ViewportHeight < LinesPopulated) {
            NewCursorLine = HexEdit->CursorLine + ViewportHeight;
        } else if (HexEdit->CursorLine + 1 < LinesPopulated) {
            NewCursorLine = LinesPopulated - 1;
        }

        NewCursorOffset = HexEdit->CursorOffset;
        YoriWinHexEditSetCursorLocationInternal(HexEdit, NewCursorOffset, NewCursorLine);
        YoriWinHexEditRepaintScrollBar(HexEdit);
        return TRUE;
    }

    return FALSE;
}

/**
 Scroll the hex edit based on a mouse wheel notification.

 @param HexEdit Pointer to the hex edit to scroll.

 @param LinesToMove The number of lines to scroll.

 @param MoveUp If TRUE, scroll backwards through the text.  If FALSE,
        scroll forwards through the text.
 */
VOID
YoriWinHexEditNotifyMouseWheel(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in DWORD LinesToMove,
    __in BOOLEAN MoveUp
    )
{
    COORD ClientSize;
    DWORD LineCountToDisplay;
    DWORD NewViewportTop;
    DWORD LinesPopulated;

    YoriWinGetControlClientSize(&HexEdit->Ctrl, &ClientSize);
    LineCountToDisplay = ClientSize.Y;
    LinesPopulated = YoriWinHexEditLinesPopulated(HexEdit);

    if (MoveUp) {
        if (HexEdit->ViewportTop < LinesToMove) {
            NewViewportTop = 0;
        } else {
            NewViewportTop = HexEdit->ViewportTop - LinesToMove;
        }
    } else {
        if (HexEdit->ViewportTop + LinesToMove + LineCountToDisplay > LinesPopulated) {
            if (LinesPopulated >= LineCountToDisplay) {
                NewViewportTop = LinesPopulated - LineCountToDisplay;
            } else {
                NewViewportTop = 0;
            }
        } else {
            NewViewportTop = HexEdit->ViewportTop + LinesToMove;
        }
    }

    YoriWinHexEditSetViewportLocation(&HexEdit->Ctrl, HexEdit->ViewportLeft, NewViewportTop);
}

/**
 When the user presses a regular key, insert that key into the control.

 @param HexEdit Pointer to the hex edit control, specifying the
        location of the cursor and contents of the control.

 @param Char The character to insert.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinHexEditAddChar(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in TCHAR Char
    )
{
    DWORD NewCursorLine;
    DWORD NewCursorOffset;

    if (!HexEdit->InsertMode) {
        if (!YoriWinHexEditOverwriteCell(HexEdit, HexEdit->CursorLine, HexEdit->CursorOffset, Char, &NewCursorLine, &NewCursorOffset)) {
            return FALSE;
        }
    } else {
        if (!YoriWinHexEditInsertCell(HexEdit, HexEdit->CursorLine, HexEdit->CursorOffset, Char, &NewCursorLine, &NewCursorOffset)) {
            return FALSE;
        }
    }

    YoriWinHexEditSetCursorLocationInternal(HexEdit, NewCursorOffset, NewCursorLine);

    return TRUE;
}

/**
 Indicates the left cursor key was pressed.

 @param HexEdit Pointer to the hex edit control.

 @return TRUE to indicate the cursor moved, FALSE if it did not.
 */
BOOLEAN
YoriWinHexEditCursorLeft(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    DWORDLONG BufferOffset;
    YORI_WIN_HEX_EDIT_CELL_TYPE CellType;
    DWORD ByteOffset;
    DWORD BitShift;
    BOOLEAN BeyondBufferEnd;
    DWORD NewCursorLine;
    DWORD NewCursorOffset;

    CellType = YoriWinHexEditCellType(HexEdit, HexEdit->CursorLine, HexEdit->CursorOffset, &ByteOffset, &BitShift, &BeyondBufferEnd);
    BufferOffset = HexEdit->CursorLine;
    BufferOffset = BufferOffset * HexEdit->BytesPerLine + ByteOffset;

    YoriWinHexEditPreviousCellSameType(HexEdit, CellType, BufferOffset, BitShift, &NewCursorLine, &NewCursorOffset);

    if (NewCursorLine != HexEdit->CursorLine || NewCursorOffset != HexEdit->CursorOffset) {

        YoriWinHexEditClearSelection(HexEdit);
        YoriWinHexEditSetCursorLocationInternal(HexEdit, NewCursorOffset, NewCursorLine);
        YoriWinHexEditEnsureCursorVisible(HexEdit);
        YoriWinHexEditPaint(HexEdit);
        return TRUE;
    }

    return FALSE;
}

/**
 Indicates the right cursor key was pressed.

 @param HexEdit Pointer to the hex edit control.

 @return TRUE to indicate the cursor moved, FALSE if it did not.
 */
BOOLEAN
YoriWinHexEditCursorRight(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    DWORDLONG BufferOffset;
    YORI_WIN_HEX_EDIT_CELL_TYPE CellType;
    DWORD ByteOffset;
    DWORD BitShift;
    BOOLEAN BeyondBufferEnd;
    DWORD NewCursorLine;
    DWORD NewCursorOffset;

    CellType = YoriWinHexEditCellType(HexEdit, HexEdit->CursorLine, HexEdit->CursorOffset, &ByteOffset, &BitShift, &BeyondBufferEnd);
    BufferOffset = HexEdit->CursorLine;
    BufferOffset = BufferOffset * HexEdit->BytesPerLine + ByteOffset;

    YoriWinHexEditNextCellSameType(HexEdit, CellType, BufferOffset, BitShift, &NewCursorLine, &NewCursorOffset);

    //
    //  If the cursor is currently on the last byte, check if the new cell
    //  would be beyond the last byte and stop
    //

    if (BufferOffset >= HexEdit->BufferValid) {
        CellType = YoriWinHexEditCellType(HexEdit, NewCursorLine, NewCursorOffset, &ByteOffset, &BitShift, &BeyondBufferEnd);
        BufferOffset = HexEdit->CursorLine;
        BufferOffset = BufferOffset * HexEdit->BytesPerLine + ByteOffset;
        if (BufferOffset > HexEdit->BufferValid) {
            return FALSE;
        }
    }

    YoriWinHexEditClearSelection(HexEdit);
    YoriWinHexEditSetCursorLocationInternal(HexEdit, NewCursorOffset, NewCursorLine);
    YoriWinHexEditEnsureCursorVisible(HexEdit);
    YoriWinHexEditPaint(HexEdit);
    return TRUE;
}

/**
 Indicates the home key was pressed.

 @param HexEdit Pointer to the hex edit control.

 @return TRUE to indicate the cursor moved, FALSE if it did not.
 */
BOOLEAN
YoriWinHexEditCursorHome(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    DWORDLONG BufferOffset;
    YORI_WIN_HEX_EDIT_CELL_TYPE CellType;
    DWORD ByteOffset;
    DWORD BitShift;
    BOOLEAN BeyondBufferEnd;

    CellType = YoriWinHexEditCellType(HexEdit, HexEdit->CursorLine, HexEdit->CursorOffset, &ByteOffset, &BitShift, &BeyondBufferEnd);
    BufferOffset = HexEdit->CursorLine;
    BufferOffset = BufferOffset * HexEdit->BytesPerLine + ByteOffset;
    ASSERT (CellType == YoriWinHexEditCellTypeHexDigit || CellType == YoriWinHexEditCellTypeCharValue);
    BufferOffset = (BufferOffset / HexEdit->BytesPerLine) * HexEdit->BytesPerLine;
    if (CellType == YoriWinHexEditCellTypeHexDigit) {
        BitShift = HexEdit->BytesPerWord * 8 - 4;
    }

    YoriWinHexEditClearSelection(HexEdit);
    return YoriWinHexEditSetCursorToBufferLocation(HexEdit,
                                                   CellType,
                                                   BufferOffset,
                                                   BitShift);
}

/**
 Indicates the end key was pressed.

 @param HexEdit Pointer to the hex edit control.

 @return TRUE to indicate the cursor moved, FALSE if it did not.
 */
BOOLEAN
YoriWinHexEditCursorEnd(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    DWORDLONG BufferOffset;
    YORI_WIN_HEX_EDIT_CELL_TYPE CellType;
    DWORD ByteOffset;
    DWORD BitShift;
    BOOLEAN BeyondBufferEnd;

    CellType = YoriWinHexEditCellType(HexEdit, HexEdit->CursorLine, HexEdit->CursorOffset, &ByteOffset, &BitShift, &BeyondBufferEnd);
    BufferOffset = HexEdit->CursorLine;
    BufferOffset = BufferOffset * HexEdit->BytesPerLine + ByteOffset;
    ASSERT (CellType == YoriWinHexEditCellTypeHexDigit || CellType == YoriWinHexEditCellTypeCharValue);
    BufferOffset = ((BufferOffset / HexEdit->BytesPerLine) + 1) * HexEdit->BytesPerLine;
    if (CellType == YoriWinHexEditCellTypeHexDigit) {
        BufferOffset = BufferOffset - HexEdit->BytesPerWord;
        if (BufferOffset > HexEdit->BufferValid) {
            BufferOffset = HexEdit->BufferValid / HexEdit->BytesPerWord * HexEdit->BytesPerWord;
        }
    } else {
        BufferOffset = BufferOffset - 1;
        if (BufferOffset > HexEdit->BufferValid) {
            BufferOffset = HexEdit->BufferValid;
        }
    }
    BitShift = 0;

    YoriWinHexEditClearSelection(HexEdit);
    return YoriWinHexEditSetCursorToBufferLocation(HexEdit,
                                                   CellType,
                                                   BufferOffset,
                                                   BitShift);
}

/**
 Indicates the up key was pressed.

 @param HexEdit Pointer to the hex edit control.

 @return TRUE to indicate the cursor moved, FALSE if it did not.
 */
BOOLEAN
YoriWinHexEditCursorUp(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    DWORDLONG BufferOffset;
    YORI_WIN_HEX_EDIT_CELL_TYPE CellType;
    DWORD ByteOffset;
    DWORD BitShift;
    BOOLEAN BeyondBufferEnd;

    if (HexEdit->CursorLine == 0) {
        return FALSE;
    }

    CellType = YoriWinHexEditCellType(HexEdit, HexEdit->CursorLine, HexEdit->CursorOffset, &ByteOffset, &BitShift, &BeyondBufferEnd);
    BufferOffset = HexEdit->CursorLine - 1;
    BufferOffset = BufferOffset * HexEdit->BytesPerLine + ByteOffset;
    ASSERT (CellType == YoriWinHexEditCellTypeHexDigit || CellType == YoriWinHexEditCellTypeCharValue);

    YoriWinHexEditClearSelection(HexEdit);
    return YoriWinHexEditSetCursorToBufferLocation(HexEdit,
                                                   CellType,
                                                   BufferOffset,
                                                   BitShift);
}

/**
 Indicates the down key was pressed.

 @param HexEdit Pointer to the hex edit control.

 @return TRUE to indicate the cursor moved, FALSE if it did not.
 */
BOOLEAN
YoriWinHexEditCursorDown(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    DWORDLONG BufferOffset;
    YORI_WIN_HEX_EDIT_CELL_TYPE CellType;
    DWORD ByteOffset;
    DWORD BitShift;
    BOOLEAN BeyondBufferEnd;

    CellType = YoriWinHexEditCellType(HexEdit, HexEdit->CursorLine, HexEdit->CursorOffset, &ByteOffset, &BitShift, &BeyondBufferEnd);
    BufferOffset = HexEdit->CursorLine + 1;
    BufferOffset = BufferOffset * HexEdit->BytesPerLine + ByteOffset;

    if (BufferOffset > HexEdit->BufferValid) {
        return FALSE;
    }
    ASSERT (CellType == YoriWinHexEditCellTypeHexDigit || CellType == YoriWinHexEditCellTypeCharValue);

    YoriWinHexEditClearSelection(HexEdit);
    return YoriWinHexEditSetCursorToBufferLocation(HexEdit,
                                                   CellType,
                                                   BufferOffset,
                                                   BitShift);
}

/**
 Indicates the Ctrl+home key was pressed.

 @param HexEdit Pointer to the hex edit control.

 @return TRUE to indicate the cursor moved, FALSE if it did not.
 */
BOOLEAN
YoriWinHexEditCursorCtrlHome(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    DWORDLONG BufferOffset;
    YORI_WIN_HEX_EDIT_CELL_TYPE CellType;
    DWORD ByteOffset;
    DWORD BitShift;
    BOOLEAN BeyondBufferEnd;

    CellType = YoriWinHexEditCellType(HexEdit, HexEdit->CursorLine, HexEdit->CursorOffset, &ByteOffset, &BitShift, &BeyondBufferEnd);
    BufferOffset = 0;
    ASSERT (CellType == YoriWinHexEditCellTypeHexDigit || CellType == YoriWinHexEditCellTypeCharValue);
    if (CellType == YoriWinHexEditCellTypeHexDigit) {
        BitShift = HexEdit->BytesPerWord * 8 - 4;
    }

    YoriWinHexEditClearSelection(HexEdit);
    return YoriWinHexEditSetCursorToBufferLocation(HexEdit,
                                                   CellType,
                                                   BufferOffset,
                                                   BitShift);
}

/**
 Indicates the Ctrl+end key was pressed.

 @param HexEdit Pointer to the hex edit control.

 @return TRUE to indicate the cursor moved, FALSE if it did not.
 */
BOOLEAN
YoriWinHexEditCursorCtrlEnd(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit
    )
{
    DWORDLONG BufferOffset;
    YORI_WIN_HEX_EDIT_CELL_TYPE CellType;
    DWORD ByteOffset;
    DWORD BitShift;
    BOOLEAN BeyondBufferEnd;

    CellType = YoriWinHexEditCellType(HexEdit, HexEdit->CursorLine, HexEdit->CursorOffset, &ByteOffset, &BitShift, &BeyondBufferEnd);
    BufferOffset = HexEdit->BufferValid;
    if (CellType == YoriWinHexEditCellTypeHexDigit) {
        BufferOffset = (HexEdit->BufferValid / HexEdit->BytesPerWord) * HexEdit->BytesPerWord;
    }
    BitShift = 0;
    ASSERT (CellType == YoriWinHexEditCellTypeHexDigit || CellType == YoriWinHexEditCellTypeCharValue);

    YoriWinHexEditClearSelection(HexEdit);
    return YoriWinHexEditSetCursorToBufferLocation(HexEdit,
                                                   CellType,
                                                   BufferOffset,
                                                   BitShift);
}

/**
 Indicates a mouse button was pressed within the client area of the control.

 @param HexEdit Pointer to the hex edit control.

 @param DisplayX Specifies the horizontal coordinate relative to the client
        area of the control.

 @param DisplayY Specifies the vertical coordinate relative to the client
        area of the control.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinHexEditMouseDown(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in DWORD DisplayX,
    __in DWORD DisplayY
    )
{
    DWORD NewCursorLine;
    DWORD NewCursorChar;
    DWORDLONG BufferOffset;
    YORI_WIN_HEX_EDIT_CELL_TYPE CellType;
    DWORD ByteOffset;
    DWORD BitShift;
    BOOLEAN BeyondBufferEnd;

    YoriWinHexEditTranslateViewportCoordinatesToCursorCoordinates(HexEdit, DisplayX, DisplayY, &NewCursorLine, &NewCursorChar);

    CellType = YoriWinHexEditCellType(HexEdit, NewCursorLine, NewCursorChar, &ByteOffset, &BitShift, &BeyondBufferEnd);
    BufferOffset = NewCursorLine;
    BufferOffset = BufferOffset * HexEdit->BytesPerLine + ByteOffset;

    if (BufferOffset <= HexEdit->BufferValid &&
        (CellType == YoriWinHexEditCellTypeHexDigit ||
         CellType == YoriWinHexEditCellTypeCharValue)) {

        YoriWinHexEditClearSelection(HexEdit);
        YoriWinHexEditSetCursorLocationInternal(HexEdit, NewCursorChar, NewCursorLine);

        YoriWinHexEditEnsureCursorVisible(HexEdit);
        YoriWinHexEditPaint(HexEdit);
    }

    return TRUE;
}

/**
 Process a key that may be an enhanced key.  Some of these keys can be either
 enhanced or non-enhanced.

 @param HexEdit Pointer to the hex edit control, indicating the
        current cursor location.

 @param Event Pointer to the event describing the state of the key being
        pressed.

 @return TRUE to indicate the key has been processed, FALSE if it is an
         unknown key.
 */
BOOLEAN
YoriWinHexEditProcessPossiblyEnhancedKey(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in PYORI_WIN_EVENT Event
    )
{
    BOOLEAN Recognized;
    Recognized = FALSE;

    if (Event->KeyDown.VirtualKeyCode == VK_LEFT) {
        YoriWinHexEditCursorLeft(HexEdit);
        Recognized = TRUE;
    } else if (Event->KeyDown.VirtualKeyCode == VK_RIGHT) {
        YoriWinHexEditCursorRight(HexEdit);
        Recognized = TRUE;
    } else if (Event->KeyDown.VirtualKeyCode == VK_HOME) {
        YoriWinHexEditCursorHome(HexEdit);
        Recognized = TRUE;
    } else if (Event->KeyDown.VirtualKeyCode == VK_END) {
        YoriWinHexEditCursorEnd(HexEdit);
        Recognized = TRUE;
    } else if (Event->KeyDown.VirtualKeyCode == VK_INSERT) {
        if (!HexEdit->ReadOnly) {
            YoriWinHexEditToggleInsert(HexEdit);
            YoriWinHexEditPaint(HexEdit);
        }
        Recognized = TRUE;
    } else if (Event->KeyDown.VirtualKeyCode == VK_UP) {
        YoriWinHexEditCursorUp(HexEdit);
        Recognized = TRUE;
    } else if (Event->KeyDown.VirtualKeyCode == VK_DOWN) {
        YoriWinHexEditCursorDown(HexEdit);
        Recognized = TRUE;
    } else if (Event->KeyDown.VirtualKeyCode == VK_PRIOR) {
        if (YoriWinHexEditPageUp(HexEdit)) {
            YoriWinHexEditPaint(HexEdit);
        }
        Recognized = TRUE;
    } else if (Event->KeyDown.VirtualKeyCode == VK_NEXT) {
        if (YoriWinHexEditPageDown(HexEdit)) {
            YoriWinHexEditPaint(HexEdit);
        }
        Recognized = TRUE;
    } else if (Event->KeyDown.VirtualKeyCode == VK_DELETE) {
        if (!HexEdit->ReadOnly && YoriWinHexEditDelete(HexEdit)) {
            YoriWinHexEditEnsureCursorVisible(HexEdit);
            YoriWinHexEditPaint(HexEdit);
        }
        Recognized = TRUE;
    }

    return Recognized;
}

/**
 Process a key that may be an enhanced key with ctrl held.  Some of these
 keys can be either enhanced or non-enhanced.

 @param HexEdit Pointer to the hex edit control, indicating the
        current cursor location.

 @param Event Pointer to the event describing the state of the key being
        pressed.

 @return TRUE to indicate the key has been processed, FALSE if it is an
         unknown key.
 */
BOOLEAN
YoriWinHexEditProcessPossiblyEnhancedCtrlKey(
    __in PYORI_WIN_CTRL_HEX_EDIT HexEdit,
    __in PYORI_WIN_EVENT Event
    )
{
    BOOLEAN Recognized;
    Recognized = FALSE;

    if (Event->KeyDown.VirtualKeyCode == VK_HOME) {
        YoriWinHexEditCursorCtrlHome(HexEdit);
        Recognized = TRUE;
    } else if (Event->KeyDown.VirtualKeyCode == VK_END) {
        YoriWinHexEditCursorCtrlEnd(HexEdit);
        Recognized = TRUE;
    }

    return Recognized;
}


/**
 Process input events for a hex edit control.

 @param Ctrl Pointer to the hex edit control.

 @param Event Pointer to the input event.

 @return TRUE to indicate that the event was processed and no further
         processing should occur.  FALSE to indicate that regular processing
         should continue (although this does not imply that no processing
         has already occurred.)
 */
BOOLEAN
YoriWinHexEditEventHandler(
    __in PYORI_WIN_CTRL Ctrl,
    __in PYORI_WIN_EVENT Event
    )
{
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);
    switch(Event->EventType) {
        case YoriWinEventParentDestroyed:
            if (HexEdit->Buffer != NULL) {
                YoriLibDereference(HexEdit->Buffer);
                HexEdit->Buffer = NULL;
            }
            YoriLibFreeStringContents(&HexEdit->Caption);
            YoriWinDestroyControl(Ctrl);
            YoriLibDereference(HexEdit);
            break;
        case YoriWinEventLoseFocus:
            ASSERT(HexEdit->HasFocus);
            HexEdit->HasFocus = FALSE;
            YoriWinHexEditPaint(HexEdit);
            break;
        case YoriWinEventGetFocus:
            ASSERT(!HexEdit->HasFocus);
            HexEdit->HasFocus = TRUE;
            YoriWinHexEditPaint(HexEdit);
            break;
        case YoriWinEventKeyDown:

            //
            // This code is trying to handle the AltGr cases while not
            // handling pure right Alt which would normally be an accelerator.
            //

            if (Event->KeyDown.CtrlMask == 0 ||
                Event->KeyDown.CtrlMask == SHIFT_PRESSED ||
                Event->KeyDown.CtrlMask == (LEFT_CTRL_PRESSED | LEFT_ALT_PRESSED) ||
                Event->KeyDown.CtrlMask == (LEFT_CTRL_PRESSED | LEFT_ALT_PRESSED | SHIFT_PRESSED) ||
                Event->KeyDown.CtrlMask == (LEFT_CTRL_PRESSED | RIGHT_ALT_PRESSED) ||
                Event->KeyDown.CtrlMask == (LEFT_CTRL_PRESSED | RIGHT_ALT_PRESSED | SHIFT_PRESSED)) {


                if (!YoriWinHexEditProcessPossiblyEnhancedKey(HexEdit, Event)) {
                    if (Event->KeyDown.Char != '\0' &&
                        Event->KeyDown.Char != '\t' &&
                        Event->KeyDown.Char != '\r' &&
                        Event->KeyDown.Char != '\b' &&
                        Event->KeyDown.Char != '\x1b' &&
                        Event->KeyDown.Char != '\n') {

                        if (!HexEdit->ReadOnly) {
                            YoriWinHexEditClearSelection(HexEdit);
                            YoriWinHexEditAddChar(HexEdit, Event->KeyDown.Char);
                            YoriWinHexEditEnsureCursorVisible(HexEdit);
                            YoriWinHexEditPaint(HexEdit);
                            return TRUE;
                        }
                    }
                }
            } else if (Event->KeyDown.CtrlMask == LEFT_CTRL_PRESSED ||
                       Event->KeyDown.CtrlMask == RIGHT_CTRL_PRESSED) {

                YoriWinHexEditProcessPossiblyEnhancedCtrlKey(HexEdit, Event);
            } else if (Event->KeyDown.CtrlMask == LEFT_ALT_PRESSED ||
                       Event->KeyDown.CtrlMask == (LEFT_ALT_PRESSED | ENHANCED_KEY)) {
                YoriLibBuildNumericKey(&HexEdit->NumericKeyValue, &HexEdit->NumericKeyType, Event->KeyDown.VirtualKeyCode, Event->KeyDown.VirtualScanCode);

            } else if (Event->KeyDown.CtrlMask == ENHANCED_KEY ||
                       Event->KeyDown.CtrlMask == (ENHANCED_KEY | SHIFT_PRESSED)) {
                YoriWinHexEditProcessPossiblyEnhancedKey(HexEdit, Event);
            } else if (Event->KeyDown.CtrlMask == (ENHANCED_KEY | LEFT_CTRL_PRESSED) ||
                       Event->KeyDown.CtrlMask == (ENHANCED_KEY | RIGHT_CTRL_PRESSED) ||
                       Event->KeyDown.CtrlMask == (SHIFT_PRESSED | LEFT_CTRL_PRESSED) ||
                       Event->KeyDown.CtrlMask == (SHIFT_PRESSED | RIGHT_CTRL_PRESSED) ||
                       Event->KeyDown.CtrlMask == (ENHANCED_KEY | SHIFT_PRESSED | LEFT_CTRL_PRESSED) ||
                       Event->KeyDown.CtrlMask == (ENHANCED_KEY | SHIFT_PRESSED | RIGHT_CTRL_PRESSED)
                       ) {
                YoriWinHexEditProcessPossiblyEnhancedCtrlKey(HexEdit, Event);
            }
            break;

        case YoriWinEventKeyUp:
            if ((Event->KeyUp.CtrlMask & (RIGHT_ALT_PRESSED | LEFT_ALT_PRESSED)) == 0 &&
                !HexEdit->ReadOnly &&
                (HexEdit->NumericKeyValue != 0 ||
                 (Event->KeyUp.VirtualKeyCode == VK_MENU && Event->KeyUp.Char != 0))) {

                DWORD NumericKeyValue;
                TCHAR Char;

                NumericKeyValue = HexEdit->NumericKeyValue;
                if (NumericKeyValue == 0) {
                    HexEdit->NumericKeyType = YoriLibNumericKeyUnicode;
                    NumericKeyValue = Event->KeyUp.Char;
                }

                YoriLibTranslateNumericKeyToChar(NumericKeyValue, HexEdit->NumericKeyType, &Char);
                HexEdit->NumericKeyValue = 0;
                HexEdit->NumericKeyType = YoriLibNumericKeyAscii;

                YoriWinHexEditClearSelection(HexEdit);
                YoriWinHexEditAddChar(HexEdit, Event->KeyDown.Char);
                YoriWinHexEditEnsureCursorVisible(HexEdit);
                YoriWinHexEditPaint(HexEdit);
            }


            break;

        case YoriWinEventMouseWheelDownInClient:
        case YoriWinEventMouseWheelDownInNonClient:
            YoriWinHexEditNotifyMouseWheel(HexEdit, Event->MouseWheel.LinesToMove, FALSE);
            break;

        case YoriWinEventMouseWheelUpInClient:
        case YoriWinEventMouseWheelUpInNonClient:
            YoriWinHexEditNotifyMouseWheel(HexEdit, Event->MouseWheel.LinesToMove, TRUE);
            break;

        case YoriWinEventMouseDownInNonClient:
        case YoriWinEventMouseDoubleClickInNonClient:
            {
                PYORI_WIN_CTRL Child;
                COORD ChildLocation;
                BOOLEAN InChildClientArea;
                Child = YoriWinFindControlAtCoordinates(Ctrl,
                                                        Event->MouseDown.Location,
                                                        FALSE,
                                                        &ChildLocation,
                                                        &InChildClientArea);

                if (Child != NULL) {
                    if (YoriWinTranslateMouseEventForChild(Event, Child, ChildLocation, InChildClientArea)) {
                        return TRUE;
                    }
                    return FALSE;
                }
            }
            break;
        case YoriWinEventMouseDownInClient:
            YoriWinHexEditMouseDown(HexEdit, Event->MouseDown.Location.X, Event->MouseDown.Location.Y);
            break;
    }

    return FALSE;
}

/**
 Invoked when the user manipulates the scroll bar to indicate that the
 position within the hex edit should be updated.

 @param ScrollCtrlHandle Pointer to the scroll bar control.
 */
VOID
YoriWinHexEditNotifyScrollChange(
    __in PYORI_WIN_CTRL_HANDLE ScrollCtrlHandle
    )
{
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    DWORDLONG ScrollValue;
    COORD ClientSize;
    WORD ElementCountToDisplay;
    PYORI_WIN_CTRL ScrollCtrl;
    DWORD NewViewportTop;
    DWORD LinesPopulated;

    ScrollCtrl = (PYORI_WIN_CTRL)ScrollCtrlHandle;
    HexEdit = CONTAINING_RECORD(ScrollCtrl->Parent, YORI_WIN_CTRL_HEX_EDIT, Ctrl);
    ASSERT(HexEdit->VScrollCtrl == ScrollCtrl);

    YoriWinGetControlClientSize(&HexEdit->Ctrl, &ClientSize);
    ElementCountToDisplay = ClientSize.Y;
    NewViewportTop = HexEdit->ViewportTop;
    LinesPopulated = YoriWinHexEditLinesPopulated(HexEdit);

    ScrollValue = YoriWinScrollBarGetPosition(ScrollCtrl);
    ASSERT(ScrollValue <= LinesPopulated);
    if (ScrollValue + ElementCountToDisplay > LinesPopulated) {
        if (LinesPopulated >= ElementCountToDisplay) {
            NewViewportTop = LinesPopulated - ElementCountToDisplay;
        } else {
            NewViewportTop = 0;
        }
    } else {
        if (ScrollValue < LinesPopulated) {
            NewViewportTop = (DWORD)ScrollValue;
        }
    }

    if (NewViewportTop != HexEdit->ViewportTop) {
        HexEdit->ViewportTop = NewViewportTop;
        YoriWinHexEditExpandDirtyRange(HexEdit, NewViewportTop, (DWORD)-1);
    } else {
        return;
    }

    if (HexEdit->CursorLine < HexEdit->ViewportTop) {
        YoriWinHexEditSetCursorLocationInternal(HexEdit, HexEdit->CursorOffset, HexEdit->ViewportTop);
    } else if (HexEdit->CursorLine >= HexEdit->ViewportTop + ClientSize.Y) {
        YoriWinHexEditSetCursorLocationInternal(HexEdit, HexEdit->CursorOffset, HexEdit->ViewportTop + ClientSize.Y - 1);
    }

    YoriWinHexEditPaint(HexEdit);
}

/**
 Set the size and location of a hex edit control, and redraw the
 contents.

 @param CtrlHandle Pointer to the hex edit to resize or reposition.

 @param CtrlRect Specifies the new size and position of the hex edit.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinHexEditReposition(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __in PSMALL_RECT CtrlRect
    )
{
    PYORI_WIN_CTRL Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    if (!YoriWinControlReposition(Ctrl, CtrlRect)) {
        return FALSE;
    }

    if (HexEdit->VScrollCtrl != NULL) {
        SMALL_RECT ScrollBarRect;

        ScrollBarRect.Left = (SHORT)(HexEdit->Ctrl.FullRect.Right - HexEdit->Ctrl.FullRect.Left);
        ScrollBarRect.Right = ScrollBarRect.Left;
        ScrollBarRect.Top = 1;
        ScrollBarRect.Bottom = (SHORT)(HexEdit->Ctrl.FullRect.Bottom - HexEdit->Ctrl.FullRect.Top - 1);

        YoriWinScrollBarReposition(HexEdit->VScrollCtrl, &ScrollBarRect);
    }

    YoriWinHexEditExpandDirtyRange(HexEdit, 0, (DWORD)-1);
    YoriWinHexEditPaintNonClient(HexEdit);
    YoriWinHexEditPaint(HexEdit);

    return TRUE;
}

/**
 Change the read only state of an existing hex edit control.

 @param CtrlHandle Pointer to the hex edit control.

 @param NewReadOnlyState TRUE to indicate the hex edit control should be
        read only, FALSE if it should be writable.

 @return TRUE to indicate success, FALSE to indicate failure.
 */
BOOLEAN
YoriWinHexEditSetReadOnly(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __in BOOLEAN NewReadOnlyState
    )
{
    PYORI_WIN_CTRL Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);
    HexEdit->ReadOnly = NewReadOnlyState;

    return TRUE;
}

/**
 Set the cursor to a specific point, expressed in terms of a buffer offset
 and bit shift.  Bit shift is only meaningful when the cell type refers to
 hex digit, so a cursor has multiple positions per buffer offset.

 @param CtrlHandle Pointer to the hex edit control.

 @param AsChar If TRUE, the cursor should be set to the character
        representation of the offset.  If FALSE, the cursor is set to the
        hex representation of the offset.

 @param BufferOffset Specifies the offset within the buffer.

 @param BitShift Specifies the bits within the buffer offset.

 @return TRUE to indicate the cursor was moved, FALSE if it did not.
 */
__success(return)
BOOLEAN
YoriWinHexEditSetCursorLocation(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __in BOOLEAN AsChar,
    __in DWORDLONG BufferOffset,
    __in DWORD BitShift
    )
{
    PYORI_WIN_CTRL Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    YORI_WIN_HEX_EDIT_CELL_TYPE CellType;

    Ctrl = (PYORI_WIN_CTRL)CtrlHandle;
    HexEdit = CONTAINING_RECORD(Ctrl, YORI_WIN_CTRL_HEX_EDIT, Ctrl);

    CellType = YoriWinHexEditCellTypeHexDigit;
    if (AsChar) {
        CellType = YoriWinHexEditCellTypeCharValue;
    }
    return YoriWinHexEditSetCursorToBufferLocation(HexEdit, CellType, BufferOffset, BitShift);
}

/**
 Set the range of selected bytes to an explicitly provided range.

 @param CtrlHandle Pointer to the hex edit control.

 @param FirstByteOffset The first byte to include in the selected range.

 @param LastByteOffset The last byte to include in the selected range.
 */
__success(return)
BOOLEAN
YoriWinHexEditSetSelectionRange(
    __in PYORI_WIN_CTRL_HANDLE CtrlHandle,
    __in DWORDLONG FirstByteOffset,
    __in DWORDLONG LastByteOffset
    )
{
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    DWORD FirstDirtyLine;
    DWORD LastDirtyLine;

    HexEdit = (PYORI_WIN_CTRL_HEX_EDIT)CtrlHandle;

    //
    //  Clear the previous selection, which is really a way to update the
    //  repaint region to redraw where it was if it existed
    //

    YoriWinHexEditClearSelection(HexEdit);

    HexEdit->Selection.Active = YoriWinHexEditSelectActive;

    if (FirstByteOffset >= HexEdit->BufferValid ||
        LastByteOffset >= HexEdit->BufferValid) {

        return FALSE;
    }

    HexEdit->Selection.FirstByteOffset = FirstByteOffset;
    HexEdit->Selection.LastByteOffset = LastByteOffset;

    FirstDirtyLine = (DWORD)(HexEdit->Selection.FirstByteOffset / HexEdit->BytesPerLine);
    LastDirtyLine = (DWORD)(HexEdit->Selection.LastByteOffset / HexEdit->BytesPerLine);

    YoriWinHexEditExpandDirtyRange(HexEdit, FirstDirtyLine, LastDirtyLine);

    YoriWinHexEditPaint(HexEdit);
    return TRUE;
}


/**
 Create a hex edit control and add it to a window.  This is destroyed
 when the window is destroyed.

 @param ParentHandle Pointer to the parent window.

 @param Caption Optionally points to the initial caption to display on the top
        of the hex edit control.  If not supplied, no caption is
        displayed.

 @param Size Specifies the location and size of the hex edit.

 @param BytesPerWord The number of bytes per word.  Valid values are 1,
        2, 4, and 8.

 @param Style Specifies style flags for the hex edit.

 @return Pointer to the newly created control or NULL on failure.
 */
PYORI_WIN_CTRL_HANDLE
YoriWinHexEditCreate(
    __in PYORI_WIN_WINDOW_HANDLE ParentHandle,
    __in_opt PYORI_STRING Caption,
    __in PSMALL_RECT Size,
    __in DWORD BytesPerWord,
    __in DWORD Style
    )
{
    PYORI_WIN_CTRL_HEX_EDIT HexEdit;
    PYORI_WIN_WINDOW Parent;
    SMALL_RECT ScrollBarRect;
    PYORI_WIN_WINDOW_HANDLE TopLevelWindow;
    PYORI_WIN_WINDOW_MANAGER_HANDLE WinMgrHandle;

    if ((Style & (YORI_WIN_HEX_EDIT_STYLE_OFFSET | YORI_WIN_HEX_EDIT_STYLE_LARGE_OFFSET)) == (YORI_WIN_HEX_EDIT_STYLE_OFFSET | YORI_WIN_HEX_EDIT_STYLE_LARGE_OFFSET)) {
        return NULL;
    }

    if (BytesPerWord != 1 &&
        BytesPerWord != 2 &&
        BytesPerWord != 4 &&
        BytesPerWord != 8) {

        return NULL;
    }

    Parent = (PYORI_WIN_WINDOW)ParentHandle;

    HexEdit = YoriLibReferencedMalloc(sizeof(YORI_WIN_CTRL_HEX_EDIT));
    if (HexEdit == NULL) {
        return NULL;
    }

    ZeroMemory(HexEdit, sizeof(YORI_WIN_CTRL_HEX_EDIT));

    HexEdit->Ctrl.NotifyEventFn = YoriWinHexEditEventHandler;
    if (!YoriWinCreateControl(Parent, Size, TRUE, TRUE, &HexEdit->Ctrl)) {
        YoriLibDereference(HexEdit);
        return NULL;
    }

    if (Caption != NULL && Caption->LengthInChars > 0) {
        if (!YoriLibCopyString(&HexEdit->Caption, Caption)) {
            YoriWinDestroyControl(&HexEdit->Ctrl);
            YoriLibDereference(HexEdit);
            return NULL;
        }
    }

    if (Style & YORI_WIN_HEX_EDIT_STYLE_VSCROLLBAR) {

        ScrollBarRect.Left = (SHORT)(HexEdit->Ctrl.FullRect.Right - HexEdit->Ctrl.FullRect.Left);
        ScrollBarRect.Right = ScrollBarRect.Left;
        ScrollBarRect.Top = 1;
        ScrollBarRect.Bottom = (SHORT)(HexEdit->Ctrl.FullRect.Bottom - HexEdit->Ctrl.FullRect.Top - 1);
        HexEdit->VScrollCtrl = YoriWinScrollBarCreate(&HexEdit->Ctrl, &ScrollBarRect, 0, YoriWinHexEditNotifyScrollChange);
    }

    if (Style & YORI_WIN_HEX_EDIT_STYLE_READ_ONLY) {
        HexEdit->ReadOnly = TRUE;
    }

    HexEdit->OffsetWidth = 0;
    if (Style & YORI_WIN_HEX_EDIT_STYLE_OFFSET) {
        HexEdit->OffsetWidth = 32;
    } else if (Style & YORI_WIN_HEX_EDIT_STYLE_LARGE_OFFSET) {
        HexEdit->OffsetWidth = 64;
    }

    HexEdit->Ctrl.ClientRect.Top++;
    HexEdit->Ctrl.ClientRect.Left++;
    HexEdit->Ctrl.ClientRect.Bottom--;
    HexEdit->Ctrl.ClientRect.Right--;

    HexEdit->BytesPerLine = YORI_LIB_HEXDUMP_BYTES_PER_LINE;
    HexEdit->BytesPerWord = BytesPerWord;
    HexEdit->InsertMode = FALSE;
    HexEdit->TextAttributes = HexEdit->Ctrl.DefaultAttributes;
    TopLevelWindow = YoriWinGetTopLevelWindow(Parent);
    WinMgrHandle = YoriWinGetWindowManagerHandle(TopLevelWindow);
    HexEdit->SelectedAttributes = YoriWinMgrDefaultColorLookup(WinMgrHandle, YoriWinColorEditSelectedText);
    HexEdit->CaptionAttributes = YoriWinMgrDefaultColorLookup(WinMgrHandle, YoriWinColorMultilineCaption);

    YoriWinHexEditSetCursorLocationToZero(HexEdit);

    YoriWinHexEditExpandDirtyRange(HexEdit, 0, (DWORD)-1);
    YoriWinHexEditPaintNonClient(HexEdit);
    YoriWinHexEditPaint(HexEdit);

    return &HexEdit->Ctrl;
}


// vim:sw=4:ts=4:et:
