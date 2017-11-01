/********************************************************
*                                                       *
*   Copyright (C) Microsoft. All rights reserved.       *
*                                                       *
********************************************************/

#include "precomp.h"

#include "vtrenderer.hpp"
#include "../../inc/conattrs.hpp"
#include "../../types/inc/convert.hpp"

#pragma hdrstop
using namespace Microsoft::Console::Render;

// Routine Description:
// - Prepares internal structures for a painting operation.
// Arguments:
// - <none>
// Return Value:
// - S_OK if we started to paint. S_FALSE if we didn't need to paint. 
//      HRESULT error code if painting didn't start successfully.
HRESULT VtEngine::StartPaint()
{
    // If there's nothing to do, quick return
    bool somethingToDo = _fInvalidRectUsed || 
                         (_scrollDelta.X != 0 || _scrollDelta.Y != 0) ||
                         _cursor.HasMoved();

    _quickReturn = !somethingToDo;

    return _quickReturn ? S_FALSE : S_OK;
}

// Routine Description:
// - EndPaint helper to perform the final cleanup after painting. If we 
//      returned S_FALSE from StartPaint, there's no guarantee this was called. 
//      That's okay however, EndPaint only zeros structs that would be zero if
//      StartPaint returns S_FALSE.
// Arguments:
// - <none>
// Return Value:
// - S_OK, else an appropriate HRESULT for failing to allocate or write.
HRESULT VtEngine::EndPaint()
{
    _srcInvalid = { 0 };
    _fInvalidRectUsed = false;
    _scrollDelta = {0};
    _cursor.ClearMoved();
    
    return S_OK;
}

// Routine Description:
// - Paints the background of the invalid area of the frame.
// Arguments:
// - <none>
// Return Value:
// - S_OK
HRESULT VtEngine::PaintBackground()
{
    return S_OK;
}

// Routine Description:
// - Draws one line of the buffer to the screen. Writes the characters to the 
//      pipe. If the characters are outside the ASCII range (0-0x7f), then 
//      instead writes a '?'
// Arguments:
// - pwsLine - string of text to be written
// - rgWidths - array specifying how many column widths that the console is 
//      expecting each character to take
// - cchLine - length of line to be read
// - coord - character coordinate target to render within viewport
// - fTrimLeft - This specifies whether to trim one character width off the left
//      side of the output. Used for drawing the right-half only of a 
//      double-wide character.
// Return Value:
// - S_OK or suitable HRESULT error from writing pipe.
HRESULT VtEngine::PaintBufferLine(_In_reads_(cchLine) PCWCHAR const pwsLine,
                                  _In_reads_(cchLine) const unsigned char* const rgWidths,
                                  _In_ size_t const cchLine,
                                  _In_ COORD const coord,
                                  _In_ bool const /*fTrimLeft*/)
{
    return VtEngine::_PaintAsciiBufferLine(pwsLine, rgWidths, cchLine, coord);
}

// Method Description:
// - Draws up to one line worth of grid lines on top of characters.
// Arguments:
// - lines - Enum defining which edges of the rectangle to draw
// - color - The color to use for drawing the edges.
// - cchLine - How many characters we should draw the grid lines along (left to right in a row)
// - coordTarget - The starting X/Y position of the first character to draw on.
// Return Value:
// - S_OK
HRESULT VtEngine::PaintBufferGridLines(_In_ GridLines const /*lines*/,
                                       _In_ COLORREF const /*color*/,
                                       _In_ size_t const /*cchLine*/,
                                       _In_ COORD const /*coordTarget*/)
{
    return S_OK;
}

// Routine Description:
// - Draws the cursor on the screen
// Arguments:
// - ulHeightPercent - The cursor will be drawn at this percentage of the 
//      current font height.
// - fIsDoubleWidth - The cursor should be drawn twice as wide as usual.
// Return Value:
// - S_OK or suitable HRESULT error from writing pipe.
HRESULT VtEngine::PaintCursor(_In_ ULONG const /*ulHeightPercent*/,
                              _In_ bool const /*fIsDoubleWidth*/)
{
    COORD const coord = _cursor.GetPosition();
    _MoveCursor(coord);

    return S_OK;
}

// Routine Description:
// - Clears out the cursor that was set in the previous PaintCursor call.
//      VT doesn't need to do anything to "unpaint" the old cursor location.
// Arguments:
// - <none>
// Return Value:
// - S_OK
HRESULT VtEngine::ClearCursor()
{
    return S_OK;
}

// Routine Description:
//  - Inverts the selected region on the current screen buffer.
//  - Reads the selected area, selection mode, and active screen buffer
//    from the global properties and dispatches a GDI invert on the selected text area.
//  Because the selection is the responsibility of the terminal, and not the 
//      host, render nothing.
// Arguments:
//  - rgsrSelection - Array of rectangles, one per line, that should be inverted to make the selection area
//  - cRectangles - Count of rectangle array length
// Return Value:
// - S_OK
HRESULT VtEngine::PaintSelection(_In_reads_(cRectangles) const SMALL_RECT* const /*rgsrSelection*/,
                                 _In_ UINT const /*cRectangles*/)
{
    return S_OK;
}

// Routine Description:
// - Write a VT sequence to change the current colors of text. Writes true RGB 
//      color sequences.
// Arguments:
// - colorForeground: The RGB Color to use to paint the foreground text.
// - colorBackground: The RGB Color to use to paint the background of the text.
// Return Value:
// - S_OK if we succeeded, else an appropriate HRESULT for failing to allocate or write.
HRESULT VtEngine::_RgbUpdateDrawingBrushes(_In_ COLORREF const colorForeground,
                                           _In_ COLORREF const colorBackground)
{
    if (colorForeground != _LastFG)
    {
        RETURN_IF_FAILED(_SetGraphicsRenditionRGBColor(colorForeground, true));
        _LastFG = colorForeground;
        
    }

    if (colorBackground != _LastBG) 
    {
        RETURN_IF_FAILED(_SetGraphicsRenditionRGBColor(colorBackground, false));
        _LastBG = colorBackground;
    }
    
    return S_OK;
}

// Routine Description:
// - Write a VT sequence to change the current colors of text. It will try to 
//      find the colors in the color table that are nearest to the input colors,
//       and write those indicies to the pipe.
// Arguments:
// - colorForeground: The RGB Color to use to paint the foreground text.
// - colorBackground: The RGB Color to use to paint the background of the text.
// - ColorTable: An array of colors to find the closest match to.
// - cColorTable: size of the color table.
// Return Value:
// - S_OK if we succeeded, else an appropriate HRESULT for failing to allocate or write.
HRESULT VtEngine::_16ColorUpdateDrawingBrushes(_In_ COLORREF const colorForeground,
                                               _In_ COLORREF const colorBackground,
                                               _In_reads_(cColorTable) const COLORREF* const ColorTable,
                                               _In_ const WORD cColorTable)
{
    if (colorForeground != _LastFG)
    {
        const WORD wNearestFg = ::FindNearestTableIndex(colorForeground, ColorTable, cColorTable);
        RETURN_IF_FAILED(_SetGraphicsRendition16Color(wNearestFg, true));
        _LastFG = colorForeground;
    }

    if (colorBackground != _LastBG) 
    {
        const WORD wNearestBg = ::FindNearestTableIndex(colorBackground, ColorTable, cColorTable);
        RETURN_IF_FAILED(_SetGraphicsRendition16Color(wNearestBg, false));
        _LastBG = colorBackground;
    }

    return S_OK;
}

// Routine Description:
// - Draws one line of the buffer to the screen. Writes the characters to the 
//      pipe. If the characters are outside the ASCII range (0-0x7f), then 
//      instead writes a '?'.
//   This is needed because the Windows internal telnet client implementation
//      doesn't know how to handle >ASCII characters. The old telnetd would 
//      just replace them with '?' characters. If we render the >ASCII 
//      characters to telnet, it will likely end up drawing them wrong, which 
//      will make the client appear buggy and broken.
// Arguments:
// - pwsLine - string of text to be written
// - rgWidths - array specifying how many column widths that the console is 
//      expecting each character to take
// - cchLine - length of line to be read
// - coord - character coordinate target to render within viewport
// Return Value:
// - S_OK or suitable HRESULT error from writing pipe.
HRESULT VtEngine::_PaintAsciiBufferLine(_In_reads_(cchLine) PCWCHAR const pwsLine,
                                        _In_reads_(cchLine) const unsigned char* const rgWidths,
                                        _In_ size_t const cchLine,
                                        _In_ COORD const coord)
{
    RETURN_IF_FAILED(_MoveCursor(coord));

    short totalWidth = 0;
    for (size_t i = 0; i < cchLine; i++)
    {
        RETURN_IF_FAILED(ShortAdd(totalWidth, static_cast<short>(rgWidths[i]), &totalWidth));
    }
    const size_t actualCch = cchLine;

    wistd::unique_ptr<char[]> rgchNeeded = wil::make_unique_nothrow<char[]>(actualCch + 1);
    RETURN_IF_NULL_ALLOC(rgchNeeded);
    
    char* nextChar = &rgchNeeded[0];
    for (size_t i = 0; i < cchLine; i++)
    {
        if (pwsLine[i] > L'\x7f')
        {
            *nextChar = '?';
            nextChar++;
        }
        else
        {
            // Mask off the non-ascii bits
            *nextChar = static_cast<char>(pwsLine[i] & 0x7f);
            nextChar++;
        }
    }

    rgchNeeded[actualCch] = '\0';

    RETURN_IF_FAILED(_Write(rgchNeeded.get(), actualCch));
    
    // Update our internal tracker of the cursor's position
    _lastText.X += totalWidth;

    return S_OK;
}

// Routine Description:
// - Draws one line of the buffer to the screen. Writes the characters to the 
//      pipe, encoded in UTF-8.
// Arguments:
// - pwsLine - string of text to be written
// - rgWidths - array specifying how many column widths that the console is 
//      expecting each character to take
// - cchLine - length of line to be read
// - coord - character coordinate target to render within viewport
// Return Value:
// - S_OK or suitable HRESULT error from writing pipe.
HRESULT VtEngine::_PaintUtf8BufferLine(_In_reads_(cchLine) PCWCHAR const pwsLine,
                                       _In_reads_(cchLine) const unsigned char* const rgWidths,
                                       _In_ size_t const cchLine,
                                       _In_ COORD const coord)
{
    RETURN_IF_FAILED(_MoveCursor(coord));

    // TODO: MSFT:14099536 Try and optimize the spaces.
    const size_t actualCch = cchLine;
    
    wistd::unique_ptr<char[]> rgchNeeded;
    size_t needed = 0;
    RETURN_IF_FAILED(ConvertToA(CP_UTF8, pwsLine, cchLine, rgchNeeded, needed));

    RETURN_IF_FAILED(_Write(rgchNeeded.get(), needed));
    
    // Update our internal tracker of the cursor's position
    short totalWidth = 0;
    for (size_t i = 0; i < cchLine; i++)
    {
        RETURN_IF_FAILED(ShortAdd(totalWidth, static_cast<short>(rgWidths[i]), &totalWidth));
    }
    _lastText.X += totalWidth;


    return S_OK;
}
