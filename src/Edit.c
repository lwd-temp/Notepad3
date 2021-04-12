// encoding: UTF-8
/******************************************************************************
*                                                                             *
*                                                                             *
* Notepad3                                                                    *
*                                                                             *
* Edit.c                                                                      *
*   Text File Editing Helper Stuff                                            *
*   Based on code from Notepad2, (c) Florian Balmer 1996-2011                 *
*                                                                             *
*                                                  (c) Rizonesoft 2008-2021   *
*                                                    https://rizonesoft.com   *
*                                                                             *
*                                                                             *
*******************************************************************************/

#include "Helpers.h"

#include <shlwapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <shellapi.h>
#include <time.h>

#include "Styles.h"
#include "Dialogs.h"
#include "crypto/crypto.h"
#include "uthash/utarray.h"
#include "uthash/utlist.h"
//#include "uthash/utstring.h"
#include "tinyexpr/tinyexpr.h"
#include "Encoding.h"
#include "MuiLanguage.h"
#include "Notepad3.h"
#include "Config/Config.h"
#include "DarkMode/DarkMode.h"

#include "SciCall.h"
//#include "SciLexer.h"

#include "Edit.h"


#ifndef LCMAP_TITLECASE
#define LCMAP_TITLECASE  0x00000300  // Title Case Letters bit mask
#endif

static bool s_bSwitchedFindReplace = false;

static int s_xFindReplaceDlgSave;
static int s_yFindReplaceDlgSave;

static char DelimChars[ANSI_CHAR_BUFFER] = { '\0' };
static char DelimCharsAccel[ANSI_CHAR_BUFFER] = { '\0' };
static char WordCharsDefault[ANSI_CHAR_BUFFER] = { '\0' };
static char WhiteSpaceCharsDefault[ANSI_CHAR_BUFFER] = { '\0' };
static char PunctuationCharsDefault[ANSI_CHAR_BUFFER] = { '\0' };
static char WordCharsAccelerated[ANSI_CHAR_BUFFER] = { '\0' };
static char WhiteSpaceCharsAccelerated[ANSI_CHAR_BUFFER] = { '\0' };
static char PunctuationCharsAccelerated[1] = { '\0' }; // empty!

static WCHAR W_DelimChars[ANSI_CHAR_BUFFER] = { L'\0' };
static WCHAR W_DelimCharsAccel[ANSI_CHAR_BUFFER] = { L'\0' };
static WCHAR W_WhiteSpaceCharsDefault[ANSI_CHAR_BUFFER] = { L'\0' };
static WCHAR W_WhiteSpaceCharsAccelerated[ANSI_CHAR_BUFFER] = { L'\0' };

static char AutoCompleteFillUpChars[64] = { '\0' };
static bool s_ACFillUpCharsHaveNewLn = false;

// Default Codepage and Character Set
#define W_AUTOC_WORD_ANSI1252 L"#$%&@0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyzÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõöøùúûüýþÿ"
static char AutoCompleteWordCharSet[ANSI_CHAR_BUFFER] = { L'\0' };

// Is the character a white space char?
#define IsWhiteSpace(ch)  StrChrA(WhiteSpaceCharsDefault, (ch))
#define IsAccelWhiteSpace(ch)  StrChrA(WhiteSpaceCharsAccelerated, (ch))
#define IsWhiteSpaceW(wch)  StrChrW(W_WhiteSpaceCharsDefault, (wch))
#define IsAccelWhiteSpaceW(wch)  StrChrW(W_WhiteSpaceCharsAccelerated, (wch))


static const char *const s_pColorRegEx = "#([0-9a-fA-F]){8}|#([0-9a-fA-F]){6}"; // ARGB, RGBA, RGB
static const char *const s_pColorRegEx_A = "#([0-9a-fA-F]){8}";                 // no RGB search (BGRA)

static const char *const s_pUnicodeRegEx = "(\\\\[uU|xX]([0-9a-fA-F]){4}|\\\\[xX]([0-9a-fA-F]){2})+";

// https://mathiasbynens.be/demo/url-regex : @stephenhay
//static const char* s_pUrlRegEx = "\\b(?:(?:https?|ftp|file)://|www\\.|ftp\\.)[^\\s/$.?#].[^\\s]*";

// using  Gruber's  Liberal Regex Pattern for All URLs (https://gist.github.com/gruber/249502)
/// => unfortunately to slow to use as scanner
//static const char *const s_pUrlRegEx = "(?i)\\b((?:[a-z][\\w-]+:(?:/{1,3}|[a-z0-9%])|www\\d{0,3}[.]|[a-z0-9.\\-]+[.][a-z]{2,4}/)"
//                                       "(?:[^\\s()<>]+|\\(([^\\s()<>]+|(\\([^\\s()<>]+\\)))*\\))+"
//                                       "(?:\\(([^\\s()<>]+|(\\([^\\s()<>]+\\)))*\\)|[^\\s`!()\\[\\]{};:'\".,<>?«»“”‘’]))";

// --- pretty fast ---
// https://www.regular-expressions.info/unicode.html
// \p{L} :  any kind of letter from any language
// \p{N} :  any kind of numeric character in any script
// \p{S} :  math symbols, currency signs, dingbats, box-drawing characters, etc.

//#define HYPLNK_REGEX_VALID_CDPT "a-zA-Z0-9\\u00A0-\\uD7FF\\uF900-\\uFDCF\\uFDF0-\\uFFEF+&@#/%=~_|$"

#define HYPLNK_REGEX_VALID_CDPT "\\p{L}\\p{N}\\p{S}+&@#/%=~_|$"

static const char *const s_pUrlRegEx = "\\b(?:(?:https?|ftp|file)://|www\\.|ftp\\.)"
                                       "(?:\\([-" HYPLNK_REGEX_VALID_CDPT "?!:,.]*\\)|[-" HYPLNK_REGEX_VALID_CDPT "?!:,.])*"
                                       "(?:\\([-" HYPLNK_REGEX_VALID_CDPT "?!:,.]*\\)|[" HYPLNK_REGEX_VALID_CDPT "])";

// ----------------------------------------------------------------------------

enum AlignMask {
    ALIGN_LEFT = 0,
    ALIGN_RIGHT = 1,
    ALIGN_CENTER = 2,
    ALIGN_JUSTIFY = 3,
    ALIGN_JUSTIFY_EX = 4
};

enum SortOrderMask {
    SORT_ASCENDING   = 0x001,
    SORT_DESCENDING  = 0x002,
    SORT_SHUFFLE     = 0x004,
    SORT_MERGEDUP    = 0x008,
    SORT_UNIQDUP     = 0x010,
    SORT_UNIQUNIQ    = 0x020,
    SORT_REMZEROLEN  = 0x040,
    SORT_REMWSPACELN = 0x080,
    SORT_NOCASE      = 0x100,
    SORT_LOGICAL     = 0x200,
    SORT_LEXICOGRAPH = 0x400,
    SORT_COLUMN      = 0x800
};


//=============================================================================
//
//  Delay Message Queue Handling  (TODO: MultiThreading)
//

static CmdMessageQueue_t* MessageQueue = NULL;

// ----------------------------------------------------------------------------

static int msgcmp(void* mqc1, void* mqc2)
{
    const CmdMessageQueue_t* const pMQC1 = (CmdMessageQueue_t*)mqc1;
    const CmdMessageQueue_t* const pMQC2 = (CmdMessageQueue_t*)mqc2;

    if ((pMQC1->cmd == pMQC2->cmd) 
        && (pMQC1->hwnd == pMQC2->hwnd) 
        && (pMQC1->wparam == pMQC2->wparam) 
        && (pMQC1->lparam == pMQC2->lparam)
       ) {
        return 0;
    }
    return 1;
}

static int sortcmp(void *mqc1, void *mqc2) {

    const CmdMessageQueue_t *const pMQC1 = (CmdMessageQueue_t *)mqc1;
    const CmdMessageQueue_t *const pMQC2 = (CmdMessageQueue_t *)mqc2;

    return (pMQC1->delay - pMQC2->delay);
}
// ----------------------------------------------------------------------------


#define _MQ_TIMER_CYCLE (USER_TIMER_MINIMUM << 1)
#define _MQ_ms2cycl(T) (((T) + USER_TIMER_MINIMUM) / _MQ_TIMER_CYCLE)
#define _MQ_STD (_MQ_TIMER_CYCLE << 2)

static void  _MQ_AppendCmd(CmdMessageQueue_t* const pMsgQCmd, int cycles)
{
    if (!pMsgQCmd) { return; }

    cycles = clampi(cycles, 0, _MQ_ms2cycl(60000));

    CmdMessageQueue_t* pmqc = NULL;
    DL_SEARCH(MessageQueue, pmqc, pMsgQCmd, msgcmp);

    if (!pmqc) { // NOT found, create new one
        pmqc = AllocMem(sizeof(CmdMessageQueue_t), HEAP_ZERO_MEMORY);
        if (pmqc) {
            *pmqc = *pMsgQCmd;
            pmqc->delay = cycles;
            DL_APPEND(MessageQueue, pmqc);
        }
    } else {
        if ((pmqc->delay > 0) && (cycles > 0)) {
            pmqc->delay = (pmqc->delay + cycles) >> 1; // median delay
        } else {
            pmqc->delay = cycles;
        }
    }
    if (0 == cycles) {
        PostMessage(pMsgQCmd->hwnd, pMsgQCmd->cmd, pMsgQCmd->wparam, pMsgQCmd->lparam);
    }
    //~DL_SORT(MessageQueue, sortcmp); // next scheduled first
}
// ----------------------------------------------------------------------------

/* not used yet
static void _MQ_DropAll() {
    CmdMessageQueue_t *pmqc = NULL;
    DL_FOREACH(MessageQueue, pmqc) {
        pmqc->delay = -1;
    }
}
*/
// ----------------------------------------------------------------------------


// ----------------------------------------------------------------------------
//
// called by MarkAll Timer
//
static void CALLBACK MQ_ExecuteNext(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    UNREFERENCED_PARAMETER(hwnd);    // must be main wnd
    UNREFERENCED_PARAMETER(uMsg);    // must be WM_TIMER
    UNREFERENCED_PARAMETER(idEvent); // must be pTimerIdentifier
    UNREFERENCED_PARAMETER(dwTime);  // This is the value returned by the GetTickCount function

    CmdMessageQueue_t *pmqc;
    DL_FOREACH(MessageQueue, pmqc) {
        if (pmqc->delay >= 0) {
            --(pmqc->delay);
        }
        if (pmqc->delay == 0) {
            if (IsWindow(pmqc->hwnd)) {
                PostMessage(pmqc->hwnd, pmqc->cmd, pmqc->wparam, pmqc->lparam);
            }
        }
    }
}


//=============================================================================
//
//  EditReplaceSelection()
//
void EditReplaceSelection(const char* text, bool bForceSel)
{
    _BEGIN_UNDO_ACTION_;
    bool const bSelWasEmpty = SciCall_IsSelectionEmpty();
    DocPos const posSelBeg = SciCall_GetSelectionStart();
    SciCall_ReplaceSel(text);
    if (bForceSel || !bSelWasEmpty) {
        SciCall_SetSel(posSelBeg, SciCall_GetCurrentPos());
    }
    _END_UNDO_ACTION_;
}


//=============================================================================
//
//  EditSetWordDelimiter()
//
void EditInitWordDelimiter(HWND hwnd)
{
    UNREFERENCED_PARAMETER(hwnd);

    ZeroMemory(WordCharsDefault, COUNTOF(WordCharsDefault));
    ZeroMemory(WhiteSpaceCharsDefault, COUNTOF(WhiteSpaceCharsDefault));
    ZeroMemory(PunctuationCharsDefault, COUNTOF(PunctuationCharsDefault));
    ZeroMemory(WordCharsAccelerated, COUNTOF(WordCharsAccelerated));
    ZeroMemory(WhiteSpaceCharsAccelerated, COUNTOF(WhiteSpaceCharsAccelerated));
    //ZeroMemory(PunctuationCharsAccelerated, COUNTOF(PunctuationCharsAccelerated)); // empty!

    // 1st get/set defaults
    SciCall_GetWordChars(WordCharsDefault);
    SciCall_GetWhiteSpaceChars(WhiteSpaceCharsDefault);
    SciCall_GetPunctuationChars(PunctuationCharsDefault);

    // default word delimiter chars are whitespace & punctuation & line ends
    const char* lineEnds = "\r\n";
    StringCchCopyA(DelimChars, COUNTOF(DelimChars), WhiteSpaceCharsDefault);
    StringCchCatA(DelimChars, COUNTOF(DelimChars), PunctuationCharsDefault);
    StringCchCatA(DelimChars, COUNTOF(DelimChars), lineEnds);

    // 2nd get user settings

    char whitesp[ANSI_CHAR_BUFFER*2] = { '\0' };
    if (StrIsNotEmpty(Settings2.ExtendedWhiteSpaceChars)) {
        WideCharToMultiByte(Encoding_SciCP, 0, Settings2.ExtendedWhiteSpaceChars, -1, whitesp, (int)COUNTOF(whitesp), NULL, NULL);
    }

    // 3rd set accelerated arrays

    // init with default
    StringCchCopyA(WhiteSpaceCharsAccelerated, COUNTOF(WhiteSpaceCharsAccelerated), WhiteSpaceCharsDefault);

    // add only 7-bit-ASCII chars to accelerated whitespace list
    size_t const wsplen = StringCchLenA(whitesp, ANSI_CHAR_BUFFER);
    for (size_t i = 0; i < wsplen; i++) {
        if (whitesp[i] & 0x7F) {
            if (!StrChrA(WhiteSpaceCharsAccelerated, whitesp[i])) {
                StringCchCatNA(WhiteSpaceCharsAccelerated, COUNTOF(WhiteSpaceCharsAccelerated), &(whitesp[i]), 1);
            }
        }
    }

    // construct word char array
    StringCchCopyA(WordCharsAccelerated, COUNTOF(WordCharsAccelerated), WordCharsDefault); // init
    // add punctuation chars not listed in white-space array
    size_t const pcdlen = StringCchLenA(PunctuationCharsDefault, ANSI_CHAR_BUFFER);
    for (size_t i = 0; i < pcdlen; i++) {
        if (!StrChrA(WhiteSpaceCharsAccelerated, PunctuationCharsDefault[i])) {
            StringCchCatNA(WordCharsAccelerated, COUNTOF(WordCharsAccelerated), &(PunctuationCharsDefault[i]), 1);
        }
    }

    // construct accelerated delimiters
    StringCchCopyA(DelimCharsAccel, COUNTOF(DelimCharsAccel), WhiteSpaceCharsDefault);
    StringCchCatA(DelimCharsAccel, COUNTOF(DelimCharsAccel), lineEnds);

    if (StrIsNotEmpty(Settings2.AutoCompleteFillUpChars)) {
        WideCharToMultiByte(Encoding_SciCP, 0, Settings2.AutoCompleteFillUpChars, -1, AutoCompleteFillUpChars, (int)COUNTOF(AutoCompleteFillUpChars), NULL, NULL);
        UnSlashA(AutoCompleteFillUpChars, Encoding_SciCP);

        s_ACFillUpCharsHaveNewLn = false;
        int i = 0;
        while (AutoCompleteFillUpChars[i]) {
            if ((AutoCompleteFillUpChars[i] == '\r') || (AutoCompleteFillUpChars[i] == '\n')) {
                s_ACFillUpCharsHaveNewLn = true;
                break;
            }
            ++i;
        }
    }

    if (StrIsNotEmpty(Settings2.AutoCompleteWordCharSet)) {
        WideCharToMultiByte(Encoding_SciCP, 0, Settings2.AutoCompleteWordCharSet, -1, AutoCompleteWordCharSet, (int)COUNTOF(AutoCompleteWordCharSet), NULL, NULL);
        Globals.bUseLimitedAutoCCharSet = true;
    } else {
        WideCharToMultiByte(Encoding_SciCP, 0, W_AUTOC_WORD_ANSI1252, -1, AutoCompleteWordCharSet, (int)COUNTOF(AutoCompleteWordCharSet), NULL, NULL);
        Globals.bUseLimitedAutoCCharSet = false;
    }

    // construct wide char arrays
    MultiByteToWideChar(Encoding_SciCP, 0, DelimChars, -1, W_DelimChars, (int)COUNTOF(W_DelimChars));
    MultiByteToWideChar(Encoding_SciCP, 0, DelimCharsAccel, -1, W_DelimCharsAccel, (int)COUNTOF(W_DelimCharsAccel));
    MultiByteToWideChar(Encoding_SciCP, 0, WhiteSpaceCharsDefault, -1, W_WhiteSpaceCharsDefault, (int)COUNTOF(W_WhiteSpaceCharsDefault));
    MultiByteToWideChar(Encoding_SciCP, 0, WhiteSpaceCharsAccelerated, -1, W_WhiteSpaceCharsAccelerated, (int)COUNTOF(W_WhiteSpaceCharsAccelerated));
}


//=============================================================================
//
//  EditSetNewText()
//
extern bool s_bFreezeAppTitle;

void EditSetNewText(HWND hwnd, const char* lpstrText, DocPosU lenText, bool bClearUndoHistory)
{
    if (!lpstrText) {
        lenText = 0;
    }

    s_bFreezeAppTitle = true;

    // clear markers, flags and positions
    if (FocusedView.HideNonMatchedLines) {
        EditToggleView(hwnd);
    }
    if (bClearUndoHistory) {
        UndoRedoRecordingStop();
    }
    _IGNORE_NOTIFY_CHANGE_;
    SciCall_Cancel();
    if (SciCall_GetReadOnly()) {
        SciCall_SetReadOnly(false);
    }
    EditClearAllBookMarks(hwnd);
    EditClearAllOccurrenceMarkers(hwnd);
    SciCall_SetScrollWidth(1);
    SciCall_SetXOffset(0);
    _OBSERVE_NOTIFY_CHANGE_;

    FileVars_Apply(&Globals.fvCurFile);

    _IGNORE_NOTIFY_CHANGE_;
    EditSetDocumentBuffer(lpstrText, lenText);
    _OBSERVE_NOTIFY_CHANGE_;

    Sci_GotoPosChooseCaret(0);

    if (bClearUndoHistory) {
        UndoRedoRecordingStart();
    }

    s_bFreezeAppTitle = false;
}



//=============================================================================
//
//  EditConvertText()
//
bool EditConvertText(HWND hwnd, cpi_enc_t encSource, cpi_enc_t encDest)
{
    if ((encSource == encDest) || (Encoding_SciCP == encDest)) {
        return false;
    }
    if (!(Encoding_IsValid(encSource) && Encoding_IsValid(encDest))) {
        return false;
    }

    DocPos const length = SciCall_GetTextLength();

    if (length <= 0) {
        EditSetNewText(hwnd, "", 0, true);
        return false;
    }

    const DocPos chBufSize = length * 5 + 2;
    char*        pchText   = AllocMem(chBufSize, HEAP_ZERO_MEMORY);

    struct Sci_TextRange tr = {{0, -1}, NULL};
    tr.lpstrText            = pchText;
    DocPos const rlength    = SciCall_GetTextRange(&tr);

    const DocPos wchBufSize = rlength * 3 + 2;
    WCHAR*       pwchText   = AllocMem(wchBufSize, HEAP_ZERO_MEMORY);

    // MultiBytes(Sci) -> WideChar(destination) -> Sci(MultiByte)
    const UINT cpDst = Encoding_GetCodePage(encDest);

    // get text as wide char
    ptrdiff_t cbwText = MultiByteToWideCharEx(Encoding_SciCP, 0, pchText, length, pwchText, wchBufSize);
    // convert wide char to destination multibyte
    ptrdiff_t cbText = WideCharToMultiByteEx(cpDst, 0, pwchText, cbwText, pchText, chBufSize, NULL, NULL);
    // re-code to wide char
    cbwText = MultiByteToWideCharEx(cpDst, 0, pchText, cbText, pwchText, wchBufSize);
    // convert to Scintilla format
    cbText = WideCharToMultiByteEx(Encoding_SciCP, 0, pwchText, cbwText, pchText, chBufSize, NULL, NULL);

    pchText[cbText]     = '\0';
    pchText[cbText + 1] = '\0';

    FreeMem(pwchText);

    EditSetNewText(hwnd, pchText, cbText, true);

    FreeMem(pchText);

    Encoding_Current(encDest);

    return true;
}


//=============================================================================
//
//  EditSetNewEncoding()
//
bool EditSetNewEncoding(HWND hwnd, cpi_enc_t iNewEncoding, bool bSupressWarning)
{
    cpi_enc_t iCurrentEncoding = Encoding_GetCurrent();

    if (iCurrentEncoding != iNewEncoding) {

        // suppress recoding message for certain encodings
        UINT const currentCP = Encoding_GetCodePage(iCurrentEncoding);
        UINT const targetCP  = Encoding_GetCodePage(iNewEncoding);
        if (((currentCP == 936) && ((targetCP == 52936) || (targetCP == 54936))) || (((currentCP == 52936) || (currentCP == 54936)) && (targetCP == 936))) {
            bSupressWarning = true;
        }

        if (Sci_IsDocEmpty()) {
            bool const doNewEncoding = (Sci_HaveUndoRedoHistory() && !bSupressWarning) ?
                                       (INFOBOX_ANSW(InfoBoxLng(MB_YESNO, L"MsgConv2", IDS_MUI_ASK_ENCODING2)) == IDYES) : true;

            if (doNewEncoding) {
                return EditConvertText(hwnd, iCurrentEncoding, iNewEncoding);
            }
        } else {

            if (!bSupressWarning) {
                bool const bIsCurANSI   = Encoding_IsANSI(iCurrentEncoding);
                bool const bIsTargetUTF = Encoding_IsUTF8(iNewEncoding) || Encoding_IsUNICODE(iNewEncoding);
                bSupressWarning         = bIsCurANSI && bIsTargetUTF;
            }

            bool const doNewEncoding = (!bSupressWarning) ?
                                       (INFOBOX_ANSW(InfoBoxLng(MB_YESNO, L"MsgConv1", IDS_MUI_ASK_ENCODING)) == IDYES) : true;

            if (doNewEncoding) {
                return EditConvertText(hwnd, iCurrentEncoding, iNewEncoding);
            }
        }
    }
    return false;
}


//=============================================================================
//
//  EditIsRecodingNeeded()
//
bool EditIsRecodingNeeded(WCHAR* pszText, int cchLen)
{
    if ((pszText == NULL) || (cchLen < 1)) {
        return false;
    }

    UINT codepage = Encoding_GetCodePage(Encoding_GetCurrent());

    if ((codepage == CP_UTF7) || (codepage == CP_UTF8)) {
        return false;
    }

    DWORD dwFlags = Encoding_GetWCMBFlagsByCodePage(codepage);
    if (dwFlags != 0) {
        dwFlags |= (WC_COMPOSITECHECK | WC_DEFAULTCHAR);
    }

    bool  useNullParams = Encoding_IsMBCS(Encoding_GetCurrent()) ? true : false;

    BOOL bDefaultCharsUsed = FALSE;
    ptrdiff_t cch = 0;
    if (useNullParams) {
        cch = WideCharToMultiByteEx(codepage, 0, pszText, cchLen, NULL, 0, NULL, NULL);
    } else {
        cch = WideCharToMultiByteEx(codepage, dwFlags, pszText, cchLen, NULL, 0, NULL, &bDefaultCharsUsed);
    }

    if (useNullParams && (cch == 0)) {
        if (GetLastError() != ERROR_NO_UNICODE_TRANSLATION) {
            cch = cchLen;    // don't care
        }
    }

    bool bSuccess = ((cch >= cchLen) && (cch != 0xFFFD)) ? true : false;

    return (!bSuccess || bDefaultCharsUsed);
}



//=============================================================================
//
//  EditGetSelectedText()
//
size_t EditGetSelectedText(LPWSTR pwchBuffer, size_t wchLength)
{
    if (!pwchBuffer || (wchLength == 0)) {
        return FALSE;
    }
    size_t const selSize = SciCall_GetSelText(NULL);
    if (1 < selSize) {
        char* pszText = AllocMem(selSize, HEAP_ZERO_MEMORY);
        if (pszText) {
            SciCall_GetSelText(pszText);
            size_t const length = (size_t)MultiByteToWideChar(Encoding_SciCP, 0, pszText, -1, pwchBuffer, (int)wchLength);
            FreeMem(pszText);
            return length;
        }
    }
    if (wchLength > 0) {
        pwchBuffer[0] = L'\0';
        return selSize;
    }
    return FALSE;
}



//=============================================================================
//
//  EditGetClipboardText()
//
char* EditGetClipboardText(HWND hwnd, bool bCheckEncoding, int* pLineCount, int* pLenLastLn)
{
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(GetParent(hwnd))) {
        char* const pEmpty = AllocMem(1, HEAP_ZERO_MEMORY);
        return pEmpty;
    }

    // get clipboard
    HANDLE hmem = GetClipboardData(CF_UNICODETEXT);
    WCHAR* pwch = GlobalLock(hmem);
    int const wlen = (int)StringCchLenW(pwch,0);

    if (bCheckEncoding && EditIsRecodingNeeded(pwch,wlen)) {
        const DocPos iPos = SciCall_GetCurrentPos();
        const DocPos iAnchor = SciCall_GetAnchor();

        // switch encoding to universal UTF-8 codepage
        SendWMCommand(Globals.hwndMain, IDM_ENCODING_UTF8);

        // restore and adjust selection
        if (iPos > iAnchor) {
            SciCall_SetSel(iAnchor, iPos);
        } else {
            SciCall_SetSel(iPos, iAnchor);
        }
        EditFixPositions(hwnd);
    }

    // translate to SCI editor component codepage (default: UTF-8)
    char* pmch = NULL;
    ptrdiff_t mlen = 0;
    if (wlen > 0) {
        mlen = WideCharToMultiByteEx(Encoding_SciCP, 0, pwch, wlen, NULL, 0, NULL, NULL);
        pmch = (char*)AllocMem(mlen + 1, HEAP_ZERO_MEMORY);
        if (pmch && mlen != 0) {
            ptrdiff_t const cnt = WideCharToMultiByteEx(Encoding_SciCP, 0, pwch, wlen, pmch, SizeOfMem(pmch), NULL, NULL);
            if (cnt == 0) {
                return pmch;
            }
        } else {
            return pmch;
        }
    } else {
        pmch = AllocMem(1, HEAP_ZERO_MEMORY);
        return pmch;
    }
    int lineCount = 0;
    int lenLastLine = 0;

    if (SciCall_GetPasteConvertEndings()) {
        char* ptmp = (char*)AllocMem((mlen+1)*2, HEAP_ZERO_MEMORY);
        if (ptmp) {
            char *s = pmch;
            char *d = ptmp;
            int eolmode = SciCall_GetEOLMode();
            for (ptrdiff_t i = 0; (i <= mlen) && (*s != '\0'); ++i, ++lenLastLine) {
                if (*s == '\n' || *s == '\r') {
                    if (eolmode == SC_EOL_CR) {
                        *d++ = '\r';
                    } else if (eolmode == SC_EOL_LF) {
                        *d++ = '\n';
                    } else { // eolmode == SC_EOL_CRLF
                        *d++ = '\r';
                        *d++ = '\n';
                    }
                    if ((*s == '\r') && (i + 1 < mlen) && (*(s + 1) == '\n')) {
                        i++;
                        s++;
                    }
                    s++;
                    ++lineCount;
                    lenLastLine = 0;
                } else {
                    *d++ = *s++;
                }
            }
            *d = '\0';
            int mlen2 = (int)(d - ptmp);

            FreeMem(pmch);
            pmch = AllocMem((size_t)mlen2 + 1LL, HEAP_ZERO_MEMORY);
            if (pmch) {
                StringCchCopyA(pmch, SizeOfMem(pmch), ptmp);
                FreeMem(ptmp);
            }
        }
    } else {
        // count lines only
        char *s = pmch;
        for (ptrdiff_t i = 0; (i <= mlen) && (*s != '\0'); ++i, ++lenLastLine) {
            if (*s == '\n' || *s == '\r') {
                if ((*s == '\r') && (i + 1 < mlen) && (*(s + 1) == '\n')) {
                    i++;
                    s++;
                }
                s++;
                ++lineCount;
                lenLastLine = 0;
            }
        }
    }

    GlobalUnlock(hmem);
    CloseClipboard();

    if (pLineCount) {
        *pLineCount = lineCount;
    }

    if (pLenLastLn) {
        *pLenLastLn = lenLastLine;
    }

    return pmch;
}


//=============================================================================
//
//  EditGetClipboardW()
//
void EditGetClipboardW(LPWSTR pwchBuffer, size_t wchLength)
{
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(Globals.hwndMain)) {
        return;
    }

    HANDLE const hmem = GetClipboardData(CF_UNICODETEXT);
    if (hmem) {
        const WCHAR* const pwch = GlobalLock(hmem);
        if (pwch) {
            StringCchCopyW(pwchBuffer, wchLength, pwch);
        }
        GlobalUnlock(hmem);
    }
    CloseClipboard();
}


//=============================================================================
//
//  EditSetClipboardText()
//
bool EditSetClipboardText(HWND hwnd, const char* pszText, size_t cchText)
{
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        SciCall_CopyText((DocPos)cchText, pszText);
        return true;
    }

    WCHAR* pszTextW = NULL;
    ptrdiff_t const cchTextW = MultiByteToWideCharEx(Encoding_SciCP, 0, pszText, cchText, NULL, 0);
    if (cchTextW > 1) {
        pszTextW = AllocMem((cchTextW + 1) * sizeof(WCHAR), HEAP_ZERO_MEMORY);
        if (pszTextW) {
            MultiByteToWideCharEx(Encoding_SciCP, 0, pszText, cchText, pszTextW, cchTextW + 1);
            pszTextW[cchTextW] = L'\0';
        }
    }

    if (pszTextW) {
        SetClipboardText(GetParent(hwnd), pszTextW, cchTextW);
        FreeMem(pszTextW);
        return true;
    }
    return false;
}


//=============================================================================
//
//  EditClearClipboard()
//
bool EditClearClipboard(HWND hwnd)
{
    bool ok = false;
    if (OpenClipboard(GetParent(hwnd))) {
        ok = EmptyClipboard();
        CloseClipboard();
    }
    return ok;
}


//=============================================================================
//
//  EditSwapClipboard()
//
bool EditSwapClipboard(HWND hwnd, bool bSkipUnicodeCheck)
{
    int lineCount = 0;
    int lenLastLine = 0;
    char* const pClip = EditGetClipboardText(hwnd, !bSkipUnicodeCheck, &lineCount, &lenLastLine);
    if (!pClip) {
        return false; // recoding canceled
    }
    DocPos const clipLen = (DocPos)StringCchLenA(pClip,0);

    DocPos const iCurPos = SciCall_GetCurrentPos();
    DocPos const iAnchorPos = SciCall_GetAnchor();

    _BEGIN_UNDO_ACTION_;

    char* pszText = NULL;
    size_t const size = SciCall_GetSelText(NULL);
    if (size > 0) {
        pszText = AllocMem(size, HEAP_ZERO_MEMORY);
        SciCall_GetSelText(pszText);
        SciCall_Paste();  //~SciCall_ReplaceSel(pClip);
        EditSetClipboardText(hwnd, pszText, (size - 1));
    } else {
        SciCall_Paste();  //~SciCall_ReplaceSel(pClip);
        SciCall_Clear();
    }
    FreeMem(pszText);

    _END_UNDO_ACTION_;

    if (!Sci_IsMultiOrRectangleSelection()) {
        //~_BEGIN_UNDO_ACTION_;
        if (iCurPos < iAnchorPos) {
            EditSetSelectionEx(iCurPos + clipLen, iCurPos, -1, -1);
        } else {
            EditSetSelectionEx(iAnchorPos, iAnchorPos + clipLen, -1, -1);
        }
        //~_END_UNDO_ACTION_;
    } else {
        // TODO: restore rectangular selection in case of swap clipboard
    }

    FreeMem(pClip);
    return true;
}


//=============================================================================
//
//  EditCopyRangeAppend()
//
bool EditCopyRangeAppend(HWND hwnd, DocPos posBegin, DocPos posEnd, bool bAppend)
{
    if (posBegin > posEnd) {
        swapos(&posBegin, &posEnd);
    }
    DocPos const length = (posEnd - posBegin);
    if (length == 0) {
        return true;
    }

    const char* const pszText = SciCall_GetRangePointer(posBegin, length);

    WCHAR* pszTextW = NULL;
    ptrdiff_t cchTextW = 0;
    if (pszText && *pszText) {
        cchTextW = MultiByteToWideChar(Encoding_SciCP, 0, pszText, (int)length, NULL, 0);
        if (cchTextW > 0) {
            pszTextW = AllocMem((cchTextW + 1) * sizeof(WCHAR), HEAP_ZERO_MEMORY);
            if (pszTextW) {
                MultiByteToWideChar(Encoding_SciCP, 0, pszText, (int)length, pszTextW, (int)(cchTextW + 1));
                pszTextW[cchTextW] = L'\0';
            }
        }
    }

    bool res = false;
    HWND const hwndParent = GetParent(hwnd);

    if (!bAppend) {
        res = SetClipboardText(hwndParent, pszTextW, cchTextW);
        FreeMem(pszTextW);
        return res;
    }

    // --- Append to Clipboard ---

    if (!OpenClipboard(hwndParent)) {
        FreeMem(pszTextW);
        return res;
    }

    HANDLE const hOld   = GetClipboardData(CF_UNICODETEXT);
    const WCHAR* pszOld = GlobalLock(hOld);

    WCHAR pszSep[3] = { L'\0' };
    Sci_GetCurrentEOL_W(pszSep);

    size_t cchNewText = cchTextW;
    if (pszOld && *pszOld) {
        cchNewText += StringCchLen(pszOld, 0) + StringCchLen(pszSep, 0);
    }

    // Copy Clip & add line break
    WCHAR* pszNewTextW = AllocMem((cchNewText + 1) * sizeof(WCHAR), HEAP_ZERO_MEMORY);
    if (pszOld && *pszOld && pszNewTextW) {
        StringCchCopy(pszNewTextW, cchNewText + 1, pszOld);
        StringCchCat(pszNewTextW, cchNewText + 1, pszSep);
    }
    GlobalUnlock(hOld);
    CloseClipboard();

    // Add New
    if (pszTextW && *pszTextW && pszNewTextW) {
        StringCchCat(pszNewTextW, cchNewText + 1, pszTextW);
        res = SetClipboardText(hwndParent, pszNewTextW, cchNewText);
    }

    FreeMem(pszTextW);
    FreeMem(pszNewTextW);
    return res;
}


//=============================================================================
//
// EditDetectEOLMode() - moved here to handle Unicode files correctly
// by zufuliu (https://github.com/zufuliu/notepad2)
//
void EditDetectEOLMode(LPCSTR lpData, size_t cbData, EditFileIOStatus* const status)
{
    if (!lpData || (cbData == 0)) {
        return;
    }

    /* '\r' and '\n' is not reused (e.g. as trailing byte in DBCS) by any known encoding,
    it's safe to check whole data byte by byte.*/

    DocLn lineCountCRLF = 0;
    DocLn lineCountCR = 0;
    DocLn lineCountLF = 0;

    // tools/GenerateTable.py
    static const uint8_t eol_table[16] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 2, 0, 0, // 00 - 0F
    };

    const uint8_t* ptr = (const uint8_t*)lpData;
    // No NULL-terminated requirement for *ptr == '\n'
    const uint8_t* const end = (const uint8_t*)lpData + cbData - 1;

    uint32_t const mask = (1 << '\r') | (1 << '\n');
    do {
        // skip to line end
        uint8_t ch = 0;
        while (ptr < end && ((ch = *ptr++) > '\r' || ((mask >> ch) & 1) == 0)) {
            // nop
        }
        switch (ch) {
        case '\n':
            ++lineCountLF;
            break;
        case '\r':
            if (*ptr == '\n') {
                ++ptr;
                ++lineCountCRLF;
            } else {
                ++lineCountCR;
            }
            break;
        }
    } while (ptr < end);

    if (ptr == end) {
        switch (*ptr) {
        case '\n':
            ++lineCountLF;
            break;
        case '\r':
            ++lineCountCR;
            break;
        }
    }

    // values must kept in same order as SC_EOL_CRLF(0), SC_EOL_CR(1), SC_EOL_LF(2)
    DocLn const linesMax = max_ln(max_ln(lineCountCRLF, lineCountCR), lineCountLF);
    DocLn linesCount[3] = { 0, 0, 0 };
    linesCount[SC_EOL_CRLF] = lineCountCRLF;
    linesCount[SC_EOL_CR] = lineCountCR;
    linesCount[SC_EOL_LF] = lineCountLF;

    int iEOLMode = status->iEOLMode;
    if (linesMax != linesCount[iEOLMode]) {
        if (linesMax == linesCount[SC_EOL_CRLF]) {
            iEOLMode = SC_EOL_CRLF;
        } else if (linesMax == linesCount[SC_EOL_CR]) {
            iEOLMode = SC_EOL_CR;
        } else {
            iEOLMode = SC_EOL_LF;
        }
    }
    status->iEOLMode = iEOLMode;

    status->bInconsistentEOLs = 1 < ((!!lineCountCRLF) + (!!lineCountCR) + (!!lineCountLF));
    status->eolCount[SC_EOL_CRLF] = lineCountCRLF;
    status->eolCount[SC_EOL_CR] = lineCountCR;
    status->eolCount[SC_EOL_LF] = lineCountLF;
}


//=============================================================================
//
// EditIndentationCount() - check indentation consistency
//
void EditIndentationStatistic(HWND hwnd, EditFileIOStatus* const status)
{
    UNREFERENCED_PARAMETER(hwnd);

    int const tabWidth = Globals.fvCurFile.iTabWidth;
    int const indentWidth = Globals.fvCurFile.iIndentWidth;
    DocLn const lineCount = SciCall_GetLineCount();

    status->indentCount[I_TAB_LN] = 0;
    status->indentCount[I_SPC_LN] = 0;
    status->indentCount[I_MIX_LN] = 0;
    status->indentCount[I_TAB_MOD_X] = 0;
    status->indentCount[I_SPC_MOD_X] = 0;

    if (Flags.bHugeFileLoadState) {
        return;
    }

    for (DocLn line = 0; line < lineCount; ++line) {
        DocPos const lineStartPos = SciCall_PositionFromLine(line);
        DocPos const lineIndentBeg = SciCall_GetLineIndentPosition(line);
        DocPos const lineIndentDepth = SciCall_GetLineIndentation(line);

        int tabCount = 0;
        int blankCount = 0;
        int subSpcCnt = 0;
        for (DocPos pos = lineStartPos; pos < lineIndentBeg; ++pos) {
            char const ch = SciCall_GetCharAt(pos);
            switch (ch) {
            case 0x09: // tab
                ++tabCount;
                break;
            case 0x20: // space
                ++subSpcCnt;
                if ((indentWidth > 0) && (subSpcCnt >= indentWidth)) {
                    ++blankCount;
                    subSpcCnt = 0;
                }
                break;
            default:
                break;
            }
        }

        // analyze
        if (tabCount || blankCount) {
            if ((tabWidth > 0) && (lineIndentDepth % tabWidth)) {
                ++(status->indentCount[I_TAB_MOD_X]);
            }
            if ((indentWidth > 0) && (lineIndentDepth % indentWidth)) {
                ++(status->indentCount[I_SPC_MOD_X]);
            }
        }
        if (tabCount && blankCount) {
            ++(status->indentCount[I_MIX_LN]);
        } else if (tabCount) {
            ++(status->indentCount[I_TAB_LN]);
        } else if (blankCount) {
            ++(status->indentCount[I_SPC_LN]);
        }
    }
}


//=============================================================================
//
//  EditLoadFile()
//
bool EditLoadFile(
    HWND hwnd,
    LPWSTR pszFile,
    EditFileIOStatus* const status,
    bool bSkipUTFDetection,
    bool bSkipANSICPDetection,
    bool bForceEncDetection,
    bool bClearUndoHistory)
{
    if (!status) {
        return false;
    }
    status->iEncoding = Settings.DefaultEncoding;
    status->bUnicodeErr = false;
    status->bUnknownExt = false;
    status->bEncryptedRaw = false;
    Flags.bHugeFileLoadState = false;

    HANDLE const hFile = CreateFile(pszFile,
                                    GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    NULL,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL,
                                    NULL);

    Globals.dwLastError = GetLastError();

    if (!IS_VALID_HANDLE(hFile)) {
        Encoding_Forced(CPI_NONE);
        return false;
    }

    // calculate buffer size and limits

    LARGE_INTEGER liFileSize = { 0, 0 };
    bool const okay = GetFileSizeEx(hFile, &liFileSize);
    //DWORD const fileSizeMB = (DWORD)liFileSize.HighPart * (DWORD_MAX >> 20) + (liFileSize.LowPart >> 20);

    bool const bLargerThan2GB = okay && ((liFileSize.HighPart > 0) || (liFileSize.LowPart >= (DWORD)INT32_MAX));

    if (!okay || bLargerThan2GB) {
        if (!okay) {
            Globals.dwLastError = GetLastError();
            CloseHandle(hFile);
            return false;
        } else {
#ifdef _WIN64
            // can only handle ASCII/UTF-8 of this size
            Encoding_Forced(CPI_UTF8);
            // @@@ TODO: Scintilla can't handle files larger than 4GB :-( yet (2020-02-25)
            bool const bFileTooBig = (liFileSize.HighPart > 0); // > DWORD_MAX
#else
            bool const bFileTooBig = true; // _WIN32: file size < 2GB only
#endif
            if (bFileTooBig) {
                // refuse to handle file in 32-bit
                WCHAR sizeStr[64] = { L'\0' };
                StrFormatByteSize((LONGLONG)liFileSize.QuadPart, sizeStr, COUNTOF(sizeStr));
                InfoBoxLng(MB_ICONERROR, NULL, IDS_MUI_ERR_FILE_TOO_LARGE, sizeStr);
                CloseHandle(hFile);
                Encoding_Forced(CPI_NONE);
                Flags.bHugeFileLoadState = true;
                return false;
            }
        }
    }

    size_t const fileSize = (size_t)liFileSize.QuadPart;

    // Check if a warning message should be displayed for large files
    size_t const fileSizeWarning = (size_t)Settings2.FileLoadWarningMB << 20;
    if ((fileSizeWarning != 0ULL) && (fileSizeWarning <= fileSize)) {
        WCHAR sizeStr[64] = { L'\0' };
        StrFormatByteSize((LONGLONG)liFileSize.QuadPart, sizeStr, COUNTOF(sizeStr));
        WCHAR sizeWarnStr[64] = { L'\0' };
        StrFormatByteSize((LONGLONG)fileSizeWarning, sizeWarnStr, COUNTOF(sizeWarnStr));
        Flags.bHugeFileLoadState = true;
        if (INFOBOX_ANSW(InfoBoxLng(MB_YESNO, L"MsgFileSizeWarning", IDS_MUI_WARN_LOAD_BIG_FILE, sizeStr, sizeWarnStr)) != IDYES) {
            CloseHandle(hFile);
            Encoding_Forced(CPI_NONE);
            return false;
        }
    }

    // check for unknown file/extension
    status->bUnknownExt = false;
    if (!Style_HasLexerForExt(pszFile)) {
        WORD const answer = INFOBOX_ANSW(InfoBoxLng(MB_YESNO, L"MsgFileUnknownExt", IDS_MUI_WARN_UNKNOWN_EXT, PathFindFileName(pszFile)));
        if (!((IDOK == answer) || (IDYES == answer))) {
            CloseHandle(hFile);
            Encoding_Forced(CPI_NONE);
            status->bUnknownExt = true;
            return false;
        }
    }

    // new document text buffer
    char* lpData = AllocMem(fileSize + 2ULL, HEAP_ZERO_MEMORY);
    if (!lpData) {
        Globals.dwLastError = GetLastError();
        CloseHandle(hFile);
        Encoding_Forced(CPI_NONE);
        Flags.bHugeFileLoadState = true;
        return false;
    }

    size_t cbData = 0LL;
    int const readFlag = ReadAndDecryptFile(hwnd, hFile, fileSize, (void **)&lpData, &cbData);
    Globals.dwLastError = GetLastError();

    CloseHandle(hFile);

    bool bReadSuccess = ((readFlag & DECRYPT_FATAL_ERROR) || (readFlag & DECRYPT_FREAD_FAILED)) ? false : true;

    if ((readFlag & DECRYPT_CANCELED_NO_PASS) || (readFlag & DECRYPT_WRONG_PASS)) {
        bReadSuccess = (INFOBOX_ANSW(InfoBoxLng(MB_OKCANCEL, L"MsgNoOrWrongPassphrase", IDS_MUI_NOPASS)) == IDOK);
        if (!bReadSuccess) {
            Encoding_Forced(CPI_NONE);
            FreeMem(lpData);
            return false;
        } else {
            status->bEncryptedRaw =  true;
        }
    }
    if (!bReadSuccess) {
        Encoding_Forced(CPI_NONE);
        FreeMem(lpData);
        return false;
    }
    
    if (cbData == 0) {
        FileVars_GetFromData(NULL, 0, &Globals.fvCurFile); // init-reset
        status->iEOLMode = Settings.DefaultEOLMode;
        EditSetNewText(hwnd, "", 0, bClearUndoHistory);
        SciCall_SetEOLMode(Settings.DefaultEOLMode);
        Encoding_Forced(CPI_NONE);
        FreeMem(lpData);
        return true;
    }

    // force very large file to be ASCII/UTF-8 (!) - Scintilla can't handle it otherwise
    if (bLargerThan2GB) {
        bool const bIsUTF8Sig = IsUTF8Signature(lpData);
        Encoding_Forced(bIsUTF8Sig ? CPI_UTF8SIGN : CPI_UTF8);

        FileVars_GetFromData(NULL, 0, &Globals.fvCurFile); // init-reset
        status->iEncoding = Encoding_Forced(CPI_GET);
        status->iEOLMode = Settings.DefaultEOLMode;

        if (bIsUTF8Sig) {
            EditSetNewText(hwnd, UTF8StringStart(lpData), cbData - 3, bClearUndoHistory);
        } else {
            EditSetNewText(hwnd, lpData, cbData, bClearUndoHistory);
        }

        SciCall_SetEOLMode(Settings.DefaultEOLMode);

        FreeMem(lpData);
        return true;
    }

    // --------------------------------------------------------------------------

    ENC_DET_T const encDetection = Encoding_DetectEncoding(pszFile, lpData, cbData,
                                   Settings.UseDefaultForFileEncoding ? Settings.DefaultEncoding : CPI_PREFERRED_ENCODING,
                                   bSkipUTFDetection, bSkipANSICPDetection, bForceEncDetection);

    #define IS_ENC_ENFORCED() (!Encoding_IsNONE(encDetection.forcedEncoding))
    #define IS_ENC_PURE_ASCII() (encDetection.analyzedEncoding == CPI_ASCII_7BIT)

    // --------------------------------------------------------------------------

    if (Flags.bDevDebugMode) {
#if TRUE
        SetAdditionalTitleInfo(Encoding_GetTitleInfo());
#else
        DocPos const iPos = SciCall_PositionFromLine(SciCall_GetFirstVisibleLine());
        int const iXOff = SciCall_GetXOffset();
        SciCall_SetXOffset(0);
        SciCall_CallTipShow(iPos, Encoding_GetTitleInfoA());
        SciCall_SetXOffset(iXOff);
#endif

        if (IS_ENC_ENFORCED()) {
            WCHAR wchBuf[128] = { L'\0' };
            StringCchPrintf(wchBuf, COUNTOF(wchBuf), L"ForcedEncoding='%s'", g_Encodings[encDetection.forcedEncoding].wchLabel);
            SetAdditionalTitleInfo(wchBuf);
        }

        if (!Encoding_IsNONE(encDetection.fileVarEncoding) && FileVars_IsValidEncoding(&Globals.fvCurFile)) {
            WCHAR wchBuf[128] = { L'\0' };
            StringCchPrintf(wchBuf, COUNTOF(wchBuf), L" - FilEncTag='%s'",
                            g_Encodings[FileVars_GetEncoding(&Globals.fvCurFile)].wchLabel);
            AppendAdditionalTitleInfo(wchBuf);
        }

        WCHAR wcBuf[128] = { L'\0' };
        StringCchPrintf(wcBuf, ARRAYSIZE(wcBuf), L" - OS-CP='%s'", g_Encodings[CPI_ANSI_DEFAULT].wchLabel);
        AppendAdditionalTitleInfo(wcBuf);
    }

    // --------------------------------------------------------------------------
    // ===  UNICODE  ( UTF-16LE / UTF-16BE ) ===
    // --------------------------------------------------------------------------

    bool const bIsUnicodeDetected = !IS_ENC_ENFORCED() && Encoding_IsUNICODE(encDetection.unicodeAnalysis);

    if (Encoding_IsUNICODE(encDetection.Encoding) || bIsUnicodeDetected) {
        // ----------------------------------------------------------------------
        status->iEncoding = encDetection.bHasBOM ? (encDetection.bIsReverse ? CPI_UNICODEBEBOM : CPI_UNICODEBOM) :
                            (encDetection.bIsReverse ? CPI_UNICODEBE    : CPI_UNICODE);
        // ----------------------------------------------------------------------

        if (encDetection.bIsReverse) {
            SwabEx(lpData, lpData, cbData);
        }

        char* const lpDataUTF8 = AllocMem((cbData * 3) + 2, HEAP_ZERO_MEMORY);

        ptrdiff_t convCnt = WideCharToMultiByteEx(Encoding_SciCP, 0, (encDetection.bHasBOM ? (LPWSTR)lpData + 1 : (LPWSTR)lpData),
                            (encDetection.bHasBOM ? (cbData / sizeof(WCHAR)) : (cbData / sizeof(WCHAR) + 1)), lpDataUTF8, SizeOfMem(lpDataUTF8), NULL, NULL);

        if (convCnt == 0) {
            convCnt = WideCharToMultiByteEx(CP_ACP, 0, (encDetection.bHasBOM ? (LPWSTR)lpData + 1 : (LPWSTR)lpData),
                                            -1, lpDataUTF8, SizeOfMem(lpDataUTF8), NULL, NULL);
            status->bUnicodeErr = true;
        }

        FileVars_GetFromData(lpDataUTF8, convCnt - 1, &Globals.fvCurFile);
        EditSetNewText(hwnd, lpDataUTF8, convCnt - 1, bClearUndoHistory);
        EditDetectEOLMode(lpDataUTF8, convCnt - 1, status);
        FreeMem(lpDataUTF8);

    } else { // ===  ALL OTHERS  ===
        // ===  UTF-8 ? ===
        bool const bValidUTF8 = encDetection.bValidUTF8;
        bool const bForcedUTF8 = Encoding_IsUTF8(encDetection.forcedEncoding);// ~ don't || encDetection.bIsUTF8Sig here !
        bool const bAnalysisUTF8 = Encoding_IsUTF8(encDetection.Encoding);

        bool const bRejectUTF8 = (IS_ENC_ENFORCED() && !bForcedUTF8) || !bValidUTF8 || (!encDetection.bIsUTF8Sig && bSkipUTFDetection);

        if (bForcedUTF8 || (!bRejectUTF8 && (encDetection.bIsUTF8Sig || bAnalysisUTF8))) {
            if (encDetection.bIsUTF8Sig) {
                EditSetNewText(hwnd, UTF8StringStart(lpData), cbData - 3, bClearUndoHistory);
                status->iEncoding = CPI_UTF8SIGN;
                EditDetectEOLMode(UTF8StringStart(lpData), cbData - 3, status);
            } else {
                EditSetNewText(hwnd, lpData, cbData, bClearUndoHistory);
                status->iEncoding = CPI_UTF8;
                EditDetectEOLMode(lpData, cbData, status);
            }
        } else if (!IS_ENC_ENFORCED() && IS_ENC_PURE_ASCII()) {
            // load ASCII(7-bit) as ANSI/UTF-8
            EditSetNewText(hwnd, lpData, cbData, bClearUndoHistory);
            status->iEncoding = (Settings.LoadASCIIasUTF8 ? CPI_UTF8 : CPI_ANSI_DEFAULT);
            EditDetectEOLMode(lpData, cbData, status);
        } else { // ===  ALL OTHER NON UTF-8 ===

            status->iEncoding = encDetection.Encoding;
            UINT const uCodePage = Encoding_GetCodePage(encDetection.Encoding);

            if (Encoding_IsEXTERNAL_8BIT(status->iEncoding)) {
                LPWSTR lpDataWide = AllocMem(cbData * 2 + 16, HEAP_ZERO_MEMORY);

                ptrdiff_t const cbDataWide = MultiByteToWideCharEx(uCodePage, 0, lpData, cbData, lpDataWide, (SizeOfMem(lpDataWide) / sizeof(WCHAR)));
                if (cbDataWide != 0) {
                    FreeMem(lpData);
                    lpData = AllocMem(cbDataWide * 3 + 16, HEAP_ZERO_MEMORY);

                    cbData = WideCharToMultiByteEx(Encoding_SciCP, 0, lpDataWide, cbDataWide, lpData, SizeOfMem(lpData), NULL, NULL);
                    if (cbData != 0) {
                        EditSetNewText(hwnd, lpData, cbData, bClearUndoHistory);
                        EditDetectEOLMode(lpData, cbData, status);
                        FreeMem(lpDataWide);
                    } else {
                        Encoding_Forced(CPI_NONE);
                        FreeMem(lpDataWide);
                        FreeMem(lpData);
                        return false;
                    }
                } else {
                    Encoding_Forced(CPI_NONE);
                    FreeMem(lpDataWide);
                    FreeMem(lpData);
                    return false;
                }
            } else {
                EditSetNewText(hwnd, lpData, cbData, bClearUndoHistory);
                EditDetectEOLMode(lpData, cbData, status);
            }
        }
    }

    SciCall_SetCharacterCategoryOptimization(Encoding_IsCJK(encDetection.analyzedEncoding) ? 0x10000 : 0x1000);

    Encoding_Forced(CPI_NONE);

    FreeMem(lpData);

    return true;
}


//=============================================================================
//
//  EditSaveFile()
//
bool EditSaveFile(
    HWND hwnd,
    LPCWSTR pszFile,
    EditFileIOStatus * const status,
    bool bSaveCopy,
    bool bPreserveTimeStamp)
{
    if (!status) {
        return false;
    }
    bool bWriteSuccess = false;
    status->bCancelDataLoss = false;

    ///~ (!) FILE_FLAG_NO_BUFFERING needs sector-size aligned buffer layout
    DWORD const dwWriteAttributes = FILE_ATTRIBUTE_NORMAL | /*FILE_FLAG_NO_BUFFERING |*/ FILE_FLAG_WRITE_THROUGH;

    HANDLE hFile = CreateFile(pszFile,
                                GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL,
                                OPEN_ALWAYS,
                                dwWriteAttributes,
                                NULL);

    Globals.dwLastError = GetLastError();

    // failure could be due to missing attributes (2k/XP)
    if (!IS_VALID_HANDLE(hFile)) {
        DWORD dwSpecialAttributes = GetFileAttributes(pszFile);
        if (dwSpecialAttributes != INVALID_FILE_ATTRIBUTES) {
            dwSpecialAttributes &= (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
            hFile = CreateFile(pszFile,
                                       GENERIC_WRITE,
                                       FILE_SHARE_READ|FILE_SHARE_WRITE,
                                       NULL,
                                       OPEN_ALWAYS,
                                       dwWriteAttributes | dwSpecialAttributes,
                                       NULL);

            Globals.dwLastError = GetLastError();
        }
    }

    if (!IS_VALID_HANDLE(hFile)) {
        return false;
    }

    //FILETIME createTime;
    //FILETIME laccessTime;
    FILETIME modTime;
    //if (!GetFileTime(status->hndlFile, &createTime, &laccessTime, &modTime)) {
    if (!GetFileTime(hFile, NULL, NULL, &modTime)) {
        return false;
    }

    // ensure consistent line endings
    if (Settings.FixLineEndings) {
        EditEnsureConsistentLineEndings(hwnd);
    }

    // strip trailing blanks
    if (Settings.FixTrailingBlanks) {
        EditStripLastCharacter(hwnd, true, true);
    }

    // get text length in bytes
    DocPos const cbData = SciCall_GetTextLength();
    size_t bytesWritten = 0ULL;

    // files larger than 2GB will be forced stored as ASCII/UTF-8
    if (cbData >= (DocPos)INT32_MAX) {
        Encoding_Current(CPI_UTF8);
        Encoding_Forced(CPI_UTF8);
        status->iEncoding = Encoding_Forced(CPI_GET);
    }

    if ((cbData <= 0) || (cbData >= DWORD_MAX)) {
        bWriteSuccess = SetEndOfFile(hFile) && (cbData < DWORD_MAX);
        Globals.dwLastError = GetLastError();
    } else {

        if (Encoding_IsUTF8(status->iEncoding)) {
            const char* bom = NULL;
            DocPos bomoffset = 0;
            if (Encoding_IsUTF8_SIGN(status->iEncoding)) {
                bom = "\xEF\xBB\xBF";
                bomoffset = 3; // in char
            }

            SetEndOfFile(hFile);

            if (IsEncryptionRequired() && (cbData < ((DocPos)INT32_MAX - 1 - bomoffset))) {
                char* const lpData = AllocMem(cbData + 1 + bomoffset, HEAP_ZERO_MEMORY); //fix: +bom
                if (lpData) {
                    if (bom) {
                        CopyMemory(lpData, bom, bomoffset);
                    }
                    SciCall_GetText((cbData + 1), &lpData[bomoffset]);
                    bWriteSuccess = EncryptAndWriteFile(hwnd, hFile, (BYTE *)lpData, (size_t)(cbData + bomoffset), &bytesWritten);
                    Globals.dwLastError = GetLastError();
                    FreeMem(lpData);
                } else {
                    Globals.dwLastError = GetLastError();
                }
            } else { // raw data handling of UTF-8 or >2GB file size
                DWORD dwBytesWritten = 0;
                if (bom) {
                    WriteFile(hFile, bom, (DWORD)bomoffset, &dwBytesWritten, NULL);
                }
                bWriteSuccess = WriteFileXL(hFile, SciCall_GetCharacterPointer(), cbData, &bytesWritten);
                bytesWritten += (size_t)dwBytesWritten;
            }
        }

        else if (Encoding_IsUNICODE(status->iEncoding)) { // UTF-16LE/BE_(BOM)
            const char* bom = NULL;
            DocPos bomoffset = 0;
            if (Encoding_IsUNICODE_BOM(status->iEncoding)) {
                bom = "\xFF\xFE";
                bomoffset = 1; // in wide-char
            }

            SetEndOfFile(hFile);

            LPWSTR const lpDataWide = AllocMem((cbData+1+bomoffset) * 2, HEAP_ZERO_MEMORY);
            if (lpDataWide) {
                if (bom) {
                    CopyMemory((char*)lpDataWide, bom, bomoffset * 2);
                    bomoffset = 1;
                }
                ptrdiff_t const cbDataWide = bomoffset +
                                             MultiByteToWideCharEx(Encoding_SciCP, 0, SciCall_GetCharacterPointer(), cbData,
                                                     &lpDataWide[bomoffset], ((SizeOfMem(lpDataWide) / sizeof(WCHAR)) - bomoffset));

                if (Encoding_IsUNICODE_REVERSE(status->iEncoding)) {
                    SwabEx((char*)lpDataWide, (char*)lpDataWide, cbDataWide * sizeof(WCHAR));
                }
                bWriteSuccess = EncryptAndWriteFile(hwnd, hFile, (BYTE *)lpDataWide, cbDataWide * sizeof(WCHAR), &bytesWritten);
                Globals.dwLastError = GetLastError();
                FreeMem(lpDataWide);
            } else {
                Globals.dwLastError = GetLastError();
            }
        }

        else if (Encoding_IsEXTERNAL_8BIT(status->iEncoding)) {
            BOOL bCancelDataLoss = FALSE;
            UINT const uCodePage = Encoding_GetCodePage(status->iEncoding);
            bool const isUTF_7_or_8 = ((uCodePage == CPI_UTF7) || (uCodePage == CPI_UTF8));

            LPWSTR const lpDataWide = AllocMem((cbData+1) * 2, HEAP_ZERO_MEMORY);
            if (lpDataWide) {
                size_t const cbDataWide = (size_t)MultiByteToWideCharEx(Encoding_SciCP, 0, SciCall_GetCharacterPointer(), cbData,
                                          lpDataWide, (SizeOfMem(lpDataWide) / sizeof(WCHAR)));

                // dry conversion run
                DWORD const dwFlags = Encoding_GetWCMBFlagsByCodePage(uCodePage);
                size_t const cbSizeNeeded = (size_t)WideCharToMultiByteEx(uCodePage, dwFlags, lpDataWide, cbDataWide, NULL, 0, NULL, NULL);
                size_t const cbDataNew = max(cbSizeNeeded, cbDataWide);

                char* const lpData = AllocMem(cbDataNew + 1, HEAP_ZERO_MEMORY);
                if (lpData) {
                    size_t cbDataConverted = 0ULL;

                    if (Encoding_IsMBCS(status->iEncoding)) {
                        cbDataConverted = (size_t)WideCharToMultiByteEx(uCodePage, dwFlags, lpDataWide, cbDataWide,
                                          lpData, SizeOfMem(lpData), NULL, NULL);
                    } else {
                        cbDataConverted = (size_t)WideCharToMultiByteEx(uCodePage, dwFlags, lpDataWide, cbDataWide,
                                          lpData, SizeOfMem(lpData), NULL, isUTF_7_or_8 ? NULL : &bCancelDataLoss);
                    }

                    FreeMem(lpDataWide);

                    if (!bCancelDataLoss || INFOBOX_ANSW(InfoBoxLng(MB_OKCANCEL, L"MsgConv3", IDS_MUI_ERR_UNICODE2)) == IDOK) {
                        SetEndOfFile(hFile);
                        if (cbDataConverted != 0) {
                            bWriteSuccess = EncryptAndWriteFile(hwnd, hFile, (BYTE *)lpData, cbDataConverted, &bytesWritten);
                            Globals.dwLastError = GetLastError();
                        }
                    } else {
                        bWriteSuccess = false;
                        status->bCancelDataLoss = true;
                    }
                    FreeMem(lpData);
                } else {
                    Globals.dwLastError = GetLastError();
                }
            } else {
                Globals.dwLastError = GetLastError();
            }
        }

        else {
            if (IsEncryptionRequired()) {
                char* const lpData = AllocMem(cbData + 1, HEAP_ZERO_MEMORY);
                if (lpData) {
                    SciCall_GetText((cbData + 1), lpData);
                    SetEndOfFile(hFile);
                    bWriteSuccess = EncryptAndWriteFile(hwnd, hFile, (BYTE *)lpData, (DWORD)cbData, &bytesWritten);
                    Globals.dwLastError = GetLastError();
                    FreeMem(lpData);
                } else {
                    Globals.dwLastError = GetLastError();
                }
            } else {
                SetEndOfFile(hFile);
                bWriteSuccess = WriteFileXL(hFile, SciCall_GetCharacterPointer(), cbData, &bytesWritten);
            }
        }
    }

    if (bPreserveTimeStamp) {
        SetFileTime(hFile, NULL, NULL, &modTime);
    }

    CloseHandle(hFile);

    if (bWriteSuccess && !bSaveCopy) {
        SetSavePoint();
    }
    return bWriteSuccess;
}


//=============================================================================
//
//  EditInvertCase()
//
void EditInvertCase(HWND hwnd)
{
    UNREFERENCED_PARAMETER(hwnd);
    const DocPos iCurPos = SciCall_GetCurrentPos();
    const DocPos iAnchorPos = SciCall_GetAnchor();

    if (iCurPos != iAnchorPos) {
        if (Sci_IsMultiOrRectangleSelection()) {
            InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
            return;
        }

        const DocPos iSelStart = SciCall_GetSelectionStart();
        const DocPos iSelEnd = SciCall_GetSelectionEnd();
        const DocPos iSelSize = SciCall_GetSelText(NULL);

        LPWSTR pszTextW = AllocMem(iSelSize * sizeof(WCHAR), HEAP_ZERO_MEMORY);
        if (pszTextW) {

            size_t const cchTextW = EditGetSelectedText(pszTextW, iSelSize);

            bool bChanged = false;
            for (size_t i = 0; i < cchTextW; i++) {
                if (IsCharUpperW(pszTextW[i])) {
                    pszTextW[i] = LOWORD(CharLowerW((LPWSTR)(LONG_PTR)MAKELONG(pszTextW[i], 0)));
                    bChanged = true;
                } else if (IsCharLowerW(pszTextW[i])) {
                    pszTextW[i] = LOWORD(CharUpperW((LPWSTR)(LONG_PTR)MAKELONG(pszTextW[i], 0)));
                    bChanged = true;
                }
            }

            if (bChanged) {
                char* pszText = AllocMem(iSelSize, HEAP_ZERO_MEMORY);
                WideCharToMultiByteEx(Encoding_SciCP, 0, pszTextW, cchTextW, pszText, iSelSize, NULL, NULL);
                _BEGIN_UNDO_ACTION_;
                SciCall_Clear();
                SciCall_AddText((iSelEnd - iSelStart), pszText);
                SciCall_SetSel(iAnchorPos, iCurPos);
                _END_UNDO_ACTION_;
                FreeMem(pszText);
            }
            FreeMem(pszTextW);
        }
    }
}


//=============================================================================
//
//  EditTitleCase()
//
void EditTitleCase(HWND hwnd)
{
    UNREFERENCED_PARAMETER(hwnd);
    const DocPos iCurPos = SciCall_GetCurrentPos();
    const DocPos iAnchorPos = SciCall_GetAnchor();

    if (iCurPos != iAnchorPos) {
        if (Sci_IsMultiOrRectangleSelection()) {
            InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
            return;
        }
        const DocPos iSelStart = SciCall_GetSelectionStart();
        const DocPos iSelEnd = SciCall_GetSelectionEnd();
        const DocPos iSelSize = SciCall_GetSelText(NULL);

        LPWSTR pszTextW = AllocMem((iSelSize * sizeof(WCHAR)), HEAP_ZERO_MEMORY);

        if (pszTextW == NULL) {
            FreeMem(pszTextW);
            return;
        }

        size_t const cchTextW = EditGetSelectedText(pszTextW, iSelSize);

        bool bChanged = false;
        LPWSTR pszMappedW = AllocMem(SizeOfMem(pszTextW), HEAP_ZERO_MEMORY);
        if (pszMappedW) {
            // first make lower case, before applying TitleCase
            if (LCMapString(LOCALE_SYSTEM_DEFAULT, (LCMAP_LINGUISTIC_CASING | LCMAP_LOWERCASE), pszTextW, (int)cchTextW, pszMappedW, (int)iSelSize)) {
                if (LCMapString(LOCALE_SYSTEM_DEFAULT, LCMAP_TITLECASE, pszMappedW, (int)cchTextW, pszTextW, (int)iSelSize)) {
                    bChanged = true;
                }
            }
            FreeMem(pszMappedW);
        }

        if (bChanged) {
            char* pszText = AllocMem(iSelSize, HEAP_ZERO_MEMORY);
            WideCharToMultiByteEx(Encoding_SciCP, 0, pszTextW, cchTextW, pszText, iSelSize, NULL, NULL);
            _BEGIN_UNDO_ACTION_;
            SciCall_Clear();
            SciCall_AddText((iSelEnd - iSelStart), pszText);
            SciCall_SetSel(iAnchorPos, iCurPos);
            _END_UNDO_ACTION_;
            FreeMem(pszText);
        }
        FreeMem(pszTextW);
    }
}

//=============================================================================
//
//  EditSentenceCase()
//
void EditSentenceCase(HWND hwnd)
{
    UNREFERENCED_PARAMETER(hwnd);
    const DocPos iCurPos = SciCall_GetCurrentPos();
    const DocPos iAnchorPos = SciCall_GetAnchor();

    if (iCurPos != iAnchorPos) {
        if (Sci_IsMultiOrRectangleSelection()) {
            InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
            return;
        }
        const DocPos iSelStart = SciCall_GetSelectionStart();
        const DocPos iSelEnd = SciCall_GetSelectionEnd();
        const DocPos iSelSize = SciCall_GetSelText(NULL);

        LPWSTR pszTextW = AllocMem((iSelSize * sizeof(WCHAR)), HEAP_ZERO_MEMORY);

        if (pszTextW == NULL) {
            FreeMem(pszTextW);
            return;
        }

        size_t const cchTextW = EditGetSelectedText(pszTextW, iSelSize);

        bool bChanged = false;
        bool bNewSentence = true;
        for (size_t i = 0; i < cchTextW; i++) {
            if (StrChr(L".;!?\r\n", pszTextW[i])) {
                bNewSentence = true;
            } else {
                if (IsCharAlphaNumericW(pszTextW[i])) {
                    if (bNewSentence) {
                        if (IsCharLowerW(pszTextW[i])) {
                            pszTextW[i] = LOWORD(CharUpperW((LPWSTR)(LONG_PTR)MAKELONG(pszTextW[i], 0)));
                            bChanged = true;
                        }
                        bNewSentence = false;
                    } else {
                        if (IsCharUpperW(pszTextW[i])) {
                            pszTextW[i] = LOWORD(CharLowerW((LPWSTR)(LONG_PTR)MAKELONG(pszTextW[i], 0)));
                            bChanged = true;
                        }
                    }
                }
            }
        }

        if (bChanged) {
            char* pszText = AllocMem(iSelSize, HEAP_ZERO_MEMORY);
            WideCharToMultiByteEx(Encoding_SciCP, 0, pszTextW, cchTextW, pszText, iSelSize, NULL, NULL);
            _BEGIN_UNDO_ACTION_;
            SciCall_Clear();
            SciCall_AddText((iSelEnd - iSelStart), pszText);
            SciCall_SetSel(iAnchorPos, iCurPos);
            _END_UNDO_ACTION_;
            FreeMem(pszText);
        }
        FreeMem(pszTextW);
    }
}



//=============================================================================
//
//  EditURLEncode()
//
void EditURLEncode(const bool isPathConvert)
{
    if (SciCall_IsSelectionEmpty()) {
        return;
    }
    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return;
    }

    _SAVE_TARGET_RANGE_;

    DocPos const iSelStart = SciCall_GetSelectionStart();
    //DocPos const iSelEnd = SciCall_GetSelectionEnd();
    DocPos const iSelSize = SciCall_GetSelText(NULL) - 1; // w/o terminating zero
    bool const bStraightSel = (SciCall_GetAnchor() <= SciCall_GetCurrentPos());

    const char* const pszText = (const char*)SciCall_GetRangePointer(iSelStart, iSelSize);

    WCHAR szTextW[INTERNET_MAX_URL_LENGTH+1];
    ptrdiff_t const cchTextW = MultiByteToWideChar(Encoding_SciCP, 0, pszText, (int)iSelSize, szTextW, INTERNET_MAX_URL_LENGTH);
    szTextW[cchTextW] = L'\0';
    StrTrim(szTextW, L" \r\n\t");

    size_t const cchEscaped = iSelSize * 3 + 1;
    char* pszEscaped = (char*)AllocMem(cchEscaped, HEAP_ZERO_MEMORY);
    if (pszEscaped == NULL) {
        return;
    }

    LPWSTR const pszEscapedW = (LPWSTR)AllocMem(cchEscaped * sizeof(WCHAR), HEAP_ZERO_MEMORY);
    if (pszEscapedW == NULL) {
        FreeMem(pszEscaped);
        return;
    }

    DWORD cchEscapedW = (DWORD)cchEscaped;
    if (isPathConvert) {
        if (FAILED(PathCreateFromUrl(szTextW, pszEscapedW, &cchEscapedW, 0))) {
            StringCchCopy(pszEscapedW, cchEscapedW, szTextW); // no op
            cchEscapedW = (DWORD)StringCchLen(pszEscapedW, INTERNET_MAX_URL_LENGTH);
        }
        if (StrStr(pszEscapedW, L" ") != NULL) {
            // quote paths with spaces in
            StringCchCopy(szTextW, INTERNET_MAX_URL_LENGTH, pszEscapedW);
            StringCchPrintf(pszEscapedW, INTERNET_MAX_URL_LENGTH, L"\"%s\"", szTextW);
            cchEscapedW = (DWORD)StringCchLen(pszEscapedW, INTERNET_MAX_URL_LENGTH);
        }
    } else {
        UrlEscapeEx(szTextW, pszEscapedW, &cchEscapedW, true);
    }

    int const cchEscapedEnc = WideCharToMultiByte(Encoding_SciCP, 0, pszEscapedW, cchEscapedW,
                                                  pszEscaped, (int)cchEscaped, NULL, NULL);

    _BEGIN_UNDO_ACTION_;

    SciCall_TargetFromSelection();
    SciCall_ReplaceTarget(cchEscapedEnc, pszEscaped);

    SciCall_SetSelectionStart(iSelStart);
    SciCall_SetSelectionEnd(iSelStart + cchEscapedEnc);
    if (!bStraightSel) {
        SciCall_SwapMainAnchorCaret();
    }
    EditEnsureSelectionVisible();

    _END_UNDO_ACTION_;

    _RESTORE_TARGET_RANGE_;

    FreeMem(pszEscaped);
    FreeMem(pszEscapedW);
}


//=============================================================================
//
//  EditURLDecode()
//
void EditURLDecode(const bool isPathConvert)
{
    if (SciCall_IsSelectionEmpty()) {
        return;
    }
    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return;
    }

    _SAVE_TARGET_RANGE_;

    DocPos const iSelStart = SciCall_GetSelectionStart();
    //DocPos const iSelEnd = SciCall_GetSelectionEnd();
    DocPos const iSelSize = SciCall_GetSelText(NULL) - 1; // w/o terminating zero
    bool const bStraightSel = (SciCall_GetAnchor() <= SciCall_GetCurrentPos());

    const char *pszText = SciCall_GetRangePointer(iSelStart, iSelSize);

    LPWSTR pszTextW = AllocMem((iSelSize + 1) * sizeof(WCHAR), HEAP_ZERO_MEMORY);
    if (pszTextW == NULL) {
        return;
    }

    /*int cchTextW =*/ MultiByteToWideChar(Encoding_SciCP, 0, pszText, (int)iSelSize, pszTextW, (int)iSelSize);

    size_t const cchUnescaped = iSelSize * 3 + 1;
    char* pszUnescaped = (char*)AllocMem(cchUnescaped, HEAP_ZERO_MEMORY);
    if (pszUnescaped == NULL) {
        FreeMem(pszTextW);
        return;
    }

    LPWSTR pszUnescapedW = (LPWSTR)AllocMem(cchUnescaped * sizeof(WCHAR), HEAP_ZERO_MEMORY);
    if (pszUnescapedW == NULL) {
        FreeMem(pszTextW);
        FreeMem(pszUnescaped);
        return;
    }

    DWORD cchUnescapedW = (DWORD)cchUnescaped;
    if (isPathConvert) {
        StrTrim(pszTextW, L"\\/ \"'");
        if (FAILED(UrlCreateFromPath(pszTextW, pszUnescapedW, &cchUnescapedW, 0))) {
            StringCchCopy(pszUnescapedW, cchUnescaped, pszTextW); // no op
            cchUnescapedW = (DWORD)StringCchLen(pszUnescapedW, INTERNET_MAX_URL_LENGTH);
        }
    } else {
        UrlUnescapeEx(pszTextW, pszUnescapedW, &cchUnescapedW);
    }

    int const cchUnescapedDec = WideCharToMultiByte(Encoding_SciCP, 0, pszUnescapedW, cchUnescapedW,
                                pszUnescaped, (int)cchUnescaped, NULL, NULL);

    // can URL be found by Hyperlink pattern matching ?
    int matchLen = 0;
    ptrdiff_t const pos = OnigRegExFind(s_pUrlRegEx, pszUnescaped, false, SciCall_GetEOLMode(), &matchLen);
    bool const bIsValidConversion = isPathConvert ? ((pos >= 0) && (cchUnescapedDec == matchLen)) : true;

    if (bIsValidConversion) {

        _BEGIN_UNDO_ACTION_;

        SciCall_TargetFromSelection();
        SciCall_ReplaceTarget(cchUnescapedDec, pszUnescaped);

        SciCall_SetSelectionStart(iSelStart);
        SciCall_SetSelectionEnd(iSelStart + cchUnescapedDec);
        if (!bStraightSel) {
            SciCall_SwapMainAnchorCaret();
        }
        EditEnsureSelectionVisible();

        _END_UNDO_ACTION_;
    }

    _RESTORE_TARGET_RANGE_;

    FreeMem(pszTextW);
    FreeMem(pszUnescaped);
    FreeMem(pszUnescapedW);
}


//=============================================================================
//
//  EditReplaceAllChr()
//  search and replace chars must not be empty
//
void EditReplaceAllChr(const WCHAR chSearch, const WCHAR chReplace) {

    if (SciCall_IsSelectionEmpty() || (chSearch == L'\0') || (chReplace == L'\0')) {
        return;
    }
    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return;
    }

    _SAVE_TARGET_RANGE_;

    DocPos const iSelStart = SciCall_GetSelectionStart();
    DocPos const iSelEnd = SciCall_GetSelectionEnd();
    DocPos const iSelSize = SciCall_GetSelText(NULL) - 1; // w/o terminating zero
    bool const bStraightSel = (SciCall_GetAnchor() <= SciCall_GetCurrentPos());

    const char *pchText = SciCall_GetRangePointer(iSelStart, iSelSize);

    int const reqsize = MultiByteToWideChar(Encoding_SciCP, 0, pchText, (int)iSelSize, NULL, 0);
    LPWSTR const pwchText = AllocMem((reqsize + 1) * sizeof(WCHAR), HEAP_ZERO_MEMORY);
    if (pwchText == NULL) {
        return;
    }
    MultiByteToWideChar(Encoding_SciCP, 0, pchText, (int)iSelSize, pwchText, (int)reqsize);

    StrReplChr(pwchText, chSearch, chReplace);

    int const cchRepl = WideCharToMultiByte(Encoding_SciCP, 0, pwchText, reqsize, NULL, 0, NULL, NULL);
    char * const pchReplace = (char *)AllocMem((cchRepl + 1), HEAP_ZERO_MEMORY);
    if (pchReplace == NULL) {
        FreeMem(pwchText);
        return;
    }
    WideCharToMultiByte(Encoding_SciCP, 0, pwchText, reqsize, pchReplace, cchRepl, NULL, NULL);

    _BEGIN_UNDO_ACTION_;

    SciCall_TargetFromSelection();
    SciCall_ReplaceTarget(cchRepl, pchReplace);

    SciCall_SetSelectionStart(iSelStart);
    SciCall_SetSelectionEnd(iSelEnd);
    if (!bStraightSel) {
        SciCall_SwapMainAnchorCaret();
    }
    EditEnsureSelectionVisible();
    
    _END_UNDO_ACTION_;

    _RESTORE_TARGET_RANGE_;

    FreeMem(pwchText);
    FreeMem(pchReplace);
}


//=============================================================================
//
//  EditEscapeCChars()
//
void EditEscapeCChars(HWND hwnd) {

    if (SciCall_IsSelectionEmpty()) {
        return;
    }
    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return;
    }

    EDITFINDREPLACE efr = INIT_EFR_DATA;
    efr.hwnd = hwnd;

    _BEGIN_UNDO_ACTION_;

    StringCchCopyA(efr.szFind, COUNTOF(efr.szFind), "\\");
    StringCchCopyA(efr.szReplace, COUNTOF(efr.szReplace), "\\\\");
    EditReplaceAllInSelection(hwnd, &efr, false);

    StringCchCopyA(efr.szFind, COUNTOF(efr.szFind), "\"");
    StringCchCopyA(efr.szReplace, COUNTOF(efr.szReplace), "\\\"");
    EditReplaceAllInSelection(hwnd, &efr, false);

    StringCchCopyA(efr.szFind, COUNTOF(efr.szFind), "\'");
    StringCchCopyA(efr.szReplace, COUNTOF(efr.szReplace), "\\\'");
    EditReplaceAllInSelection(hwnd, &efr, false);

    _END_UNDO_ACTION_;
}


//=============================================================================
//
//  EditUnescapeCChars()
//
void EditUnescapeCChars(HWND hwnd) {

    if (SciCall_IsSelectionEmpty()) {
        return;
    }
    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return;
    }

    EDITFINDREPLACE efr = INIT_EFR_DATA;
    efr.hwnd = hwnd;

    _BEGIN_UNDO_ACTION_;

    StringCchCopyA(efr.szFind, FNDRPL_BUFFER, "\\\\");
    StringCchCopyA(efr.szReplace, FNDRPL_BUFFER, "\\");
    EditReplaceAllInSelection(hwnd, &efr, false);

    StringCchCopyA(efr.szFind, FNDRPL_BUFFER, "\\\"");
    StringCchCopyA(efr.szReplace, FNDRPL_BUFFER, "\"");
    EditReplaceAllInSelection(hwnd, &efr, false);

    StringCchCopyA(efr.szFind, FNDRPL_BUFFER, "\\\'");
    StringCchCopyA(efr.szReplace, FNDRPL_BUFFER, "\'");
    EditReplaceAllInSelection(hwnd, &efr, false);

    _END_UNDO_ACTION_;
}


//=============================================================================
//
// EditChar2Hex()
//
void EditChar2Hex(HWND hwnd)
{
    UNREFERENCED_PARAMETER(hwnd);

    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return;
    }

    bool const bSelEmpty = SciCall_IsSelectionEmpty();

    DocPos const iAnchorPos = bSelEmpty ? SciCall_GetCurrentPos() : SciCall_GetAnchor();
    DocPos const iCurPos = bSelEmpty ? SciCall_PositionAfter(iAnchorPos) : SciCall_GetCurrentPos();
    if (iAnchorPos == iCurPos) {
        return;
    }

    if (bSelEmpty) {
        SciCall_SetSelection(iCurPos, iAnchorPos);
    }
    DocPos const count = Sci_GetSelTextLength();

    char const uesc = 'u';
    //???char const uesc = (LEXER == CSHARP) ? 'x' : 'u';  // '\xn[n][n][n]' - variable length version
    //switch (Style_GetCurrentLexerPtr()->lexerID)
    //{
    //  case SCLEX_CPP:
    //    uesc = 'x';
    //  default:
    //    break;
    //}

    size_t const alloc = count * (2 + MAX_ESCAPE_HEX_DIGIT) + 1;
    char* ch = (char*)AllocMem(alloc, HEAP_ZERO_MEMORY);
    WCHAR* wch = (WCHAR*)AllocMem(alloc * sizeof(WCHAR), HEAP_ZERO_MEMORY);

    SciCall_GetSelText(ch);
    int const nchars = (DocPos)MultiByteToWideChar(Encoding_SciCP, 0, ch, -1, wch, (int)alloc) - 1; // '\0'
    memset(ch, 0, alloc);

    for (int i = 0, j = 0; i < nchars; ++i) {
        if (wch[i] <= 0xFF) {
            StringCchPrintfA(&ch[j], (alloc - j), "\\x%02X", (wch[i] & 0xFF));  // \xhh
            j += 4;
        } else {
            StringCchPrintfA(ch + j, (alloc - j), "\\%c%04X", uesc, wch[i]);  // \uhhhh \xhhhh
            j += 6;
        }
    }

    _BEGIN_UNDO_ACTION_;

    SciCall_ReplaceSel(ch);

    DocPos const iReplLen = (DocPos)StringCchLenA(ch, alloc);

    if (!bSelEmpty) {
        if (iCurPos < iAnchorPos) {
            EditSetSelectionEx(iCurPos + iReplLen, iCurPos, -1, -1);
        } else if (iCurPos > iAnchorPos) {
            EditSetSelectionEx(iAnchorPos, iAnchorPos + iReplLen, -1, -1);
        } else { // empty selection
            EditSetSelectionEx(iCurPos + iReplLen, iCurPos + iReplLen, -1, -1);
        }
    }

    _END_UNDO_ACTION_;

    FreeMem(ch);
    FreeMem(wch);
}

//=============================================================================
//
// EditHex2Char()
//
void EditHex2Char(HWND hwnd)
{
    UNREFERENCED_PARAMETER(hwnd);

    if (SciCall_IsSelectionEmpty()) {
        return;
    }
    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return;
    }

    DocPos const iCurPos = SciCall_GetCurrentPos();
    DocPos const iAnchorPos = SciCall_GetAnchor();
    DocPos const count = Sci_GetSelTextLength();
    if (count <= 0) {
        return;
    }

    size_t const alloc = count * (2 + MAX_ESCAPE_HEX_DIGIT) + 1;
    char* ch = (char*)AllocMem(alloc, HEAP_ZERO_MEMORY);

    SciCall_GetSelText(ch);

    int const cch = Hex2Char(ch, (int)alloc);

    _BEGIN_UNDO_ACTION_;
    SciCall_ReplaceSel(ch);
    if (iCurPos < iAnchorPos) {
        EditSetSelectionEx(iCurPos + cch, iCurPos, -1, -1);
    } else {
        EditSetSelectionEx(iAnchorPos, iAnchorPos + cch, -1, -1);
    }
    _END_UNDO_ACTION_;

    FreeMem(ch);
}


//=============================================================================
//
//  EditFindMatchingBrace()
//
void EditFindMatchingBrace()
{
    bool bIsAfter = false;
    DocPos iMatchingBracePos = (DocPos)-1;
    const DocPos iCurPos = SciCall_GetCurrentPos();
    const char c = SciCall_GetCharAt(iCurPos);
    if (StrChrA(NP3_BRACES_TO_MATCH, c)) {
        iMatchingBracePos = SciCall_BraceMatch(iCurPos);
    } else { // Try one before
        const DocPos iPosBefore = SciCall_PositionBefore(iCurPos);
        const char cb = SciCall_GetCharAt(iPosBefore);
        if (StrChrA(NP3_BRACES_TO_MATCH, cb)) {
            iMatchingBracePos = SciCall_BraceMatch(iPosBefore);
        }
        bIsAfter = true;
    }
    if (iMatchingBracePos != (DocPos)-1) {
        iMatchingBracePos = bIsAfter ? iMatchingBracePos : SciCall_PositionAfter(iMatchingBracePos);
        Sci_GotoPosChooseCaret(iMatchingBracePos);
        EditEnsureSelectionVisible();
    }
}


//=============================================================================
//
//  EditSelectToMatchingBrace()
//
void EditSelectToMatchingBrace()
{
    bool bIsAfter = false;
    DocPos iMatchingBracePos = -1;
    const DocPos iCurPos = SciCall_GetCurrentPos();
    const char c = SciCall_GetCharAt(iCurPos);
    if (StrChrA(NP3_BRACES_TO_MATCH, c)) {
        iMatchingBracePos = SciCall_BraceMatch(iCurPos);
    } else { // Try one before
        const DocPos iPosBefore = SciCall_PositionBefore(iCurPos);
        const char cb = SciCall_GetCharAt(iPosBefore);
        if (StrChrA(NP3_BRACES_TO_MATCH, cb)) {
            iMatchingBracePos = SciCall_BraceMatch(iPosBefore);
        }
        bIsAfter = true;
    }

    if (iMatchingBracePos != (DocPos)-1) {
        _BEGIN_UNDO_ACTION_;
        if (bIsAfter) {
            EditSetSelectionEx(iCurPos, iMatchingBracePos, -1, -1);
        } else {
            EditSetSelectionEx(iCurPos, SciCall_PositionAfter(iMatchingBracePos), -1, -1);
        }
        _END_UNDO_ACTION_;
    }
}


//=============================================================================
//
//  EditModifyNumber()
//
void EditModifyNumber(HWND hwnd,bool bIncrease)
{

    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return;
    }

    const DocPos iSelStart = SciCall_GetSelectionStart();
    const DocPos iSelEnd = SciCall_GetSelectionEnd();

    if ((iSelEnd - iSelStart) > 0) {
        char chNumber[32] = { '\0' };
        if (SciCall_GetSelText(NULL) <= COUNTOF(chNumber)) {
            SciCall_GetSelText(chNumber);

            if (StrChrIA(chNumber, '-')) {
                return;
            }

            unsigned int iNumber;
            int iWidth;
            char chFormat[32] = { '\0' };
            if (!StrChrIA(chNumber, 'x') && sscanf_s(chNumber, "%ui", &iNumber) == 1) {
                iWidth = (int)StringCchLenA(chNumber, COUNTOF(chNumber));
                if (bIncrease && (iNumber < UINT_MAX)) {
                    iNumber++;
                }
                if (!bIncrease && (iNumber > 0)) {
                    iNumber--;
                }

                StringCchPrintfA(chFormat, COUNTOF(chFormat), "%%0%ii", iWidth);
                StringCchPrintfA(chNumber, COUNTOF(chNumber), chFormat, iNumber);
                EditReplaceSelection(chNumber, false);
            } else if (sscanf_s(chNumber, "%x", &iNumber) == 1) {
                iWidth = (int)StringCchLenA(chNumber, COUNTOF(chNumber)) - 2;
                if (bIncrease && iNumber < UINT_MAX) {
                    iNumber++;
                }
                if (!bIncrease && iNumber > 0) {
                    iNumber--;
                }
                bool bUppercase = false;
                for (int i = (int)StringCchLenA(chNumber, COUNTOF(chNumber)) - 1; i >= 0; i--) {
                    if (IsCharLowerA(chNumber[i])) {
                        break;
                    }
                    if (IsCharUpperA(chNumber[i])) {
                        bUppercase = true;
                        break;
                    }
                }
                if (bUppercase) {
                    StringCchPrintfA(chFormat, COUNTOF(chFormat), "%%#0%iX", iWidth);
                } else {
                    StringCchPrintfA(chFormat, COUNTOF(chFormat), "%%#0%ix", iWidth);
                }

                StringCchPrintfA(chNumber, COUNTOF(chNumber), chFormat, iNumber);
                EditReplaceSelection(chNumber, false);
            }
        }
    }
    UNREFERENCED_PARAMETER(hwnd);
}


//=============================================================================
//
//  _GetCurrentDateTimeString()
//
static void _GetCurrentDateTimeString(LPWSTR pwchDateTimeStrg, size_t cchBufLen, bool bShortFmt)
{
    SYSTEMTIME st;
    GetLocalTime(&st);

    const WCHAR* const confFormat = bShortFmt ? Settings2.DateTimeFormat : Settings2.DateTimeLongFormat;

    if (StrIsNotEmpty(pwchDateTimeStrg) || StrIsNotEmpty(confFormat)) {
        WCHAR wchTemplate[MIDSZ_BUFFER] = {L'\0'};
        StringCchCopyW(wchTemplate, COUNTOF(wchTemplate), StrIsNotEmpty(pwchDateTimeStrg) ? pwchDateTimeStrg : confFormat);

        struct tm sst = { 0 };
        sst.tm_isdst = -1;
        sst.tm_sec = (int)st.wSecond;
        sst.tm_min = (int)st.wMinute;
        sst.tm_hour = (int)st.wHour;
        sst.tm_mday = (int)st.wDay;
        sst.tm_mon = (int)st.wMonth - 1;
        sst.tm_year = (int)st.wYear - 1900;
        sst.tm_wday = (int)st.wDayOfWeek;
        mktime(&sst);
        size_t const cnt = wcsftime(pwchDateTimeStrg, cchBufLen, wchTemplate, &sst);
        if (cnt == 0) {
            StringCchCopy(pwchDateTimeStrg, cchBufLen, wchTemplate);
        }

    } else { // use configured Language Locale DateTime Format

        WCHAR wchTime[SMALL_BUFFER] = { L'\0' };
        WCHAR wchDate[SMALL_BUFFER] = { L'\0' };
        WCHAR wchFormat[SMALL_BUFFER] = { L'\0' };

        LPCWSTR const pLocaleName = Settings.PreferredLocale4DateFmt ? Settings2.PreferredLanguageLocaleName : LOCALE_NAME_USER_DEFAULT;

        GetLocaleInfoEx(pLocaleName, (bShortFmt ? LOCALE_SSHORTDATE : LOCALE_SLONGDATE), wchFormat, COUNTOF(wchFormat));
        GetDateFormatEx(pLocaleName, DATE_AUTOLAYOUT, &st, wchFormat, wchDate, COUNTOF(wchDate), NULL);
        StrDelChrW(wchDate, L"\x200E"); // clear off the Left-to-Right Mark (LRM)

        LPCWSTR const tfmt = bShortFmt ? NULL : wchFormat;
        if (tfmt) {
            GetLocaleInfoEx(pLocaleName, LOCALE_STIMEFORMAT, wchFormat, COUNTOF(wchFormat));
        }
        GetTimeFormatEx(pLocaleName, (bShortFmt ? TIME_NOSECONDS : 0), &st, tfmt, wchTime, COUNTOF(wchTime));
        StrDelChrW(wchTime, L"\x200E"); // clear off the Left-to-Right Mark (LRM)

        StringCchPrintf(pwchDateTimeStrg, cchBufLen, L"%s %s", wchTime, wchDate);
    }
}

static void _GetCurrentTimeStamp(LPWSTR pwchDateTimeStrg, size_t cchBufLen, bool bShortFmt)
{
    if (StrIsEmpty(pwchDateTimeStrg)) {
        // '%s' is not allowd pattern of wcsftime(), so it must be string format
        PCWSTR p = StrStr(Settings2.TimeStampFormat, L"%s");
        if (p && !StrStr(p + 2, L"%s")) {
            WCHAR wchDateTime[SMALL_BUFFER] = {L'\0'};
            _GetCurrentDateTimeString(wchDateTime, COUNTOF(wchDateTime), bShortFmt);
            StringCchPrintfW(pwchDateTimeStrg, cchBufLen, Settings2.TimeStampFormat, wchDateTime);
            return;
        }
        // use configuration
        StringCchCopyW(pwchDateTimeStrg, cchBufLen, Settings2.TimeStampFormat);
    }
    _GetCurrentDateTimeString(pwchDateTimeStrg, cchBufLen, bShortFmt);
}


//=============================================================================
//
//  EditInsertDateTimeStrg()
//


void EditInsertDateTimeStrg(bool bShortFmt, bool bTimestampFmt)
{
    //~~~_BEGIN_UNDO_ACTION_;

    WCHAR wchDateTime[SMALL_BUFFER] = { L'\0' };
    char  chTimeStamp[MIDSZ_BUFFER] = {'\0'};

    if (bTimestampFmt) {
        _GetCurrentTimeStamp(wchDateTime, COUNTOF(wchDateTime), bShortFmt);
    } else {
        StringCchCopyW(wchDateTime, COUNTOF(wchDateTime), bShortFmt ? Settings2.DateTimeFormat : Settings2.DateTimeLongFormat);
        _GetCurrentDateTimeString(wchDateTime, COUNTOF(wchDateTime), bShortFmt);
    }
    WideCharToMultiByte(Encoding_SciCP, 0, wchDateTime, -1, chTimeStamp, COUNTOF(chTimeStamp), NULL, NULL);
    EditReplaceSelection(chTimeStamp, false);

    //~~~_END_UNDO_ACTION_;
}


//=============================================================================
//
//  EditUpdateTimestamps()
//
void EditUpdateTimestamps()
{
    WCHAR wchReplaceStrg[MIDSZ_BUFFER] = { L'\0' };
    _GetCurrentTimeStamp(wchReplaceStrg, COUNTOF(wchReplaceStrg), true); // DateTimeFormat

    EDITFINDREPLACE efrTS_L = INIT_EFR_DATA;
    efrTS_L.hwnd = Globals.hwndEdit;
    efrTS_L.fuFlags = (SCFIND_REGEXP | SCFIND_POSIX);
    WideCharToMultiByte(Encoding_SciCP, 0, Settings2.TimeStampRegEx, -1, efrTS_L.szFind, COUNTOF(efrTS_L.szFind), NULL, NULL);
    WideCharToMultiByte(Encoding_SciCP, 0, wchReplaceStrg, -1, efrTS_L.szReplace, COUNTOF(efrTS_L.szReplace), NULL, NULL);

    if (!SciCall_IsSelectionEmpty()) {
        EditReplaceAllInSelection(Globals.hwndEdit, &efrTS_L, true);
    } else {
        EditReplaceAll(Globals.hwndEdit, &efrTS_L, true);
    }
}


//=============================================================================
//
//  EditTabsToSpaces()
//
void EditTabsToSpaces(int nTabWidth,bool bOnlyIndentingWS)
{
    if (SciCall_IsSelectionEmpty()) {
        return;    // no selection
    }

    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return;
    }

    DocPos iCurPos    = SciCall_GetCurrentPos();
    DocPos iAnchorPos = SciCall_GetAnchor();

    DocPos iSelStart = SciCall_GetSelectionStart();
    //DocLn iLine = SciCall_LineFromPosition(iSelStart);
    //iSelStart = SciCall_PositionFromLine(iLine);   // re-base selection to start of line
    DocPos iSelEnd = SciCall_GetSelectionEnd();
    DocPos iSelCount = (iSelEnd - iSelStart);


    const char* pszText = SciCall_GetRangePointer(iSelStart, iSelCount);

    LPWSTR pszTextW = AllocMem((iSelCount + 1) * sizeof(WCHAR), HEAP_ZERO_MEMORY);
    if (pszTextW == NULL) {
        return;
    }

    ptrdiff_t const cchTextW = MultiByteToWideCharEx(Encoding_SciCP,0,pszText,iSelCount,pszTextW,iSelCount+1);

    LPWSTR pszConvW = AllocMem(cchTextW*sizeof(WCHAR)*nTabWidth+2, HEAP_ZERO_MEMORY);
    if (pszConvW == NULL) {
        FreeMem(pszTextW);
        return;
    }

    int cchConvW = 0;

    // Contributed by Homam
    // Thank you very much!
    int i = 0;
    bool bIsLineStart = true;
    bool bModified = false;
    for (int iTextW = 0; iTextW < cchTextW; iTextW++) {
        WCHAR w = pszTextW[iTextW];
        if (w == L'\t' && (!bOnlyIndentingWS || bIsLineStart)) {
            for (int j = 0; j < nTabWidth - i % nTabWidth; j++) {
                pszConvW[cchConvW++] = L' ';
            }
            i = 0;
            bModified = true;
        } else {
            i++;
            if (w == L'\n' || w == L'\r') {
                i = 0;
                bIsLineStart = true;
            } else if (w != L' ') {
                bIsLineStart = false;
            }
            pszConvW[cchConvW++] = w;
        }
    }

    FreeMem(pszTextW);

    if (bModified) {
        char *pszText2 = AllocMem((size_t)cchConvW * 3, HEAP_ZERO_MEMORY);

        ptrdiff_t cchConvM = WideCharToMultiByteEx(Encoding_SciCP,0,pszConvW,cchConvW,
                             pszText2,SizeOfMem(pszText2),NULL,NULL);

        if (iCurPos < iAnchorPos) {
            iCurPos = iSelStart;
            iAnchorPos = iSelStart + cchConvM;
        } else {
            iAnchorPos = iSelStart;
            iCurPos = iSelStart + cchConvM;
        }

        _SAVE_TARGET_RANGE_;
        _BEGIN_UNDO_ACTION_;
        SciCall_SetTargetRange(iSelStart, iSelEnd);
        SciCall_ReplaceTarget(cchConvM, pszText2);
        EditSetSelectionEx(iAnchorPos, iCurPos, -1, -1);
        _END_UNDO_ACTION_;
        _RESTORE_TARGET_RANGE_;
        FreeMem(pszText2);
    }
    FreeMem(pszConvW);
}


//=============================================================================
//
//  EditSpacesToTabs()
//
void EditSpacesToTabs(int nTabWidth,bool bOnlyIndentingWS)
{
    if (SciCall_IsSelectionEmpty()) {
        return;    // no selection
    }

    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return;
    }

    _SAVE_TARGET_RANGE_;

    DocPos iCurPos = SciCall_GetCurrentPos();
    DocPos iAnchorPos = SciCall_GetAnchor();

    DocPos const iSelStart = SciCall_GetSelectionStart();
    //DocLn iLine = SciCall_LineFromPosition(iSelStart);
    //iSelStart = SciCall_PositionFromLine(iLine);   // re-base selection to start of line
    DocPos const iSelEnd = SciCall_GetSelectionEnd();
    DocPos const iSelCount = (iSelEnd - iSelStart);

    const char* pszText = SciCall_GetRangePointer(iSelStart, iSelCount);

    LPWSTR pszTextW = AllocMem((iSelCount + 1) * sizeof(WCHAR), HEAP_ZERO_MEMORY);
    if (pszTextW == NULL) {
        return;
    }

    ptrdiff_t const cchTextW = MultiByteToWideCharEx(Encoding_SciCP,0,pszText,iSelCount,pszTextW,iSelCount+1);

    LPWSTR pszConvW = AllocMem(cchTextW*sizeof(WCHAR)+2, HEAP_ZERO_MEMORY);
    if (pszConvW == NULL) {
        FreeMem(pszTextW);
        return;
    }

    int cchConvW = 0;

    // Contributed by Homam
    // Thank you very much!
    int i = 0;
    int j = 0;
    bool bIsLineStart = true;
    bool bModified = false;
    WCHAR space[256] = { L'\0' };
    for (int iTextW = 0; iTextW < cchTextW; iTextW++) {
        WCHAR w = pszTextW[iTextW];
        if ((w == L' ' || w == L'\t') && (!bOnlyIndentingWS || bIsLineStart)) {
            space[j++] = w;
            if (j == nTabWidth - i % nTabWidth || w == L'\t') {
                if (j > 1 || pszTextW[iTextW+1] == L' ' || pszTextW[iTextW+1] == L'\t') {
                    pszConvW[cchConvW++] = L'\t';
                } else {
                    pszConvW[cchConvW++] = w;
                }
                i = j = 0;
                bModified = bModified || (w != pszConvW[cchConvW-1]);
            }
        } else {
            i += j + 1;
            if (j > 0) {
                //space[j] = '\0';
                for (int t = 0; t < j; t++) {
                    pszConvW[cchConvW++] = space[t];
                }
                j = 0;
            }
            if (w == L'\n' || w == L'\r') {
                i = 0;
                bIsLineStart = true;
            } else {
                bIsLineStart = false;
            }
            pszConvW[cchConvW++] = w;
        }
    }
    if (j > 0) {
        for (int t = 0; t < j; t++) {
            pszConvW[cchConvW++] = space[t];
        }
    }

    FreeMem(pszTextW);

    if (bModified || cchConvW != cchTextW) {
        char *pszText2 = AllocMem((size_t)cchConvW * 3, HEAP_ZERO_MEMORY);

        ptrdiff_t cchConvM = WideCharToMultiByteEx(Encoding_SciCP,0,pszConvW,cchConvW,
                             pszText2,SizeOfMem(pszText2),NULL,NULL);

        if (iAnchorPos > iCurPos) {
            iCurPos = iSelStart;
            iAnchorPos = iSelStart + cchConvM;
        } else {
            iAnchorPos = iSelStart;
            iCurPos = iSelStart + cchConvM;
        }

        _BEGIN_UNDO_ACTION_;
        SciCall_SetTargetRange(iSelStart, iSelEnd);
        SciCall_ReplaceTarget(cchConvM, pszText2);
        EditSetSelectionEx(iAnchorPos, iCurPos, -1, -1);
        _END_UNDO_ACTION_;
        FreeMem(pszText2);
    }

    _RESTORE_TARGET_RANGE_;

    FreeMem(pszConvW);
}



//=============================================================================
//
//  _EditMoveLines()
//
static void  _EditMoveLines(bool bMoveUp)
{
    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return;
    }

    DocPos const iSelBeg = SciCall_GetSelectionStart();
    DocLn  const iBegLine = SciCall_LineFromPosition(iSelBeg);
    DocPos const iSelEnd = SciCall_GetSelectionEnd();
    DocLn const iEndLine = SciCall_LineFromPosition(iSelEnd);

    DocLn lastLine = Sci_GetLastDocLineNumber();

    if (Sci_GetNetLineLength(lastLine) == 0) {
        if (SciCall_PositionFromLine(iEndLine) < iSelEnd) {
            --lastLine;
        }
    }

    bool const bCanMove = bMoveUp ? (iBegLine > 0) : (iEndLine < lastLine);
    if (bCanMove) {

        bool const bForwardSelection = Sci_IsForwardSelection();
        int const direction = (bMoveUp ? -1 : 1);

        DocPos const iBegChCount = SciCall_CountCharacters(SciCall_PositionFromLine(iBegLine), iSelBeg);
        DocPos const iEndChCount = SciCall_CountCharacters(SciCall_PositionFromLine(iEndLine), iSelEnd);

        _BEGIN_UNDO_ACTION_;

        if (bMoveUp) {
            SciCall_MoveSelectedLinesUp();
        } else {
            SciCall_MoveSelectedLinesDown();
        }

        DocPos const iNewSelBeg = SciCall_PositionRelative(SciCall_PositionFromLine(iBegLine + direction), iBegChCount);
        DocPos const iNewSelEnd = SciCall_PositionRelative(SciCall_PositionFromLine(iEndLine + direction), iEndChCount);

        if (bForwardSelection) {
            SciCall_SetSel(iNewSelBeg, iNewSelEnd);
        } else {
            SciCall_SetSel(iNewSelEnd, iNewSelBeg);
        }

        _END_UNDO_ACTION_;
    }
}


//=============================================================================
//
//  EditMoveUp()
//
void EditMoveUp(HWND hwnd)
{
    UNREFERENCED_PARAMETER(hwnd);
    _EditMoveLines(true);
}


//=============================================================================
//
//  EditMoveDown()
//
void EditMoveDown(HWND hwnd)
{
    UNREFERENCED_PARAMETER(hwnd);
    _EditMoveLines(false);
}


//=============================================================================
//
//  EditSetCaretToSelectionStart()
//
bool EditSetCaretToSelectionStart()
{
    DocPos const c = SciCall_GetSelectionNCaret(0) + SciCall_GetSelectionNCaretVirtualSpace(0);
    DocPos const s = SciCall_GetSelectionNStart(0) + SciCall_GetSelectionNStartVirtualSpace(0);
    bool const bSwap = (c != s);
    if (bSwap) {
        size_t const n = SciCall_GetSelections();
        for (size_t i = 0; i < n; ++i) {
            SciCall_SwapMainAnchorCaret();
            SciCall_RotateSelection();
        }
    }
    return bSwap;
}

//=============================================================================
//
//  EditSetCaretToSelectionEnd()
//
bool EditSetCaretToSelectionEnd()
{
    DocPos const c = SciCall_GetSelectionNCaret(0) + SciCall_GetSelectionNCaretVirtualSpace(0);
    DocPos const e = SciCall_GetSelectionNEnd(0) + SciCall_GetSelectionNEndVirtualSpace(0);
    bool const bSwap = (c != e);
    if (bSwap) {
        size_t const n = SciCall_GetSelections();
        for (size_t i = 0; i < n; ++i) {
            SciCall_SwapMainAnchorCaret();
            SciCall_RotateSelection();
        }
    }
    return bSwap;
}


//=============================================================================
//
//  EditModifyLines()
//
void EditModifyLines(LPCWSTR pwszPrefix, LPCWSTR pwszAppend)
{
    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return;
    }

    _SAVE_TARGET_RANGE_;

    char mszPrefix1[256 * 3] = { '\0' };
    char  mszAppend1[256 * 3] = { '\0' };

    DocPos iSelStart = SciCall_GetSelectionStart();
    DocPos iSelEnd = SciCall_GetSelectionEnd();

    if (StrIsNotEmpty(pwszPrefix)) {
        WideCharToMultiByteEx(Encoding_SciCP, 0, pwszPrefix, -1, mszPrefix1, COUNTOF(mszPrefix1), NULL, NULL);
    }
    if (StrIsNotEmpty(pwszAppend)) {
        WideCharToMultiByteEx(Encoding_SciCP, 0, pwszAppend, -1, mszAppend1, COUNTOF(mszAppend1), NULL, NULL);
    }

    DocLn iLineStart = SciCall_LineFromPosition(iSelStart);
    DocLn iLineEnd = SciCall_LineFromPosition(iSelEnd);

    //if (iSelStart > SciCall_PositionFromLine(iLineStart))
    //  iLineStart++;

    if (iSelEnd <= SciCall_PositionFromLine(iLineEnd)) {
        if ((iLineEnd - iLineStart) >= 1) {
            --iLineEnd;
        }
    }

    bool  bPrefixNum = false;
    DocLn iPrefixNum = 0;
    int   iPrefixNumWidth = 1;
    DocLn iAppendNum = 0;
    int   iAppendNumWidth = 1;
    char  pszPrefixNumPad[2] = { '\0', '\0' };
    char  pszAppendNumPad[2] = { '\0', '\0' };
    char  mszPrefix2[256 * 3] = { '\0' };
    char  mszAppend2[256 * 3] = { '\0' };

    if (!StrIsEmptyA(mszPrefix1)) {
        char* p = StrStrA(mszPrefix1, "$(");
        while (!bPrefixNum && p) {

            if (StrCmpNA(p, "$(I)", CONSTSTRGLEN("$(I)")) == 0) {
                *p = 0;
                StringCchCopyA(mszPrefix2, COUNTOF(mszPrefix2), p + CONSTSTRGLEN("$(I)"));
                bPrefixNum = true;
                iPrefixNum = 0;
                for (DocLn i = iLineEnd - iLineStart; i >= 10; i = i / 10) {
                    iPrefixNumWidth++;
                }
                pszPrefixNumPad[0] = '\0';
            }

            else if (StrCmpNA(p, "$(0I)", CONSTSTRGLEN("$(0I)")) == 0) {
                *p = 0;
                StringCchCopyA(mszPrefix2, COUNTOF(mszPrefix2), p + CONSTSTRGLEN("$(0I)"));
                bPrefixNum = true;
                iPrefixNum = 0;
                for (DocLn i = iLineEnd - iLineStart; i >= 10; i = i / 10) {
                    iPrefixNumWidth++;
                }
                pszPrefixNumPad[0] = '0';
            }

            else if (StrCmpNA(p, "$(N)", CONSTSTRGLEN("$(N)")) == 0) {
                *p = 0;
                StringCchCopyA(mszPrefix2, COUNTOF(mszPrefix2), p + CONSTSTRGLEN("$(N)"));
                bPrefixNum = true;
                iPrefixNum = 1;
                for (DocLn i = iLineEnd - iLineStart + 1; i >= 10; i = i / 10) {
                    iPrefixNumWidth++;
                }
                pszPrefixNumPad[0] = '\0';
            }

            else if (StrCmpNA(p, "$(0N)", CONSTSTRGLEN("$(0N)")) == 0) {
                *p = 0;
                StringCchCopyA(mszPrefix2, COUNTOF(mszPrefix2), p + CONSTSTRGLEN("$(0N)"));
                bPrefixNum = true;
                iPrefixNum = 1;
                for (DocLn i = iLineEnd - iLineStart + 1; i >= 10; i = i / 10) {
                    iPrefixNumWidth++;
                }
                pszPrefixNumPad[0] = '0';
            }

            else if (StrCmpNA(p, "$(L)", CONSTSTRGLEN("$(L)")) == 0) {
                *p = 0;
                StringCchCopyA(mszPrefix2, COUNTOF(mszPrefix2), p + CONSTSTRGLEN("$(L)"));
                bPrefixNum = true;
                iPrefixNum = iLineStart + 1;
                for (DocLn i = iLineEnd + 1; i >= 10; i = i / 10) {
                    iPrefixNumWidth++;
                }
                pszPrefixNumPad[0] = '\0';
            }

            else if (StrCmpNA(p, "$(0L)", CONSTSTRGLEN("$(0L)")) == 0) {
                *p = 0;
                StringCchCopyA(mszPrefix2, COUNTOF(mszPrefix2), p + CONSTSTRGLEN("$(0L)"));
                bPrefixNum = true;
                iPrefixNum = iLineStart + 1;
                for (DocLn i = iLineEnd + 1; i >= 10; i = i / 10) {
                    iPrefixNumWidth++;
                }
                pszPrefixNumPad[0] = '0';
            }
            p += CONSTSTRGLEN("$(");
            p = StrStrA(p, "$("); // next
        }
    }

    bool  bAppendNum = false;

    if (!StrIsEmptyA(mszAppend1)) {
        char* p = StrStrA(mszAppend1, "$(");
        while (!bAppendNum && p) {

            if (StrCmpNA(p, "$(I)", CONSTSTRGLEN("$(I)")) == 0) {
                *p = 0;
                StringCchCopyA(mszAppend2, COUNTOF(mszAppend2), p + CONSTSTRGLEN("$(I)"));
                bAppendNum = true;
                iAppendNum = 0;
                for (DocLn i = iLineEnd - iLineStart; i >= 10; i = i / 10) {
                    iAppendNumWidth++;
                }
                pszAppendNumPad[0] = '\0';
            }

            else if (StrCmpNA(p, "$(0I)", CONSTSTRGLEN("$(0I)")) == 0) {
                *p = 0;
                StringCchCopyA(mszAppend2, COUNTOF(mszAppend2), p + CONSTSTRGLEN("$(0I)"));
                bAppendNum = true;
                iAppendNum = 0;
                for (DocLn i = iLineEnd - iLineStart; i >= 10; i = i / 10) {
                    iAppendNumWidth++;
                }
                pszAppendNumPad[0] = '0';
            }

            else if (StrCmpNA(p, "$(N)", CONSTSTRGLEN("$(N)")) == 0) {
                *p = 0;
                StringCchCopyA(mszAppend2, COUNTOF(mszAppend2), p + CONSTSTRGLEN("$(N)"));
                bAppendNum = true;
                iAppendNum = 1;
                for (DocLn i = iLineEnd - iLineStart + 1; i >= 10; i = i / 10) {
                    iAppendNumWidth++;
                }
                pszAppendNumPad[0] = '\0';
            }

            else if (StrCmpNA(p, "$(0N)", CONSTSTRGLEN("$(0N)")) == 0) {
                *p = 0;
                StringCchCopyA(mszAppend2, COUNTOF(mszAppend2), p + CONSTSTRGLEN("$(0N)"));
                bAppendNum = true;
                iAppendNum = 1;
                for (DocLn i = iLineEnd - iLineStart + 1; i >= 10; i = i / 10) {
                    iAppendNumWidth++;
                }
                pszAppendNumPad[0] = '0';
            }

            else if (StrCmpNA(p, "$(L)", CONSTSTRGLEN("$(L)")) == 0) {
                *p = 0;
                StringCchCopyA(mszAppend2, COUNTOF(mszAppend2), p + CONSTSTRGLEN("$(L)"));
                bAppendNum = true;
                iAppendNum = iLineStart + 1;
                for (DocLn i = iLineEnd + 1; i >= 10; i = i / 10) {
                    iAppendNumWidth++;
                }
                pszAppendNumPad[0] = '\0';
            }

            else if (StrCmpNA(p, "$(0L)", CONSTSTRGLEN("$(0L)")) == 0) {
                *p = 0;
                StringCchCopyA(mszAppend2, COUNTOF(mszAppend2), p + CONSTSTRGLEN("$(0L)"));
                bAppendNum = true;
                iAppendNum = iLineStart + 1;
                for (DocLn i = iLineEnd + 1; i >= 10; i = i / 10) {
                    iAppendNumWidth++;
                }
                pszAppendNumPad[0] = '0';
            }
            p += CONSTSTRGLEN("$(");
            p = StrStrA(p, "$("); // next
        }
    }

    _BEGIN_UNDO_ACTION_;

    for (DocLn iLine = iLineStart; iLine <= iLineEnd; ++iLine) {

        if (StrIsNotEmpty(pwszPrefix)) {

            char mszInsert[512 * 3] = { '\0' };
            StringCchCopyA(mszInsert, COUNTOF(mszInsert), mszPrefix1);

            if (bPrefixNum) {
                char tchFmt[64] = { '\0' };
                char tchNum[64] = { '\0' };
                StringCchPrintfA(tchFmt, COUNTOF(tchFmt), "%%%s%ii", pszPrefixNumPad, iPrefixNumWidth);
                StringCchPrintfA(tchNum, COUNTOF(tchNum), tchFmt, iPrefixNum);
                StringCchCatA(mszInsert, COUNTOF(mszInsert), tchNum);
                StringCchCatA(mszInsert, COUNTOF(mszInsert), mszPrefix2);
                iPrefixNum++;
            }
            DocPos const iPos = SciCall_PositionFromLine(iLine);
            SciCall_SetTargetRange(iPos, iPos);
            SciCall_ReplaceTarget(-1, mszInsert);
        }

        if (StrIsNotEmpty(pwszAppend)) {

            char mszInsert[512 * 3] = { '\0' };
            StringCchCopyA(mszInsert, COUNTOF(mszInsert), mszAppend1);

            if (bAppendNum) {
                char tchFmt[64] = { '\0' };
                char tchNum[64] = { '\0' };
                StringCchPrintfA(tchFmt, COUNTOF(tchFmt), "%%%s%ii", pszAppendNumPad, iAppendNumWidth);
                StringCchPrintfA(tchNum, COUNTOF(tchNum), tchFmt, iAppendNum);
                StringCchCatA(mszInsert, COUNTOF(mszInsert), tchNum);
                StringCchCatA(mszInsert, COUNTOF(mszInsert), mszAppend2);
                iAppendNum++;
            }
            DocPos const iPos = SciCall_GetLineEndPosition(iLine);
            SciCall_SetTargetRange(iPos, iPos);
            SciCall_ReplaceTarget(-1, mszInsert);
        }
    }

    // extend selection to start of first line
    // the above code is not required when last line has been excluded
    if (iSelStart != iSelEnd) {

        DocPos iCurPos = SciCall_GetCurrentPos();
        DocPos iAnchorPos = SciCall_GetAnchor();
        if (iCurPos < iAnchorPos) {
            iCurPos = SciCall_PositionFromLine(iLineStart);
            iAnchorPos = SciCall_PositionFromLine(iLineEnd + 1);
        } else {
            iAnchorPos = SciCall_PositionFromLine(iLineStart);
            iCurPos = SciCall_PositionFromLine(iLineEnd + 1);
        }
        EditSetSelectionEx(iAnchorPos, iCurPos, -1, -1);
    }

    _END_UNDO_ACTION_;
    
    _RESTORE_TARGET_RANGE_;
}


//=============================================================================
//
//  EditIndentBlock()
//
void EditIndentBlock(HWND hwnd, int cmd, bool bFormatIndentation, bool bForceAll)
{
    if ((cmd != SCI_TAB) && (cmd != SCI_BACKTAB)) {
        SendMessage(hwnd, cmd, 0, 0);
        return;
    }
    if (!bForceAll && Sci_IsMultiOrRectangleSelection()) {
        SendMessage(hwnd, cmd, 0, 0);
        return;
    }

    DocPos const iInitialPos = SciCall_GetCurrentPos();
    if (bForceAll) {
        SciCall_SelectAll();
    }

    DocPos const iCurPos = SciCall_GetCurrentPos();
    DocPos const iAnchorPos = SciCall_GetAnchor();

    DocLn const iCurLine = SciCall_LineFromPosition(iCurPos);
    DocLn const iAnchorLine = SciCall_LineFromPosition(iAnchorPos);
    bool const bSingleLine = Sci_IsSelectionSingleLine();

    bool const _bTabIndents = SciCall_GetTabIndents();
    bool const _bBSpUnindents = SciCall_GetBackSpaceUnIndents();

    DocPos iDiffCurrent = 0;
    DocPos iDiffAnchor = 0;
    bool bFixStart = false;

    _BEGIN_UNDO_ACTION_;

    if (bSingleLine) {
        if (bFormatIndentation) {
            SciCall_VCHome();
            if (SciCall_PositionFromLine(iCurLine) == SciCall_GetCurrentPos()) {
                SciCall_VCHome();
            }
            iDiffCurrent = (iCurPos - SciCall_GetCurrentPos());
        }
    } else {
        iDiffCurrent = (SciCall_GetLineEndPosition(iCurLine) - iCurPos);
        iDiffAnchor = (SciCall_GetLineEndPosition(iAnchorLine) - iAnchorPos);
        if (iCurPos < iAnchorPos) {
            bFixStart = (SciCall_PositionFromLine(iCurLine) == SciCall_GetCurrentPos());
        } else {
            bFixStart = (SciCall_PositionFromLine(iAnchorLine) == SciCall_GetAnchor());
        }
    }

    if (cmd == SCI_TAB) {
        SciCall_SetTabIndents(bFormatIndentation ? true : _bTabIndents);
        SciCall_Tab();
        if (bFormatIndentation) {
            SciCall_SetTabIndents(_bTabIndents);
        }
    } else { // SCI_BACKTAB
        SciCall_SetBackSpaceUnIndents(bFormatIndentation ? true : _bBSpUnindents);
        SciCall_BackTab();
        if (bFormatIndentation) {
            SciCall_SetBackSpaceUnIndents(_bBSpUnindents);
        }
    }

    if (!bForceAll) {
        if (bSingleLine) {
            if (bFormatIndentation) {
                EditSetSelectionEx(SciCall_GetCurrentPos() + iDiffCurrent + (iAnchorPos - iCurPos), SciCall_GetCurrentPos() + iDiffCurrent, -1, -1);
            }
        } else { // on multiline indentation, anchor and current positions are moved to line begin resp. end
            if (bFixStart) {
                if (iCurPos < iAnchorPos) {
                    iDiffCurrent = SciCall_LineLength(iCurLine) - Sci_GetEOLLen();
                } else {
                    iDiffAnchor = SciCall_LineLength(iAnchorLine) - Sci_GetEOLLen();
                }
            }
            EditSetSelectionEx(SciCall_GetLineEndPosition(iAnchorLine) - iDiffAnchor, SciCall_GetLineEndPosition(iCurLine) - iDiffCurrent, -1, -1);
        }
    } else {
        Sci_GotoPosChooseCaret(iInitialPos);
        EditEnsureSelectionVisible();
    }

    _END_UNDO_ACTION_;
}


//=============================================================================
//
//  EditAlignText()
//
void EditAlignText(int nMode)
{
    _SAVE_TARGET_RANGE_;

    DocPos iCurPos = SciCall_GetCurrentPos();
    DocPos iAnchorPos = SciCall_GetAnchor();

    DocPos const iSelStart = SciCall_GetSelectionStart();
    DocPos const iSelEnd = SciCall_GetSelectionEnd();

    DocLn const iLineStart = SciCall_LineFromPosition(iSelStart);
    DocLn const _lnend = SciCall_LineFromPosition(iSelEnd);
    DocLn const iLineEnd = (iSelEnd <= SciCall_PositionFromLine(_lnend)) ? (_lnend - 1) : _lnend;

    DocPos const iCurCol = SciCall_GetColumn(iCurPos);
    DocPos const iAnchorCol = SciCall_GetColumn(iAnchorPos);

    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return;
    }

    if (iLineEnd <= iLineStart) {
        return;
    }

    int iMinIndent = INT_MAX;
    DocPos iMaxLength = 0;

    for (DocLn iLine = iLineStart; iLine <= iLineEnd; iLine++) {

        DocPos iLineEndPos = SciCall_GetLineEndPosition(iLine);
        const DocPos iLineIndentPos = SciCall_GetLineIndentPosition(iLine);

        if (iLineIndentPos != iLineEndPos) {
            int const iIndentCol = SciCall_GetLineIndentation(iLine);
            DocPos iTail = iLineEndPos - 1;
            char ch = SciCall_GetCharAt(iTail);
            while (iTail >= iLineStart && (ch == ' ' || ch == '\t')) {
                --iTail;
                ch = SciCall_GetCharAt(iTail);
                --iLineEndPos;
            }
            const DocPos iEndCol = SciCall_GetColumn(iLineEndPos);

            iMinIndent = min_i(iMinIndent, iIndentCol);
            iMaxLength = max_p(iMaxLength, iEndCol);
        }
    }

    size_t const iBufCount = (iMaxLength + 3) * 3;
    char* chNewLineBuf = AllocMem(iBufCount, HEAP_ZERO_MEMORY);
    WCHAR* wchLineBuf = AllocMem(iBufCount * sizeof(WCHAR), HEAP_ZERO_MEMORY);
    WCHAR* wchNewLineBuf = AllocMem(iBufCount * sizeof(WCHAR), HEAP_ZERO_MEMORY);
    PWCHAR* pWords = (PWCHAR*)AllocMem(iBufCount * sizeof(PWCHAR), HEAP_ZERO_MEMORY);

    _BEGIN_UNDO_ACTION_;

    if (chNewLineBuf && wchLineBuf && wchNewLineBuf) {

        for (DocLn iLine = iLineStart; iLine <= iLineEnd; iLine++) {
            DocPos const iStartPos = SciCall_PositionFromLine(iLine);
            DocPos const iEndPos = SciCall_GetLineEndPosition(iLine);
            DocPos const iIndentPos = SciCall_GetLineIndentPosition(iLine);

            if ((iIndentPos == iEndPos) && (iEndPos > 0)) {
                SciCall_SetTargetRange(iStartPos, iEndPos);
                SciCall_ReplaceTarget(0, "");
            } else {
                int iWords = 0;
                int iWordsLength = 0;
                DocPos const cchLine = SciCall_LineLength(iLine);
                DocPos const cwch = (DocPos)MultiByteToWideCharEx(Encoding_SciCP, 0,
                                    SciCall_GetRangePointer(iStartPos, cchLine),
                                    cchLine, wchLineBuf, iBufCount);
                wchLineBuf[cwch] = L'\0';
                StrTrim(wchLineBuf, L"\r\n\t ");

                WCHAR* p = wchLineBuf;
                while (*p) {
                    if ((*p != L' ') && (*p != L'\t')) {
                        pWords[iWords++] = p++;
                        iWordsLength++;
                        while (*p && (*p != L' ') && (*p != L'\t')) {
                            p++;
                            iWordsLength++;
                        }
                    } else {
                        *p++ = L'\0';
                    }
                }

                if (iWords > 0) {

                    if (nMode == ALIGN_JUSTIFY || nMode == ALIGN_JUSTIFY_EX) {

                        bool bNextLineIsBlank = false;
                        if (nMode == ALIGN_JUSTIFY_EX) {
                            if (SciCall_GetLineCount() <= iLine + 1) {
                                bNextLineIsBlank = true;
                            } else {
                                DocPos const iLineEndPos = SciCall_GetLineEndPosition(iLine + 1);
                                DocPos const iLineIndentPos = SciCall_GetLineIndentPosition(iLine + 1);
                                if (iLineIndentPos == iLineEndPos) {
                                    bNextLineIsBlank = true;
                                }
                            }
                        }

                        if ((nMode == ALIGN_JUSTIFY || nMode == ALIGN_JUSTIFY_EX) &&
                                iWords > 1 && iWordsLength >= 2 &&
                                ((nMode != ALIGN_JUSTIFY_EX || !bNextLineIsBlank || iLineStart == iLineEnd) ||
                                 (bNextLineIsBlank && iWordsLength > (iMaxLength - iMinIndent) * 0.75))) {
                            int iGaps = iWords - 1;
                            DocPos const iSpacesPerGap = (iMaxLength - iMinIndent - iWordsLength) / iGaps;
                            DocPos const iExtraSpaces = (iMaxLength - iMinIndent - iWordsLength) % iGaps;

                            DocPos const length = iMaxLength * 3;
                            StringCchCopy(wchNewLineBuf, iBufCount, pWords[0]);
                            p = (WCHAR*)StrEnd(wchNewLineBuf, iBufCount);

                            for (int i = 1; i < iWords; i++) {
                                for (int j = 0; j < iSpacesPerGap; j++) {
                                    *p++ = L' ';
                                    *p = 0;
                                }
                                if (i > iGaps - iExtraSpaces) {
                                    *p++ = L' ';
                                    *p = 0;
                                }
                                StringCchCat(p, (length - StringCchLenW(wchNewLineBuf, iBufCount)), pWords[i]);
                                p = (WCHAR*)StrEnd(p, 0);
                            }
                        } else {
                            StringCchCopy(wchNewLineBuf, iBufCount, pWords[0]);
                            p = (WCHAR*)StrEnd(wchNewLineBuf, iBufCount);

                            for (int i = 1; i < iWords; i++) {
                                *p++ = L' ';
                                *p = 0;
                                StringCchCat(p, (iBufCount - StringCchLenW(wchNewLineBuf, iBufCount)), pWords[i]);
                                p = (WCHAR*)StrEnd(p, 0);
                            }
                        }

                        ptrdiff_t const cch = WideCharToMultiByteEx(Encoding_SciCP, 0, wchNewLineBuf, -1, chNewLineBuf, (int)iBufCount, NULL, NULL) - 1;

                        SciCall_SetTargetRange(SciCall_PositionFromLine(iLine), SciCall_GetLineEndPosition(iLine));
                        SciCall_ReplaceTarget(cch, chNewLineBuf);
                        SciCall_SetLineIndentation(iLine, iMinIndent);
                    } else {
                        chNewLineBuf[0] = '\0';
                        wchNewLineBuf[0] = L'\0';
                        p = wchNewLineBuf;

                        DocPos const iExtraSpaces = iMaxLength - iMinIndent - iWordsLength - iWords + 1;
                        if (nMode == ALIGN_RIGHT) {
                            for (int i = 0; i < iExtraSpaces; i++) {
                                *p++ = L' ';
                            }
                            *p = 0;
                        }

                        DocPos iOddSpaces = iExtraSpaces % 2;
                        if (nMode == ALIGN_CENTER) {
                            for (int i = 1; i < iExtraSpaces - iOddSpaces; i += 2) {
                                *p++ = L' ';
                            }
                            *p = 0;
                        }
                        for (int i = 0; i < iWords; i++) {
                            StringCchCat(p, (iBufCount - StringCchLenW(wchNewLineBuf, iBufCount)), pWords[i]);
                            if (i < iWords - 1) {
                                StringCchCat(p, (iBufCount - StringCchLenW(wchNewLineBuf, iBufCount)), L" ");
                            }
                            if (nMode == ALIGN_CENTER && iWords > 1 && iOddSpaces > 0 && i + 1 >= iWords / 2) {
                                StringCchCat(p, (iBufCount - StringCchLenW(wchNewLineBuf, iBufCount)), L" ");
                                iOddSpaces--;
                            }
                            p = (WCHAR*)StrEnd(p, 0);
                        }

                        ptrdiff_t const cch = WideCharToMultiByteEx(Encoding_SciCP, 0, wchNewLineBuf, -1,
                                              chNewLineBuf, iBufCount, NULL, NULL) - 1;

                        if (cch >= 0) {
                            DocPos iPos = 0;
                            if (nMode == ALIGN_RIGHT || nMode == ALIGN_CENTER) {
                                SciCall_SetLineIndentation(iLine, iMinIndent);
                                iPos = SciCall_GetLineIndentPosition(iLine);
                            } else {
                                iPos = SciCall_PositionFromLine(iLine);
                            }
                            SciCall_SetTargetRange(iPos, SciCall_GetLineEndPosition(iLine));
                            SciCall_ReplaceTarget(cch, chNewLineBuf);

                            if (nMode == ALIGN_LEFT) {
                                SciCall_SetLineIndentation(iLine, iMinIndent);
                            }
                        }
                    }
                }
            }
        }

        FreeMem(pWords);
        FreeMem(wchNewLineBuf);
        FreeMem(wchLineBuf);
        FreeMem(chNewLineBuf);
    } else {
        InfoBoxLng(MB_ICONERROR, NULL, IDS_MUI_BUFFERTOOSMALL);
    }

    if (iAnchorPos > iCurPos) {
        iCurPos = SciCall_FindColumn(iLineStart, iCurCol);
        iAnchorPos = SciCall_FindColumn(_lnend, iAnchorCol);
    } else {
        iAnchorPos = SciCall_FindColumn(iLineStart, iAnchorCol);
        iCurPos = SciCall_FindColumn(_lnend, iCurCol);
    }
    EditSetSelectionEx(iAnchorPos, iCurPos, -1, -1);

    _END_UNDO_ACTION_;

    _RESTORE_TARGET_RANGE_;
}



//=============================================================================
//
//  EditEncloseSelection()
//
void EditEncloseSelection(LPCWSTR pwszOpen, LPCWSTR pwszClose)
{
    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return;
    }

    _SAVE_TARGET_RANGE_;

    char  mszOpen[256 * 3] = { '\0' };
    char  mszClose[256 * 3] = { '\0' };

    bool const bStraightSel = (SciCall_GetCurrentPos() >= SciCall_GetAnchor());
    DocPos const iSelStart = SciCall_GetSelectionStart();
    DocPos const iSelEnd = SciCall_GetSelectionEnd();

    if (StrIsNotEmpty(pwszOpen)) {
        WideCharToMultiByteEx(Encoding_SciCP, 0, pwszOpen, -1, mszOpen, COUNTOF(mszOpen), NULL, NULL);
    }
    if (StrIsNotEmpty(pwszClose)) {
        WideCharToMultiByteEx(Encoding_SciCP, 0, pwszClose, -1, mszClose, COUNTOF(mszClose), NULL, NULL);
    }
    DocPos const iLenOpen = (DocPos)StringCchLenA(mszOpen, COUNTOF(mszOpen));
    DocPos const iLenClose = (DocPos)StringCchLenA(mszClose, COUNTOF(mszClose));

    _BEGIN_UNDO_ACTION_;

    if (iLenOpen > 0) {
        SciCall_SetTargetRange(iSelStart, iSelStart);
        SciCall_ReplaceTarget(-1, mszOpen);
    }

    if (iLenClose > 0) {
        SciCall_SetTargetRange(iSelEnd + iLenOpen, iSelEnd + iLenOpen);
        SciCall_ReplaceTarget(-1, mszClose);
    }

    // Move selection
    SciCall_SetSelectionStart(iSelStart + iLenOpen);
    SciCall_SetSelectionEnd(iSelEnd + iLenOpen);
    if (!bStraightSel) {
        SciCall_SwapMainAnchorCaret();
    }
    EditEnsureSelectionVisible();

    _END_UNDO_ACTION_;

    _RESTORE_TARGET_RANGE_;
}


//=============================================================================
//
//  EditToggleLineCommentsSimple()
//
void EditToggleLineCommentsSimple(LPCWSTR pwszComment, bool bInsertAtStart)
{
    _SAVE_TARGET_RANGE_;

    bool const bStraightSel = SciCall_GetAnchor() <= SciCall_GetCurrentPos();

    DocPos const iSelStart = Sci_GetSelectionStartEx();
    DocPos const iSelEnd = Sci_GetSelectionEndEx();

    //const DocPos iSelBegCol = SciCall_GetColumn(iSelStart);

    char mszPrefix[32 * 3] = { '\0' };
    char mszComment[96 * 3] = { '\0' };

    if (StrIsNotEmpty(pwszComment)) {
        char mszPostfix[64 * 3] = { '\0' };
        WideCharToMultiByteEx(Encoding_SciCP, 0, pwszComment, -1, mszPrefix, COUNTOF(mszPrefix), NULL, NULL);
        StringCchCopyA(mszComment, COUNTOF(mszComment), mszPrefix);
        if (StrIsNotEmpty(Settings2.LineCommentPostfixStrg)) {
            WideCharToMultiByteEx(Encoding_SciCP, 0, Settings2.LineCommentPostfixStrg, -1, mszPostfix, COUNTOF(mszPostfix), NULL, NULL);
            StringCchCatA(mszComment, COUNTOF(mszComment), mszPostfix);
        }
    }
    DocPos const cchPrefix = (DocPos)StringCchLenA(mszPrefix, COUNTOF(mszPrefix));
    DocPos const cchComment = (DocPos)StringCchLenA(mszComment, COUNTOF(mszComment));

    if (cchComment == 0) {
        return;
    }

    const DocLn iLineStart = SciCall_LineFromPosition(iSelStart);
    DocLn iLineEnd = SciCall_LineFromPosition(iSelEnd);

    if (!Sci_IsMultiOrRectangleSelection()) {
        // don't consider (last) line where caret is before 1st column
        if (iSelEnd <= SciCall_PositionFromLine(iLineEnd)) {
            if ((iLineEnd - iLineStart) >= 1) { // except it is the only one
                --iLineEnd;
            }
        }
    }

    DocPos iCommentCol = 0;

    if (!bInsertAtStart) {
        iCommentCol = (DocPos)INT_MAX;
        for (DocLn iLine = iLineStart; iLine <= iLineEnd; iLine++) {
            const DocPos iLineEndPos = SciCall_GetLineEndPosition(iLine);
            const DocPos iLineIndentPos = SciCall_GetLineIndentPosition(iLine);
            if (iLineIndentPos != iLineEndPos) {
                const DocPos iIndentColumn = SciCall_GetColumn(iLineIndentPos);
                iCommentCol = min_p(iCommentCol, iIndentColumn);
            }
        }
    }

    DocPos iSelStartOffset = 0;
    DocPos iSelEndOffset = 0;

    _BEGIN_UNDO_ACTION_;

    int iAction = 0;
    bool const bKeepActionOf1stLine = false;

    for (DocLn iLine = iLineStart; iLine <= iLineEnd; ++iLine) {

        if (!bKeepActionOf1stLine) {
            iAction = 0;
        }

        DocPos const iIndentPos = SciCall_GetLineIndentPosition(iLine);

        if (iIndentPos == SciCall_GetLineEndPosition(iLine)) {
            // don't set comment char on "empty" (white-space only) lines
            //~iAction = 1;
            continue;
        }

        const char* tchBuf = SciCall_GetRangePointer(iIndentPos, cchComment + 1);
        if (StrCmpNA(tchBuf, mszComment, (int)cchComment) == 0) {
            // remove comment chars incl. Postfix
            DocPos const iSelPos = iIndentPos + cchComment;
            switch (iAction) {
            case 0:
                iAction = 2;
            case 2:
                SciCall_SetTargetRange(iIndentPos, iSelPos);
                SciCall_ReplaceTarget(-1, "");
                if (iLine == iLineStart) {
                    iSelStartOffset -= (iSelStart <= iIndentPos) ? 0 : (iSelStart < iSelPos) ? (iSelStart - iIndentPos) : cchComment;
                }
                DocPos const movedSelEnd = iSelEnd + iSelEndOffset;
                iSelEndOffset -= (movedSelEnd < iIndentPos) ? 0 : (movedSelEnd < iSelPos) ? (movedSelEnd - iIndentPos) : cchComment;
                break;
            case 1:
                break;
            }
        } else if (StrCmpNA(tchBuf, mszPrefix, (int)cchPrefix) == 0) {
            // remove pure comment chars
            DocPos const iSelPos = iIndentPos + cchPrefix;
            switch (iAction) {
            case 0:
                iAction = 2;
            case 2:
                SciCall_SetTargetRange(iIndentPos, iSelPos);
                SciCall_ReplaceTarget(-1, "");
                if (iLine == iLineStart) {
                    iSelStartOffset -= (iSelStart <= iIndentPos) ? 0 : (iSelStart < iSelPos) ? (iSelStart - iIndentPos) : cchPrefix;
                }
                DocPos const movedSelEnd = iSelEnd + iSelEndOffset;
                iSelEndOffset -= (movedSelEnd < iIndentPos) ? 0 : (movedSelEnd < iSelPos) ? (movedSelEnd - iIndentPos) : cchPrefix;
                break;
            case 1:
                break;
            }
        } else {
            // set comment chars at indent pos
            switch (iAction) {
            case 0:
                iAction = 1;
            case 1: {
                DocPos const iPos = SciCall_FindColumn(iLine, iCommentCol);
                SciCall_InsertText(iPos, mszComment);
                if (iLine == iLineStart) {
                    iSelStartOffset += (iSelStart <= iPos) ? 0 : cchComment;
                }
                DocPos const movedSelEnd = iSelEnd + iSelEndOffset;
                iSelEndOffset += (movedSelEnd <= iPos) ? 0 : cchComment;
            }
            break;
            case 2:
                break;
            }
        }
    }

    SciCall_SetSelectionStart(iSelStart + iSelStartOffset);
    SciCall_SetSelectionEnd(iSelEnd + iSelEndOffset);
    if (!bStraightSel) {
        SciCall_SwapMainAnchorCaret();
    }
    EditEnsureSelectionVisible();

    _END_UNDO_ACTION_;

    _RESTORE_TARGET_RANGE_;
}


//=============================================================================
//
//  EditToggleLineCommentsExtended()
//
void EditToggleLineCommentsExtended(LPCWSTR pwszComment, bool bInsertAtStart)
{
    _SAVE_TARGET_RANGE_;

    DocPos const iSelStart = Sci_GetSelectionStartEx();
    DocPos const iSelEnd = Sci_GetSelectionEndEx();

    char mszComment[96 * 3] = { '\0' };
    if (StrIsNotEmpty(pwszComment)) {
        WideCharToMultiByteEx(Encoding_SciCP, 0, pwszComment, -1, mszComment, COUNTOF(mszComment), NULL, NULL);
        char mszPostfix[64 * 3] = { '\0' };
        if (StrIsNotEmpty(Settings2.LineCommentPostfixStrg)) {
            WideCharToMultiByteEx(Encoding_SciCP, 0, Settings2.LineCommentPostfixStrg, -1, mszPostfix, COUNTOF(mszPostfix), NULL, NULL);
            StringCchCatA(mszComment, COUNTOF(mszComment), mszPostfix);
        }
    }
    DocPos const cchComment = (DocPos)StringCchLenA(mszComment, COUNTOF(mszComment));

    if (cchComment == 0) {
        return;
    }

    const DocLn iLineStart = SciCall_LineFromPosition(iSelStart);
    DocLn iLineEnd = SciCall_LineFromPosition(iSelEnd);

    if (!Sci_IsMultiOrRectangleSelection()) {
        // don't consider (last) line where caret is before 1st column
        if (iSelEnd <= SciCall_PositionFromLine(iLineEnd)) {
            if ((iLineEnd - iLineStart) >= 1) { // except it is the only one
                --iLineEnd;
            }
        }
    }

    DocPos iCommentCol = 0;

    if (!bInsertAtStart) {
        iCommentCol = (DocPos)INT_MAX;
        for (DocLn iLine = iLineStart; iLine <= iLineEnd; iLine++) {
            const DocPos iLineEndPos = SciCall_GetLineEndPosition(iLine);
            const DocPos iLineIndentPos = SciCall_GetLineIndentPosition(iLine);
            if (iLineIndentPos != iLineEndPos) {
                const DocPos iIndentColumn = SciCall_GetColumn(iLineIndentPos);
                iCommentCol = min_p(iCommentCol, iIndentColumn);
            }
        }
    }

    UT_icd docpos_icd = { sizeof(DocPos), NULL, NULL, NULL };
    UT_array* sel_positions = NULL;
    utarray_new(sel_positions, &docpos_icd);
    utarray_reserve(sel_positions, (int)(iLineEnd - iLineStart + 1));

    _BEGIN_UNDO_ACTION_;

    int iAction = 0;
    bool const bKeepActionOf1stLine = true;

    for (DocLn iLine = iLineStart; iLine <= iLineEnd; ++iLine) {

        if (!bKeepActionOf1stLine) {
            iAction = 0;
        }

        DocPos const iIndentPos = SciCall_GetLineIndentPosition(iLine);

        if (iIndentPos == SciCall_GetLineEndPosition(iLine)) {
            // don't set comment char on "empty" (white-space only) lines
            //~iAction = 1;
            continue;
        }

        const char* tchBuf = SciCall_GetRangePointer(iIndentPos, cchComment + 1);
        if (StrCmpNIA(tchBuf, mszComment, (int)cchComment) == 0) {
            // remove comment chars
            DocPos const iSelPos = iIndentPos + cchComment;
            switch (iAction) {
            case 0:
                iAction = 2;
            case 2:
                SciCall_SetTargetRange(iIndentPos, iSelPos);
                SciCall_ReplaceTarget(-1, "");
                utarray_push_back(sel_positions, &iIndentPos);
                break;
            case 1:
                utarray_push_back(sel_positions, &iSelPos);
                break;
            }
        } else {
            // set comment chars at indent pos
            switch (iAction) {
            case 0:
                iAction = 1;
            case 1: {
                DocPos const iPos = SciCall_FindColumn(iLine, iCommentCol);
                SciCall_InsertText(iPos, mszComment);
                DocPos const iSelPos = iIndentPos + cchComment;
                utarray_push_back(sel_positions, &iSelPos);
            }
            break;
            case 2:
                break;
            }
        }
    }

    // cppcheck-suppress nullPointerArithmetic
    DocPos* p = (DocPos*)utarray_next(sel_positions, NULL);
    if (p) {
        SciCall_SetSelection(*p, *p);
    }
    while (p) {
        p = (DocPos*)utarray_next(sel_positions, p);
        if (p) {
            SciCall_AddSelection(*p, *p);
        }
    }
    utarray_free(sel_positions);

    _END_UNDO_ACTION_;
    
    _RESTORE_TARGET_RANGE_;
}


//=============================================================================
//
//  _AppendSpaces()
//
static DocPos  _AppendSpaces(HWND hwnd, DocLn iLineStart, DocLn iLineEnd, DocPos iMaxColumn, bool bSkipEmpty)
{
    UNREFERENCED_PARAMETER(hwnd);

    _SAVE_TARGET_RANGE_;

    size_t size = (size_t)iMaxColumn;
    char* pmszPadStr = AllocMem(size + 1, HEAP_ZERO_MEMORY);
    FillMemory(pmszPadStr, size, ' ');

    DocPos spcCount = 0;

    _IGNORE_NOTIFY_CHANGE_;

    for (DocLn iLine = iLineStart; iLine <= iLineEnd; ++iLine) {

        // insertion position is at end of line
        const DocPos iPos = SciCall_GetLineEndPosition(iLine);
        const DocPos iCol = SciCall_GetColumn(iPos);

        if (iCol >= iMaxColumn) {
            continue;
        }
        if (bSkipEmpty && (iPos <= SciCall_PositionFromLine(iLine))) {
            continue;
        }

        const DocPos iPadLen = (iMaxColumn - iCol);

        pmszPadStr[iPadLen] = '\0'; // slice

        SciCall_SetTargetRange(iPos, iPos);
        SciCall_ReplaceTarget(-1, pmszPadStr); // pad

        pmszPadStr[iPadLen] = ' '; // reset
        spcCount += iPadLen;
    }

    _OBSERVE_NOTIFY_CHANGE_;

    _RESTORE_TARGET_RANGE_;

    FreeMem(pmszPadStr);

    return spcCount;
}

//=============================================================================
//
//  EditPadWithSpaces()
//
void EditPadWithSpaces(HWND hwnd, bool bSkipEmpty, bool bNoUndoGroup)
{
    if (SciCall_IsSelectionEmpty()) {
        return;
    }

    _IGNORE_NOTIFY_CHANGE_;

    int const token = (!bNoUndoGroup ? BeginUndoAction() : -1);
    __try {

        if (Sci_IsMultiOrRectangleSelection() && !SciCall_IsSelectionEmpty()) {
            DocPos const selAnchorMainPos = SciCall_GetRectangularSelectionAnchor();
            DocPos const selCaretMainPos = SciCall_GetRectangularSelectionCaret();

            DocPos const iAnchorColumn = SciCall_GetColumn(SciCall_GetSelectionNAnchor(0)) + SciCall_GetSelectionNAnchorVirtualSpace(0);
            DocPos const iCaretColumn = SciCall_GetColumn(SciCall_GetSelectionNCaret(0)) + SciCall_GetSelectionNCaretVirtualSpace(0);
            bool const bSelLeft2Right = (iAnchorColumn <= iCaretColumn);

            DocLn iRcAnchorLine = SciCall_LineFromPosition(selAnchorMainPos);
            DocLn iRcCaretLine = SciCall_LineFromPosition(selCaretMainPos);
            DocLn const iLineCount = abs_p(iRcCaretLine - iRcAnchorLine) + 1;

            // lots of spaces
            DocPos const spBufSize = max_p(iAnchorColumn, selCaretMainPos);
            char* pSpaceBuffer = (char*)AllocMem((spBufSize + 1) * sizeof(char), HEAP_ZERO_MEMORY);
            FillMemory(pSpaceBuffer, spBufSize * sizeof(char), ' ');

            DocPos* pVspAVec = (DocPos*)AllocMem(iLineCount * sizeof(DocPos), HEAP_ZERO_MEMORY);
            DocPos* pVspCVec = (DocPos*)AllocMem(iLineCount * sizeof(DocPos), HEAP_ZERO_MEMORY);

            for (DocLn i = 0; i < iLineCount; ++i) {
                pVspAVec[i] = SciCall_GetSelectionNAnchorVirtualSpace(i);
                pVspCVec[i] = SciCall_GetSelectionNCaretVirtualSpace(i);
            }

            DocPosU i = 0;
            DocPos iSpcCount = 0;
            DocLn const iLnIncr = (iRcAnchorLine <= iRcCaretLine) ? (DocLn)+1 : (DocLn)-1;
            DocLn iLine = iRcAnchorLine - iLnIncr;
            do {
                iLine += iLnIncr;
                DocPos const iInsPos = SciCall_GetLineEndPosition(iLine);
                DocPos const cntVSp = bSelLeft2Right ? pVspCVec[i++] : pVspAVec[i++];
                bool const bSkip = (bSkipEmpty && (iInsPos <= SciCall_PositionFromLine(iLine)));

                if ((cntVSp > 0) && !bSkip) {
                    pSpaceBuffer[cntVSp] = '\0';
                    SciCall_InsertText(iInsPos, pSpaceBuffer);
                    pSpaceBuffer[cntVSp] = ' ';
                    iSpcCount += cntVSp;
                }
            } while (iLine != iRcCaretLine);

            FreeMem(pSpaceBuffer);

            if (iRcAnchorLine <= iRcCaretLine) {
                if (bSelLeft2Right) {
                    EditSetSelectionEx(selAnchorMainPos + pVspAVec[0], selCaretMainPos + iSpcCount, 0, 0);
                } else {
                    EditSetSelectionEx(selAnchorMainPos + pVspAVec[0], selCaretMainPos + pVspCVec[iLineCount - 1] + iSpcCount - pVspAVec[iLineCount - 1], 0, 0);
                }
            } else {
                if (bSelLeft2Right) {
                    EditSetSelectionEx(selAnchorMainPos + pVspAVec[0] + iSpcCount - pVspCVec[0], selCaretMainPos + pVspCVec[iLineCount - 1], 0, 0);
                } else {
                    EditSetSelectionEx(selAnchorMainPos + iSpcCount, selCaretMainPos + pVspCVec[iLineCount - 1], 0, 0);
                }
            }

            FreeMem(pVspCVec);
            FreeMem(pVspAVec);
        } else { // SC_SEL_LINES | SC_SEL_STREAM
            const DocPos iCurPos = SciCall_GetCurrentPos();
            const DocPos iAnchorPos = SciCall_GetAnchor();

            const DocPos iSelStart = SciCall_GetSelectionStart();
            const DocPos iSelEnd = SciCall_GetSelectionEnd();

            DocLn iStartLine = 0;
            DocLn iEndLine = SciCall_GetLineCount() - 1;

            if (iSelStart != iSelEnd) {
                iStartLine = SciCall_LineFromPosition(iSelStart);
                iEndLine = SciCall_LineFromPosition(iSelEnd);
                if (iSelEnd < SciCall_GetLineEndPosition(iEndLine)) {
                    --iEndLine;
                }
            }
            if (iStartLine < iEndLine) {
                DocPos iMaxColumn = 0;
                for (DocLn iLine = iStartLine; iLine <= iEndLine; ++iLine) {
                    iMaxColumn = max_p(iMaxColumn, SciCall_GetColumn(SciCall_GetLineEndPosition(iLine)));
                }
                if (iMaxColumn > 0) {
                    const DocPos iSpcCount = _AppendSpaces(hwnd, iStartLine, iEndLine, iMaxColumn, bSkipEmpty);
                    if (iCurPos < iAnchorPos) {
                        EditSetSelectionEx(iAnchorPos + iSpcCount, iCurPos, -1, -1);
                    } else {
                        EditSetSelectionEx(iAnchorPos, iCurPos + iSpcCount, -1, -1);
                    }
                }
            }
        }
    } __finally {
        if (token >= 0) {
            EndUndoAction(token);
        }
    }
    _OBSERVE_NOTIFY_CHANGE_;
}


//=============================================================================
//
//  EditStripFirstCharacter()
//
void EditStripFirstCharacter(HWND hwnd)
{
    UNREFERENCED_PARAMETER(hwnd);

    if (SciCall_IsSelectionEmpty()) {
        return;
    }

    _SAVE_TARGET_RANGE_;

    DocPos const iSelStart = SciCall_IsSelectionEmpty() ? 0 : SciCall_GetSelectionStart();
    DocPos const iSelEnd = SciCall_IsSelectionEmpty() ? Sci_GetDocEndPosition() : SciCall_GetSelectionEnd();
    DocLn const iLineStart = SciCall_LineFromPosition(iSelStart);
    DocLn const iLineEnd = SciCall_LineFromPosition(iSelEnd);

    _BEGIN_UNDO_ACTION_;

    if (SciCall_IsSelectionRectangle()) {
        const DocPos selAnchorMainPos = SciCall_GetRectangularSelectionAnchor();
        const DocPos selCaretMainPos = SciCall_GetRectangularSelectionCaret();
        const DocPos vSpcAnchorMainPos = SciCall_GetRectangularSelectionAnchorVirtualSpace();
        const DocPos vSpcCaretMainPos = SciCall_GetRectangularSelectionCaretVirtualSpace();

        DocPos iMaxLineLen = Sci_GetRangeMaxLineLength(iLineStart, iLineEnd);
        char* lineBuffer = AllocMem(iMaxLineLen + 1, HEAP_ZERO_MEMORY);
        DocPos remCount = 0;
        if (lineBuffer) {
            DocPosU const selCount = SciCall_GetSelections();
            for (DocPosU s = 0; s < selCount; ++s) {
                DocPos const selTargetStart = SciCall_GetSelectionNStart(s);
                DocPos const selTargetEnd = SciCall_GetSelectionNEnd(s);
                DocPos const nextPos = SciCall_PositionAfter(selTargetStart);
                DocPos const len = (selTargetEnd - nextPos);
                if (len > 0) {
                    StringCchCopyNA(lineBuffer, SizeOfMem(lineBuffer), SciCall_GetRangePointer(nextPos, len + 1), len);
                    SciCall_SetTargetRange(selTargetStart, selTargetEnd);
                    SciCall_ReplaceTarget(len, lineBuffer);
                }
                remCount += (nextPos - selTargetStart);
            } // for()
            FreeMem(lineBuffer);
        }

        SciCall_SetRectangularSelectionAnchor(selAnchorMainPos);
        if (vSpcAnchorMainPos > 0) {
            SciCall_SetRectangularSelectionAnchorVirtualSpace(vSpcAnchorMainPos);
        }

        SciCall_SetRectangularSelectionCaret(selCaretMainPos - remCount);
        if (vSpcCaretMainPos > 0) {
            SciCall_SetRectangularSelectionCaretVirtualSpace(vSpcCaretMainPos);
        }
    } else if (Sci_IsMultiSelection()) {
        EditSetCaretToSelectionStart();
        SciCall_CharLeft();   // -> thin multi-selection at begin
        SciCall_CharRight();  // -> no SCI_DEL, so use SCI_DELBACK:
        SciCall_DeleteBack();
    } else { // SC_SEL_LINES | SC_SEL_STREAM
        for (DocLn iLine = iLineStart; iLine <= iLineEnd; ++iLine) {
            const DocPos iPos = SciCall_PositionFromLine(iLine);
            if (iPos < SciCall_GetLineEndPosition(iLine)) {
                SciCall_SetTargetRange(iPos, SciCall_PositionAfter(iPos));
                SciCall_ReplaceTarget(0, "");
            }
        }
    }

    _END_UNDO_ACTION_;

    _RESTORE_TARGET_RANGE_;
}


//=============================================================================
//
//  EditStripLastCharacter()
//
void EditStripLastCharacter(HWND hwnd, bool bIgnoreSelection, bool bTrailingBlanksOnly)
{
    UNREFERENCED_PARAMETER(hwnd);

    if (SciCall_IsSelectionEmpty() && !(bIgnoreSelection || bTrailingBlanksOnly)) {
        return;
    }

    _SAVE_TARGET_RANGE_;

    DocPos const iSelStart = (SciCall_IsSelectionEmpty() || bIgnoreSelection) ? 0 : SciCall_GetSelectionStart();
    DocPos const iSelEnd = (SciCall_IsSelectionEmpty() || bIgnoreSelection) ? Sci_GetDocEndPosition() : SciCall_GetSelectionEnd();
    DocLn const iLineStart = SciCall_LineFromPosition(iSelStart);
    DocLn const iLineEnd = SciCall_LineFromPosition(iSelEnd);

    _BEGIN_UNDO_ACTION_;

    if (Sci_IsMultiOrRectangleSelection() && !bIgnoreSelection) {
        if (SciCall_IsSelectionEmpty()) {
            SciCall_Clear();
        } else {
            const DocPos selAnchorMainPos = SciCall_GetRectangularSelectionAnchor();
            const DocPos selCaretMainPos = SciCall_GetRectangularSelectionCaret();
            const DocPos vSpcAnchorMainPos = SciCall_GetRectangularSelectionAnchorVirtualSpace();
            const DocPos vSpcCaretMainPos = SciCall_GetRectangularSelectionCaretVirtualSpace();

            DocPos iMaxLineLen = Sci_GetRangeMaxLineLength(iLineStart, iLineEnd);
            char* lineBuffer = AllocMem(iMaxLineLen + 1, HEAP_ZERO_MEMORY);
            DocPos remCount = 0;
            if (lineBuffer) {
                const DocPosU selCount = SciCall_GetSelections();
                for (DocPosU s = 0; s < selCount; ++s) {
                    DocPos const selTargetStart = SciCall_GetSelectionNStart(s);
                    DocPos const selTargetEnd = SciCall_GetSelectionNEnd(s);

                    DocPos diff = 0;
                    DocPos len = 0;
                    if (bTrailingBlanksOnly) {
                        len = (selTargetEnd - selTargetStart);
                        if (len > 0) {
                            StringCchCopyNA(lineBuffer, SizeOfMem(lineBuffer), SciCall_GetRangePointer(selTargetStart, len + 1), len);
                            DocPos end = (DocPos)StrCSpnA(lineBuffer, "\r\n");
                            DocPos i = end;
                            while (--i >= 0) {
                                const char ch = lineBuffer[i];
                                if (IsBlankCharA(ch)) {
                                    lineBuffer[i] = '\0';
                                } else {
                                    break;
                                }
                            }
                            while (end < len) {
                                lineBuffer[++i] = lineBuffer[end++];  // add "\r\n" if any
                            }
                            diff = len - (++i);
                            SciCall_SetTargetRange(selTargetStart, selTargetEnd);
                            SciCall_ReplaceTarget(-1, lineBuffer);
                        }
                    } else {
                        DocPos const prevPos = SciCall_PositionBefore(selTargetEnd);
                        diff = (selTargetEnd - prevPos);
                        len = (prevPos - selTargetStart);
                        if (len > 0) {
                            StringCchCopyNA(lineBuffer, iMaxLineLen, SciCall_GetRangePointer(selTargetStart, len + 1), len);
                            SciCall_SetTargetRange(selTargetStart, selTargetEnd);
                            SciCall_ReplaceTarget(len, lineBuffer);
                        }
                    }
                    remCount += diff;
                } // for()
                FreeMem(lineBuffer);
            }

            SciCall_SetRectangularSelectionAnchor(selAnchorMainPos);
            if (vSpcAnchorMainPos > 0) {
                SciCall_SetRectangularSelectionAnchorVirtualSpace(vSpcAnchorMainPos);
            }

            SciCall_SetRectangularSelectionCaret(selCaretMainPos - remCount);
            if (vSpcCaretMainPos > 0) {
                SciCall_SetRectangularSelectionCaretVirtualSpace(vSpcCaretMainPos);
            }
        }
    } else { // SC_SEL_LINES | SC_SEL_STREAM
        for (DocLn iLine = iLineStart; iLine <= iLineEnd; ++iLine) {
            DocPos const iStartPos = SciCall_PositionFromLine(iLine);
            DocPos const iEndPos = SciCall_GetLineEndPosition(iLine);

            if (bTrailingBlanksOnly) {
                DocPos i = iEndPos;
                char ch = '\0';
                do {
                    ch = SciCall_GetCharAt(--i);
                } while ((i >= iStartPos) && IsBlankCharA(ch));
                if ((++i) < iEndPos) {
                    SciCall_SetTargetRange(i, iEndPos);
                    SciCall_ReplaceTarget(0, "");
                }
            } else { // any char at line end
                if (iStartPos < iEndPos) {
                    SciCall_SetTargetRange(SciCall_PositionBefore(iEndPos), iEndPos);
                    SciCall_ReplaceTarget(0, "");
                }

            }
        }
    }
    _END_UNDO_ACTION_;
    
    _RESTORE_TARGET_RANGE_;
}


//=============================================================================
//
//  EditCompressBlanks()
//
void EditCompressBlanks()
{
    _SAVE_TARGET_RANGE_;

    const bool bIsSelEmpty = SciCall_IsSelectionEmpty();

    const DocPos iSelStartPos = SciCall_GetSelectionStart();
    const DocPos iSelEndPos = SciCall_GetSelectionEnd();
    const DocLn iLineStart = SciCall_LineFromPosition(iSelStartPos);
    const DocLn iLineEnd = SciCall_LineFromPosition(iSelEndPos);

    if (SciCall_IsSelectionRectangle()) {
        if (bIsSelEmpty) {
            return;
        }

        const DocPos selAnchorMainPos = SciCall_GetRectangularSelectionAnchor();
        const DocPos selCaretMainPos = SciCall_GetRectangularSelectionCaret();
        const DocPos vSpcAnchorMainPos = SciCall_GetRectangularSelectionAnchorVirtualSpace();
        const DocPos vSpcCaretMainPos = SciCall_GetRectangularSelectionCaretVirtualSpace();

        _BEGIN_UNDO_ACTION_;

        DocPos iMaxLineLen = Sci_GetRangeMaxLineLength(iLineStart, iLineEnd);
        char* lineBuffer = AllocMem(iMaxLineLen + 1, HEAP_ZERO_MEMORY);
        DocPos remCount = 0;
        if (lineBuffer) {
            const DocPosU selCount = SciCall_GetSelections();
            for (DocPosU s = 0; s < selCount; ++s) {
                const DocPos selCaretPos = SciCall_GetSelectionNCaret(s);
                const DocPos selAnchorPos = SciCall_GetSelectionNAnchor(s);
                //const DocPos vSpcCaretPos = SciCall_GetSelectionNCaretVirtualSpace(s);
                //const DocPos vSpcAnchorPos = SciCall_GetSelectionNAnchorVirtualSpace(s);

                const DocPos selTargetStart = (selAnchorPos < selCaretPos) ? selAnchorPos : selCaretPos;
                const DocPos selTargetEnd = (selAnchorPos < selCaretPos) ? selCaretPos : selAnchorPos;
                //const DocPos vSpcLength = (selAnchorPos < selCaretPos) ? (vSpcCaretPos - vSpcAnchorPos) : (vSpcAnchorPos - vSpcCaretPos);

                DocPos diff = 0;
                DocPos const len = (selTargetEnd - selTargetStart);
                if (len >= 0) {
                    char* pText = SciCall_GetRangePointer(selTargetStart, len + 1);
                    const char* pEnd = (pText + len);
                    DocPos i = 0;
                    while (pText < pEnd) {
                        const char ch = *pText++;
                        if (IsBlankCharA(ch)) {
                            lineBuffer[i++] = ' ';
                            while (IsBlankCharA(*pText)) {
                                ++pText;
                            }
                        } else {
                            lineBuffer[i++] = ch;
                        }
                    }
                    lineBuffer[i] = '\0';
                    diff = len - i;
                    SciCall_SetTargetRange(selTargetStart, selTargetEnd);
                    SciCall_ReplaceTarget(-1, lineBuffer);
                }
                remCount += diff;
            } // for()
            FreeMem(lineBuffer);
        }

        SciCall_SetRectangularSelectionAnchor(selAnchorMainPos);
        if (vSpcAnchorMainPos > 0) {
            SciCall_SetRectangularSelectionAnchorVirtualSpace(vSpcAnchorMainPos);
        }
        SciCall_SetRectangularSelectionCaret(selCaretMainPos - remCount);
        if (vSpcCaretMainPos > 0) {
            SciCall_SetRectangularSelectionCaretVirtualSpace(vSpcCaretMainPos);
        }

        _END_UNDO_ACTION_;

    } else if (Sci_IsMultiSelection()) {
        // @@@ not implemented
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELMULTI);
    } else { // SC_SEL_LINES | SC_SEL_STREAM
        const DocPos iCurPos = SciCall_GetCurrentPos();
        const DocPos iAnchorPos = SciCall_GetAnchor();
        const DocPos iSelLength = (iSelEndPos - iSelStartPos);

        bool bIsLineStart = true;
        bool bIsLineEnd = true;

        const char* pszIn = NULL;
        char* pszOut = NULL;
        DocPos cch = 0;
        if (bIsSelEmpty) {
            pszIn = (const char*)SciCall_GetCharacterPointer();
            cch = SciCall_GetTextLength();
            pszOut = AllocMem(cch + 1, HEAP_ZERO_MEMORY);
        } else {
            pszIn = (const char*)SciCall_GetRangePointer(iSelStartPos, iSelLength + 1);
            cch = SciCall_GetSelText(NULL) - 1;
            pszOut = AllocMem(cch + 1, HEAP_ZERO_MEMORY);
            bIsLineStart = (iSelStartPos == SciCall_PositionFromLine(iLineStart));
            bIsLineEnd = (iSelEndPos == SciCall_GetLineEndPosition(iLineEnd));
        }

        if (pszIn && pszOut) {
            bool bModified = false;
            char* co = pszOut;
            DocPos remWSuntilCaretPos = 0;
            for (int i = 0; i < cch; ++i) {
                if (IsBlankCharA(pszIn[i])) {
                    if (pszIn[i] == '\t') {
                        bModified = true;
                    }
                    while (IsBlankCharA(pszIn[i + 1])) {
                        if (bIsSelEmpty && (i < iSelStartPos)) {
                            ++remWSuntilCaretPos;
                        }
                        ++i;
                        bModified = true;
                    }
                    if (!bIsLineStart && ((pszIn[i + 1] != '\n') && (pszIn[i + 1] != '\r'))) {
                        *co++ = ' ';
                    } else {
                        bModified = true;
                    }
                } else {
                    bIsLineStart = (pszIn[i] == '\n' || pszIn[i] == '\r') ? true : false;
                    *co++ = pszIn[i];
                }
            }

            if (bIsLineEnd && (co > pszOut) && (*(co - 1) == ' ')) {
                if (bIsSelEmpty && ((cch - 1) < iSelStartPos)) {
                    --remWSuntilCaretPos;
                }
                *--co = '\0';
                bModified = true;
            }

            if (bModified) {

                _BEGIN_UNDO_ACTION_;

                if (!SciCall_IsSelectionEmpty()) {
                    SciCall_TargetFromSelection();
                } else {
                    SciCall_TargetWholeDocument();
                }
                SciCall_ReplaceTarget(-1, pszOut);

                DocPos const iNewLen = (DocPos)StringCchLenA(pszOut, SizeOfMem(pszOut));

                if (iCurPos < iAnchorPos) {
                    EditSetSelectionEx(iCurPos + iNewLen, iCurPos, -1, -1);
                } else if (iCurPos > iAnchorPos) {
                    EditSetSelectionEx(iAnchorPos, iAnchorPos + iNewLen, -1, -1);
                } else { // empty selection
                    DocPos iNewPos = iCurPos;
                    if (iCurPos > 0) {
                        iNewPos = SciCall_PositionBefore(SciCall_PositionAfter(iCurPos - remWSuntilCaretPos));
                    }
                    EditSetSelectionEx(iNewPos, iNewPos, -1, -1);
                }

                _END_UNDO_ACTION_;
            }
        }
        FreeMem(pszOut);
    }

    _RESTORE_TARGET_RANGE_;
}


//=============================================================================
//
//  EditRemoveBlankLines()
//
void EditRemoveBlankLines(HWND hwnd, bool bMerge, bool bRemoveWhiteSpace)
{
    UNREFERENCED_PARAMETER(hwnd);

    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return;
    }

    const DocPos iSelStart = (SciCall_IsSelectionEmpty() ? 0 : SciCall_GetSelectionStart());
    const DocPos iSelEnd = (SciCall_IsSelectionEmpty() ? Sci_GetDocEndPosition() : SciCall_GetSelectionEnd());

    DocLn iBegLine = SciCall_LineFromPosition(iSelStart);
    DocLn iEndLine = SciCall_LineFromPosition(iSelEnd);

    if (iSelStart > SciCall_PositionFromLine(iBegLine)) {
        ++iBegLine;
    }
    if ((iSelEnd <= SciCall_PositionFromLine(iEndLine)) && (iEndLine != SciCall_GetLineCount() - 1)) {
        --iEndLine;
    }

    _SAVE_TARGET_RANGE_;

    _BEGIN_UNDO_ACTION_;

    for (DocLn iLine = iBegLine; iLine <= iEndLine; ) {
        DocLn nBlanks = 0;
        bool bSpcOnly = true;
        while (((iLine + nBlanks) <= iEndLine) && bSpcOnly) {
            bSpcOnly = false;
            const DocPos posLnBeg = SciCall_PositionFromLine(iLine + nBlanks);
            const DocPos posLnEnd = SciCall_GetLineEndPosition(iLine + nBlanks);
            const DocPos iLnLength = (posLnEnd - posLnBeg);

            if (iLnLength == 0) {
                ++nBlanks;
                bSpcOnly = true;
            } else if (bRemoveWhiteSpace) {
                const char* pLine = SciCall_GetRangePointer(posLnBeg, iLnLength);
                DocPos i = 0;
                for (; i < iLnLength; ++i) {
                    if (!IsBlankCharA(pLine[i])) {
                        break;
                    }
                }
                if (i >= iLnLength) {
                    ++nBlanks;
                    bSpcOnly = true;
                }
            }
        }
        if ((nBlanks == 0) || ((nBlanks == 1) && bMerge)) {
            iLine += (nBlanks + 1);
        } else {
            if (bMerge) {
                --nBlanks;
            }

            SciCall_SetTargetRange(SciCall_PositionFromLine(iLine), SciCall_PositionFromLine(iLine + nBlanks));
            SciCall_ReplaceTarget(0, "");

            if (bMerge) {
                ++iLine;
            }
            iEndLine -= nBlanks;
        }
    }

    _END_UNDO_ACTION_;

    _RESTORE_TARGET_RANGE_;
}


//=============================================================================
//
//  EditRemoveDuplicateLines()
//
void EditRemoveDuplicateLines(HWND hwnd, bool bRemoveEmptyLines)
{
    UNREFERENCED_PARAMETER(hwnd);

    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return;
    }

    _SAVE_TARGET_RANGE_;

    DocPos const iSelStart = SciCall_GetSelectionStart();
    DocPos const iSelEnd = SciCall_GetSelectionEnd();

    DocLn iStartLine = 0;
    DocLn iEndLine = 0;
    if (iSelStart != iSelEnd) {
        iStartLine = SciCall_LineFromPosition(iSelStart);
        if (iSelStart > SciCall_PositionFromLine(iStartLine)) {
            ++iStartLine;
        }
        iEndLine = SciCall_LineFromPosition(iSelEnd);
        if (iSelEnd <= SciCall_PositionFromLine(iEndLine)) {
            --iEndLine;
        }
    } else {
        iEndLine = Sci_GetLastDocLineNumber();
    }

    if ((iEndLine - iStartLine) <= 1) {
        return;
    }

    _BEGIN_UNDO_ACTION_;

    for (DocLn iCurLine = iStartLine; iCurLine < iEndLine; ++iCurLine) {
        DocPos const iCurLnLen = Sci_GetNetLineLength(iCurLine);
        DocPos const iBegCurLine = SciCall_PositionFromLine(iCurLine);
        const char* const pCurrentLine = SciCall_GetRangePointer(iBegCurLine, iCurLnLen + 1);

        if (bRemoveEmptyLines || (iCurLnLen > 0)) {
            DocLn iPrevLine = iCurLine;

            for (DocLn iCompareLine = iCurLine + 1; iCompareLine <= iEndLine; ++iCompareLine) {
                DocPos const iCmpLnLen = Sci_GetNetLineLength(iCompareLine);
                if (bRemoveEmptyLines || (iCmpLnLen > 0)) {
                    DocPos const iBegCmpLine = SciCall_PositionFromLine(iCompareLine);
                    const char* const pCompareLine = SciCall_GetRangePointer(iBegCmpLine, iCmpLnLen + 1);

                    if (iCurLnLen == iCmpLnLen) {
                        if (StringCchCompareNA(pCurrentLine, iCurLnLen, pCompareLine, iCmpLnLen) == 0) {
                            SciCall_SetTargetRange(SciCall_GetLineEndPosition(iPrevLine), SciCall_GetLineEndPosition(iCompareLine));
                            SciCall_ReplaceTarget(0, "");
                            --iCompareLine; // proactive preventing progress to avoid comparison line skip
                            --iEndLine;
                        }
                    }
                } // empty
                iPrevLine = iCompareLine;
            }
        } // empty
    }

    _END_UNDO_ACTION_;

    _RESTORE_TARGET_RANGE_;
}


//=============================================================================
//
//  EditFocusMarkedLinesCmd()
//
void EditFocusMarkedLinesCmd(HWND hwnd, bool bCopy, bool bDelete)
{
    if (!(bCopy || bDelete)) {
        return;    // nothing todo
    }

    DocLn const curLn = Sci_GetCurrentLineNumber();
    int const bitmask = SciCall_MarkerGet(curLn) & OCCURRENCE_MARKER_BITMASK();

    if (!bitmask) {
        return;
    }

    DocLn line = 0;

    if (bCopy) { // --- copy to clipboard ---

        DocPosU copyBufSize = 0;
        while (line >= 0) {
            line = SciCall_MarkerNext(line, bitmask);
            if (line >= 0) {
                int const lnmask = SciCall_MarkerGet(line) & OCCURRENCE_MARKER_BITMASK();
                if (lnmask == bitmask) { // must fit all markers
                    copyBufSize += SciCall_LineLength(line); // incl line-breaks
                }
                ++line; // next
            }
        }

        if (copyBufSize > 0) {
            char *const pchBuffer = (char *const)AllocMem(copyBufSize + 1, HEAP_ZERO_MEMORY);
            if (pchBuffer) {
                // --- collect marked lines ---
                line = 0;
                char *p = pchBuffer;
                while (line >= 0) {
                    line = SciCall_MarkerNext(line, bitmask);
                    if (line >= 0) {
                        int const lnmask = SciCall_MarkerGet(line) & OCCURRENCE_MARKER_BITMASK();
                        if (lnmask == bitmask) { // must fit all markers
                            DocPos const lnBeg = SciCall_PositionFromLine(line);
                            DocPos const lnLen = SciCall_LineLength(line); // incl line-breaks
                            const char *const pszLine = SciCall_GetRangePointer(lnBeg, lnLen);
                            memcpy(p, pszLine, lnLen);
                            p += lnLen;
                        }
                        ++line; // next
                    }
                }
                // --- copy collection to clipboard ---
                int const cchTextW = MultiByteToWideChar(Encoding_SciCP, 0, pchBuffer, -1, NULL, 0);
                if (cchTextW > 0) {
                    bool ok = false;
                    HANDLE const hData = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, ((size_t)cchTextW + 1LL) * sizeof(WCHAR));
                    if (hData) {
                        WCHAR* const pszClipBoard = GlobalLock(hData);
                        if (pszClipBoard) {
                            MultiByteToWideChar(Encoding_SciCP, 0, pchBuffer, -1, pszClipBoard, (int)(cchTextW + 1));
                            GlobalUnlock(hData);
                            if (OpenClipboard(hwnd)) {
                                EmptyClipboard();
                                ok = (SetClipboardData(CF_UNICODETEXT, hData) != NULL); // move ownership
                                CloseClipboard();
                            }
                        }
                        if (!ok) {
                            GlobalFree(hData);
                        }
                    }
                }
                FreeMem(pchBuffer);
            }
        }
    } // bCopy

    if (bDelete) {

        _IGNORE_NOTIFY_CHANGE_;
        SciCall_BeginUndoAction();

        line = 0;
        while (line >= 0) {
            line = SciCall_MarkerNext(line, bitmask);
            if (line >= 0) {
                int const lnmask = SciCall_MarkerGet(line) & OCCURRENCE_MARKER_BITMASK();
                if (lnmask == bitmask) { // must fit all markers
                    SciCall_MarkerDelete(line, -1);
                    DocPos const lnBeg = SciCall_PositionFromLine(line);
                    DocPos const lnLen = SciCall_LineLength(line); // incl line-breaks
                    SciCall_DeleteRange(lnBeg, lnLen); // complete line
                } else {
                    ++line; // next
                }
            }
        }

        SciCall_EndUndoAction();
        _OBSERVE_NOTIFY_CHANGE_;
    }

    SciCall_GotoLine(min_ln(curLn, Sci_GetLastDocLineNumber()));
}


//=============================================================================
//
//  EditWrapToColumn()
//
void EditWrapToColumn(DocPosU nColumn)
{
    _SAVE_TARGET_RANGE_;

    DocPosU const tabWidth = SciCall_GetTabWidth();
    nColumn = clamppu(nColumn, tabWidth, LONG_LINES_MARKER_LIMIT);

    DocPosU iCurPos = SciCall_GetCurrentPos();
    DocPosU iAnchorPos = SciCall_GetAnchor();

    DocPosU iSelStart = 0;
    DocPosU iSelEnd = Sci_GetDocEndPosition();
    DocPosU iSelCount = iSelEnd;

    if (!SciCall_IsSelectionEmpty()) {
        iSelStart = SciCall_GetSelectionStart();
        DocLn iLine = SciCall_LineFromPosition(iSelStart);
        iSelStart = SciCall_PositionFromLine(iLine);   // re-base selection to start of line
        iSelEnd = SciCall_GetSelectionEnd();
        iSelCount = (iSelEnd - iSelStart);
    }

    char* pszText = SciCall_GetRangePointer(iSelStart, iSelCount);

    LPWSTR pszTextW = AllocMem((iSelCount+1)*sizeof(WCHAR), HEAP_ZERO_MEMORY);
    if (pszTextW == NULL) {
        return;
    }

    DocPosU const cchTextW = (DocPosU)MultiByteToWideCharEx(Encoding_SciCP,0,pszText,iSelCount,
                             pszTextW,(SizeOfMem(pszTextW)/sizeof(WCHAR)));

    WCHAR wszEOL[3] = { L'\0' };
    int const cchEOL = Sci_GetCurrentEOL_W(wszEOL);

    DocPosU const convSize = (cchTextW * sizeof(WCHAR) * 3) + (cchEOL * (iSelCount/nColumn + 1)) + 1;
    LPWSTR pszConvW = AllocMem(convSize, HEAP_ZERO_MEMORY);
    if (pszConvW == NULL) {
        FreeMem(pszTextW);
        return;
    }

    // --------------------------------------------------------------------------
    //#define W_DELIMITER  L"!\"#$%&'()*+,-./:;<=>?@[\\]^`{|}~"  // underscore counted as part of word
    const WCHAR* const W_DELIMITER  = Settings.AccelWordNavigation ? W_DelimCharsAccel : W_DelimChars;
#define ISDELIMITER(wc) (!(wc) || StrChrW(W_DELIMITER,(wc)))
    //#define ISWHITE(wc) StrChr(L" \t\f",wc)
    const WCHAR* const W_WHITESPACE = Settings.AccelWordNavigation ? W_WhiteSpaceCharsAccelerated : W_WhiteSpaceCharsDefault;
#define ISWHITE(wc) (!(wc) || StrChrW(W_WHITESPACE,(wc)))
#define ISLINEBREAK(wc) (!(wc) || ((wc) == wszEOL[0]) || ((wc) == wszEOL[1]))
#define ISWORDCHAR(wc) (!ISWHITE(wc) && !ISLINEBREAK(wc) && !ISDELIMITER(wc))
#define ISTAB(wc) ((wc) == L'\t')
    // --------------------------------------------------------------------------

    DocPos iCaretShift = 0;
    bool bModified = false;

    DocPosU  cchConvW = 0;
    DocPosU  iLineLength = 0;

    for (DocPosU iTextW = 0; iTextW < cchTextW; ++iTextW) {
        WCHAR w = pszTextW[iTextW];

        // read complete words
        while (ISWORDCHAR(w) && (iTextW < cchTextW)) {
            pszConvW[cchConvW++] = w;
            ++iLineLength;
            w = pszTextW[++iTextW];
        }

        // read delimiter until column limit
        while (!ISWORDCHAR(w) && (iTextW < cchTextW)) {
            if (ISLINEBREAK(w)) {
                if (w != L'\0') {
                    pszConvW[cchConvW++] = w;
                }
                iLineLength = 0;
            } else if (iLineLength >= nColumn) {
                pszConvW[cchConvW++] = wszEOL[0];
                if (cchEOL > 1) {
                    pszConvW[cchConvW++] = wszEOL[1];
                }
                if (cchConvW <= iCurPos) {
                    iCaretShift += cchEOL;
                }
                bModified = true;
                pszConvW[cchConvW++] = w;
                iLineLength = ISTAB(w) ? tabWidth : 1;
            } else {
                pszConvW[cchConvW++] = w;
                iLineLength += ISTAB(w) ? tabWidth : 1;
            }
            w = pszTextW[++iTextW];
        }

        // does next word exceeds column limit ?
        DocPosU iNextWordLen = 1;
        DocPosU iNextW = iTextW;
        WCHAR w2 = pszTextW[iNextW];
        while (ISWORDCHAR(w2) && (iNextW < cchTextW)) {
            w2 = pszTextW[++iNextW];
            ++iNextWordLen;
        }
        if (w != L'\0') {
            if ((iLineLength + iNextWordLen) >= nColumn) {
                pszConvW[cchConvW++] = wszEOL[0];
                if (cchEOL > 1) {
                    pszConvW[cchConvW++] = wszEOL[1];
                }
                if (cchConvW <= iCurPos) {
                    iCaretShift += cchEOL;
                }
                iLineLength = 0;
                bModified = true;
            }
            pszConvW[cchConvW++] = w;
            iLineLength += ISTAB(w) ? tabWidth : 1;
        }

    }
    FreeMem(pszTextW);

    if (bModified) {
        pszText = AllocMem(cchConvW * 3, HEAP_ZERO_MEMORY);
        if (pszText) {
            DocPosU const cchConvM = WideCharToMultiByteEx(Encoding_SciCP, 0, pszConvW, cchConvW,
                                     pszText, SizeOfMem(pszText), NULL, NULL);

            if (iCurPos < iAnchorPos) {
                iAnchorPos = iSelStart + cchConvM;
            } else if (iCurPos > iAnchorPos) {
                iCurPos = iSelStart + cchConvM;
            } else {
                iCurPos += iCaretShift;
                iAnchorPos = iCurPos;
            }

            _BEGIN_UNDO_ACTION_;
            SciCall_SetTargetRange(iSelStart, iSelEnd);
            SciCall_ReplaceTarget(cchConvM, pszText);
            EditSetSelectionEx(iAnchorPos, iCurPos, -1, -1);
            _END_UNDO_ACTION_;
            FreeMem(pszText);
        }
    }
    FreeMem(pszConvW);

    _RESTORE_TARGET_RANGE_;
}


#if FALSE
//=============================================================================
//
//  EditWrapToColumnForce()
//
void EditWrapToColumnForce(HWND hwnd, DocPosU nColumn/*,int nTabWidth*/)
{
    UNREFERENCED_PARAMETER(hwnd);

    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return;
    }

    _SAVE_TARGET_RANGE_;

    size_t const size = (size_t)nColumn + 1LL;
    char const spc = ' ';
    char* const pTxt = (char* const)AllocMem(size + 1, HEAP_ZERO_MEMORY);
    memset(pTxt, spc, size);
    int const width_pix = SciCall_TextWidth(STYLE_DEFAULT, pTxt);
    FreeMem(pTxt);

    _BEGIN_UNDO_ACTION_;
    if (SciCall_IsSelectionEmpty()) {
        SciCall_TargetWholeDocument();
    } else {
        SciCall_TargetFromSelection();
    }
    SciCall_LinesSplit(width_pix);
    _END_UNDO_ACTION_;
    _RESTORE_TARGET_RANGE_;
}
#endif


//=============================================================================
//
//  EditSplitLines()
//
void EditSplitLines(HWND hwnd)
{
    UNREFERENCED_PARAMETER(hwnd);
    _SAVE_TARGET_RANGE_;
    _BEGIN_UNDO_ACTION_;
    SciCall_TargetFromSelection();
    SciCall_LinesSplit(0);
    _END_UNDO_ACTION_;
    _RESTORE_TARGET_RANGE_;
}


//=============================================================================
//
//  EditJoinLinesEx()
//
//  Customized version of  SCI_LINESJOIN  (w/o using TARGET transaction)
//
void EditJoinLinesEx(bool bPreserveParagraphs, bool bCRLF2Space)
{
    bool bModified = false;

    if (SciCall_IsSelectionEmpty()) {
        return;
    }

    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return;
    }

    _SAVE_TARGET_RANGE_;

    DocPos const iSelStart = SciCall_GetSelectionStart();
    DocPos const iSelEnd = SciCall_GetSelectionEnd();
    DocPos const iSelLength = (iSelEnd - iSelStart);
    DocPos iCurPos = SciCall_GetCurrentPos();
    DocPos iAnchorPos = SciCall_GetAnchor();

    DocPos cchJoin = (DocPos)-1;

    char* pszText = SciCall_GetRangePointer(iSelStart, iSelLength);

    char* pszJoin = AllocMem(iSelLength + 1, HEAP_ZERO_MEMORY);
    if (pszJoin == NULL) {
        return;
    }

    char szEOL[3] = { '\0' };
    int const cchEOL = Sci_GetCurrentEOL_A(szEOL);

    for (int i = 0; i < iSelLength; ++i) {
        int j = i;
        // try to swallow next line-breaks
        while (StrChrA("\r\n", pszText[j])) {
            ++j;
        }

        if (i < j) {
            // swallowed!
            if (((j - i) >= 2*cchEOL) && bPreserveParagraphs) {
                for (int k = 0; k < 2*cchEOL; ++k) {
                    pszJoin[++cchJoin] = szEOL[k % cchEOL];
                }
            } else if (bCRLF2Space) {
                pszJoin[++cchJoin] = ' ';
            }
            i = j;
            bModified = true;
        }
        if (i < iSelLength) {
            pszJoin[++cchJoin] = pszText[i]; // copy char
        }
    }
    ++cchJoin; // start at -1

    if (bModified) {
        if (iAnchorPos > iCurPos) {
            iCurPos = iSelStart;
            iAnchorPos = iSelStart + cchJoin;
        } else {
            iAnchorPos = iSelStart;
            iCurPos = iSelStart + cchJoin;
        }

        _BEGIN_UNDO_ACTION_;
        SciCall_SetTargetRange(iSelStart, iSelEnd);
        SciCall_ReplaceTarget(cchJoin, pszJoin);
        EditSetSelectionEx(iAnchorPos, iCurPos, -1, -1);
        _END_UNDO_ACTION_;
    }
    FreeMem(pszJoin);
    _RESTORE_TARGET_RANGE_;
}


//=============================================================================
//
//  EditSortLines()
//
typedef struct _SORTLINE {
    wchar_t* pwszLine;
    wchar_t* pwszSortEntry;
} SORTLINE;

typedef int (*FNSTRCMP)(const wchar_t*, const wchar_t*);
typedef int (*FNSTRLOGCMP)(const wchar_t*, const wchar_t*);

// ----------------------------------------------------------------------------

int CmpStd(const void *s1, const void *s2)
{
    //~StrCmp()
    int const cmp =      wcscoll_s(((SORTLINE*)s1)->pwszSortEntry, ((SORTLINE*)s2)->pwszSortEntry);
    return (cmp) ? cmp : wcscoll_s(((SORTLINE*)s1)->pwszLine, ((SORTLINE*)s2)->pwszLine);
}

int CmpStdRev(const void* s1, const void* s2)
{
    return -1 * CmpStd(s1, s2);
}


int CmpStdI(const void* s1, const void* s2)
{
    //~StrCmpI()
    int const cmp =      wcsicoll_s(((SORTLINE*)s1)->pwszSortEntry, ((SORTLINE*)s2)->pwszSortEntry);
    return (cmp) ? cmp : wcsicoll_s(((SORTLINE*)s1)->pwszLine, ((SORTLINE*)s2)->pwszLine);
}

int CmpStdIRev(const void* s1, const void* s2)
{
    return -1 * CmpStdI(s1, s2);
}

// ----------------------------------------------------------------------------

int CmpLexicographical(const void *s1, const void *s2)
{
    int const cmp =      wcscmp_s(((SORTLINE*)s1)->pwszSortEntry, ((SORTLINE*)s2)->pwszSortEntry);
    return (cmp) ? cmp : wcscmp_s(((SORTLINE*)s1)->pwszLine, ((SORTLINE*)s2)->pwszLine);
}

//int CmpLexicographicalI(const void* s1, const void* s2) {
//  int const cmp = _wcsicmp(((SORTLINE*)s1)->pwszSortEntry, ((SORTLINE*)s2)->pwszSortEntry);
//  return (cmp) ? cmp : _wcsicmp(((SORTLINE*)s1)->pwszLine, ((SORTLINE*)s2)->pwszLine);
//}

int CmpLexicographicalRev(const void* s1, const void* s2)
{
    return -1 * CmpLexicographical(s1, s2);
}

//int CmpLexicographicalIRev(const void* s1, const void* s2) { return -1 * CmpLexicographicalI(s1, s2); }

// non inlined for function pointer
static int _wcscmp_s(const wchar_t* s1, const wchar_t* s2)
{
    return wcscmp_s(s1, s2);
}
static int _wcscoll_s(const wchar_t* s1, const wchar_t* s2)
{
    return wcscoll_s(s1, s2);
}
static int _wcsicmp_s(const wchar_t* s1, const wchar_t* s2)
{
    return wcsicmp_s(s1, s2);
}
static int _wcsicoll_s(const wchar_t* s1, const wchar_t* s2)
{
    return wcsicoll_s(s1, s2);
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

int CmpStdLogical(const void *s1, const void *s2)
{
    if (StrIsNotEmpty(s1) && StrIsNotEmpty(s2)) {
        int cmp = StrCmpLogicalW(((SORTLINE*)s1)->pwszSortEntry, ((SORTLINE*)s2)->pwszSortEntry);
        if (cmp == 0) {
            cmp = StrCmpLogicalW(((SORTLINE*)s1)->pwszLine, ((SORTLINE*)s2)->pwszLine);
        }
        return (cmp) ? cmp : CmpStd(s1, s2);
    } else {
        return (StrIsNotEmpty(s1) ? 1 : (StrIsNotEmpty(s2) ? -1 : 0));
    }
}

int CmpStdLogicalRev(const void* s1, const void* s2)
{
    return -1 * CmpStdLogical(s1, s2);
}

// ----------------------------------------------------------------------------

void EditSortLines(HWND hwnd, int iSortFlags)
{
    if (SciCall_IsSelectionEmpty()) {
        return;    // no selection
    }

    _SAVE_TARGET_RANGE_;

    bool const bIsMultiSel = Sci_IsMultiOrRectangleSelection();

    DocPos const iSelStart = SciCall_GetSelectionStart(); //iSelStart = SciCall_PositionFromLine(iLine);
    DocPos const iSelEnd = SciCall_GetSelectionEnd();
    //DocLn const iLine = SciCall_LineFromPosition(iSelStart);

    DocPos iCurPos = bIsMultiSel ? SciCall_GetRectangularSelectionCaret() : SciCall_GetCurrentPos();
    DocPos iAnchorPos = bIsMultiSel ? SciCall_GetRectangularSelectionAnchor() : SciCall_GetAnchor();
    DocPos iCurPosVS = bIsMultiSel ? SciCall_GetRectangularSelectionCaretVirtualSpace() : 0;
    DocPos iAnchorPosVS = bIsMultiSel ? SciCall_GetRectangularSelectionAnchorVirtualSpace() : 0;

    DocLn const iRcCurLine = bIsMultiSel ? SciCall_LineFromPosition(iCurPos) : 0;
    DocLn const iRcAnchorLine = bIsMultiSel ? SciCall_LineFromPosition(iAnchorPos) : 0;

    DocPos const iCurCol = SciCall_GetColumn(iCurPos);
    DocPos const iAnchorCol = SciCall_GetColumn(iAnchorPos);
    DocLn const iSortColumn = bIsMultiSel ? min_p(iCurCol, iAnchorCol) : (UINT)SciCall_GetColumn(iCurPos);

    DocLn const iLineStart = bIsMultiSel ? min_ln(iRcCurLine, iRcAnchorLine) : SciCall_LineFromPosition(iSelStart);
    DocLn const _lnend = bIsMultiSel ? max_ln(iRcCurLine, iRcAnchorLine) : SciCall_LineFromPosition(iSelEnd);
    DocLn const iLineEnd = (iSelEnd <= SciCall_PositionFromLine(_lnend)) ? (_lnend - 1) : _lnend;
    if (iLineEnd <= iLineStart) {
        return;
    }

    DocLn const iLineCount = iLineEnd - iLineStart + 1;

    char mszEOL[3] = { '\0' };
    Sci_GetCurrentEOL_A(mszEOL);

    int const _iTabWidth = SciCall_GetTabWidth();

    if (bIsMultiSel) {
        EditPadWithSpaces(hwnd, !(iSortFlags & SORT_SHUFFLE), true);
        // changed rectangular selection
        iCurPos = SciCall_GetRectangularSelectionCaret();
        iAnchorPos = SciCall_GetRectangularSelectionAnchor();
        iCurPosVS = SciCall_GetRectangularSelectionCaretVirtualSpace();
        iAnchorPosVS = SciCall_GetRectangularSelectionAnchorVirtualSpace();
    }

    SORTLINE* pLines = AllocMem(sizeof(SORTLINE) * iLineCount, HEAP_ZERO_MEMORY);
    if (!pLines) {
        return;
    }

    DocPos iMaxLineLen = Sci_GetRangeMaxLineLength(iLineStart, iLineEnd);
    char* pmsz = AllocMem(iMaxLineLen + 1, HEAP_ZERO_MEMORY);

    int ichlMax = 3;
    DocPos cchTotal = 0;
    DocLn iZeroLenLineCount = 0;
    for (DocLn i = 0, iLn = iLineStart; iLn <= iLineEnd; ++iLn, ++i) {

        int const cchm = (int)SciCall_LineLength(iLn);
        cchTotal += cchm;
        ichlMax = max_i(ichlMax, cchm);

        SciCall_GetLine_Safe(iLn, pmsz);

        if (iSortFlags & SORT_REMWSPACELN) {
            StrTrimA(pmsz, "\t\v \r\n"); // try clean line
            if (StrIsEmptyA(pmsz)) {
                // white-space only - remove
                continue;
            }
        }
        StrTrimA(pmsz, "\r\n"); // ignore line-breaks

        int const cchw = MultiByteToWideChar(Encoding_SciCP, 0, pmsz, -1, NULL, 0);
        if (cchw > 1) {
            DocLn tabs = _iTabWidth;
            ptrdiff_t const lnLen = (sizeof(WCHAR) * cchw);
            pLines[i].pwszLine = AllocMem(lnLen, HEAP_ZERO_MEMORY);
            MultiByteToWideChar(Encoding_SciCP, 0, pmsz, -1, pLines[i].pwszLine, cchw);
            pLines[i].pwszSortEntry = pLines[i].pwszLine;
            if (iSortFlags & SORT_COLUMN) {
                DocLn col = 0;
                while (*(pLines[i].pwszSortEntry)) {
                    if (*(pLines[i].pwszSortEntry) == L'\t') {
                        if (col + tabs <= iSortColumn) {
                            col += tabs;
                            tabs = _iTabWidth;
                            pLines[i].pwszSortEntry = CharNext(pLines[i].pwszSortEntry);
                        } else {
                            break;
                        }
                    } else if (col < iSortColumn) {
                        col++;
                        if (--tabs == 0) {
                            tabs = _iTabWidth;
                        }
                        pLines[i].pwszSortEntry = CharNext(pLines[i].pwszSortEntry);
                    } else {
                        break;
                    }
                }
            }
        } else {
            ++iZeroLenLineCount;
        }
    }
    FreeMem(pmsz);

    if (iSortFlags & SORT_ASCENDING) {
        if (iSortFlags & SORT_NOCASE) {
            qsort(pLines, iLineCount, sizeof(SORTLINE), CmpStdI);
        } else if (iSortFlags & SORT_LOGICAL) {
            qsort(pLines, iLineCount, sizeof(SORTLINE), CmpStdLogical);
        } else if (iSortFlags & SORT_LEXICOGRAPH) {
            qsort(pLines, iLineCount, sizeof(SORTLINE), CmpLexicographical);
        } else {
            qsort(pLines, iLineCount, sizeof(SORTLINE), CmpStd);
        }
    } else if (iSortFlags & SORT_DESCENDING) {
        if (iSortFlags & SORT_NOCASE) {
            qsort(pLines, iLineCount, sizeof(SORTLINE), CmpStdIRev);
        } else if (iSortFlags & SORT_LOGICAL) {
            qsort(pLines, iLineCount, sizeof(SORTLINE), CmpStdLogicalRev);
        } else if (iSortFlags & SORT_LEXICOGRAPH) {
            qsort(pLines, iLineCount, sizeof(SORTLINE), CmpLexicographicalRev);
        } else {
            qsort(pLines, iLineCount, sizeof(SORTLINE), CmpStdRev);
        }
    } else { /*if (iSortFlags & SORT_SHUFFLE)*/
        srand((UINT)GetTickCount());
        for (DocLn i = (iLineCount - 1); i > 0; --i) {
            int j = rand() % i;
            SORTLINE sLine = { NULL, NULL };
            sLine.pwszLine = pLines[i].pwszLine;
            sLine.pwszSortEntry = pLines[i].pwszSortEntry;
            pLines[i] = pLines[j];
            pLines[j].pwszLine = sLine.pwszLine;
            pLines[j].pwszSortEntry = sLine.pwszSortEntry;
        }
    }

    DocLn const lenRes = cchTotal + (2 * iLineCount) + 1;
    char* pmszResult = AllocMem(lenRes, HEAP_ZERO_MEMORY);
    char* pmszResOffset = pmszResult;
    char* pmszBuf = AllocMem((size_t)ichlMax + 1LL, HEAP_ZERO_MEMORY);

    FNSTRCMP const pFctStrCmp = (iSortFlags & SORT_NOCASE) ? ((iSortFlags & SORT_LEXICOGRAPH) ? _wcsicmp_s : _wcsicoll_s) :
                                ((iSortFlags & SORT_LEXICOGRAPH) ? _wcscmp_s  : _wcscoll_s);

    bool bLastDup = false;
    for (DocLn i = 0; i < iLineCount; ++i) {
        if (pLines[i].pwszLine && ((iSortFlags & SORT_SHUFFLE) || StrIsNotEmpty(pLines[i].pwszLine))) {
            bool bDropLine = false;
            if (!(iSortFlags & SORT_SHUFFLE)) {
                if (iSortFlags & SORT_MERGEDUP || iSortFlags & SORT_UNIQDUP || iSortFlags & SORT_UNIQUNIQ) {
                    if (i < (iLineCount - 1)) {
                        if (pFctStrCmp(pLines[i].pwszLine, pLines[i + 1].pwszLine) == 0) {
                            bLastDup = true;
                            bDropLine = (iSortFlags & SORT_MERGEDUP || iSortFlags & SORT_UNIQDUP);
                        } else {
                            bDropLine = (!bLastDup && (iSortFlags & SORT_UNIQUNIQ)) || (bLastDup && (iSortFlags & SORT_UNIQDUP));
                            bLastDup = false;
                        }
                    } else {
                        bDropLine = (!bLastDup && (iSortFlags & SORT_UNIQUNIQ)) || (bLastDup && (iSortFlags & SORT_UNIQDUP));
                        bLastDup = false;
                    }
                }
            }
            if (!bDropLine) {
                WideCharToMultiByte(Encoding_SciCP, 0, pLines[i].pwszLine, -1, pmszBuf, (ichlMax + 1), NULL, NULL);
                StringCchCatA(pmszResult, lenRes, pmszBuf);
                StringCchCatA(pmszResult, lenRes, mszEOL);
            }
        }
    }
    FreeMem(pmszBuf);

    // Handle empty (no whitespace or other char) lines (always at the end)
    if (!(iSortFlags & SORT_UNIQDUP) || (iZeroLenLineCount == 0)) {
        StrTrimA(pmszResOffset, "\r\n"); // trim end only
    }
    if (((iSortFlags & SORT_UNIQDUP) && (iZeroLenLineCount > 1)) || (iSortFlags & SORT_MERGEDUP)) {
        iZeroLenLineCount = 1; // removes duplicate empty lines
    }
    if (!(iSortFlags & SORT_REMZEROLEN)) {
        for (DocLn i = 0; i < iZeroLenLineCount; ++i) {
            StringCchCatA(pmszResult, lenRes, mszEOL);
        }
    }

    for (DocLn i = 0; i < iLineCount; ++i) {
        FreeMem(pLines[i].pwszLine);
    }
    FreeMem(pLines);

    //DocPos const iResultLength = (DocPos)StringCchLenA(pmszResult, lenRes) + ((cEOLMode == SC_EOL_CRLF) ? 2 : 1);
    if (!bIsMultiSel) {
        if (iAnchorPos > iCurPos) {
            iCurPos = SciCall_FindColumn(iLineStart, iCurCol);
            iAnchorPos = SciCall_FindColumn(_lnend, iAnchorCol);
        } else {
            iAnchorPos = SciCall_FindColumn(iLineStart, iAnchorCol);
            iCurPos = SciCall_FindColumn(_lnend, iCurCol);
        }
    }

    _BEGIN_UNDO_ACTION_;
    //SciCall_SetTargetRange(SciCall_PositionFromLine(iLineStart), SciCall_PositionFromLine(iLineEnd + 1));
    SciCall_SetTargetRange(SciCall_PositionFromLine(iLineStart), SciCall_GetLineEndPosition(iLineEnd));
    SciCall_ReplaceTarget(-1, pmszResult);
    FreeMem(pmszResult);
    if (bIsMultiSel) {
        EditSetSelectionEx(iAnchorPos, iCurPos, iAnchorPosVS, iCurPosVS);
    } else {
        EditSetSelectionEx(iAnchorPos, iCurPos, -1, -1);
    }
    _END_UNDO_ACTION_;
    _RESTORE_TARGET_RANGE_;
}



//=============================================================================
//
//  _EnsureRangeVisible()
//
static void _EnsureRangeVisible(const DocPos iAnchorPos, const DocPos iCurrentPos) {

    DocLn const iAnchorLine = SciCall_LineFromPosition(iAnchorPos);
    DocLn const iCurrentLine = SciCall_LineFromPosition(iCurrentPos);
    if (iAnchorLine != iCurrentLine) {
        if (!SciCall_GetLineVisible(iAnchorLine)) {
            SciCall_EnsureVisible(iAnchorLine);
        }
    }
    if (!SciCall_GetLineVisible(iCurrentLine)) {
        SciCall_EnsureVisibleEnforcePolicy(iCurrentLine);
    }
}


//=============================================================================
//
//  EditEnsureSelectionVisible()
//
void EditEnsureSelectionVisible() {
    DocPos const iAnchorPos = SciCall_GetAnchor();
    DocPos const iCurrentPos = SciCall_GetCurrentPos();
    _EnsureRangeVisible(iAnchorPos, iCurrentPos);
    SciCall_ScrollRange(iAnchorPos, iCurrentPos);
}


//=============================================================================
//
//  EditSetSelectionEx()
//
void EditSetSelectionEx(DocPos iAnchorPos, DocPos iCurrentPos, DocPos vSpcAnchor, DocPos vSpcCurrent)
{
    //~~~_BEGIN_UNDO_ACTION_;~~~

    if ((iAnchorPos < 0) && (iCurrentPos < 0)) {
        SciCall_SelectAll();
    } else {
        if (iAnchorPos < 0) {
            iAnchorPos = 0;
        }
        if (iCurrentPos < 0) {
            iCurrentPos = Sci_GetDocEndPosition();
        }

        // Ensure that the first and last lines of a selection are always unfolded
        // This needs to be done *before* the SCI_SETSEL message
        _EnsureRangeVisible(iAnchorPos, iCurrentPos);

        if ((vSpcAnchor >= 0) && (vSpcCurrent >= 0)) {
            SciCall_SetRectangularSelectionAnchor(iAnchorPos);
            if (vSpcAnchor > 0) {
                SciCall_SetRectangularSelectionAnchorVirtualSpace(vSpcAnchor);
            }
            SciCall_SetRectangularSelectionCaret(iCurrentPos);
            if (vSpcCurrent > 0) {
                SciCall_SetRectangularSelectionCaretVirtualSpace(vSpcCurrent);
            }
            SciCall_ScrollRange(iAnchorPos, iCurrentPos);
        } else {
            SciCall_SetSel(iAnchorPos, iCurrentPos);  // scrolls into view
        }
        SciCall_ChooseCaretX();
    }
    //~~~_END_UNDO_ACTION_;~~~
}


//=============================================================================
//
//  EditEnsureConsistentLineEndings()
//
void EditEnsureConsistentLineEndings(HWND hwnd)
{
    SciCall_ConvertEOLs(SciCall_GetEOLMode());
    Globals.bDocHasInconsistentEOLs = false;
    EditFixPositions(hwnd);
}


//=============================================================================
//
//  EditJumpTo()
//
void EditJumpTo(DocLn iNewLine, DocPos iNewCol)
{
    // Line maximum is iMaxLine - 1 (doc line count starts with 0)
    DocLn const iMaxLine = SciCall_GetLineCount() - 1;

    // jump to end with line set to -1
    if ((iNewLine < 0) || (iNewLine > iMaxLine)) {
        SciCall_DocumentEnd();
        return;
    }
    if (iNewLine == 0) {
        iNewLine = 1;
    }

    iNewLine = (min_ln(iNewLine, iMaxLine) - 1);
    DocPos const iLineEndPos = SciCall_GetLineEndPosition(iNewLine);

    // Column minimum is 1
    DocPos const colOffset = Globals.bZeroBasedColumnIndex ? 0 : 1;
    iNewCol = clampp((iNewCol - colOffset), 0, iLineEndPos);

    Sci_GotoPosChooseCaret(SciCall_FindColumn(iNewLine, iNewCol));
}


//=============================================================================
//
//  EditFixPositions()
//
void EditFixPositions()
{
    DocPos const iCurrentPos = SciCall_GetCurrentPos();
    DocPos const iAnchorPos = SciCall_GetAnchor();
    DocPos const iMaxPos = Sci_GetDocEndPosition();

    DocPos iNewPos = iCurrentPos;

    if ((iCurrentPos > 0) && (iCurrentPos <= iMaxPos)) {
        iNewPos = SciCall_PositionAfter(SciCall_PositionBefore(iCurrentPos));

        if (iNewPos != iCurrentPos) {
            SciCall_SetCurrentPos(iNewPos);
        }
    }

    if ((iAnchorPos != iNewPos) && (iAnchorPos > 0) && (iAnchorPos <= iMaxPos)) {
        iNewPos = SciCall_PositionAfter(SciCall_PositionBefore(iAnchorPos));
        if (iNewPos != iAnchorPos) {
            SciCall_SetAnchor(iNewPos);
        }
    }
}


//=============================================================================
//
//  EditGetExcerpt()
//
void EditGetExcerpt(HWND hwnd,LPWSTR lpszExcerpt,DWORD cchExcerpt)
{
    UNREFERENCED_PARAMETER(hwnd);

    const DocPos iCurPos = SciCall_GetCurrentPos();
    const DocPos iAnchorPos = SciCall_GetAnchor();

    if ((iCurPos == iAnchorPos) || Sci_IsMultiOrRectangleSelection()) {
        StringCchCopy(lpszExcerpt,cchExcerpt,L"");
        return;
    }

    WCHAR tch[256] = { L'\0' };
    struct Sci_TextRange tr = { { 0, 0 }, NULL };
    /*if (iCurPos != iAnchorPos && !Sci_IsMultiOrRectangleSelection()) {*/
    tr.chrg.cpMin = (DocPosCR)SciCall_GetSelectionStart();
    tr.chrg.cpMax = min_cr((tr.chrg.cpMin + (DocPosCR)COUNTOF(tch)), (DocPosCR)SciCall_GetSelectionEnd());
    /*}
    else {
      int iLine = SciCall_LineFromPosition(iCurPos);
      tr.chrg.cpMin = SciCall_PositionFromLine(iLine);
      tr.chrg.cpMax = min_cr(SciCall_GetLineEndPosition(iLine),(LONG)(tr.chrg.cpMin + COUNTOF(tchBuf2)));
    }*/
    tr.chrg.cpMax = min_cr(tr.chrg.cpMax, (DocPosCR)Sci_GetDocEndPosition());

    size_t const len = ((size_t)tr.chrg.cpMax - (size_t)tr.chrg.cpMin);
    char*  pszText  = AllocMem(len+1LL, HEAP_ZERO_MEMORY);
    LPWSTR pszTextW = AllocMem((len+1LL) * sizeof(WCHAR), HEAP_ZERO_MEMORY);

    DWORD cch = 0;
    if (pszText && pszTextW) {
        tr.lpstrText = pszText;
        DocPos const rlen = SciCall_GetTextRange(&tr);
        MultiByteToWideCharEx(Encoding_SciCP,0,pszText,rlen,pszTextW,len);

        for (WCHAR* p = pszTextW; *p && cch < COUNTOF(tch)-1; p++) {
            if (*p == L'\r' || *p == L'\n' || *p == L'\t' || *p == L' ') {
                tch[cch++] = L' ';
                while (*(p+1) == L'\r' || *(p+1) == L'\n' || *(p+1) == L'\t' || *(p+1) == L' ') {
                    p++;
                }
            } else {
                tch[cch++] = *p;
            }
        }
        tch[cch++] = L'\0';
        StrTrim(tch,L" ");
    }

    if (cch == 1) {
        StringCchCopy(tch,COUNTOF(tch),L" ... ");
    }

    if ((cch > cchExcerpt) && (cchExcerpt >= 4)) {
        tch[cchExcerpt-2] = L'.';
        tch[cchExcerpt-3] = L'.';
        tch[cchExcerpt-4] = L'.';
    }
    StringCchCopyN(lpszExcerpt,cchExcerpt,tch,cchExcerpt);

    FreeMem(pszText);
    FreeMem(pszTextW);
}


//=============================================================================
//
//  _SetSearchFlags()
//
static void  _SetSearchFlags(HWND hwnd, LPEDITFINDREPLACE lpefr)
{
    if (lpefr) {
        if (hwnd) {
            char szBuf[FNDRPL_BUFFER] = { '\0' };
            bool bIsFindDlg = (GetDlgItem(Globals.hwndDlgFindReplace, IDC_REPLACE) == NULL);

            ComboBox_GetTextW2MB(hwnd, IDC_FINDTEXT, szBuf, COUNTOF(szBuf));
            if (StringCchCompareXA(szBuf, lpefr->szFind) != 0) {
                StringCchCopyA(lpefr->szFind, COUNTOF(lpefr->szFind), szBuf);
                lpefr->bStateChanged = true;
            }

            ComboBox_GetTextW2MB(hwnd, IDC_REPLACETEXT, szBuf, COUNTOF(szBuf));
            if (StringCchCompareXA(szBuf, lpefr->szReplace) != 0) {
                StringCchCopyA(lpefr->szReplace, COUNTOF(lpefr->szReplace), szBuf);
                lpefr->bStateChanged = true;
            }


            bool bIsFlagSet = ((lpefr->fuFlags & SCFIND_MATCHCASE) != 0);
            if (IsButtonChecked(hwnd, IDC_FINDCASE)) {
                if (!bIsFlagSet) {
                    lpefr->fuFlags |= SCFIND_MATCHCASE;
                    lpefr->bStateChanged = true;
                }
            } else {
                if (bIsFlagSet) {
                    lpefr->fuFlags &= ~(SCFIND_MATCHCASE);
                    lpefr->bStateChanged = true;
                }
            }

            bIsFlagSet = ((lpefr->fuFlags & SCFIND_WHOLEWORD) != 0);
            if (IsButtonChecked(hwnd, IDC_FINDWORD)) {
                if (!bIsFlagSet) {
                    lpefr->fuFlags |= SCFIND_WHOLEWORD;
                    lpefr->bStateChanged = true;
                }
            } else {
                if (bIsFlagSet) {
                    lpefr->fuFlags &= ~(SCFIND_WHOLEWORD);
                    lpefr->bStateChanged = true;
                }
            }

            bIsFlagSet = ((lpefr->fuFlags & SCFIND_WORDSTART) != 0);
            if (IsButtonChecked(hwnd, IDC_FINDSTART)) {
                if (!bIsFlagSet) {
                    lpefr->fuFlags |= SCFIND_WORDSTART;
                    lpefr->bStateChanged = true;
                }
            } else {
                if (bIsFlagSet) {
                    lpefr->fuFlags &= ~(SCFIND_WORDSTART);
                    lpefr->bStateChanged = true;
                }
            }

            bIsFlagSet = lpefr->bRegExprSearch;
            if (IsButtonChecked(hwnd, IDC_FINDREGEXP)) {
                if (!bIsFlagSet) {
                    lpefr->bRegExprSearch = true;
                    lpefr->fuFlags |= SCFIND_REGEXP;
                    lpefr->bStateChanged = true;
                }
            } else {
                if (bIsFlagSet) {
                    lpefr->bRegExprSearch = false;
                    lpefr->fuFlags &= ~SCFIND_REGEXP;
                    lpefr->bStateChanged = true;
                }
            }

            if (IsDialogControlEnabled(hwnd, IDC_DOT_MATCH_ALL)) {
                bIsFlagSet = ((lpefr->fuFlags & SCFIND_DOT_MATCH_ALL) != 0);
                if (IsButtonChecked(hwnd, IDC_DOT_MATCH_ALL)) {
                    if (!bIsFlagSet) {
                        lpefr->fuFlags |= SCFIND_DOT_MATCH_ALL;
                        lpefr->bStateChanged = true;
                    }
                } else {
                    if (bIsFlagSet) {
                        lpefr->fuFlags &= ~SCFIND_DOT_MATCH_ALL;
                        lpefr->bStateChanged = true;
                    }
                }
            }

            // force consistency
            if (lpefr->bRegExprSearch) {
                CheckDlgButton(hwnd, IDC_WILDCARDSEARCH, BST_UNCHECKED);
            }
            bIsFlagSet = lpefr->bWildcardSearch;
            if (IsButtonChecked(hwnd, IDC_WILDCARDSEARCH)) {
                if (!bIsFlagSet) {
                    lpefr->bWildcardSearch = true;
                    lpefr->fuFlags |= SCFIND_REGEXP;  // Wildcard search based on RegExpr
                    lpefr->bStateChanged = true;
                }
            } else {
                if (bIsFlagSet) {
                    lpefr->bWildcardSearch = false;
                    if (!(lpefr->bRegExprSearch)) {
                        lpefr->fuFlags &= ~SCFIND_REGEXP;
                    }
                    lpefr->bStateChanged = true;
                }
            }

            bIsFlagSet = lpefr->bOverlappingFind;
            if (IsButtonChecked(hwnd, IDC_FIND_OVERLAPPING)) {
                if (!bIsFlagSet) {
                    lpefr->bOverlappingFind = true;
                    lpefr->bStateChanged = false; // no effect on state
                }
            } else {
                if (bIsFlagSet) {
                    lpefr->bOverlappingFind = false;
                    lpefr->bStateChanged = false; // no effect on state
                }
            }

            bIsFlagSet = lpefr->bNoFindWrap;
            if (IsButtonChecked(hwnd, IDC_NOWRAP)) {
                if (!bIsFlagSet) {
                    lpefr->bNoFindWrap = true;
                    lpefr->bStateChanged = true;
                }
            } else {
                if (bIsFlagSet) {
                    lpefr->bNoFindWrap = false;
                    lpefr->bStateChanged = true;
                }
            }

            bIsFlagSet = lpefr->bMarkOccurences;
            if (IsButtonChecked(hwnd, IDC_ALL_OCCURRENCES)) {
                if (!bIsFlagSet) {
                    lpefr->bMarkOccurences = true;
                    lpefr->bStateChanged = true;
                }
            } else {
                if (bIsFlagSet) {
                    lpefr->bMarkOccurences = false;
                    lpefr->bStateChanged = true;
                }
            }

            if (IsDialogControlEnabled(hwnd, IDC_FINDTRANSFORMBS)) {
                bIsFlagSet = lpefr->bTransformBS;
                if (IsButtonChecked(hwnd, IDC_FINDTRANSFORMBS)) {
                    if (!bIsFlagSet) {
                        lpefr->bTransformBS = true;
                        lpefr->bStateChanged = true;
                    }
                } else {
                    if (bIsFlagSet) {
                        lpefr->bTransformBS = false;
                        lpefr->bStateChanged = true;
                    }
                }
            }

            if (bIsFindDlg) {
                bIsFlagSet = lpefr->bFindClose;
                if (IsButtonChecked(hwnd, IDC_FINDCLOSE)) {
                    if (!bIsFlagSet) {
                        lpefr->bFindClose = true;
                        lpefr->bStateChanged = true;
                    }
                } else {
                    if (bIsFlagSet) {
                        lpefr->bFindClose = false;
                        lpefr->bStateChanged = true;
                    }
                }
            } else { // replace close
                bIsFlagSet = lpefr->bReplaceClose;
                if (IsButtonChecked(hwnd, IDC_FINDCLOSE)) {
                    if (!bIsFlagSet) {
                        lpefr->bReplaceClose = true;
                        lpefr->bStateChanged = true;
                    }
                } else {
                    if (bIsFlagSet) {
                        lpefr->bReplaceClose = false;
                        lpefr->bStateChanged = true;
                    }
                }
            }
        } // if hwnd
    }
}


// Wildcard search uses the regexp engine to perform a simple search with * ? as wildcards
// instead of more advanced and user-unfriendly regexp syntax
// for speed, we only need POSIX syntax here
static void  _EscapeWildcards(char* szFind2, size_t cch, LPEDITFINDREPLACE lpefr)
{
    char *const szWildcardEscaped = (char *)AllocMem((cch<<1) + 1, HEAP_ZERO_MEMORY);
    if (szWildcardEscaped) {
        size_t iSource = 0;
        size_t iDest = 0;

        lpefr->fuFlags |= SCFIND_REGEXP;

        while ((iSource < cch) && (szFind2[iSource] != '\0')) {
            char c = szFind2[iSource];
            if (c == '*') {
                szWildcardEscaped[iDest++] = '.';
            } else if (c == '?') {
                c = '.';
            } else {
                if (c == '^' ||
                        c == '$' ||
                        c == '(' ||
                        c == ')' ||
                        c == '[' ||
                        c == ']' ||
                        c == '{' ||
                        c == '}' ||
                        c == '.' ||
                        c == '+' ||
                        c == '|' ||
                        c == '\\') {
                    szWildcardEscaped[iDest++] = '\\';
                }
            }
            szWildcardEscaped[iDest++] = c;
            ++iSource;
        }

        StringCchCopyNA(szFind2, cch, szWildcardEscaped, SizeOfMem(szWildcardEscaped));

        FreeMem(szWildcardEscaped);
    }
}


//=============================================================================
//
//  _EditGetFindStrg()
//
static size_t _EditGetFindStrg(HWND hwnd, LPEDITFINDREPLACE lpefr, LPSTR szFind, size_t cchCnt)
{
    if (!lpefr) {
        return 0;
    }
    if (!StrIsEmptyA(lpefr->szFind)) {
        StringCchCopyA(szFind, cchCnt, lpefr->szFind);
    } else {
        CopyFindPatternMB(szFind, cchCnt);
    }
    if (StrIsEmptyA(szFind)) {
        // get most recently used find pattern
        WCHAR mruItem[FNDRPL_BUFFER] = { L'\0' };
        MRU_Enum(Globals.pMRUfind, 0, mruItem, COUNTOF(mruItem));
        if (StrIsNotEmpty(mruItem)) {
            WideCharToMultiByte(Encoding_SciCP, 0, mruItem, -1, szFind, (int)cchCnt, NULL, NULL);
        }
    }
    if (StrIsEmptyA(szFind)) {
        // get clipboard content
        char *const pClip = EditGetClipboardText(hwnd, false, NULL, NULL);
        if (!StrIsEmptyA(pClip)) {
            StringCchCopyA(szFind, cchCnt, pClip);
        }
        FreeMem(pClip);
    }
    if (StrIsEmptyA(szFind)) {
        return 0;
    }

    // ensure to F/R-dialog data structure consistency
    StringCchCopyA(lpefr->szFind, COUNTOF(lpefr->szFind), szFind);

    bool const bIsRegEx = (lpefr->fuFlags & SCFIND_REGEXP);
    if (lpefr->bTransformBS || bIsRegEx) {
        TransformBackslashes(szFind, bIsRegEx, Encoding_SciCP, NULL);
    }
    if (!StrIsEmptyA(szFind) && (lpefr->bWildcardSearch)) {
        _EscapeWildcards(szFind, cchCnt, lpefr);
    }

    return StringCchLenA(szFind, cchCnt);
}


//=============================================================================
//
//  _FindInTarget()
//
static DocPos  _FindInTarget(LPCSTR szFind, DocPos length, int sFlags,
                             DocPos* begin, DocPos* end, bool bForceNext, FR_UPD_MODES fMode)
{
    UNREFERENCED_PARAMETER(bForceNext);

    DocPos iPos = -1LL; // not found

    if ((length < 1LL) || StrIsEmptyA(szFind)) {
        return iPos;
    }

    // remember original target range
    DocPos const saveTargetBeg = SciCall_GetTargetStart();
    DocPos const saveTargetEnd = SciCall_GetTargetEnd();

    DocPos start = *begin;
    DocPos stop  = *end;
    bool const bFindNext = (start <= stop); // else find previous

    SciCall_SetSearchFlags(sFlags);
    SciCall_SetTargetRange(start, stop);
    iPos = SciCall_SearchInTarget(length, szFind);
    //  handle next in case of zero-length-matches (regex) !
    if (iPos == start) {
        DocPos const nstop = SciCall_GetTargetEnd();
        if ((start == nstop) && bForceNext) {
            DocPos const new_start = (bFindNext ? SciCall_PositionAfter(start) : SciCall_PositionBefore(start));
            bool const bProceed = (bFindNext ? (new_start < stop) : (new_start > stop));
            if ((new_start != start) && bProceed) {
                SciCall_SetTargetRange(new_start, stop);
                iPos = SciCall_SearchInTarget(length, szFind);
            } else {
                iPos = (DocPos)-1LL; // already at document begin or end => not found
            }
        }
    }
    if (iPos >= 0) {
        if (fMode != FRMOD_IGNORE) {
            Globals.FindReplaceMatchFoundState = bFindNext ?
                                                 ((fMode == FRMOD_WRAPED) ? NXT_WRP_FND : NXT_FND) :
                                                 ((fMode == FRMOD_WRAPED) ? PRV_WRP_FND : PRV_FND);
        }
        // found in range, set begin and end of finding
        *begin = SciCall_GetTargetStart();
        *end = SciCall_GetTargetEnd();
    } else {
        if (fMode != FRMOD_IGNORE) {
            Globals.FindReplaceMatchFoundState = (fMode != FRMOD_WRAPED) ? (bFindNext ? NXT_NOT_FND : PRV_NOT_FND) : FND_NOP;
        }
    }

    SciCall_SetTargetRange(saveTargetBeg, saveTargetEnd); // restore original target range

    return iPos;
}


//=============================================================================
//
//  _FindHasMatch()
//
typedef enum { MATCH = 0, NO_MATCH = 1, INVALID = 2 } RegExResult_t;

static RegExResult_t _FindHasMatch(HWND hwnd, LPEDITFINDREPLACE lpefr, DocPos iStartPos, bool bMarkAll)
{
    char szFind[FNDRPL_BUFFER] = { '\0' };
    DocPos const slen = _EditGetFindStrg(hwnd, lpefr, szFind, COUNTOF(szFind));
    if (slen == 0) {
        return NO_MATCH;
    }
    int const sFlags = (int)(lpefr->fuFlags);

    DocPos const iStart = iStartPos;
    DocPos const iTextEnd = Sci_GetDocEndPosition();

    DocPos start = iStart;
    DocPos end = iTextEnd;
    DocPos const iPos = _FindInTarget(szFind, slen, sFlags, &start, &end, false, FRMOD_IGNORE);

    if (bMarkAll) {
        EditClearAllOccurrenceMarkers(hwnd);
        if (iPos >= 0) {
            EditMarkAll(szFind, (int)(lpefr->fuFlags), 0, iTextEnd, false);
            if (FocusedView.HideNonMatchedLines) {
                EditFoldMarkedLineRange(lpefr->hwnd, true);
            }
        } else {
            if (FocusedView.HideNonMatchedLines) {
                EditFoldMarkedLineRange(lpefr->hwnd, false);
            }
        }
    }
    return ((iPos >= 0) ? MATCH : ((iPos == (DocPos)(-1)) ? NO_MATCH : INVALID));
}


//=============================================================================
//
//  _DelayMarkAll()
//
//
static void  _DelayMarkAll(int delay)
{
    static CmdMessageQueue_t mqc = MQ_WM_CMD_INIT(IDT_TIMER_CALLBACK_MRKALL, 0LL);
    if (mqc.hwnd != Globals.hwndDlgFindReplace) {
        mqc.hwnd = Globals.hwndDlgFindReplace;
        //mqc.lparam = 0LL; // start position always 0
    }
    _MQ_AppendCmd(&mqc, _MQ_ms2cycl(delay));
}

//=============================================================================

static bool s_SaveMarkOccurrences = false;
static bool s_SaveMarkMatchVisible = false;

//=============================================================================
//
//  EditBoxForPasteFixes()
//
static LRESULT CALLBACK EditBoxForPasteFixes(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
        UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    WCHAR* const s_wchBuffer = (WCHAR*)dwRefData;

    if (s_wchBuffer) {
        switch (uMsg) {
        case WM_PASTE: {
            EditGetClipboardW(s_wchBuffer, FNDRPL_BUFFER);
            SendMessage(hwnd, EM_REPLACESEL, (WPARAM)TRUE, (LPARAM)s_wchBuffer);
        }
        return TRUE;

        //case WM_LBUTTONDOWN:
        //  SendMessage(hwnd, EM_REPLACESEL, (WPARAM)TRUE, (LPARAM)L"X");
        //  return TRUE;

        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, EditBoxForPasteFixes, uIdSubclass);
            break;

        default:
            break;
        }
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}


//=============================================================================
//
//  _ShowZeroLengthCallTip()
//
static void _ShowZeroLengthCallTip(DocPos iPosition)
{
    char chZeroLenCT[80] = { '\0' };
    GetLngStringW2MB(IDS_MUI_ZERO_LEN_MATCH, chZeroLenCT, COUNTOF(chZeroLenCT));
    SciCall_CallTipShow(iPosition, chZeroLenCT);
}


//=============================================================================
//
//  EditFindReplaceDlgProc()
//
extern int    g_flagMatchText;

static INT_PTR CALLBACK EditFindReplaceDlgProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam)
{
    static EDITFINDREPLACE s_efrSave = INIT_EFR_DATA;
    static LPEDITFINDREPLACE s_pEfrDataDlg = NULL;
    static bool s_bIsReplaceDlg = false;

    static UINT_PTR pTimerIdentifier = 0;

    static WCHAR s_tchBuf[FNDRPL_BUFFER] = { L'\0' }; // tmp working buffer

    static DocPos s_InitialSearchStart = 0;
    static DocPos s_InitialAnchorPos = 0;
    static DocPos s_InitialCaretPos = 0;
    static DocLn s_InitialTopLine = -1;

#define SET_INITIAL_ANCHORS() {\
        s_InitialTopLine = -1;\
        s_InitialCaretPos = SciCall_GetCurrentPos();\
        s_InitialAnchorPos = SciCall_GetAnchor();\
        s_InitialSearchStart = SciCall_GetSelectionStart(); \
    }

    static RegExResult_t s_anyMatch = NO_MATCH;

    static HBRUSH hBrushRed;
    static HBRUSH hBrushGreen;
    static HBRUSH hBrushBlue;

    switch (umsg) {
    case WM_INITDIALOG: {

        Globals.hwndDlgFindReplace = hwnd;
        s_bIsReplaceDlg = (GetDlgItem(hwnd, IDC_REPLACETEXT) != NULL);

        // clear cmd line stuff
        g_flagMatchText = 0;
        s_pEfrDataDlg = NULL;

        // the global static Find/Replace data structure
        SetWindowLongPtr(hwnd, DWLP_USER, (LONG_PTR)lParam);

        SetDialogIconNP3(hwnd);
        InitWindowCommon(hwnd, true);

#ifdef D_NP3_WIN10_DARK_MODE
        if (UseDarkMode()) {
            int const ctlx[] = { IDOK, IDCANCEL, IDC_FINDPREV, IDC_REPLACE, IDC_REPLACEALL,
                                 IDC_REPLACEINSEL, IDC_SWAPSTRG, IDC_TOGGLE_VISIBILITY
                               };
            for (int i = 0; i < COUNTOF(ctlx); ++i) {
                SetExplorerTheme(GetDlgItem(hwnd, ctlx[i]));
            }
            int const ctl[] = { IDC_FINDCASE, IDC_FINDWORD, IDC_FINDSTART, IDC_FINDTRANSFORMBS, IDC_FINDESCCTRLCHR, IDC_REPLESCCTRLCHR,
                                IDC_FINDREGEXP, IDC_DOT_MATCH_ALL, IDC_FIND_OVERLAPPING, IDC_NOWRAP, IDC_FINDCLOSE,
                                IDC_ALL_OCCURRENCES, IDC_WILDCARDSEARCH, IDC_TRANSPARENT, IDC_STATIC, IDC_STATIC2
                              };
            for (int i = 0; i < COUNTOF(ctl); ++i) {
                SetWindowTheme(GetDlgItem(hwnd, ctl[i]), L"", L""); // remove theme for BS_AUTORADIOBUTTON
            }
            //SetExplorerTheme(GetDlgItem(hwnd, IDC_RESIZEGRIP));
        }
#endif

        pTimerIdentifier = SetTimer(NULL, 0, _MQ_TIMER_CYCLE, MQ_ExecuteNext);

        SET_INITIAL_ANCHORS()
        s_InitialTopLine = SciCall_GetFirstVisibleLine();

        EditSetCaretToSelectionStart(); // avoid search text selection jumps to next match (before ResizeDlg_InitX())

        s_pEfrDataDlg = (LPEDITFINDREPLACE)GetWindowLongPtr(hwnd, DWLP_USER);

        Globals.iReplacedOccurrences = 0;
        Globals.FindReplaceMatchFoundState = FND_NOP;

        s_SaveMarkOccurrences = s_bSwitchedFindReplace ? s_SaveMarkOccurrences : Settings.MarkOccurrences;
        s_SaveMarkMatchVisible = s_bSwitchedFindReplace ? s_SaveMarkMatchVisible : Settings.MarkOccurrencesMatchVisible;
        // switch off normal mark occurrences
        Settings.MarkOccurrences = false;
        Settings.MarkOccurrencesMatchVisible = false;
        EnableCmd(GetMenu(Globals.hwndMain), IDM_VIEW_MARKOCCUR_ONOFF, false);

        // Load MRUs
        for (int i = 0; i < MRU_Count(Globals.pMRUfind); i++) {
            MRU_Enum(Globals.pMRUfind, i, s_tchBuf, COUNTOF(s_tchBuf));
            SendDlgItemMessage(hwnd, IDC_FINDTEXT, CB_ADDSTRING, 0, (LPARAM)s_tchBuf);
        }

        SendDlgItemMessage(hwnd, IDC_FINDTEXT, CB_LIMITTEXT, FNDRPL_BUFFER, 0);
        SendDlgItemMessage(hwnd, IDC_FINDTEXT, CB_SETEXTENDEDUI, true, 0);

        COMBOBOXINFO cbInfoF = { sizeof(COMBOBOXINFO) };
        GetComboBoxInfo(GetDlgItem(hwnd, IDC_FINDTEXT), &cbInfoF);
        if (cbInfoF.hwndItem) {
            SetWindowSubclass(cbInfoF.hwndItem, EditBoxForPasteFixes, 0, (DWORD_PTR) &(s_tchBuf[0]));
            SHAutoComplete(cbInfoF.hwndItem, SHACF_FILESYS_ONLY | SHACF_AUTOAPPEND_FORCE_OFF | SHACF_AUTOSUGGEST_FORCE_OFF);
        }

        if (!GetWindowTextLengthW(GetDlgItem(hwnd, IDC_FINDTEXT))) {
            if (!StrIsEmptyA(s_pEfrDataDlg->szFind)) {
                ComboBox_SetTextMB2W(hwnd, IDC_FINDTEXT, s_pEfrDataDlg->szFind);
            }
        }

        if (s_bIsReplaceDlg) {

            // Load MRUs
            for (int i = 0; i < MRU_Count(Globals.pMRUreplace); i++) {
                MRU_Enum(Globals.pMRUreplace, i, s_tchBuf, COUNTOF(s_tchBuf));
                SendDlgItemMessage(hwnd, IDC_REPLACETEXT, CB_ADDSTRING, 0, (LPARAM)s_tchBuf);
            }

            SendDlgItemMessage(hwnd, IDC_REPLACETEXT, CB_LIMITTEXT, FNDRPL_BUFFER, 0);
            SendDlgItemMessage(hwnd, IDC_REPLACETEXT, CB_SETEXTENDEDUI, true, 0);

            COMBOBOXINFO cbInfoR = { sizeof(COMBOBOXINFO) };
            GetComboBoxInfo(GetDlgItem(hwnd, IDC_REPLACETEXT), &cbInfoR);
            if (cbInfoR.hwndItem) {
                SetWindowSubclass(cbInfoR.hwndItem, EditBoxForPasteFixes, 0, (DWORD_PTR) &(s_tchBuf[0]));
                SHAutoComplete(cbInfoR.hwndItem, SHACF_FILESYS_ONLY | SHACF_AUTOAPPEND_FORCE_OFF | SHACF_AUTOSUGGEST_FORCE_OFF);
            }

            if (!StrIsEmptyA(s_pEfrDataDlg->szReplace)) {
                ComboBox_SetTextMB2W(hwnd, IDC_REPLACETEXT, s_pEfrDataDlg->szReplace);
            }
        }

        CheckDlgButton(hwnd, IDC_FINDREGEXP, SetBtn(s_pEfrDataDlg->bRegExprSearch));

        bool const bDotMatchAll = (s_pEfrDataDlg->fuFlags & SCFIND_DOT_MATCH_ALL) != 0;
        CheckDlgButton(hwnd, IDC_DOT_MATCH_ALL, SetBtn(s_pEfrDataDlg->bRegExprSearch && bDotMatchAll));
        DialogEnableControl(hwnd, IDC_DOT_MATCH_ALL, s_pEfrDataDlg->bRegExprSearch);

        if (s_pEfrDataDlg->bRegExprSearch) {
            s_pEfrDataDlg->bWildcardSearch = false;
        }
        if (s_pEfrDataDlg->bWildcardSearch) {
            CheckDlgButton(hwnd, IDC_WILDCARDSEARCH, BST_CHECKED);
            CheckDlgButton(hwnd, IDC_FINDREGEXP, BST_UNCHECKED);
        }

        // transform BS handled by regex (wildcard search based on):
        CheckDlgButton(hwnd, IDC_FINDTRANSFORMBS, SetBtn(s_pEfrDataDlg->bTransformBS || s_pEfrDataDlg->bRegExprSearch || s_pEfrDataDlg->bWildcardSearch));
        DialogEnableControl(hwnd, IDC_FINDTRANSFORMBS, !(s_pEfrDataDlg->bRegExprSearch || s_pEfrDataDlg->bWildcardSearch));

        CheckDlgButton(hwnd, IDC_FIND_OVERLAPPING, SetBtn(s_pEfrDataDlg->bOverlappingFind));

        CheckDlgButton(hwnd, IDC_ALL_OCCURRENCES, SetBtn(s_pEfrDataDlg->bMarkOccurences));
        if (!s_pEfrDataDlg->bMarkOccurences) {
            EditClearAllOccurrenceMarkers(s_pEfrDataDlg->hwnd);
            Globals.iMarkOccurrencesCount = 0;
        }

        CheckDlgButton(hwnd, IDC_FINDCASE, SetBtn(s_pEfrDataDlg->fuFlags & SCFIND_MATCHCASE));
        CheckDlgButton(hwnd, IDC_FINDWORD, SetBtn(s_pEfrDataDlg->fuFlags & SCFIND_WHOLEWORD));
        CheckDlgButton(hwnd, IDC_FINDSTART, SetBtn(s_pEfrDataDlg->fuFlags & SCFIND_WORDSTART));
        CheckDlgButton(hwnd, IDC_NOWRAP, SetBtn(s_pEfrDataDlg->bNoFindWrap));

        if (s_bIsReplaceDlg) {
            if (s_bSwitchedFindReplace) {
                CheckDlgButton(hwnd, IDC_FINDCLOSE, SetBtn(s_pEfrDataDlg->bFindClose));
            } else {
                CheckDlgButton(hwnd, IDC_FINDCLOSE, SetBtn(s_pEfrDataDlg->bReplaceClose));
            }
        } else {
            if (s_bSwitchedFindReplace) {
                CheckDlgButton(hwnd, IDC_FINDCLOSE, SetBtn(s_pEfrDataDlg->bReplaceClose));
            } else {
                CheckDlgButton(hwnd, IDC_FINDCLOSE, SetBtn(s_pEfrDataDlg->bFindClose));
            }
        }

        CheckDlgButton(hwnd, IDC_TRANSPARENT, SetBtn(Settings.FindReplaceTransparentMode));

        if (!s_bSwitchedFindReplace) {
            if (Settings.FindReplaceDlgPosX == CW_USEDEFAULT || Settings.FindReplaceDlgPosY == CW_USEDEFAULT) {
                CenterDlgInParent(hwnd, NULL);
            } else {
                SetDlgPos(hwnd, Settings.FindReplaceDlgPosX, Settings.FindReplaceDlgPosY);
            }
        } else {
            SetDlgPos(hwnd, s_xFindReplaceDlgSave, s_yFindReplaceDlgSave);
            s_bSwitchedFindReplace = false;
            CopyMemory(s_pEfrDataDlg, &s_efrSave, sizeof(EDITFINDREPLACE));
        }

        WCHAR wchMenuBuf[80] = {L'\0'};
        HMENU hmenu = GetSystemMenu(hwnd, false);

        GetLngString(IDS_MUI_SAVEPOS, wchMenuBuf, COUNTOF(wchMenuBuf));
        InsertMenu(hmenu, 0, MF_BYPOSITION | MF_STRING | MF_ENABLED, IDS_MUI_SAVEPOS, wchMenuBuf);
        GetLngString(IDS_MUI_RESETPOS, wchMenuBuf, COUNTOF(wchMenuBuf));
        InsertMenu(hmenu, 1, MF_BYPOSITION | MF_STRING | MF_ENABLED, IDS_MUI_RESETPOS, wchMenuBuf);
        InsertMenu(hmenu, 2, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
        GetLngString(IDS_MUI_CLEAR_FIND_HISTORY, wchMenuBuf, COUNTOF(wchMenuBuf));
        InsertMenu(hmenu, 3, MF_BYPOSITION | MF_STRING | MF_ENABLED, IDS_MUI_CLEAR_FIND_HISTORY, wchMenuBuf);
        GetLngString(IDS_MUI_CLEAR_REPL_HISTORY, wchMenuBuf, COUNTOF(wchMenuBuf));
        InsertMenu(hmenu, 4, MF_BYPOSITION | MF_STRING | MF_ENABLED, IDS_MUI_CLEAR_REPL_HISTORY, wchMenuBuf);
        InsertMenu(hmenu, 5, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);


        hBrushRed = CreateSolidBrush(rgbRedColorRef);
        hBrushGreen = CreateSolidBrush(rgbGreenColorRef);
        hBrushBlue = CreateSolidBrush(rgbBlueColorRef);

        s_anyMatch = NO_MATCH;

        _SetSearchFlags(hwnd, s_pEfrDataDlg); // sync
        s_pEfrDataDlg->bStateChanged = true;  // force update

        DialogEnableControl(hwnd, IDC_TOGGLE_VISIBILITY, s_pEfrDataDlg->bMarkOccurences);

        _DelayMarkAll(_MQ_STD);

        PostMessage(hwnd, WM_THEMECHANGED, 0, 0);
    }
    return TRUE; // (!) further processing

    case WM_ENABLE:
        // modal child dialog should disable main window too
        EnableWindow(Globals.hwndMain, (BOOL)wParam);
        return TRUE;

    case WM_DESTROY: {

        KillTimer(NULL, pTimerIdentifier);
        pTimerIdentifier = 0;

        _SetSearchFlags(hwnd, s_pEfrDataDlg); // sync
        CopyMemory(&(Settings.EFR_Data), s_pEfrDataDlg, sizeof(EDITFINDREPLACE));  // remember options

        if (!s_bSwitchedFindReplace) {
            if (s_anyMatch == MATCH) {
                // Save MRUs
                if (!StrIsEmptyA(s_pEfrDataDlg->szFind)) {
                    if (GetDlgItemText(hwnd, IDC_FINDTEXT, s_tchBuf, COUNTOF(s_tchBuf))) {
                        MRU_Add(Globals.pMRUfind, s_tchBuf, 0, -1, -1, NULL);
                        SetFindPattern(s_tchBuf);
                    }
                }
            }

            Globals.iReplacedOccurrences = 0;
            Globals.FindReplaceMatchFoundState = FND_NOP;

            Settings.MarkOccurrences = s_SaveMarkOccurrences;
            Settings.MarkOccurrencesMatchVisible = s_SaveMarkMatchVisible;
            EnableCmd(GetMenu(Globals.hwndMain), IDM_VIEW_MARKOCCUR_ONOFF, true);

            if (FocusedView.HideNonMatchedLines) {
                EditToggleView(s_pEfrDataDlg->hwnd);
            }

            if (IsMarkOccurrencesEnabled()) {
                MarkAllOccurrences(_MQ_STD, true);
            } else {
                EditClearAllOccurrenceMarkers(s_pEfrDataDlg->hwnd);
                Globals.iMarkOccurrencesCount = 0;
            }

            if (s_InitialTopLine >= 0) {
                SciCall_SetFirstVisibleLine(s_InitialTopLine);
                s_InitialTopLine = -1;  // reset
            } else {
                if (s_anyMatch == NO_MATCH) {
                    EditSetSelectionEx(s_InitialAnchorPos, s_InitialCaretPos, -1, -1);
                } else {
                    EditEnsureSelectionVisible();
                }
            }

            CmdMessageQueue_t* pmqc = NULL;
            CmdMessageQueue_t* dummy;
            DL_FOREACH_SAFE(MessageQueue, pmqc, dummy) {
                DL_DELETE(MessageQueue, pmqc);
                FreeMem(pmqc);
            }
        }

        DeleteObject(hBrushRed);
        DeleteObject(hBrushGreen);
        DeleteObject(hBrushBlue);
        s_pEfrDataDlg = NULL;
        Globals.hwndDlgFindReplace = NULL;
    }
    return FALSE;


    case WM_DPICHANGED: {
        UINT const dpi = LOWORD(wParam);
        UpdateWindowLayoutForDPI(hwnd, (RECT *)lParam, dpi);
    }
    return TRUE; // further processing


#ifdef D_NP3_WIN10_DARK_MODE

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORBTN:
    //~case WM_CTLCOLOREDIT:
    //~case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
        return SetDarkModeCtlColors((HDC)wParam, UseDarkMode());
        break;

    case WM_SETTINGCHANGE:
        if (IsDarkModeSupported() && IsColorSchemeChangeMessage(lParam)) {
            SendMessage(hwnd, WM_THEMECHANGED, 0, 0);
        }
        break;

    case WM_THEMECHANGED:
        if (IsDarkModeSupported()) {
            bool const darkModeEnabled = CheckDarkModeEnabled();
            AllowDarkModeForWindowEx(hwnd, darkModeEnabled);
            RefreshTitleBarThemeColor(hwnd);
            int const ctlx[] = { IDOK, IDCANCEL, IDC_FINDPREV, IDC_REPLACE, IDC_SWAPSTRG,
                                 IDC_REPLACEALL, IDC_REPLACEINSEL, IDC_TOGGLE_VISIBILITY
                               };
            for (int i = 0; i < COUNTOF(ctlx); ++i) {
                HWND const hBtn = GetDlgItem(hwnd, ctlx[i]);
                AllowDarkModeForWindowEx(hBtn, darkModeEnabled);
                SendMessage(hBtn, WM_THEMECHANGED, 0, 0);
            }
            UpdateWindowEx(hwnd);
        }
        break;

#endif

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        if (!s_pEfrDataDlg) {
            return false;
        }
        HWND hCheck = (HWND)lParam;
        HDC hDC = (HDC)wParam;

        HWND hComboBox = GetDlgItem(hwnd, IDC_FINDTEXT);
        COMBOBOXINFO ci = { sizeof(COMBOBOXINFO) };
        GetComboBoxInfo(hComboBox, &ci);

        //if (hCheck == ci.hwndItem || hCheck == ci.hwndList)
        if (hCheck == ci.hwndItem) {
            SetBkMode(hDC, TRANSPARENT);
            INT_PTR hBrush;
            switch (s_anyMatch) {
            case MATCH:
                //SetTextColor(hDC, green);
                SetBkColor(hDC, rgbGreenColorRef);
                hBrush = (INT_PTR)hBrushGreen;
                break;
            case NO_MATCH:
                //SetTextColor(hDC, blue);
                SetBkColor(hDC, rgbBlueColorRef);
                hBrush = (INT_PTR)hBrushBlue;
                break;
            case INVALID:
            default:
                //SetTextColor(hDC, red);
                SetBkColor(hDC, rgbRedColorRef);
                hBrush = (INT_PTR)hBrushRed;
                break;
            }
            return hBrush;
        }
    }
#ifdef D_NP3_WIN10_DARK_MODE
    return SetDarkModeCtlColors((HDC)wParam, UseDarkMode());
#else
    return FALSE;
#endif


    case WM_ACTIVATE: {
        if (!s_pEfrDataDlg) {
            return false;
        }

        switch (LOWORD(wParam)) {
        case WA_INACTIVE:
            SetWindowTransparentMode(hwnd, Settings.FindReplaceTransparentMode, Settings2.FindReplaceOpacityLevel);
            break;

        case WA_CLICKACTIVE:
        // mouse click activation
        case WA_ACTIVE:
            SetWindowTransparentMode(hwnd, false, 100);

            // selection changed ?
            if ((SciCall_GetCurrentPos() != s_InitialCaretPos) ||
                    (SciCall_GetAnchor() != s_InitialAnchorPos)) {
                EditSetCaretToSelectionStart();
                s_InitialAnchorPos = SciCall_GetAnchor();
                s_InitialCaretPos = SciCall_GetCurrentPos();
                s_InitialTopLine = SciCall_GetFirstVisibleLine();
                s_InitialSearchStart = s_InitialCaretPos;
                if (s_pEfrDataDlg) {
                    s_pEfrDataDlg->bStateChanged = true;
                }
            }

            bool const bEnableReplInSel = !(SciCall_IsSelectionEmpty() || Sci_IsMultiOrRectangleSelection());
            DialogEnableControl(hwnd, IDC_REPLACEINSEL, bEnableReplInSel);

            _DelayMarkAll(_MQ_STD);

            if (!SciCall_IsSelectionEmpty()) {
                EditEnsureSelectionVisible();
            }
            /// don't do:  ///~SendWMCommandEx(hwnd, IDC_FINDTEXT, CBN_EDITCHANGE);
            break;

        default:
            break;
        }
    }
    return false;


    case WM_COMMAND: {
        if (!s_pEfrDataDlg) {
            return FALSE;
        }

        switch (LOWORD(wParam)) {

        case IDC_DOC_MODIFIED:
            s_InitialSearchStart = SciCall_GetSelectionStart();
            s_InitialTopLine = -1;  // reset
            s_pEfrDataDlg->bStateChanged = true;
            _DelayMarkAll(_MQ_STD);
            break;


        case IDC_FINDTEXT:
        case IDC_REPLACETEXT: {

            bool bPatternChanged = false;
            if (Globals.bFindReplCopySelOrClip) {
                char* lpszSelection = NULL;
                DocPos const cchSelection = SciCall_GetSelText(NULL);
                if ((cchSelection > 1) && (LOWORD(wParam) != IDC_REPLACETEXT)) {
                    lpszSelection = AllocMem(cchSelection + 1, HEAP_ZERO_MEMORY);
                    SciCall_GetSelText(lpszSelection);
                } else { // (cchSelection <= 1)
                    // nothing is selected in the editor:
                    // if first time you bring up find/replace dialog,
                    // use most recent search pattern to find box
                    lpszSelection = AllocMem(FNDRPL_BUFFER, HEAP_ZERO_MEMORY);
                    if (lpszSelection) {
                        _EditGetFindStrg(Globals.hwndEdit, s_pEfrDataDlg, lpszSelection, SizeOfMem(lpszSelection));
                    }
                }

                if (lpszSelection) {
                    ComboBox_SetTextMB2W(hwnd, IDC_FINDTEXT, lpszSelection);
                    FreeMem(lpszSelection);
                    lpszSelection = NULL;
                    bPatternChanged = true;
                }
                s_InitialTopLine = -1;  // reset
                s_anyMatch = NO_MATCH;
                Globals.bFindReplCopySelOrClip = false;

            } // Globals.bFindReplCopySelOrClip

            switch (HIWORD(wParam)) {
            case CBN_CLOSEUP:
            case CBN_EDITCHANGE:
                bPatternChanged = (LOWORD(wParam) == IDC_FINDTEXT);
                break;
            default:
                break;
            }

            if (!bPatternChanged) {
                break;
            }

            bool const bEmptyFnd = (ComboBox_GetTextLenth(hwnd, IDC_FINDTEXT) ||
                                    CB_ERR != SendDlgItemMessage(hwnd, IDC_FINDTEXT, CB_GETCURSEL, 0, 0));

            bool const bEmptyRpl = (ComboBox_GetTextLenth(hwnd, IDC_REPLACETEXT) ||
                                    CB_ERR != SendDlgItemMessage(hwnd, IDC_REPLACETEXT, CB_GETCURSEL, 0, 0));

            bool const bEmptySel = !(SciCall_IsSelectionEmpty() || Sci_IsMultiOrRectangleSelection());

            DialogEnableControl(hwnd, IDOK, bEmptyFnd);
            DialogEnableControl(hwnd, IDC_FINDPREV, bEmptyFnd);
            DialogEnableControl(hwnd, IDC_REPLACE, bEmptyFnd);
            DialogEnableControl(hwnd, IDC_REPLACEALL, bEmptyFnd);
            DialogEnableControl(hwnd, IDC_REPLACEINSEL, bEmptyFnd && bEmptySel);
            DialogEnableControl(hwnd, IDC_SWAPSTRG, bEmptyFnd || bEmptyRpl);

            if (!bEmptyFnd) {
                s_anyMatch = NO_MATCH;
                EditSetSelectionEx(s_InitialAnchorPos, s_InitialCaretPos, -1, -1);
            }

            if (HIWORD(wParam) == CBN_CLOSEUP) {
                LONG lSelEnd = 0;
                SendDlgItemMessage(hwnd, LOWORD(wParam), CB_GETEDITSEL, 0, (LPARAM)&lSelEnd);
                SendDlgItemMessage(hwnd, LOWORD(wParam), CB_SETEDITSEL, 0, MAKELPARAM(lSelEnd, lSelEnd));
            }

            _SetSearchFlags(hwnd, s_pEfrDataDlg);

            if (StrIsEmptyA(s_pEfrDataDlg->szFind)) {
                SetFindPattern(L"");
            }

            DocPos start = s_InitialSearchStart;
            DocPos end = Sci_GetDocEndPosition();
            DocPos const slen = StringCchLenA(s_pEfrDataDlg->szFind, COUNTOF(s_pEfrDataDlg->szFind));
            DocPos const iPos = _FindInTarget(s_pEfrDataDlg->szFind, slen, (int)(s_pEfrDataDlg->fuFlags), &start, &end, false, FRMOD_NORM);
            if (iPos >= 0) {
                if (s_bIsReplaceDlg) {
                    SciCall_ScrollRange(end, iPos);
                } else {
                    EditSetSelectionEx(end, iPos, -1, -1);
                }
                if (iPos == end) {
                    _ShowZeroLengthCallTip(iPos);
                }
            } else {
                if (s_bIsReplaceDlg) {
                    SciCall_ScrollRange(s_InitialAnchorPos, s_InitialCaretPos);
                } else {
                    EditSetSelectionEx(s_InitialAnchorPos, s_InitialCaretPos, -1, -1);
                }
                if (s_InitialTopLine >= 0) {
                    SciCall_SetFirstVisibleLine(s_InitialTopLine);
                }
            }
            _DelayMarkAll(_MQ_STD);
        }
        break;

        case IDT_TIMER_CALLBACK_MRKALL: {
            //DocPos const startPos = (DocPos)lParam;
            s_anyMatch = _FindHasMatch(s_pEfrDataDlg->hwnd, s_pEfrDataDlg, 0, false);
            InvalidateRect(GetDlgItem(hwnd, IDC_FINDTEXT), NULL, TRUE); // coloring
            if (s_pEfrDataDlg->bMarkOccurences) {
                static char s_lastFind[FNDRPL_BUFFER] = { L'\0' };
                if (s_pEfrDataDlg->bStateChanged || (StringCchCompareXA(s_lastFind, s_pEfrDataDlg->szFind) != 0)) {
                    StringCchCopyA(s_lastFind, COUNTOF(s_lastFind), s_pEfrDataDlg->szFind);
                    _FindHasMatch(s_pEfrDataDlg->hwnd, s_pEfrDataDlg, 0, s_pEfrDataDlg->bMarkOccurences);
                    if (FocusedView.HideNonMatchedLines) {
                        EditToggleView(s_pEfrDataDlg->hwnd);
                    }
                }
            } else if (s_pEfrDataDlg->bStateChanged) {
                if (FocusedView.HideNonMatchedLines) {
                    SendWMCommand(hwnd, IDC_TOGGLE_VISIBILITY);
                } else {
                    EditClearAllOccurrenceMarkers(s_pEfrDataDlg->hwnd);
                }
            }
            s_pEfrDataDlg->bStateChanged = false;
        }
        break;

        case IDC_ALL_OCCURRENCES: {
            _SetSearchFlags(hwnd, s_pEfrDataDlg);

            if (IsButtonChecked(hwnd, IDC_ALL_OCCURRENCES)) {
                DialogEnableControl(hwnd, IDC_TOGGLE_VISIBILITY, true);
                _DelayMarkAll(_MQ_STD);
            } else { // switched OFF
                DialogEnableControl(hwnd, IDC_TOGGLE_VISIBILITY, false);
                if (FocusedView.HideNonMatchedLines) {
                    EditToggleView(s_pEfrDataDlg->hwnd);
                }
                EditClearAllOccurrenceMarkers(s_pEfrDataDlg->hwnd);
                Globals.iMarkOccurrencesCount = 0;
                InvalidateRect(GetDlgItem(hwnd, IDC_FINDTEXT), NULL, TRUE);
            }
        }
        break;

        case IDC_TOGGLE_VISIBILITY:
            if (s_pEfrDataDlg) {
                EditToggleView(s_pEfrDataDlg->hwnd);
                if (!FocusedView.HideNonMatchedLines) {
                    s_pEfrDataDlg->bStateChanged = true;
                    s_InitialTopLine = -1;  // reset
                    EditClearAllOccurrenceMarkers(s_pEfrDataDlg->hwnd);
                    _DelayMarkAll(_MQ_STD);
                }
            }
            break;

        case IDC_FINDREGEXP:
            if (IsButtonChecked(hwnd, IDC_FINDREGEXP)) {
                DialogEnableControl(hwnd, IDC_DOT_MATCH_ALL, true);
                // Can not use wildcard search together with regexp
                CheckDlgButton(hwnd, IDC_WILDCARDSEARCH, BST_UNCHECKED);
                // transform BS handled by regex
                CheckDlgButton(hwnd, IDC_FINDTRANSFORMBS, BST_CHECKED);
                DialogEnableControl(hwnd, IDC_FINDTRANSFORMBS, false);
            } else { // unchecked
                DialogEnableControl(hwnd, IDC_DOT_MATCH_ALL, false);
                DialogEnableControl(hwnd, IDC_FINDTRANSFORMBS, true);
                CheckDlgButton(hwnd, IDC_FINDTRANSFORMBS, SetBtn(s_pEfrDataDlg->bTransformBS));
            }
            _SetSearchFlags(hwnd, s_pEfrDataDlg);
            _DelayMarkAll(_MQ_STD);
            break;

        case IDC_DOT_MATCH_ALL:
            _SetSearchFlags(hwnd, s_pEfrDataDlg);
            _DelayMarkAll(_MQ_STD);
            break;

        case IDC_WILDCARDSEARCH: {
            if (IsButtonChecked(hwnd, IDC_WILDCARDSEARCH)) {
                // Can not use wildcard search together with regexp
                CheckDlgButton(hwnd, IDC_FINDREGEXP, BST_UNCHECKED);
                DialogEnableControl(hwnd, IDC_DOT_MATCH_ALL, false);
                // transform BS handled by regex (wildcard search based on):
                CheckDlgButton(hwnd, IDC_FINDTRANSFORMBS, BST_CHECKED);
                DialogEnableControl(hwnd, IDC_FINDTRANSFORMBS, false);
            } else { // unchecked
                DialogEnableControl(hwnd, IDC_FINDTRANSFORMBS, true);
                CheckDlgButton(hwnd, IDC_FINDTRANSFORMBS, SetBtn(s_pEfrDataDlg->bTransformBS));
            }
            _SetSearchFlags(hwnd, s_pEfrDataDlg);
            _DelayMarkAll(_MQ_STD);
        }
        break;

        case IDC_FIND_OVERLAPPING:
            _SetSearchFlags(hwnd, s_pEfrDataDlg);
            _DelayMarkAll(_MQ_STD);
            break;

        case IDC_FINDTRANSFORMBS: {
            _SetSearchFlags(hwnd, s_pEfrDataDlg);
            _DelayMarkAll(_MQ_STD);
        }
        break;

        case IDC_FINDCASE:
            _SetSearchFlags(hwnd, s_pEfrDataDlg);
            _DelayMarkAll(_MQ_STD);
            break;

        case IDC_FINDWORD:
            _SetSearchFlags(hwnd, s_pEfrDataDlg);
            _DelayMarkAll(_MQ_STD);
            break;

        case IDC_FINDSTART:
            _SetSearchFlags(hwnd, s_pEfrDataDlg);
            _DelayMarkAll(_MQ_STD);
            break;

        case IDC_TRANSPARENT:
            Settings.FindReplaceTransparentMode = IsButtonChecked(hwnd, IDC_TRANSPARENT);
            break;

        case IDC_REPLACE:
        case IDC_REPLACEALL:
        case IDC_REPLACEINSEL:
            Globals.iReplacedOccurrences = 0;
        case IDOK:
        case IDC_FINDPREV:
        case IDACC_SELTONEXT:
        case IDACC_SELTOPREV:
        case IDMSG_SWITCHTOFIND:
        case IDMSG_SWITCHTOREPLACE: {

            if ((!s_bIsReplaceDlg && LOWORD(wParam) == IDMSG_SWITCHTOREPLACE) ||
                (s_bIsReplaceDlg && LOWORD(wParam) == IDMSG_SWITCHTOFIND)) {
                GetDlgPos(hwnd, &s_xFindReplaceDlgSave, &s_yFindReplaceDlgSave);
                s_bSwitchedFindReplace = true;
                CopyMemory(&s_efrSave, s_pEfrDataDlg, sizeof(EDITFINDREPLACE));
            }

            if (!s_bSwitchedFindReplace &&
                    !ComboBox_GetTextW2MB(hwnd, IDC_FINDTEXT, s_pEfrDataDlg->szFind, COUNTOF(s_pEfrDataDlg->szFind))) {
                DialogEnableControl(hwnd, IDOK, false);
                DialogEnableControl(hwnd, IDC_FINDPREV, false);
                DialogEnableControl(hwnd, IDC_REPLACE, false);
                DialogEnableControl(hwnd, IDC_REPLACEALL, false);
                DialogEnableControl(hwnd, IDC_REPLACEINSEL, false);
                if (!ComboBox_GetTextW2MB(hwnd, IDC_REPLACETEXT, s_pEfrDataDlg->szReplace, COUNTOF(s_pEfrDataDlg->szReplace))) {
                    DialogEnableControl(hwnd, IDC_SWAPSTRG, false);
                }
                return true;
            }

            _SetSearchFlags(hwnd, s_pEfrDataDlg);

            if (!s_bSwitchedFindReplace) {
                // Save MRUs
                if (!StrIsEmptyA(s_pEfrDataDlg->szFind)) {
                    MultiByteToWideChar(Encoding_SciCP, 0, s_pEfrDataDlg->szFind, -1, s_tchBuf, (int)COUNTOF(s_tchBuf));
                    MRU_Add(Globals.pMRUfind, s_tchBuf, 0, -1, -1, NULL);
                    SetFindPattern(s_tchBuf);
                }
                if (!StrIsEmptyA(s_pEfrDataDlg->szReplace)) {
                    MultiByteToWideChar(Encoding_SciCP, 0, s_pEfrDataDlg->szReplace, -1, s_tchBuf, (int)COUNTOF(s_tchBuf));
                    MRU_Add(Globals.pMRUreplace, s_tchBuf, 0, -1, -1, NULL);
                }
            }

            // Reload MRUs
            SendDlgItemMessage(hwnd, IDC_FINDTEXT, CB_RESETCONTENT, 0, 0);
            SendDlgItemMessage(hwnd, IDC_REPLACETEXT, CB_RESETCONTENT, 0, 0);

            for (int i = 0; i < MRU_Count(Globals.pMRUfind); i++) {
                MRU_Enum(Globals.pMRUfind, i, s_tchBuf, COUNTOF(s_tchBuf));
                SendDlgItemMessage(hwnd, IDC_FINDTEXT, CB_ADDSTRING, 0, (LPARAM)s_tchBuf);
            }
            for (int i = 0; i < MRU_Count(Globals.pMRUreplace); i++) {
                MRU_Enum(Globals.pMRUreplace, i, s_tchBuf, COUNTOF(s_tchBuf));
                SendDlgItemMessage(hwnd, IDC_REPLACETEXT, CB_ADDSTRING, 0, (LPARAM)s_tchBuf);
            }

            ComboBox_SetTextMB2W(hwnd, IDC_FINDTEXT, s_pEfrDataDlg->szFind);
            ComboBox_SetTextMB2W(hwnd, IDC_REPLACETEXT, s_pEfrDataDlg->szReplace);

            if (!s_bSwitchedFindReplace) {
                SendMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)(GetFocus()), 1);
            }

            switch (LOWORD(wParam)) {
            case IDOK: // find next
            case IDACC_SELTONEXT:
                if (s_bIsReplaceDlg) {
                    Globals.bReplaceInitialized = true;
                }
                EditFindNext(s_pEfrDataDlg->hwnd, s_pEfrDataDlg, (LOWORD(wParam) == IDACC_SELTONEXT), IsKeyDown(VK_F3));
                SET_INITIAL_ANCHORS()
                break;

            case IDC_FINDPREV: // find previous
            case IDACC_SELTOPREV:
                if (s_bIsReplaceDlg) {
                    Globals.bReplaceInitialized = true;
                }
                EditFindPrev(s_pEfrDataDlg->hwnd, s_pEfrDataDlg, (LOWORD(wParam) == IDACC_SELTOPREV), IsKeyDown(VK_F3));
                SET_INITIAL_ANCHORS()
                break;

            case IDC_REPLACE: {
                Globals.bReplaceInitialized = true;
                EditReplace(s_pEfrDataDlg->hwnd, s_pEfrDataDlg);
            }
            break;

            case IDC_REPLACEALL:
                Globals.bReplaceInitialized = true;
                EditReplaceAll(s_pEfrDataDlg->hwnd, s_pEfrDataDlg, true);
                break;

            case IDC_REPLACEINSEL:
                if (!SciCall_IsSelectionEmpty()) {
                    Globals.bReplaceInitialized = true;
                    EditReplaceAllInSelection(s_pEfrDataDlg->hwnd, s_pEfrDataDlg, true);
                }
                break;
            }

            if (!s_bIsReplaceDlg && (s_pEfrDataDlg->bFindClose)) {
                //~EndDialog(hwnd, LOWORD(wParam)); ~ (!) not running on own message loop
                DestroyWindow(hwnd);
            } else if ((LOWORD(wParam) != IDOK) && s_pEfrDataDlg->bReplaceClose) {
                //~EndDialog(hwnd, LOWORD(wParam)); ~ (!) not running on own message loop
                DestroyWindow(hwnd);
            }
        }
        _DelayMarkAll(_MQ_STD);
        break;


        case IDCANCEL:
            //~EndDialog(hwnd, IDCANCEL); ~ (!) not running on own message loop
            DestroyWindow(hwnd);
            break;

        case IDC_FINDESCCTRLCHR:
        case IDC_REPLESCCTRLCHR: {
            WCHAR trf[FNDRPL_BUFFER] = { L'\0' };
            UINT const ctrl_id = (LOWORD(wParam) == IDC_FINDESCCTRLCHR) ? IDC_FINDTEXT : IDC_REPLACETEXT;
            GetDlgItemTextW(hwnd, ctrl_id, s_tchBuf, COUNTOF(s_tchBuf));
            if (SlashCtrlW(trf, COUNTOF(trf), s_tchBuf) == StringCchLen(s_tchBuf, 0)) {
                UnSlashCtrlW(trf);
            }
            SetDlgItemTextW(hwnd, ctrl_id, trf);
        }
        break;

        case IDC_SWAPSTRG: {
            WCHAR* wszFind = s_tchBuf;
            WCHAR wszRepl[FNDRPL_BUFFER] = { L'\0' };
            GetDlgItemTextW(hwnd, IDC_FINDTEXT, wszFind, COUNTOF(s_tchBuf));
            GetDlgItemTextW(hwnd, IDC_REPLACETEXT, wszRepl, COUNTOF(wszRepl));
            SetDlgItemTextW(hwnd, IDC_FINDTEXT, wszRepl);
            SetDlgItemTextW(hwnd, IDC_REPLACETEXT, wszFind);
            Globals.FindReplaceMatchFoundState = FND_NOP;
            _SetSearchFlags(hwnd, s_pEfrDataDlg);
            _DelayMarkAll(_MQ_STD);
        }
        break;

        case IDACC_FIND:
            PostWMCommand(GetParent(hwnd), IDM_EDIT_FIND);
            break;

        case IDACC_REPLACE:
            PostWMCommand(GetParent(hwnd), IDM_EDIT_REPLACE);
            break;

        case IDACC_SAVEPOS:
            GetDlgPos(hwnd, &Settings.FindReplaceDlgPosX, &Settings.FindReplaceDlgPosY);
            break;

        case IDACC_RESETPOS:
            CenterDlgInParent(hwnd, NULL);
            Settings.FindReplaceDlgPosX = Settings.FindReplaceDlgPosY = CW_USEDEFAULT;
            break;

        case IDACC_CLEAR_FIND_HISTORY:
            MRU_Empty(Globals.pMRUfind, false);
            if (Globals.bCanSaveIniFile) {
                MRU_Save(Globals.pMRUfind);
            }
            if (s_pEfrDataDlg) {
                s_pEfrDataDlg->szFind[0] = '\0';
            }
            SetFindPattern(NULL);
            while ((int)SendDlgItemMessage(hwnd, IDC_FINDTEXT, CB_DELETESTRING, 0, 0) > 0) {};
            SetDlgItemText(hwnd, IDC_FINDTEXT, L"");
            break;

        case IDACC_CLEAR_REPL_HISTORY:
            MRU_Empty(Globals.pMRUreplace, false);
            if (Globals.bCanSaveIniFile) {
                MRU_Save(Globals.pMRUreplace);
            }
            if (s_pEfrDataDlg) {
                s_pEfrDataDlg->szReplace[0] = '\0';
            }
            while ((int)SendDlgItemMessage(hwnd, IDC_REPLACETEXT, CB_DELETESTRING, 0, 0) > 0) {};
            SetDlgItemText(hwnd, IDC_REPLACETEXT, L"");
            break;

        case IDACC_FINDNEXT:
            PostWMCommand(hwnd, IDOK);
            break;

        case IDACC_FINDPREV:
            PostWMCommand(hwnd, IDC_FINDPREV);
            break;

        case IDACC_REPLACENEXT:
            if (s_bIsReplaceDlg) {
                PostWMCommand(hwnd, IDC_REPLACE);
            }
            break;

        case IDACC_SAVEFIND:
            Globals.FindReplaceMatchFoundState = FND_NOP;
            SendWMCommand(Globals.hwndMain, IDM_EDIT_SAVEFIND);
            ComboBox_SetTextMB2W(hwnd, IDC_FINDTEXT, s_pEfrDataDlg->szFind);
            CheckDlgButton(hwnd, IDC_FINDREGEXP, BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_DOT_MATCH_ALL, BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_WILDCARDSEARCH, BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_FIND_OVERLAPPING, BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_FINDTRANSFORMBS, BST_UNCHECKED);
            PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)(GetDlgItem(hwnd, IDC_FINDTEXT)), 1);
            break;

        case IDACC_VIEWSCHEMECONFIG:
            PostWMCommand(GetParent(hwnd), IDM_VIEW_SCHEMECONFIG);
            break;

        default:
            return FALSE;
        }

    } // WM_COMMAND:
    return TRUE;


    case WM_SYSCOMMAND:
        if (wParam == IDS_MUI_SAVEPOS) {
            PostWMCommand(hwnd, IDACC_SAVEPOS);
            return TRUE;
        } else if (wParam == IDS_MUI_RESETPOS) {
            PostWMCommand(hwnd, IDACC_RESETPOS);
            return TRUE;
        } else if (wParam == IDS_MUI_CLEAR_FIND_HISTORY) {
            PostWMCommand(hwnd, IDACC_CLEAR_FIND_HISTORY);
            return TRUE;
        } else if (wParam == IDS_MUI_CLEAR_REPL_HISTORY) {
            PostWMCommand(hwnd, IDACC_CLEAR_REPL_HISTORY);
            return TRUE;
        }
        break;

    case WM_NOTIFY: {
        LPNMHDR pnmhdr = (LPNMHDR)lParam;
        switch (pnmhdr->code) {
        case NM_CLICK:
        case NM_RETURN:
            switch (pnmhdr->idFrom) {
            case IDC_TOGGLEFINDREPLACE:
                if (s_bIsReplaceDlg) {
                    PostWMCommand(GetParent(hwnd), IDM_EDIT_FIND);
                } else {
                    PostWMCommand(GetParent(hwnd), IDM_EDIT_REPLACE);
                }
                break;
            case IDC_FINDESCCTRLCHR:
                SendWMCommand(hwnd, IDC_FINDESCCTRLCHR);
                break;
            case IDC_REPLESCCTRLCHR:
                SendWMCommand(hwnd, IDC_REPLESCCTRLCHR);
                break;
            case IDC_BACKSLASHHELP:
                // Display help messages in the find/replace windows
                MessageBoxLng(MB_ICONINFORMATION, IDS_MUI_BACKSLASHHELP);
                break;
            case IDC_REGEXPHELP:
                MessageBoxLng(MB_ICONINFORMATION, IDS_MUI_REGEXPHELP);
                break;
            case IDC_WILDCARDHELP:
                MessageBoxLng(MB_ICONINFORMATION, IDS_MUI_WILDCARDHELP);
                break;
            default:
                break;
            }
            break;

        default:
            return false;
        }
    }
    break;

    default:
        break;

    } // switch(umsg)

    return FALSE; // message handled
}


//=============================================================================
//
//  EditFindReplaceDlg()
//
HWND EditFindReplaceDlg(HWND hwnd, LPEDITFINDREPLACE lpefr, bool bReplace)
{
    (void)CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_SPEED_OVER_MEMORY);

    lpefr->hwnd = hwnd;
    HWND hDlg = CreateThemedDialogParam(Globals.hLngResContainer,
                                        (bReplace) ? MAKEINTRESOURCEW(IDD_MUI_REPLACE) : MAKEINTRESOURCEW(IDD_MUI_FIND),
                                        GetParent(hwnd),
                                        EditFindReplaceDlgProc,
                                        (LPARAM) lpefr);

    if (IS_VALID_HANDLE(hDlg)) {
        ShowWindow(hDlg, SW_SHOW);
    }
    CoUninitialize();
    return hDlg;
}


//=============================================================================
//
//  EditFindNext()
//
bool EditFindNext(HWND hwnd, LPEDITFINDREPLACE lpefr, bool bExtendSelection, bool bFocusWnd)
{
    bool bSuppressNotFound = false;
    bool bFoundWrapAround = false;

    if (bFocusWnd) {
        SetFocus(hwnd);
    }

    char szFind[FNDRPL_BUFFER];
    DocPos const slen = _EditGetFindStrg(hwnd, lpefr, szFind, COUNTOF(szFind));
    if (slen <= 0LL) {
        return false;
    }
    int const sFlags = (int)(lpefr->fuFlags);

    DocPos const iDocEndPos = Sci_GetDocEndPosition();
    //DocPos const iSelStartPos = SciCall_GetSelectionStart();
    //DocPos const iSelEndPos = SciCall_GetSelectionEnd();

    EditSetCaretToSelectionEnd(); // fluent swittch between Prev/Next
    DocPos start = SciCall_GetCurrentPos();
    DocPos end = iDocEndPos;

    SciCall_CallTipCancel();

    DocPos iPos = _FindInTarget(szFind, slen, sFlags, &start, &end, true, FRMOD_NORM);

    if ((iPos < -1LL) && (lpefr->fuFlags & SCFIND_REGEXP)) {
        InfoBoxLng(MB_ICONWARNING, L"MsgInvalidRegex", IDS_MUI_REGEX_INVALID);
        bSuppressNotFound = true;
    } else if ((iPos < 0LL) && (start >= 0LL) && !bExtendSelection) {
        UpdateStatusbar(false);
        if (!lpefr->bNoFindWrap && !bSuppressNotFound) {
            DocPos const _start = start;
            //DocPos const _end = end;
            end = min_p(start, iDocEndPos);
            start = 0LL;
            iPos = _FindInTarget(szFind, slen, sFlags, &start, &end, false, FRMOD_WRAPED);

            if ((iPos < 0LL) || (end == _start)) {
                if ((iPos < -1) && (lpefr->fuFlags & SCFIND_REGEXP)) {
                    InfoBoxLng(MB_ICONWARNING, L"MsgInvalidRegex", IDS_MUI_REGEX_INVALID);
                    bSuppressNotFound = true;
                }
            } else {
                LONG const result = InfoBoxLng(MB_OKCANCEL, L"MsgFindWrap1", IDS_MUI_FIND_WRAPFW);
                if (INFOBOX_ANSW(result) != IDOK) {
                    iPos = -1LL;
                    bSuppressNotFound = true;
                }
                bFoundWrapAround = (INFOBOX_MODE(result) != 0);
            }
        }
    }

    if (iPos < 0LL) {
        if (!bSuppressNotFound) {
            InfoBoxLng(MB_OK, L"MsgNotFound", IDS_MUI_NOTFOUND);
        }
        return false;
    }

    if (bExtendSelection) {
        DocPos const iSelPos = SciCall_GetCurrentPos();
        DocPos const iSelAnchor = SciCall_GetAnchor();
        EditSetSelectionEx(min_p(iSelAnchor, iSelPos), end, -1, -1);
    } else {
        EditSetSelectionEx(iPos, end, -1, -1);
    }

    if (iPos == end) {
        _ShowZeroLengthCallTip(iPos);
    }
    if (bFoundWrapAround) {
        ShowWrapAroundCallTip(true);
    }

    return true;
}


//=============================================================================
//
//  EditFindPrev()
//
bool EditFindPrev(HWND hwnd, LPEDITFINDREPLACE lpefr, bool bExtendSelection, bool bFocusWnd)
{
    bool bSuppressNotFound = false;
    bool bFoundWrapAround = false;

    if (bFocusWnd) {
        SetFocus(hwnd);
    }
    char szFind[FNDRPL_BUFFER];
    DocPos const slen = _EditGetFindStrg(hwnd, lpefr, szFind, COUNTOF(szFind));
    if (slen <= 0LL) {
        return false;
    }
    int const sFlags = (int)(lpefr->fuFlags);

    DocPos const iDocEndPos = Sci_GetDocEndPosition();
    //DocPos const iSelStartPos = SciCall_GetSelectionStart();
    //DocPos const iSelEndPos = SciCall_GetSelectionEnd();

    EditSetCaretToSelectionStart(); // fluent switch between Next/Prev
    DocPos start = SciCall_GetCurrentPos();
    DocPos end = 0LL;

    SciCall_CallTipCancel();

    DocPos iPos = _FindInTarget(szFind, slen, sFlags, &start, &end, true, FRMOD_NORM);

    if ((iPos < -1LL) && (sFlags & SCFIND_REGEXP)) {
        InfoBoxLng(MB_ICONWARNING, L"MsgInvalidRegex", IDS_MUI_REGEX_INVALID);
        bSuppressNotFound = true;
    } else if ((iPos < 0LL) && (start <= iDocEndPos) &&  !bExtendSelection) {
        UpdateStatusbar(false);
        if (!lpefr->bNoFindWrap && !bSuppressNotFound) {
            DocPos const _start = start;
            //DocPos const _end = end;
            end = max_p(start, 0LL);
            start = iDocEndPos;
            iPos = _FindInTarget(szFind, slen, sFlags, &start, &end, false, FRMOD_WRAPED);

            if ((iPos < 0LL) || (start == _start)) {
                if ((iPos < -1LL) && (sFlags & SCFIND_REGEXP)) {
                    InfoBoxLng(MB_ICONWARNING, L"MsgInvalidRegex", IDS_MUI_REGEX_INVALID);
                    bSuppressNotFound = true;
                }
            } else {
                LONG const result = InfoBoxLng(MB_OKCANCEL, L"MsgFindWrap2", IDS_MUI_FIND_WRAPRE);
                if (INFOBOX_ANSW(result) != IDOK) {
                    iPos = -1LL;
                    bSuppressNotFound = true;
                }
                bFoundWrapAround = (INFOBOX_MODE(result) != 0);
            }
        }
    }

    if (iPos < 0LL) {
        if (!bSuppressNotFound) {
            InfoBoxLng(MB_OK, L"MsgNotFound", IDS_MUI_NOTFOUND);
        }
        return false;
    }

    if (bExtendSelection) {
        DocPos const iSelPos = SciCall_GetCurrentPos();
        DocPos const iSelAnchor = SciCall_GetAnchor();
        EditSetSelectionEx(max_p(iSelPos, iSelAnchor), iPos, -1, -1);
    } else {
        EditSetSelectionEx(end, iPos, -1, -1);
    }

    if (iPos == end) {
        _ShowZeroLengthCallTip(iPos);
    }
    if (bFoundWrapAround) {
        ShowWrapAroundCallTip(false);
    }

    return true;
}


//=============================================================================
//
//  EditMarkAllOccurrences()
//
void EditMarkAllOccurrences(HWND hwnd, bool bForceClear)
{
    if (bForceClear) {
        EditClearAllOccurrenceMarkers(hwnd);
    }

    if (!IsMarkOccurrencesEnabled()) {
        return;
    }

    int const searchFlags = GetMarkAllOccSearchFlags();

    _IGNORE_NOTIFY_CHANGE_;

    if (Settings.MarkOccurrencesMatchVisible) {

        // get visible lines for update
        DocLn const iStartLine = SciCall_DocLineFromVisible(SciCall_GetFirstVisibleLine());
        DocLn const iEndLine = min_ln((iStartLine + SciCall_LinesOnScreen()), (SciCall_GetLineCount() - 1));
        DocPos const iPosStart = SciCall_PositionFromLine(iStartLine);
        DocPos const iPosEnd = SciCall_GetLineEndPosition(iEndLine);

        // !!! don't clear all marks, else this method is re-called
        // !!! on UpdateUI notification on drawing indicator mark
        EditMarkAll(NULL, searchFlags, iPosStart, iPosEnd, false);
    } else {
        EditMarkAll(NULL, searchFlags, 0, Sci_GetDocEndPosition(), false);
    }

    _OBSERVE_NOTIFY_CHANGE_;
}


//=============================================================================
//
//  EditSelectionMultiSelectAll()
//
void EditSelectionMultiSelectAll()
{
    if (SciCall_GetSelText(NULL) > 1) {

        _SAVE_TARGET_RANGE_;

        SciCall_TargetWholeDocument();

        SciCall_SetSearchFlags(GetMarkAllOccSearchFlags());

        SciCall_MultipleSelectAddEach();

        SciCall_SetMainSelection(0);
        if (SciCall_GetSelectionNAnchor(0) > SciCall_GetSelectionNCaret(0)) {
            SciCall_SwapMainAnchorCaret();
        }
        EditEnsureSelectionVisible();

        _RESTORE_TARGET_RANGE_;
    }
}


//=============================================================================
//
//  EditSelectionMultiSelectAllEx()
//
void EditSelectionMultiSelectAllEx(CLPCEDITFINDREPLACE edFndRpl)
{
    EDITFINDREPLACE efr;
    CopyMemory(&efr, edFndRpl, sizeof(EDITFINDREPLACE));

    if (IsWindow(Globals.hwndDlgFindReplace)) {
        _SetSearchFlags(Globals.hwndDlgFindReplace, &efr);
    } else {
        efr.fuFlags = GetMarkAllOccSearchFlags();
    }

    _IGNORE_NOTIFY_CHANGE_;
    EditMarkAll(efr.szFind, efr.fuFlags, 0, Sci_GetDocEndPosition(), true);
    _OBSERVE_NOTIFY_CHANGE_;
}


//=============================================================================
//
//  _GetReplaceString()
//
static char* _GetReplaceString(HWND hwnd, CLPCEDITFINDREPLACE lpefr, int* iReplaceMsg)
{
    char* pszReplace = NULL; // replace text of arbitrary size
    if (StringCchCompareNIA(lpefr->szReplace, COUNTOF(lpefr->szReplace), "^c", 2) == 0) {
        *iReplaceMsg = SCI_REPLACETARGET;
        pszReplace = EditGetClipboardText(hwnd, true, NULL, NULL);
    } else {
        size_t const cch = StringCchLenA(lpefr->szReplace, COUNTOF(lpefr->szReplace));
        pszReplace = (char*)AllocMem(cch + 1, HEAP_ZERO_MEMORY);
        if (pszReplace) {
            StringCchCopyA(pszReplace, SizeOfMem(pszReplace), lpefr->szReplace);
            bool const bIsRegEx = (lpefr->fuFlags & SCFIND_REGEXP);
            if (lpefr->bTransformBS || bIsRegEx) {
                TransformBackslashes(pszReplace, bIsRegEx, Encoding_SciCP, iReplaceMsg);
            }
        }
    }
    return pszReplace; // move ownership
}


//=============================================================================
//
//  EditReplace()
//
bool EditReplace(HWND hwnd, LPEDITFINDREPLACE lpefr)
{
    int iReplaceMsg = SCI_REPLACETARGET;
    char* pszReplace = _GetReplaceString(hwnd, lpefr, &iReplaceMsg);
    if (!pszReplace) {
        return false; // recoding of clipboard canceled
    }
    DocPos const selBeg = SciCall_GetSelectionStart();
    DocPos const selEnd = SciCall_GetSelectionEnd();

    // redo find to get group ranges filled
    DocPos start = (SciCall_IsSelectionEmpty() ? SciCall_GetCurrentPos() : selBeg);
    DocPos end = Sci_GetDocEndPosition();
    DocPos _start = start;
    Globals.iReplacedOccurrences = 0;

    char szFind[FNDRPL_BUFFER];
    DocPos const slen = _EditGetFindStrg(hwnd, lpefr, szFind, COUNTOF(szFind));
    int const sFlags = (int)(lpefr->fuFlags);
    DocPos const iPos = _FindInTarget(szFind, slen, sFlags, &start, &end, false, FRMOD_NORM);

    // w/o selection, replacement string is put into current position
    // but this maybe not intended here
    if (SciCall_IsSelectionEmpty()) {
        if ((iPos < 0LL) || (_start != start) || (_start != end)) {
            // empty-replace was not intended
            FreeMem(pszReplace);
            if (iPos < 0LL) {
                return EditFindNext(hwnd, lpefr, false, false);
            }
            EditSetSelectionEx(start, end, -1, -1);
            return true;
        }
    }
    // if selection is is not equal current find, set selection
    else if ((selBeg != start) || (selEnd != end)) {
        FreeMem(pszReplace);
        SciCall_SetCurrentPos(selBeg);
        return EditFindNext(hwnd, lpefr, false, false);
    }

    _SAVE_TARGET_RANGE_;

    _BEGIN_UNDO_ACTION_;
    SciCall_TargetFromSelection();
    Sci_ReplaceTarget(iReplaceMsg, -1, pszReplace);
    // move caret behind replacement
    SciCall_SetCurrentPos(SciCall_GetTargetEnd());
    Globals.iReplacedOccurrences = 1;

    _END_UNDO_ACTION_;
    FreeMem(pszReplace);

    _RESTORE_TARGET_RANGE_;

    return EditFindNext(hwnd, lpefr, false, false);
}



//=============================================================================
//
//  EditReplaceAllInRange()
//
//
int EditReplaceAllInRange(HWND hwnd, LPEDITFINDREPLACE lpefr, DocPos iStartPos, DocPos iEndPos, DocPos *enlargement)
{
    if (iStartPos > iEndPos) {
        swapos(&iStartPos, &iEndPos);
    }
    DocPos const iOrigEndPos = iEndPos; // remember

    char szFind[FNDRPL_BUFFER];
    size_t const slen = _EditGetFindStrg(hwnd, lpefr, szFind, COUNTOF(szFind));
    if (slen <= 0) {
        return FALSE;
    }
    int const sFlags = (int)(lpefr->fuFlags);
    bool const bIsRegExpr = (sFlags & SCFIND_REGEXP);

    // SCI_REPLACETARGET or SCI_REPLACETARGETRE
    int iReplaceMsg = SCI_REPLACETARGET;
    char *pszReplace = _GetReplaceString(hwnd, lpefr, &iReplaceMsg);
    if (!pszReplace) {
        return -1; // recoding of clipboard canceled
    }

    DocPos const _saveTargetBeg_ = SciCall_GetTargetStart();
    DocPos const _saveTargetEnd_ = SciCall_GetTargetEnd();

    DocPos start = iStartPos;
    DocPos end     = iEndPos;
    DocPos iPos    = _FindInTarget(szFind, slen, sFlags, &start, &end, false, FRMOD_NORM);

    if ((iPos < -1LL) && bIsRegExpr) {
        InfoBoxLng(MB_ICONWARNING, L"MsgInvalidRegex", IDS_MUI_REGEX_INVALID);
        return 0;
    }

    int iCount = 0;
    _BEGIN_UNDO_ACTION_;
    while ((iPos >= 0LL) && (start <= iEndPos)) {
        SciCall_SetTargetRange(iPos, end);
        DocPos const replLen = Sci_ReplaceTarget(iReplaceMsg, -1, pszReplace);
        ++iCount;
        iStartPos = SciCall_GetTargetEnd();
        iEndPos += replLen - (end - iPos);
        start = iStartPos;
        end   = iEndPos;
        iPos = (start <= end) ? _FindInTarget(szFind, slen, sFlags, &start, &end, true, FRMOD_NORM) : -1LL;
    }
    _END_UNDO_ACTION_;

    *enlargement = (iEndPos - iOrigEndPos);
    SciCall_SetTargetRange(_saveTargetBeg_, _saveTargetEnd_ + *enlargement); //restore
    return iCount;
}


//=============================================================================
//
//  EditReplaceAll()
//
bool EditReplaceAll(HWND hwnd, LPEDITFINDREPLACE lpefr, bool bShowInfo)
{
    DocPos const start = 0;
    DocPos const end = Sci_GetDocEndPosition();
    DocPos enlargement = 0;

    BeginWaitCursorUID(true, IDS_MUI_SB_REPLACE_ALL);
    Globals.iReplacedOccurrences = EditReplaceAllInRange(hwnd, lpefr, start, end, &enlargement);
    EndWaitCursor();

    if (bShowInfo) {
        if (Globals.iReplacedOccurrences > 0) {
            InfoBoxLng(MB_OK, L"MsgReplaceCount", IDS_MUI_REPLCOUNT, Globals.iReplacedOccurrences);
        } else {
            InfoBoxLng(MB_OK, L"MsgNotFound", IDS_MUI_NOTFOUND);
        }
    }

    return (Globals.iReplacedOccurrences > 0) ? true : false;
}


//=============================================================================
//
//  EditReplaceAllInSelection()
//
bool EditReplaceAllInSelection(HWND hwnd, LPEDITFINDREPLACE lpefr, bool bShowInfo)
{
    if (Sci_IsMultiOrRectangleSelection()) {
        InfoBoxLng(MB_ICONWARNING, NULL, IDS_MUI_SELRECTORMULTI);
        return false;
    }

    const DocPos start = SciCall_GetSelectionStart();
    const DocPos end = SciCall_GetSelectionEnd();
    const DocPos currPos = SciCall_GetCurrentPos();
    const DocPos anchorPos = SciCall_GetAnchor();
    DocPos enlargement = 0;

    Globals.iReplacedOccurrences = EditReplaceAllInRange(hwnd, lpefr, start, end, &enlargement);

    if (Globals.iReplacedOccurrences > 0) {
        if (currPos < anchorPos) {
            SciCall_SetSel(anchorPos + enlargement, currPos);
        } else {
            SciCall_SetSel(anchorPos, currPos + enlargement);
        }

        if (bShowInfo) {
            if (Globals.iReplacedOccurrences > 0) {
                InfoBoxLng(MB_OK, L"MsgReplaceCount", IDS_MUI_REPLCOUNT, Globals.iReplacedOccurrences);
            } else {
                InfoBoxLng(MB_OK, L"MsgNotFound", IDS_MUI_NOTFOUND);
            }
        }
    }

    return (Globals.iReplacedOccurrences > 0) ? true : false;
}



//=============================================================================
//
//  EditClearAllOccurrenceMarkers()
//
void EditClearAllOccurrenceMarkers(HWND hwnd)
{
    UNREFERENCED_PARAMETER(hwnd);
    Globals.iMarkOccurrencesCount = 0;

    _IGNORE_NOTIFY_CHANGE_;

    SciCall_SetIndicatorCurrent(INDIC_NP3_MARK_OCCURANCE);
    SciCall_IndicatorClearRange(0, Sci_GetDocEndPosition());

    SciCall_MarkerDeleteAll(MARKER_NP3_OCCURRENCE);

    _OBSERVE_NOTIFY_CHANGE_;
}


//=============================================================================
//
//  EditClearAllBookMarks()
//
void EditClearAllBookMarks(HWND hwnd)
{
    UNREFERENCED_PARAMETER(hwnd);
    int const bitmask = OCCURRENCE_MARKER_BITMASK() & ~(1 << MARKER_NP3_BOOKMARK);
    DocLn const line = SciCall_MarkerNext(0, bitmask);
    if (line >= 0) {
        // 1st press: clear all occurrences marker
        for (int m = MARKER_NP3_1; m < MARKER_NP3_BOOKMARK; ++m) {
            SciCall_MarkerDeleteAll(m);
        }
    } else {
        // if no occurrences marker found
        SciCall_MarkerDeleteAll(MARKER_NP3_BOOKMARK);
    }
}


//=============================================================================
//
//  EditToggleView()
//
void EditToggleView(HWND hwnd)
{
    if (Settings.FocusViewMarkerMode & FVMM_FOLD) {

        BeginWaitCursorUID(true, IDS_MUI_SB_TOGGLE_VIEW);

        FocusedView.HideNonMatchedLines = !FocusedView.HideNonMatchedLines; // toggle

        if (FocusedView.HideNonMatchedLines) {
            EditFoldMarkedLineRange(hwnd, true);
            if (Settings.FocusViewMarkerMode & (FVMM_MARGIN | FVMM_LN_BACKGR)) {
                EditBookMarkLineRange(hwnd);
            }
        } else {
            EditFoldMarkedLineRange(hwnd, false);
        }

        SciCall_SetReadOnly(FocusedView.HideNonMatchedLines);
        SciCall_ScrollCaret();

        EndWaitCursor();

    } else if (Settings.FocusViewMarkerMode & (FVMM_MARGIN | FVMM_LN_BACKGR)) {
        EditBookMarkLineRange(hwnd);
    }
}


//=============================================================================
//
//  EditSelectWordAtPos()
//
void EditSelectWordAtPos(const DocPos iPos, const bool bForceWord)
{
    DocPos iWordStart = SciCall_WordStartPosition(iPos, true);
    DocPos iWordEnd = SciCall_WordEndPosition(iPos, true);

    if ((iWordStart == iWordEnd) && bForceWord) { // we are in whitespace salad...
        iWordStart = SciCall_WordEndPosition(iPos, false);
        iWordEnd = SciCall_WordEndPosition(iWordStart, true);
        if (iWordStart != iWordEnd) {
            SciCall_SetSelection(iWordEnd, iWordStart);
        }
    } else {
        SciCall_SetSelection(iWordEnd, iWordStart);
    }
}


//=============================================================================
//
//  EditAddSearchFlags()
//
int EditAddSearchFlags(int flags, bool bRegEx, bool bWordStart, bool bMatchCase, bool bMatchWords, bool bDotMatchAll)
{
    flags |= (bRegEx ? SCFIND_REGEXP : 0);
    flags |= (bWordStart ? SCFIND_WORDSTART : 0);
    flags |= (bMatchWords ? SCFIND_WHOLEWORD : 0);
    flags |= (bMatchCase ? SCFIND_MATCHCASE : 0);
    flags |= (bDotMatchAll ? SCFIND_DOT_MATCH_ALL : 0);
    return flags;
}


//=============================================================================
//
//  EditMarkAll()
//  Mark all occurrences of the matching text in range (by Aleksandar Lekov)
//
void EditMarkAll(char* pszFind, int sFlags, DocPos rangeStart, DocPos rangeEnd, bool bMultiSel)
{
    BeginWaitCursorUID(Flags.bHugeFileLoadState, IDS_MUI_SB_MARK_ALL_OCC);

    char txtBuffer[FNDRPL_BUFFER] = { '\0' };
    char* pszText = (pszFind != NULL) ? pszFind : txtBuffer;

    DocPos iFindLength = 0;

    if (StrIsEmptyA(pszText)) {
        if (SciCall_IsSelectionEmpty()) {
            // nothing selected, get word under caret if flagged
            if (Settings.MarkOccurrencesCurrentWord && (sFlags & SCFIND_WHOLEWORD)) {
                DocPos const iCurPos = SciCall_GetCurrentPos();
                DocPos iWordStart = SciCall_WordStartPosition(iCurPos, true);
                DocPos iWordEnd = SciCall_WordEndPosition(iCurPos, true);
                if (iWordStart == iWordEnd) {
                    return;
                }
                iFindLength = (iWordEnd - iWordStart);
                StringCchCopyNA(txtBuffer, COUNTOF(txtBuffer), SciCall_GetRangePointer(iWordStart, iFindLength), iFindLength);
            } else {
                return; // no pattern, no selection and no word mark chosen
            }
        } else { // we have a selection
            if (Sci_IsMultiSelection()) {
                return;
            }

            // get current selection
            DocPos const iSelStart = SciCall_GetSelectionStart();
            DocPos const iSelEnd = SciCall_GetSelectionEnd();
            DocPos const iSelCount = (iSelEnd - iSelStart);

            // if multiple lines are selected exit
            if ((SciCall_LineFromPosition(iSelStart) != SciCall_LineFromPosition(iSelEnd)) || (iSelCount >= COUNTOF(txtBuffer))) {
                return;
            }

            iFindLength = SciCall_GetSelText(pszText) - 1;

            // exit if selection is not a word and Match whole words only is enabled
            if (sFlags & SCFIND_WHOLEWORD) {
                DocPos iSelStart2 = 0;
                const char* delims = (Settings.AccelWordNavigation ? DelimCharsAccel : DelimChars);
                while ((iSelStart2 <= iSelCount) && pszText[iSelStart2]) {
                    if (StrChrIA(delims, pszText[iSelStart2])) {
                        return;
                    }
                    ++iSelStart2;
                }
            }
        }
    } else {
        iFindLength = (DocPos)StringCchLenA(pszFind, FNDRPL_BUFFER);
    }

    if (iFindLength > 0) {

        if (bMultiSel) {
            SciCall_ClearSelections();
        }

        DocPos const iTextEnd = Sci_GetDocEndPosition();
        rangeStart = max_p(0, rangeStart);
        rangeEnd = min_p(rangeEnd, iTextEnd);

        DocPos start = rangeStart;
        DocPos end = rangeEnd;
        DocPos iPos = _FindInTarget(pszText, iFindLength, sFlags, &start, &end, false, FRMOD_NORM);

        DocPosU count = 0;
        while ((iPos >= 0LL) && (start <= rangeEnd)) {

            if (bMultiSel) {
                if (count) {
                    SciCall_AddSelection(end, iPos);
                } else {
                    SciCall_SetSelection(end, iPos);
                }
            } else {
                // mark this match if not done before
                SciCall_SetIndicatorCurrent(INDIC_NP3_MARK_OCCURANCE);
                SciCall_IndicatorFillRange(iPos, (end - start));
                SciCall_MarkerAdd(SciCall_LineFromPosition(iPos), MARKER_NP3_OCCURRENCE);
            }
            ++count;
            start = end;
            end = rangeEnd;
            iPos = _FindInTarget(pszText, iFindLength, sFlags, &start, &end, true, FRMOD_NORM);
        };

        Globals.iMarkOccurrencesCount = count;
    }
    EndWaitCursor();
}


//=============================================================================
//
//  EditCheckNewLineInACFillUps()
//
bool EditCheckNewLineInACFillUps()
{
    return s_ACFillUpCharsHaveNewLn;
}


//=============================================================================
//
//  EditAutoCompleteWord()
//  Auto-complete words (by Aleksandar Lekov)
//

#define _MAX_AUTOC_WORD_LEN 240

typedef struct WLIST {
    struct WLIST* next;
    char word[_MAX_AUTOC_WORD_LEN];
} WLIST, *PWLIST;


static int  wordcmp(PWLIST a, PWLIST b)
{
    return StringCchCompareXA(a->word, b->word);
}

/* unused yet
static int  wordcmpi(PWLIST a, PWLIST b) {
  return StringCchCompareXIA(a->word, b->word);
}
*/

// ----------------------------------------------

static const char*  _strNextLexKeyWord(const char* strg, const char* const wdroot, DocPosCR* pwdlen)
{
    char const sep = ' ';
    bool found = false;
    const char* start = strg;
    do {
        start = StrStrIA(start, wdroot);
        if (start) {
            if ((start == strg) || (start[-1] == sep)) { // word begin
                found = true;
                break;
            }
            ++start;
        }
    } while (start && *start);

    if (found) {
        DocPosCR len = *pwdlen;
        while (start[len] && (start[len] != sep)) {
            ++len;
        }
        *pwdlen = len;
    } else {
        *pwdlen = 0;
    }
    return (found ? start : NULL);
}

// ----------------------------------------------

bool EditAutoCompleteWord(HWND hwnd, bool autoInsert)
{
    if (SciCall_IsIMEModeCJK()) {
        SciCall_AutoCCancel();
        return false;
    }

    DocPos const iMinWdChCnt = autoInsert ? 0 : 2;  // min number of typed chars before AutoC

    char const* const pchAllowdWordChars =
        ((Globals.bIsCJKInputCodePage || Globals.bUseLimitedAutoCCharSet) ? AutoCompleteWordCharSet :
         (Settings.AccelWordNavigation ? WordCharsAccelerated : WordCharsDefault));

    SciCall_SetWordChars(pchAllowdWordChars);

    DocPos const iDocEndPos = Sci_GetDocEndPosition();
    DocPos const iCurrentPos = SciCall_GetCurrentPos();
    DocPos const iCol = SciCall_GetColumn(iCurrentPos);
    DocPos const iPosBefore = SciCall_PositionBefore(iCurrentPos);
    DocPos const iWordStartPos = SciCall_WordStartPosition(iPosBefore, true);

    if (((iPosBefore - iWordStartPos) < iMinWdChCnt) || (iCol < iMinWdChCnt) || ((iCurrentPos - iWordStartPos) < iMinWdChCnt)) {
        EditSetAccelWordNav(hwnd, Settings.AccelWordNavigation);
        return true;
    }

    DocPos iPos = iWordStartPos;
    bool bWordAllNumbers = true;
    while ((iPos < iCurrentPos) && bWordAllNumbers && (iPos <= iDocEndPos)) {
        char const ch = SciCall_GetCharAt(iPos);
        if (ch < '0' || ch > '9') {
            bWordAllNumbers = false;
        }
        iPos = SciCall_PositionAfter(iPos);
    }
    if (!autoInsert && bWordAllNumbers) {
        EditSetAccelWordNav(hwnd, Settings.AccelWordNavigation);
        return true;
    }

    char pRoot[_MAX_AUTOC_WORD_LEN];
    DocPos const iRootLen = (iCurrentPos - iWordStartPos);
    StringCchCopyNA(pRoot, COUNTOF(pRoot), SciCall_GetRangePointer(iWordStartPos, iRootLen), (size_t)iRootLen);
    if ((iRootLen <= 0) || StrIsEmptyA(pRoot)) {
        return true;    // nothing to find
    }

    int iNumWords = 0;
    size_t iWListSize = 0;

    PWLIST pListHead = NULL;

    if (Settings.AutoCompleteWords || (autoInsert && !Settings.AutoCLexerKeyWords)) {
        struct Sci_TextToFind ft = { { 0, 0 }, 0, { 0, 0 } };
        ft.lpstrText = pRoot;
        ft.chrg.cpMax = (DocPosCR)iDocEndPos;

        DocPos iPosFind = SciCall_FindText(SCFIND_WORDSTART, &ft);
        PWLIST pwlNewWord = NULL;

        while ((iPosFind >= 0) && ((iPosFind + iRootLen) < iDocEndPos)) {
            DocPos const iWordEndPos = SciCall_WordEndPosition(iPosFind + iRootLen, true);

            if (iPosFind != (iCurrentPos - iRootLen)) {
                DocPos const wordLength = (iWordEndPos - iPosFind);
                if (wordLength > iRootLen) {
                    if (!pwlNewWord) {
                        pwlNewWord = (PWLIST)AllocMem(sizeof(WLIST), HEAP_ZERO_MEMORY);
                    }
                    if (pwlNewWord) {
                        StringCchCopyNA(pwlNewWord->word, _MAX_AUTOC_WORD_LEN, SciCall_GetRangePointer(iPosFind, wordLength), wordLength);

                        PWLIST pPrev = NULL;
                        PWLIST pWLItem = NULL;
                        LL_SEARCH_ORDERED(pListHead, pPrev, pWLItem, pwlNewWord, wordcmp);
                        if (!pWLItem) { // not found
                            //LL_INSERT_INORDER(pListHead, pwlNewWord, wordcmpi);
                            LL_APPEND_ELEM(pListHead, pPrev, pwlNewWord);
                            ++iNumWords;
                            iWListSize += (wordLength + 1);
                            pwlNewWord = NULL; // alloc new
                        }
                    }
                }
            }

            ft.chrg.cpMin = (DocPosCR)iWordEndPos;
            iPosFind = SciCall_FindText(SCFIND_WORDSTART, &ft);
        }
        FreeMem(pwlNewWord);
        pwlNewWord = NULL;
    }
    // --------------------------------------------------------------------------
    if (Settings.AutoCLexerKeyWords || (autoInsert && !Settings.AutoCompleteWords))
        // --------------------------------------------------------------------------
    {
        PKEYWORDLIST const pKeyWordList = Style_GetCurrentLexerPtr()->pKeyWords;

        PWLIST pwlNewWord = NULL;
        for (int i = 0; i <= KEYWORDSET_MAX; ++i) {
            const char* word = pKeyWordList->pszKeyWords[i];
            do {
                DocPosCR wlen = (DocPosCR)iRootLen;
                word = _strNextLexKeyWord(word, pRoot, &wlen);
                if (word) {
                    if (wlen > iRootLen) {
                        if (!pwlNewWord) {
                            pwlNewWord = (PWLIST)AllocMem(sizeof(WLIST), HEAP_ZERO_MEMORY);
                        }
                        if (pwlNewWord) {
                            StringCchCopyNA(pwlNewWord->word, _MAX_AUTOC_WORD_LEN, word, wlen);

                            PWLIST pPrev = NULL;
                            PWLIST pWLItem = NULL;
                            LL_SEARCH_ORDERED(pListHead, pPrev, pWLItem, pwlNewWord, wordcmp);
                            if (!pWLItem) { // not found
                                //LL_INSERT_INORDER(pListHead, pwlNewWord, wordcmpi);
                                LL_APPEND_ELEM(pListHead, pPrev, pwlNewWord);
                                ++iNumWords;
                                iWListSize += ((size_t)wlen + 1LL);
                                pwlNewWord = NULL; // alloc new
                            }
                        }
                    }
                    word += (wlen ? wlen : 1);
                }
            } while (word && word[0]);
        }
        FreeMem(pwlNewWord);
        pwlNewWord = NULL;
    }

    // --------------------------------------------------------------------------

    if (iNumWords > 0) {
        const char* const sep = " ";
        SciCall_AutoCCancel();
        SciCall_ClearRegisteredImages();

        SciCall_AutoCSetSeperator(sep[0]);
        SciCall_AutoCSetIgnoreCase(true);
        //~SciCall_AutoCSetCaseInsensitiveBehaviour(SC_CASEINSENSITIVEBEHAVIOUR_IGNORECASE);
        SciCall_AutoCSetChooseSingle(autoInsert);
        SciCall_AutoCSetOrder(SC_ORDER_PERFORMSORT); // already sorted
        SciCall_AutoCSetFillups(AutoCompleteFillUpChars);

        ++iWListSize; // zero termination
        char* const pList = AllocMem(iWListSize + 1, HEAP_ZERO_MEMORY);
        if (pList) {
            PWLIST pTmp = NULL;
            PWLIST pWLItem = NULL;
            LL_FOREACH_SAFE(pListHead, pWLItem, pTmp) {
                if (pWLItem->word[0]) {
                    StringCchCatA(pList, SizeOfMem(pList), sep);
                    StringCchCatA(pList, SizeOfMem(pList), pWLItem->word);
                }
                LL_DELETE(pListHead, pWLItem);
                FreeMem(pWLItem);
            }
            SciCall_AutoCShow(iRootLen, &pList[1]); // skip first sep
            FreeMem(pList);
        }
    }

    EditSetAccelWordNav(hwnd, Settings.AccelWordNavigation);
    return true;
}


//=============================================================================
//
//  EditUpdateVisibleIndicators()
//
void EditUpdateVisibleIndicators()
{
    DocLn const iStartLine = SciCall_DocLineFromVisible(SciCall_GetFirstVisibleLine());
    DocLn const iEndLine = min_ln((iStartLine + SciCall_LinesOnScreen()), (SciCall_GetLineCount() - 1));
    EditUpdateIndicators(SciCall_PositionFromLine(iStartLine), SciCall_GetLineEndPosition(iEndLine), false);
}


//=============================================================================
//
//  EditUpdateIndicators()
//  Find and mark all COLOR refs (#RRGGBB)
//
static void _ClearIndicatorInRange(const int indicator, const int indicator2nd,
                                   const DocPos startPos, const DocPos endPos)
{
    SciCall_SetIndicatorCurrent(indicator);
    SciCall_IndicatorClearRange(startPos, endPos - startPos);
    if (indicator2nd >= 0) {
        SciCall_SetIndicatorCurrent(indicator2nd);
        SciCall_IndicatorClearRange(startPos, endPos - startPos);
    }
}

static void _UpdateIndicators(const int indicator, const int indicator2nd,
                              const char* regExpr, DocPos startPos, DocPos endPos)
{
    if (endPos < 0) {
        endPos = Sci_GetDocEndPosition();
    } else if (endPos < startPos) {
        swapos(&startPos, &endPos);
    }
    if (startPos < 0) { // current line only
        DocLn const lineNo = SciCall_LineFromPosition(SciCall_GetCurrentPos());
        startPos = SciCall_PositionFromLine(lineNo);
        endPos = SciCall_GetLineEndPosition(lineNo);
    } else if (endPos == startPos) {
        return;
    }

    // --------------------------------------------------------------------------

    int const iRegExLen = (int)StringCchLenA(regExpr, 0);

    DocPos start = startPos;
    DocPos end = endPos;
    do {

        DocPos const start_m = start;
        DocPos const end_m   = end;
        DocPos const iPos = _FindInTarget(regExpr, iRegExLen, SCFIND_REGEXP, &start, &end, true, FRMOD_IGNORE);

        if (iPos < 0) {
            // not found
            _ClearIndicatorInRange(indicator, indicator2nd, start_m, end_m);
            break;
        }
        DocPos const mlen = end - start;
        if ((mlen <= 0) || (end > endPos)) {
            // wrong match
            _ClearIndicatorInRange(indicator, indicator2nd, start_m, end_m);
            break; // wrong match
        }

        _ClearIndicatorInRange(indicator, indicator2nd, start_m, end);

        SciCall_SetIndicatorCurrent(indicator);
        SciCall_IndicatorFillRange(start, mlen);
        if (indicator2nd >= 0) {
            SciCall_SetIndicatorCurrent(indicator2nd);
            SciCall_IndicatorFillRange(start, mlen);
        }

        // next occurrence
        start = end;
        end = endPos;

    } while (start < end);

}

//=============================================================================
//
//  EditUpdateIndicators()
//  - Find and mark all URL hot-spots
//  - Find and mark all COLOR refs (#RRGGBB)
//
void EditUpdateIndicators(DocPos startPos, DocPos endPos, bool bClearOnly)
{
    if (bClearOnly) {
        _ClearIndicatorInRange(INDIC_NP3_HYPERLINK, INDIC_NP3_HYPERLINK_U, startPos, endPos);
        _ClearIndicatorInRange(INDIC_NP3_COLOR_DEF, INDIC_NP3_COLOR_DEF_T, startPos, endPos);
        _ClearIndicatorInRange(INDIC_NP3_UNICODE_POINT, -1, startPos, endPos);
        return;
    }
    if (Settings.HyperlinkHotspot) {
        _UpdateIndicators(INDIC_NP3_HYPERLINK, INDIC_NP3_HYPERLINK_U, s_pUrlRegEx, startPos, endPos);
    } else {
        _ClearIndicatorInRange(INDIC_NP3_HYPERLINK, INDIC_NP3_HYPERLINK_U, startPos, endPos);
    }

    if (IsColorDefHotspotEnabled()) {
        if (Settings.ColorDefHotspot < 3) {
            _UpdateIndicators(INDIC_NP3_COLOR_DEF, -1, s_pColorRegEx, startPos, endPos);
        } else {
            _UpdateIndicators(INDIC_NP3_COLOR_DEF, -1, s_pColorRegEx_A, startPos, endPos);
        }
    } else {
        _ClearIndicatorInRange(INDIC_NP3_COLOR_DEF, INDIC_NP3_COLOR_DEF_T, startPos, endPos);
    }

    if (Settings.HighlightUnicodePoints) {
        _UpdateIndicators(INDIC_NP3_UNICODE_POINT, -1, s_pUnicodeRegEx, startPos, endPos);
    } else {
        _ClearIndicatorInRange(INDIC_NP3_UNICODE_POINT, -1, startPos, endPos);
    }
}


//=============================================================================
//
//  EditFoldMarkedLineRange()
//
void EditFoldMarkedLineRange(HWND hwnd, bool bHideLines)
{
    if (!bHideLines) {
        // reset
        SciCall_FoldAll(EXPAND);
        Style_SetFoldingAvailability(Style_GetCurrentLexerPtr());
        FocusedView.ShowCodeFolding = Settings.ShowCodeFolding;
        Style_SetFoldingProperties(FocusedView.CodeFoldingAvailable);
        Style_SetFolding(hwnd, FocusedView.CodeFoldingAvailable && FocusedView.ShowCodeFolding);
        Sci_ColouriseAll();
        EditMarkAllOccurrences(hwnd, true);
    } else { // =====   fold lines without marker   =====
        // prepare hidden (folding) settings
        FocusedView.CodeFoldingAvailable = true;
        FocusedView.ShowCodeFolding      = true;
        Style_SetFoldingFocusedView();
        Style_SetFolding(hwnd, true);

        int const baseLevel = SC_FOLDLEVELBASE;

        DocLn const iStartLine = 0;
        DocLn const iEndLine   = SciCall_GetLineCount() - 1;

        // 1st line
        int level = baseLevel;
        SciCall_SetFoldLevel(iStartLine, SC_FOLDLEVELHEADERFLAG | level++); // visible in any case

        int const bitmask = (1 << MARKER_NP3_OCCURRENCE);
        DocLn     markerLine = SciCall_MarkerNext(iStartLine + 1, bitmask);

        for (DocLn line = iStartLine + 1; line <= iEndLine; ++line) {
            if (line == markerLine) { // visible
                level = baseLevel;
                SciCall_SetFoldLevel(line, SC_FOLDLEVELHEADERFLAG | level++);
                markerLine = SciCall_MarkerNext(line + 1, bitmask); // next
            } else { // hide line
                SciCall_SetFoldLevel(line, SC_FOLDLEVELWHITEFLAG | level);
            }
        }
        SciCall_FoldAll(FOLD);
    }
}


//=============================================================================
//
//  EditBookMarkLineRange()
//
void EditBookMarkLineRange(HWND hwnd)
{
    UNREFERENCED_PARAMETER(hwnd);
    // get next free bookmark
    int marker;
    for (marker = MARKER_NP3_1; marker < MARKER_NP3_BOOKMARK; ++marker) { // all(!)
        if (SciCall_MarkerNext(0, (1 << marker)) < 0) {
            break; // found unused
        }
    }
    if (marker >= MARKER_NP3_BOOKMARK) {
        InfoBoxLng(MB_ICONWARNING, L"OutOfOccurrenceMarkers", IDS_MUI_OUT_OFF_OCCMRK);
        return;
    }

    DocLn line = 0;
    int const bitmask = (1 << MARKER_NP3_OCCURRENCE);
    do {
        line = SciCall_MarkerNext(line, bitmask);
        if (line >= 0) {
            SciCall_MarkerAdd(line, marker);
            ++line;
        }
    } while (line >= 0);
}


//=============================================================================
//
//  EditDeleteMarkerInSelection()
//
void EditDeleteMarkerInSelection()
{
    if (SciCall_IsSelectionEmpty()) {
        SciCall_MarkerDelete(Sci_GetCurrentLineNumber(), -1);
    } else if (Sci_IsStreamSelection()) {
        DocPos const posSelBeg = SciCall_GetSelectionStart();
        DocPos const posSelEnd = SciCall_GetSelectionEnd();
        DocLn const lnBeg = SciCall_LineFromPosition(posSelBeg);
        DocLn const lnEnd = SciCall_LineFromPosition(posSelEnd);
        DocLn const lnDelBeg = (posSelBeg <= SciCall_PositionFromLine(lnBeg)) ? lnBeg : lnBeg + 1;
        DocLn const lnDelEnd = (posSelEnd  > SciCall_GetLineEndPosition(lnEnd)) ? lnEnd : lnEnd - 1;
        for (DocLn ln = lnDelBeg; ln <= lnDelEnd; ++ln) {
            SciCall_MarkerDelete(ln, -1);
        }
    }
}


//=============================================================================
//
//  _HighlightIfBrace()
//
static bool _HighlightIfBrace(const HWND hwnd, const DocPos iPos)
{
    UNREFERENCED_PARAMETER(hwnd);
    if (iPos < 0) {
        // clear indicator
        SciCall_BraceBadLight(INVALID_POSITION);
        SciCall_SetHighLightGuide(0);
        return true;
    }

    char const c = SciCall_GetCharAt(iPos);

    if (StrChrA(NP3_BRACES_TO_MATCH, c)) {
        DocPos const iBrace2 = SciCall_BraceMatch(iPos);
        if (iBrace2 != (DocPos)-1) {
            int const col1 = (int)SciCall_GetColumn(iPos);
            int const col2 = (int)SciCall_GetColumn(iBrace2);
            SciCall_BraceHighLight(iPos, iBrace2);
            SciCall_SetHighLightGuide(min_i(col1, col2));
        } else {
            SciCall_BraceBadLight(iPos);
            SciCall_SetHighLightGuide(0);
        }
        return true;
    }
    return false;
}


//=============================================================================
//
//  EditMatchBrace()
//
void EditMatchBrace(HWND hwnd)
{
    DocPos iPos = SciCall_GetCurrentPos();
    if (!_HighlightIfBrace(hwnd, iPos)) {
        // try one before
        iPos = SciCall_PositionBefore(iPos);
        if (!_HighlightIfBrace(hwnd, iPos)) {
            // clear mark
            _HighlightIfBrace(hwnd, -1);
        }
    }
}



//=============================================================================
//
//  EditLinenumDlgProc()
//
static INT_PTR CALLBACK EditLinenumDlgProc(HWND hwnd,UINT umsg,WPARAM wParam,LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch(umsg) {
    case WM_INITDIALOG: {
        SetDialogIconNP3(hwnd);
        InitWindowCommon(hwnd, true);

#ifdef D_NP3_WIN10_DARK_MODE
        if (UseDarkMode()) {
            SetExplorerTheme(GetDlgItem(hwnd, IDOK));
            SetExplorerTheme(GetDlgItem(hwnd, IDCANCEL));
            //SetExplorerTheme(GetDlgItem(hwnd, IDC_RESIZEGRIP));
        }
#endif

        DocLn const iCurLine = SciCall_LineFromPosition(SciCall_GetCurrentPos())+1;
        DocLn const iMaxLnNum = SciCall_GetLineCount();
        DocPos const iCurColumn = SciCall_GetColumn(SciCall_GetCurrentPos()) + 1;
        DocPos const iLineEndPos = Sci_GetNetLineLength(iCurLine);

        WCHAR wchLineCaption[96];
        WCHAR wchColumnCaption[96];
        FormatLngStringW(wchLineCaption, COUNTOF(wchLineCaption), IDS_MUI_GOTO_LINE,
                         (int)clampp(iMaxLnNum, 0, INT_MAX));
        FormatLngStringW(wchColumnCaption, COUNTOF(wchColumnCaption), IDS_MUI_GOTO_COLUMN,
                         (int)clampp(max_p(iLineEndPos, (DocPos)Settings.LongLinesLimit), 0, INT_MAX));
        SetDlgItemText(hwnd, IDC_LINE_TEXT, wchLineCaption);
        SetDlgItemText(hwnd, IDC_COLUMN_TEXT, wchColumnCaption);

        SetDlgItemInt(hwnd, IDC_LINENUM, (int)clampp(iCurLine, 0, INT_MAX), false);
        SetDlgItemInt(hwnd, IDC_COLNUM, (int)clampp(iCurColumn, 0, INT_MAX), false);
        SendDlgItemMessage(hwnd,IDC_LINENUM,EM_LIMITTEXT,80,0);
        SendDlgItemMessage(hwnd,IDC_COLNUM,EM_LIMITTEXT,80,0);
        CenterDlgInParent(hwnd, NULL);
    }
    return true;


    case WM_DPICHANGED: {
        UINT const dpi = LOWORD(wParam);
        UpdateWindowLayoutForDPI(hwnd, (RECT *)lParam, dpi);
    }
    return true;


#ifdef D_NP3_WIN10_DARK_MODE

CASE_WM_CTLCOLOR_SET:
    return SetDarkModeCtlColors((HDC)wParam, UseDarkMode());
    break;

    case WM_SETTINGCHANGE:
        if (IsDarkModeSupported() && IsColorSchemeChangeMessage(lParam)) {
            SendMessage(hwnd, WM_THEMECHANGED, 0, 0);
        }
        break;

    case WM_THEMECHANGED:
        if (IsDarkModeSupported()) {
            bool const darkModeEnabled = CheckDarkModeEnabled();
            AllowDarkModeForWindowEx(hwnd, darkModeEnabled);
            RefreshTitleBarThemeColor(hwnd);

            int const buttons[] = { IDOK, IDCANCEL };
            for (int id = 0; id < COUNTOF(buttons); ++id) {
                HWND const hBtn = GetDlgItem(hwnd, buttons[id]);
                AllowDarkModeForWindowEx(hBtn, darkModeEnabled);
                SendMessage(hBtn, WM_THEMECHANGED, 0, 0);
            }
            UpdateWindowEx(hwnd);
        }
        break;

#endif


    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDOK: {
            DocLn const iMaxLnNum = SciCall_GetLineCount();

            //~BOOL fTranslated = TRUE;
            //~DocLn iNewLine = (DocLn)GetDlgItemInt(hwnd, IDC_LINENUM, &fTranslated, FALSE);

            intptr_t iExprError    = 0;
            bool bLnTranslated = true;
            DocLn iNewLine = 0;
            if (SendDlgItemMessage(hwnd, IDC_LINENUM, WM_GETTEXTLENGTH, 0, 0) > 0) {
                char chLineNumber[96];
                GetDlgItemTextA(hwnd, IDC_LINENUM, chLineNumber, COUNTOF(chLineNumber));
                iNewLine = (DocLn)te_interp(chLineNumber, &iExprError);
                if (iExprError > 1) {
                    chLineNumber[iExprError-1] = '\0';
                    iNewLine = (DocLn)te_interp(chLineNumber, &iExprError);
                }
                bLnTranslated = (iExprError == 0);
            }

            bool bColTranslated = true;
            DocPos iNewCol = 1;
            if (SendDlgItemMessage(hwnd, IDC_COLNUM, WM_GETTEXTLENGTH, 0, 0) > 0) {
                char chColumnNumber[96];
                GetDlgItemTextA(hwnd, IDC_COLNUM, chColumnNumber, COUNTOF(chColumnNumber));
                iNewCol = (DocPos)te_interp(chColumnNumber, &iExprError);
                if (iExprError > 1) {
                    chColumnNumber[iExprError-1] = '\0';
                    iNewLine = (DocLn)te_interp(chColumnNumber, &iExprError);
                }
                bColTranslated = (iExprError == 0);
            }

            if (!bLnTranslated || !bColTranslated) {
                PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)(GetDlgItem(hwnd, (!bLnTranslated) ? IDC_LINENUM : IDC_COLNUM)), 1);
                return true;
            }

            if ((iNewLine > 0) && (iNewLine <= iMaxLnNum) && (iNewCol > 0)) {
                EditJumpTo(iNewLine, iNewCol);
                EndDialog(hwnd, IDOK);
            } else {
                PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)(GetDlgItem(hwnd, (!((iNewLine > 0) && (iNewLine <= iMaxLnNum))) ? IDC_LINENUM : IDC_COLNUM)), 1);
            }
        }
        break;

        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
            break;

        }
    }
    return true;
    }
    return false;
}


//=============================================================================
//
//  EditLinenumDlg()
//
bool EditLinenumDlg(HWND hwnd)
{
    if (IDOK == ThemedDialogBoxParam(Globals.hLngResContainer, MAKEINTRESOURCE(IDD_MUI_LINENUM),
                                     GetParent(hwnd), EditLinenumDlgProc, (LPARAM)hwnd)) {
        return true;
    }
    return false;
}


//=============================================================================
//
//  EditModifyLinesDlg()
//
//  Controls: 100 Input
//            101 Input
//
typedef struct _modlinesdata {
    LPWSTR pwsz1;
    LPWSTR pwsz2;
} MODLINESDATA, *PMODLINESDATA;


static INT_PTR CALLBACK EditModifyLinesDlgProc(HWND hwnd,UINT umsg,WPARAM wParam,LPARAM lParam)
{
    static PMODLINESDATA pdata;

    static unsigned id_hover = 0;
    static unsigned id_capture = 0;

    //static HFONT   hFontNormal = NULL;
    static HFONT   hFontHover = NULL;
    static HCURSOR hCursorNormal;
    static HCURSOR hCursorHover;

    switch(umsg) {
    case WM_INITDIALOG: {
        id_hover = 0;
        id_capture = 0;

        SetDialogIconNP3(hwnd);
        InitWindowCommon(hwnd, true);

#ifdef D_NP3_WIN10_DARK_MODE
        if (UseDarkMode()) {
            SetExplorerTheme(GetDlgItem(hwnd, IDOK));
            SetExplorerTheme(GetDlgItem(hwnd, IDCANCEL));
            //SetExplorerTheme(GetDlgItem(hwnd, IDC_RESIZEGRIP));
        }
#endif

        HFONT const hFont = (HFONT)SendDlgItemMessage(hwnd, 200, WM_GETFONT, 0, 0);
        if (hFont) {
            LOGFONT lf = { 0 };
            GetObject(hFont, sizeof(LOGFONT), &lf);
            lf.lfUnderline = true;
            //lf.lfWeight    = FW_BOLD;
            if (hFontHover) {
                DeleteObject(hFontHover);
            }
            hFontHover = CreateFontIndirectW(&lf);
        }

        hCursorNormal = LoadCursor(NULL, IDC_ARROW);
        hCursorHover = LoadCursor(NULL,IDC_HAND);
        if (!hCursorHover) {
            hCursorHover = LoadCursor(Globals.hInstance, IDC_ARROW);
        }
        pdata = (PMODLINESDATA)lParam;
        SetDlgItemTextW(hwnd,100,pdata->pwsz1);
        SendDlgItemMessage(hwnd,100,EM_LIMITTEXT,255,0);
        SetDlgItemTextW(hwnd,101,pdata->pwsz2);
        SendDlgItemMessage(hwnd,101,EM_LIMITTEXT,255,0);
        CenterDlgInParent(hwnd, NULL);
    }
    return true;

    case WM_DPICHANGED: {
        //UINT const dpi = LOWORD(wParam);
        HFONT const hFont = (HFONT)SendDlgItemMessage(hwnd, 200, WM_GETFONT, 0, 0);
        if (hFont) {
            LOGFONT lf = { 0 };
            GetObject(hFont, sizeof(LOGFONT), &lf);
            lf.lfUnderline = true;
            //lf.lfWeight    = FW_BOLD;
            if (hFontHover) {
                DeleteObject(hFontHover);
            }
            hFontHover = CreateFontIndirectW(&lf);
        }
        //@@@UpdateWindowLayoutForDPI(hwnd, NULL, dpi);
        UpdateWindowLayoutForDPI(hwnd, (RECT*)lParam, 0);
    }
    return TRUE;

    case WM_DESTROY:
        //DeleteObject(hFontNormal);
        DeleteObject(hFontHover);
        return FALSE;

    case WM_NCACTIVATE:
        if (!(bool)wParam) {
            if (id_hover != 0) {
                //int _id_hover = id_hover;
                id_hover = 0;
                id_capture = 0;
            }
        }
        return FALSE;

#ifdef D_NP3_WIN10_DARK_MODE

CASE_WM_CTLCOLOR_SET: {
            DWORD const dwId = GetWindowLong((HWND)lParam, GWL_ID);
            HDC const hdc = (HDC)wParam;
            INT_PTR const hbrReturn = SetDarkModeCtlColors(hdc, UseDarkMode());
            if (dwId >= 200 && dwId <= 205) {
                SetBkMode(hdc, TRANSPARENT);
                if (GetSysColorBrush(COLOR_HOTLIGHT)) {
                    SetTextColor(hdc, GetSysColor(COLOR_HOTLIGHT));
                } else {
                    SetTextColor(hdc, RGB(0, 0, 0xFF));
                }
                //SelectObject(hdc, (dwId == id_hover) ? hFontHover : hFontNormal);
                SelectObject(hdc, hFontHover);
            }
            return hbrReturn;
        }
        break;

    case WM_SETTINGCHANGE:
        if (IsDarkModeSupported() && IsColorSchemeChangeMessage(lParam)) {
            SendMessage(hwnd, WM_THEMECHANGED, 0, 0);
        }
        break;

    case WM_THEMECHANGED:
        if (IsDarkModeSupported()) {
            bool const darkModeEnabled = CheckDarkModeEnabled();
            AllowDarkModeForWindowEx(hwnd, darkModeEnabled);
            RefreshTitleBarThemeColor(hwnd);

            int const buttons[] = { IDOK, IDCANCEL };
            for (int id = 0; id < COUNTOF(buttons); ++id) {
                HWND const hBtn = GetDlgItem(hwnd, buttons[id]);
                AllowDarkModeForWindowEx(hBtn, darkModeEnabled);
                SendMessage(hBtn, WM_THEMECHANGED, 0, 0);
            }
            UpdateWindowEx(hwnd);
        }
        break;

#endif

    case WM_MOUSEMOVE: {
        POINT pt = { 0, 0 };
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);
        HWND hwndHover = ChildWindowFromPoint(hwnd,pt);
        DWORD dwId = (DWORD)GetWindowLong(hwndHover,GWL_ID);

        if (GetActiveWindow() == hwnd) {
            if (dwId >= 200 && dwId <= 205) {
                if (id_capture == (int)dwId || id_capture == 0) {
                    if (id_hover != id_capture || id_hover == 0) {
                        id_hover = (int)dwId;
                    }
                } else {
                    id_hover = 0;
                }
            } else {
                id_hover = 0;
            }
            SetCursor((id_hover != 0) ? hCursorHover : hCursorNormal);
        }
    }
    break;

    case WM_LBUTTONDOWN: {
        POINT pt = { 0, 0 };
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);
        HWND hwndHover = ChildWindowFromPoint(hwnd,pt);
        DWORD dwId = GetWindowLong(hwndHover,GWL_ID);

        if (dwId >= 200 && dwId <= 205) {
            GetCapture();
            id_hover = dwId;
            id_capture = dwId;
        }
        SetCursor((id_hover != 0) ? hCursorHover : hCursorNormal);
    }
    break;

    case WM_LBUTTONUP: {
        //POINT pt;
        //pt.x = LOWORD(lParam);  pt.y = HIWORD(lParam);
        //HWND hwndHover = ChildWindowFromPoint(hwnd,pt);
        //DWORD dwId = GetWindowLong(hwndHover,GWL_ID);
        if (id_capture != 0) {
            ReleaseCapture();
            if (id_hover == id_capture) {
                int id_focus = GetWindowLong(GetFocus(),GWL_ID);
                if (id_focus == 100 || id_focus == 101) {
                    WCHAR wch[8];
                    GetDlgItemText(hwnd,id_capture,wch,COUNTOF(wch));
                    SendDlgItemMessage(hwnd,id_focus,EM_SETSEL,(WPARAM)0,(LPARAM)-1);
                    SendDlgItemMessage(hwnd,id_focus,EM_REPLACESEL,(WPARAM)true,(LPARAM)wch);
                    PostMessage(hwnd,WM_NEXTDLGCTL,(WPARAM)(GetFocus()),1);
                }
            }
            id_capture = 0;
        }
        SetCursor((id_hover != 0) ? hCursorHover : hCursorNormal);
    }
    break;

    case WM_CANCELMODE:
        if (id_capture != 0) {
            ReleaseCapture();
            id_hover = 0;
            id_capture = 0;
            SetCursor(hCursorNormal);
        }
        break;

    case WM_COMMAND:
        switch(LOWORD(wParam)) {
        case IDOK: {
            GetDlgItemTextW(hwnd,100,pdata->pwsz1,256);
            GetDlgItemTextW(hwnd,101,pdata->pwsz2,256);
            EndDialog(hwnd,IDOK);
        }
        break;
        case IDCANCEL:
            EndDialog(hwnd,IDCANCEL);
            break;
        }
        return TRUE;
    }
    return FALSE;
}


//=============================================================================
//
//  EditModifyLinesDlg()
//
bool EditModifyLinesDlg(HWND hwnd,LPWSTR pwsz1,LPWSTR pwsz2)
{

    INT_PTR iResult;
    MODLINESDATA data = { 0 };
    data.pwsz1 = pwsz1;
    data.pwsz2 = pwsz2;

    iResult = ThemedDialogBoxParam(
                  Globals.hLngResContainer,
                  MAKEINTRESOURCEW(IDD_MUI_MODIFYLINES),
                  hwnd,
                  EditModifyLinesDlgProc,
                  (LPARAM)&data);

    return (iResult == IDOK) ? true : false;

}


//=============================================================================
//
//  EditAlignDlgProc()
//
//  Controls: 100 Radio Button
//            101 Radio Button
//            102 Radio Button
//            103 Radio Button
//            104 Radio Button
//
static INT_PTR CALLBACK EditAlignDlgProc(HWND hwnd,UINT umsg,WPARAM wParam,LPARAM lParam)
{
    static int *piAlignMode;
    switch(umsg) {
    case WM_INITDIALOG: {
        piAlignMode = (int*)lParam;
        SetDialogIconNP3(hwnd);
        InitWindowCommon(hwnd, true);

#ifdef D_NP3_WIN10_DARK_MODE
        if (UseDarkMode()) {
            SetExplorerTheme(GetDlgItem(hwnd, IDOK));
            SetExplorerTheme(GetDlgItem(hwnd, IDCANCEL));
            //SetExplorerTheme(GetDlgItem(hwnd, IDC_RESIZEGRIP));
            int const ctl[] = { 100, 101, 102, 103, 104, -1 };
            for (int i = 0; i < COUNTOF(ctl); ++i) {
                SetWindowTheme(GetDlgItem(hwnd, ctl[i]), L"", L""); // remove theme for BS_AUTORADIOBUTTON
            }
        }
#endif
        CheckRadioButton(hwnd,100,104,*piAlignMode+100);
        CenterDlgInParent(hwnd, NULL);
    }
    return true;

    case WM_DPICHANGED:
        UpdateWindowLayoutForDPI(hwnd, (RECT*)lParam, 0);
        return true;

#ifdef D_NP3_WIN10_DARK_MODE

CASE_WM_CTLCOLOR_SET:
        return SetDarkModeCtlColors((HDC)wParam, UseDarkMode());
        break;

    case WM_SETTINGCHANGE:
        if (IsDarkModeSupported() && IsColorSchemeChangeMessage(lParam)) {
            SendMessage(hwnd, WM_THEMECHANGED, 0, 0);
        }
        break;

    case WM_THEMECHANGED:
        if (IsDarkModeSupported()) {
            bool const darkModeEnabled = CheckDarkModeEnabled();
            AllowDarkModeForWindowEx(hwnd, darkModeEnabled);
            RefreshTitleBarThemeColor(hwnd);

            int const buttons[] = { IDOK, IDCANCEL };
            for (int id = 0; id < COUNTOF(buttons); ++id) {
                HWND const hBtn = GetDlgItem(hwnd, buttons[id]);
                AllowDarkModeForWindowEx(hBtn, darkModeEnabled);
                SendMessage(hBtn, WM_THEMECHANGED, 0, 0);
            }
            UpdateWindowEx(hwnd);
        }
        break;

#endif

    case WM_COMMAND:
        switch(LOWORD(wParam)) {
        case IDOK: {
            *piAlignMode = 0;
            if (IsButtonChecked(hwnd,100)) {
                *piAlignMode = ALIGN_LEFT;
            } else if (IsButtonChecked(hwnd,101)) {
                *piAlignMode = ALIGN_RIGHT;
            } else if (IsButtonChecked(hwnd,102)) {
                *piAlignMode = ALIGN_CENTER;
            } else if (IsButtonChecked(hwnd,103)) {
                *piAlignMode = ALIGN_JUSTIFY;
            } else if (IsButtonChecked(hwnd,104)) {
                *piAlignMode = ALIGN_JUSTIFY_EX;
            }
            EndDialog(hwnd,IDOK);
        }
        break;

        case IDCANCEL:
            EndDialog(hwnd,IDCANCEL);
            break;
        }
        return true;
    }
    return false;
}


//=============================================================================
//
//  EditAlignDlg()
//
bool EditAlignDlg(HWND hwnd,int *piAlignMode)
{

    INT_PTR iResult;

    iResult = ThemedDialogBoxParam(
                  Globals.hLngResContainer,
                  MAKEINTRESOURCEW(IDD_MUI_ALIGN),
                  hwnd,
                  EditAlignDlgProc,
                  (LPARAM)piAlignMode);

    return (iResult == IDOK) ? true : false;

}


//=============================================================================
//
//  EditEncloseSelectionDlgProc()
//
//  Controls: 100 Input
//            101 Input
//
typedef struct _encloseselectiondata {
    LPWSTR pwsz1;
    LPWSTR pwsz2;
} ENCLOSESELDATA, *PENCLOSESELDATA;


static INT_PTR CALLBACK EditEncloseSelectionDlgProc(HWND hwnd,UINT umsg,WPARAM wParam,LPARAM lParam)
{
    static PENCLOSESELDATA pdata;
    switch(umsg) {
    case WM_INITDIALOG: {
        pdata = (PENCLOSESELDATA)lParam;
        SetDialogIconNP3(hwnd);
        InitWindowCommon(hwnd, true);

#ifdef D_NP3_WIN10_DARK_MODE
        if (UseDarkMode()) {
            SetExplorerTheme(GetDlgItem(hwnd, IDOK));
            SetExplorerTheme(GetDlgItem(hwnd, IDCANCEL));
            //SetExplorerTheme(GetDlgItem(hwnd, IDC_RESIZEGRIP));
        }
#endif
        SendDlgItemMessage(hwnd, 100, EM_LIMITTEXT, 255, 0);
        SetDlgItemTextW(hwnd,100,pdata->pwsz1);
        SendDlgItemMessage(hwnd,101,EM_LIMITTEXT,255,0);
        SetDlgItemTextW(hwnd,101,pdata->pwsz2);
        CenterDlgInParent(hwnd, NULL);
    }
    return TRUE;

    case WM_DPICHANGED:
        UpdateWindowLayoutForDPI(hwnd, (RECT*)lParam, 0);
        return TRUE;

#ifdef D_NP3_WIN10_DARK_MODE

CASE_WM_CTLCOLOR_SET:
        return SetDarkModeCtlColors((HDC)wParam, UseDarkMode());
        break;

    case WM_SETTINGCHANGE:
        if (IsDarkModeSupported() && IsColorSchemeChangeMessage(lParam)) {
            SendMessage(hwnd, WM_THEMECHANGED, 0, 0);
        }
        break;

    case WM_THEMECHANGED:
        if (IsDarkModeSupported()) {
            bool const darkModeEnabled = CheckDarkModeEnabled();
            AllowDarkModeForWindowEx(hwnd, darkModeEnabled);
            RefreshTitleBarThemeColor(hwnd);

            int const buttons[] = { IDOK, IDCANCEL };
            for (int id = 0; id < COUNTOF(buttons); ++id) {
                HWND const hBtn = GetDlgItem(hwnd, buttons[id]);
                AllowDarkModeForWindowEx(hBtn, darkModeEnabled);
                SendMessage(hBtn, WM_THEMECHANGED, 0, 0);
            }
            UpdateWindowEx(hwnd);
        }
        break;

#endif

    case WM_COMMAND:
        switch(LOWORD(wParam)) {
        case IDOK: {
            GetDlgItemTextW(hwnd,100,pdata->pwsz1,256);
            GetDlgItemTextW(hwnd,101,pdata->pwsz2,256);
            EndDialog(hwnd,IDOK);
        }
        break;
        case IDCANCEL:
            EndDialog(hwnd,IDCANCEL);
            break;
        }
        return TRUE;
    }
    return FALSE;
}


//=============================================================================
//
//  EditEncloseSelectionDlg()
//
bool EditEncloseSelectionDlg(HWND hwnd,LPWSTR pwszOpen,LPWSTR pwszClose)
{

    INT_PTR iResult;
    ENCLOSESELDATA data = { 0 };
    data.pwsz1 = pwszOpen;
    data.pwsz2 = pwszClose;

    iResult = ThemedDialogBoxParam(
                  Globals.hLngResContainer,
                  MAKEINTRESOURCEW(IDD_MUI_ENCLOSESELECTION),
                  hwnd,
                  EditEncloseSelectionDlgProc,
                  (LPARAM)&data);

    return (iResult == IDOK) ? true : false;

}


//=============================================================================
//
//  EditInsertTagDlgProc()
//
//  Controls: 100 Input
//            101 Input
//            102 Times
//
typedef struct _tagsdata {
    LPWSTR pwsz1;
    LPWSTR pwsz2;
    UINT   repeat;
} TAGSDATA, *PTAGSDATA;


static INT_PTR CALLBACK EditInsertTagDlgProc(HWND hwnd,UINT umsg,WPARAM wParam,LPARAM lParam)
{
    static PTAGSDATA pdata;
    static WCHAR wchOpenTagStrg[256] = { L'\0' };
    static WCHAR wchCloseTagStrg[256] = { L'\0' };

    switch(umsg) {
    case WM_INITDIALOG: {
        pdata = (PTAGSDATA)lParam;
        SetDialogIconNP3(hwnd);
        InitWindowCommon(hwnd, true);

#ifdef D_NP3_WIN10_DARK_MODE
        if (UseDarkMode()) {
            SetExplorerTheme(GetDlgItem(hwnd, IDOK));
            SetExplorerTheme(GetDlgItem(hwnd, IDCANCEL));
            //SetExplorerTheme(GetDlgItem(hwnd, IDC_RESIZEGRIP));
        }
#endif
        if (!wchOpenTagStrg[0]) {
            StringCchCopy(wchOpenTagStrg, COUNTOF(wchOpenTagStrg), L"<tag>");
        }
        if (!wchCloseTagStrg[0]) {
            StringCchCopy(wchCloseTagStrg, COUNTOF(wchCloseTagStrg), L"</tag>");
        }
        SendDlgItemMessage(hwnd,100,EM_LIMITTEXT, COUNTOF(wchOpenTagStrg)-1,0);
        SetDlgItemTextW(hwnd,100, wchOpenTagStrg);
        SendDlgItemMessage(hwnd,101,EM_LIMITTEXT, COUNTOF(wchCloseTagStrg)-1,0);
        SetDlgItemTextW(hwnd,101, wchCloseTagStrg);
        pdata->repeat = 1;
        SetDlgItemInt(hwnd, 102, pdata->repeat, FALSE);
        SetFocus(GetDlgItem(hwnd,100));
        PostMessageW(GetDlgItem(hwnd,100),EM_SETSEL,1,(LPARAM)(StringCchLen(wchOpenTagStrg,0)-1));
        CenterDlgInParent(hwnd, NULL);
    }
    return false;

    case WM_DPICHANGED:
        UpdateWindowLayoutForDPI(hwnd, (RECT*)lParam, 0);
        return true;

#ifdef D_NP3_WIN10_DARK_MODE

CASE_WM_CTLCOLOR_SET:
        return SetDarkModeCtlColors((HDC)wParam, UseDarkMode());
        break;

    case WM_SETTINGCHANGE:
        if (IsDarkModeSupported() && IsColorSchemeChangeMessage(lParam)) {
            SendMessage(hwnd, WM_THEMECHANGED, 0, 0);
        }
        break;

    case WM_THEMECHANGED:
        if (IsDarkModeSupported()) {
            bool const darkModeEnabled = CheckDarkModeEnabled();
            AllowDarkModeForWindowEx(hwnd, darkModeEnabled);
            RefreshTitleBarThemeColor(hwnd);

            int const buttons[] = { IDOK, IDCANCEL };
            for (int id = 0; id < COUNTOF(buttons); ++id) {
                HWND const hBtn = GetDlgItem(hwnd, buttons[id]);
                AllowDarkModeForWindowEx(hBtn, darkModeEnabled);
                SendMessage(hBtn, WM_THEMECHANGED, 0, 0);
            }
            UpdateWindowEx(hwnd);
        }
        break;

#endif

    case WM_COMMAND:
        switch(LOWORD(wParam)) {
        case 100: {
            if (HIWORD(wParam) == EN_CHANGE) {
                bool bClear = true;
                GetDlgItemTextW(hwnd,100,wchOpenTagStrg, COUNTOF(wchOpenTagStrg));
                if (StringCchLenW(wchOpenTagStrg,COUNTOF(wchOpenTagStrg)) >= 3) {

                    if (wchOpenTagStrg[0] == L'<') {
                        WCHAR wchIns[COUNTOF(wchCloseTagStrg)] = { L'\0' };
                        StringCchCopy(wchIns, COUNTOF(wchIns), L"</");
                        int  cchIns = 2;
                        const WCHAR* pwCur = &wchOpenTagStrg[1];
                        while (
                            *pwCur &&
                            *pwCur != L'<' &&
                            *pwCur != L'>' &&
                            *pwCur != L' ' &&
                            *pwCur != L'\t' &&
                            (StrChr(L":_-.",*pwCur) || IsCharAlphaNumericW(*pwCur)))

                        {
                            wchIns[cchIns++] = *pwCur++;
                        }

                        while (*pwCur && *pwCur != L'>') {
                            pwCur++;
                        }

                        if (*pwCur == L'>' && *(pwCur-1) != L'/') {
                            wchIns[cchIns++] = L'>';
                            wchIns[cchIns] = L'\0';

                            if (cchIns > 3 &&
                                    StringCchCompareXI(wchIns,L"</base>") &&
                                    StringCchCompareXI(wchIns,L"</bgsound>") &&
                                    StringCchCompareXI(wchIns,L"</br>") &&
                                    StringCchCompareXI(wchIns,L"</embed>") &&
                                    StringCchCompareXI(wchIns,L"</hr>") &&
                                    StringCchCompareXI(wchIns,L"</img>") &&
                                    StringCchCompareXI(wchIns,L"</input>") &&
                                    StringCchCompareXI(wchIns,L"</link>") &&
                                    StringCchCompareXI(wchIns,L"</meta>")) {
                                SetDlgItemTextW(hwnd,101, wchIns);
                                bClear = false;
                            }
                        }
                    }
                }
                if (bClear) {
                    SetDlgItemTextW(hwnd, 101, L"");
                }
            }
        }
        break;
        case IDOK: {
            GetDlgItemTextW(hwnd, 100, wchOpenTagStrg, COUNTOF(wchOpenTagStrg));
            GetDlgItemTextW(hwnd, 101, wchCloseTagStrg, COUNTOF(wchCloseTagStrg));
            StringCchCopy(pdata->pwsz1, 256, wchOpenTagStrg);
            StringCchCopy(pdata->pwsz2, 256, wchCloseTagStrg);
            BOOL fTranslated = FALSE;
            UINT const iTimes = GetDlgItemInt(hwnd, 102, &fTranslated, FALSE);
            if (fTranslated) {
                pdata->repeat = clampu(iTimes, 1, UINT_MAX);
            }
            EndDialog(hwnd,IDOK);
        }
        break;
        case IDCANCEL:
            EndDialog(hwnd,IDCANCEL);
            break;
        }
        return true;
    }
    return false;
}


//=============================================================================
//
//  EditInsertTagDlg()
//
bool EditInsertTagDlg(HWND hwnd,LPWSTR pwszOpen,LPWSTR pwszClose, UINT* pRepeat)
{

    INT_PTR iResult = 0;
    TAGSDATA data = { 0 };
    data.pwsz1 = pwszOpen;
    data.pwsz2 = pwszClose;
    data.repeat = 1;

    iResult = ThemedDialogBoxParam(
                  Globals.hLngResContainer,
                  MAKEINTRESOURCEW(IDD_MUI_INSERTTAG),
                  hwnd,
                  EditInsertTagDlgProc,
                  (LPARAM)&data);

    if (iResult == IDOK) {
        *pRepeat = data.repeat;
        return true;
    }
    return false;
}


//=============================================================================
//
//  EditSortDlgProc()
//
//  Controls: 100-102 Radio Button
//            103-109 Check Box
//
static INT_PTR CALLBACK EditSortDlgProc(HWND hwnd,UINT umsg,WPARAM wParam,LPARAM lParam)
{
    static int* piSortFlags;
    switch(umsg) {
    case WM_INITDIALOG: {
        piSortFlags = (int*)lParam;

        if (*piSortFlags == 0) {
            *piSortFlags = SORT_ASCENDING | SORT_REMZEROLEN;
        }

        SetDialogIconNP3(hwnd);
        InitWindowCommon(hwnd, true);

#ifdef D_NP3_WIN10_DARK_MODE
        if (UseDarkMode()) {
            SetExplorerTheme(GetDlgItem(hwnd, IDOK));
            SetExplorerTheme(GetDlgItem(hwnd, IDCANCEL));
            //SetExplorerTheme(GetDlgItem(hwnd, IDC_RESIZEGRIP));
            int const ctl[] = { 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, -1 };
            for (int i = 0; i < COUNTOF(ctl); ++i) {
                SetWindowTheme(GetDlgItem(hwnd, ctl[i]), L"", L""); // remove theme for BS_AUTORADIOBUTTON
            }
        }
#endif

        if (*piSortFlags & SORT_DESCENDING) {
            CheckRadioButton(hwnd, 100, 102, 101);
        } else if (*piSortFlags & SORT_SHUFFLE) {
            CheckRadioButton(hwnd,100,102,102);
            int const ctl[] = { 103, 104, 105, 106, 107, 108, 109, 110, 111, 112 };
            for (int i = 0; i < COUNTOF(ctl); ++i) {
                DialogEnableControl(hwnd, ctl[i], false);
            }
        } else {
            CheckRadioButton(hwnd, 100, 102, 100);
        }
        if (*piSortFlags & SORT_MERGEDUP) {
            CheckDlgButton(hwnd, 103, BST_CHECKED);
        }
        if (*piSortFlags & SORT_UNIQDUP) {
            CheckDlgButton(hwnd, 104, BST_CHECKED);
            DialogEnableControl(hwnd, 103, false);
        }
        if (*piSortFlags & SORT_UNIQUNIQ) {
            CheckDlgButton(hwnd, 105, BST_CHECKED);
        }
        if (*piSortFlags & SORT_REMZEROLEN) {
            CheckDlgButton(hwnd, 106, BST_CHECKED);
        }
        if (*piSortFlags & SORT_REMWSPACELN) {
            CheckDlgButton(hwnd, 107, BST_CHECKED);
            CheckDlgButton(hwnd, 106, BST_CHECKED);
            DialogEnableControl(hwnd, 106, false);
        }

        CheckRadioButton(hwnd, 108, 111, 108);

        if (*piSortFlags & SORT_NOCASE) {
            CheckRadioButton(hwnd, 108, 111, 109);
        } else if (*piSortFlags & SORT_LOGICAL) {
            CheckRadioButton(hwnd, 108, 111, 110);
        } else if (*piSortFlags & SORT_LEXICOGRAPH) {
            CheckRadioButton(hwnd, 108, 111, 111);
        }
        if (!Sci_IsMultiOrRectangleSelection()) {
            *piSortFlags &= ~SORT_COLUMN;
            DialogEnableControl(hwnd,112,false);
        } else {
            *piSortFlags |= SORT_COLUMN;
            CheckDlgButton(hwnd,112,BST_CHECKED);
        }
        CenterDlgInParent(hwnd, NULL);
    }
    return true;

    case WM_DPICHANGED:
        UpdateWindowLayoutForDPI(hwnd, (RECT*)lParam, 0);
        return true;

#ifdef D_NP3_WIN10_DARK_MODE

        //case WM_DRAWITEM: {
        //  // needs .rc: BS_OWNERDRAW flag
        //  const DRAWITEMSTRUCT *const pDIS = (const DRAWITEMSTRUCT *const)lParam;
        //  UINT const ctl[] = { 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112 };
        //  UINT const ctlId = pDIS->CtlID;
        //  for (UINT i = 0; i < COUNTOF(ctl); ++i) {
        //    if (ctl[i] == ctlId)
        //      return OwnerDrawTextItem(hwnd, wParam, lParam);
        //  }
        //  return FALSE;
        //}

CASE_WM_CTLCOLOR_SET:
        return SetDarkModeCtlColors((HDC)wParam, UseDarkMode());
        break;

    case WM_SETTINGCHANGE:
        if (IsDarkModeSupported() && IsColorSchemeChangeMessage(lParam)) {
            SendMessage(hwnd, WM_THEMECHANGED, 0, 0);
        }
        break;

    case WM_THEMECHANGED:
        if (IsDarkModeSupported()) {
            bool const darkModeEnabled = CheckDarkModeEnabled();
            AllowDarkModeForWindowEx(hwnd, darkModeEnabled);
            RefreshTitleBarThemeColor(hwnd);

            int const buttons[] = { IDOK, IDCANCEL, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112 };
            for (int id = 0; id < COUNTOF(buttons); ++id) {
                HWND const hBtn = GetDlgItem(hwnd, buttons[id]);
                AllowDarkModeForWindowEx(hBtn, darkModeEnabled);
                SendMessage(hBtn, WM_THEMECHANGED, 0, 0);
            }
            UpdateWindowEx(hwnd);
        }
        break;

#endif

    case WM_COMMAND:
        switch(LOWORD(wParam)) {
        case IDOK: {
            *piSortFlags = 0;
            if (IsButtonChecked(hwnd, 100)) {
                *piSortFlags |= SORT_ASCENDING;
            }
            if (IsButtonChecked(hwnd,101)) {
                *piSortFlags |= SORT_DESCENDING;
            }
            if (IsButtonChecked(hwnd,102)) {
                *piSortFlags |= SORT_SHUFFLE;
            }
            if (IsButtonChecked(hwnd,103)) {
                *piSortFlags |= SORT_MERGEDUP;
            }
            if (IsButtonChecked(hwnd,104)) {
                *piSortFlags |= SORT_UNIQDUP;
            }
            if (IsButtonChecked(hwnd,105)) {
                *piSortFlags |= SORT_UNIQUNIQ;
            }
            if (IsButtonChecked(hwnd,106)) {
                *piSortFlags |= SORT_REMZEROLEN;
            }
            if (IsButtonChecked(hwnd,107)) {
                *piSortFlags |= SORT_REMWSPACELN;
            }
            if (IsButtonChecked(hwnd,108)) {
                *piSortFlags &= ~SORT_NOCASE;
            }
            if (IsButtonChecked(hwnd,109)) {
                *piSortFlags |= SORT_NOCASE;
            }
            if (IsButtonChecked(hwnd,110)) {
                *piSortFlags |= SORT_LOGICAL;
            }
            if (IsButtonChecked(hwnd,111)) {
                *piSortFlags |= SORT_LEXICOGRAPH;
            }
            if (IsButtonChecked(hwnd,112)) {
                *piSortFlags |= SORT_COLUMN;
            }
            EndDialog(hwnd,IDOK);
        }
        break;

        case IDCANCEL:
            EndDialog(hwnd,IDCANCEL);
            break;

        case 100:
        case 101:
            DialogEnableControl(hwnd,103, IsButtonUnchecked(hwnd,105));
            DialogEnableControl(hwnd,104,true);
            DialogEnableControl(hwnd,105,true);
            DialogEnableControl(hwnd,106,true);
            DialogEnableControl(hwnd,107,true);
            DialogEnableControl(hwnd,108,true);
            DialogEnableControl(hwnd,109,true);
            DialogEnableControl(hwnd,110,true);
            DialogEnableControl(hwnd,111,true);
            break;
        case 102:
            DialogEnableControl(hwnd,103,false);
            DialogEnableControl(hwnd,104,false);
            DialogEnableControl(hwnd,105,false);
            DialogEnableControl(hwnd,106,false);
            DialogEnableControl(hwnd,107,false);
            DialogEnableControl(hwnd,108,false);
            DialogEnableControl(hwnd,109,false);
            DialogEnableControl(hwnd,110,false);
            DialogEnableControl(hwnd,111,false);
            break;
        case 104:
            DialogEnableControl(hwnd,103,IsButtonUnchecked(hwnd,104));
            break;
        case 107:
            if (IsButtonChecked(hwnd, 107)) {
                CheckDlgButton(hwnd, 106, BST_CHECKED);
                DialogEnableControl(hwnd, 106, false);
            } else {
                DialogEnableControl(hwnd, 106, true);
            }
            break;
        default:
            break;
        }
        return true;
    }
    return false;
}


//=============================================================================
//
//  EditSortDlg()
//
bool EditSortDlg(HWND hwnd,int* piSortFlags)
{

    INT_PTR iResult;

    iResult = ThemedDialogBoxParam(
                  Globals.hLngResContainer,
                  MAKEINTRESOURCEW(IDD_MUI_SORT),
                  hwnd,
                  EditSortDlgProc,
                  (LPARAM)piSortFlags);

    return (iResult == IDOK) ? true : false;

}


//=============================================================================
//
//  EditSetAccelWordNav()
//
void EditSetAccelWordNav(HWND hwnd,bool bAccelWordNav)
{
    UNREFERENCED_PARAMETER(hwnd);
    Settings.AccelWordNavigation = bAccelWordNav;
    if (Settings.AccelWordNavigation) {
        SciCall_SetWordChars(WordCharsAccelerated);
        SciCall_SetWhitespaceChars(WhiteSpaceCharsAccelerated);
        SciCall_SetPunctuationChars(PunctuationCharsAccelerated);
    } else {
        SciCall_SetCharsDefault();
    }
}


//=============================================================================
//
//  EditGetBookmarkList()
//
void  EditGetBookmarkList(HWND hwnd, LPWSTR pszBookMarks, int cchLength)
{
    UNREFERENCED_PARAMETER(hwnd);
    WCHAR tchLine[32];
    StringCchCopyW(pszBookMarks, cchLength, L"");
    int const bitmask = (1 << MARKER_NP3_BOOKMARK);
    DocLn iLine = 0;
    do {
        iLine = SciCall_MarkerNext(iLine, bitmask);
        if (iLine >= 0) {
            StringCchPrintfW(tchLine, COUNTOF(tchLine), DOCPOSFMTW L";", iLine);
            StringCchCatW(pszBookMarks, cchLength, tchLine);
            ++iLine;
        }
    } while (iLine >= 0);

    StrTrim(pszBookMarks, L";");
}


//=============================================================================
//
//  EditSetBookmarkList()
//
void  EditSetBookmarkList(HWND hwnd, LPCWSTR pszBookMarks)
{
    UNREFERENCED_PARAMETER(hwnd);
    WCHAR lnNum[32];
    const WCHAR* p1 = pszBookMarks;
    if (!p1) {
        return;
    }

    DocLn const iLineMax = SciCall_GetLineCount() - 1;

    while (*p1) {
        const WCHAR* p2 = StrChr(p1, L';');
        if (!p2) {
            p2 = StrEnd(p1,0);
        }
        StringCchCopyNW(lnNum, COUNTOF(lnNum), p1, min_s((size_t)(p2 - p1), 16));
        DocLn iLine = 0;
        if (swscanf_s(lnNum, DOCPOSFMTW, &iLine) == 1) {
            if (iLine <= iLineMax) {
                SciCall_MarkerAdd(iLine, MARKER_NP3_BOOKMARK);
            }
        }
        p1 = (*p2) ? (p2 + 1) : p2;
    }
}



//=============================================================================
//
//  EditBookmarkNext()
//
void EditBookmarkNext(HWND hwnd, const DocLn iLine)
{
    UNREFERENCED_PARAMETER(hwnd);
    int bitmask = SciCall_MarkerGet(iLine) & OCCURRENCE_MARKER_BITMASK();
    if (!bitmask) {
        bitmask = (1 << MARKER_NP3_BOOKMARK);
    }
    DocLn iNextLine = SciCall_MarkerNext(iLine + 1, bitmask);
    if (iNextLine == (DocLn)-1) {
        iNextLine = SciCall_MarkerNext(0, bitmask); // wrap around
    }
    if (iNextLine == (DocLn)-1) {
        bitmask = OCCURRENCE_MARKER_BITMASK();
        iNextLine = SciCall_MarkerNext(iLine + 1, bitmask); // find any bookmark
    }
    if (iNextLine == (DocLn)-1) {
        iNextLine = SciCall_MarkerNext(0, bitmask); // wrap around
    }

    if (iNextLine != (DocLn)-1) {
        SciCall_GotoLine(iNextLine);
    }
}

//=============================================================================
//
//  EditBookmarkPrevious()
//
void EditBookmarkPrevious(HWND hwnd, const DocLn iLine)
{
    UNREFERENCED_PARAMETER(hwnd);
    int bitmask = SciCall_MarkerGet(iLine) & OCCURRENCE_MARKER_BITMASK();
    if (!bitmask) {
        bitmask = (1 << MARKER_NP3_BOOKMARK);
    }
    DocLn iPrevLine = SciCall_MarkerPrevious(max_ln(0, iLine - 1), bitmask);
    if (iPrevLine == (DocLn)-1) {
        iPrevLine = SciCall_MarkerPrevious(SciCall_GetLineCount(), bitmask); // wrap around
    }
    if (iPrevLine == (DocLn)-1) {
        bitmask = OCCURRENCE_MARKER_BITMASK();
        iPrevLine = SciCall_MarkerPrevious(max_ln(0, iLine - 1), bitmask); //find any bookmark
    }
    if (iPrevLine == (DocLn)-1) {
        iPrevLine = SciCall_MarkerPrevious(SciCall_GetLineCount(), bitmask); // wrap around
    }

    if (iPrevLine != (DocLn)-1) {
        SciCall_GotoLine(iPrevLine);
    }
}


//=============================================================================
//
//  EditBookmarkToggle()
//
void EditBookmarkToggle(HWND hwnd, const DocLn ln, const int modifiers)
{
    UNREFERENCED_PARAMETER(hwnd);
    int const bitmask = SciCall_MarkerGet(ln) & OCCURRENCE_MARKER_BITMASK();
    if (!bitmask) {
        SciCall_MarkerAdd(ln, MARKER_NP3_BOOKMARK); // set
    } else if (bitmask & (1 << MARKER_NP3_BOOKMARK)) {
        SciCall_MarkerDelete(ln, MARKER_NP3_BOOKMARK); // unset
    } else {
        for (int m = MARKER_NP3_1; m < MARKER_NP3_BOOKMARK; ++m) {
            if (bitmask & (1 << m)) {
                SciCall_MarkerDeleteAll(m);
            }
        }
    }
    if (modifiers & SCMOD_ALT) {
        SciCall_GotoLine(ln);
    }
}


//==============================================================================
//
//  Folding Functions
//
//
#define FOLD_CHILDREN SCMOD_CTRL
#define FOLD_SIBLINGS SCMOD_SHIFT

inline bool _FoldToggleNode(const DocLn ln, const FOLD_ACTION action)
{
    bool const fExpanded = SciCall_GetFoldExpanded(ln);
    if ((action == SNIFF) || ((action == FOLD) && fExpanded) || ((action == EXPAND) && !fExpanded)) {
        SciCall_ToggleFold(ln);
        return true;
    }
    return false;
}


void EditFoldPerformAction(DocLn ln, int mode, FOLD_ACTION action)
{
    bool fToggled = false;
    if (action == SNIFF) {
        action = SciCall_GetFoldExpanded(ln) ? FOLD : EXPAND;
    }
    if (mode & (FOLD_CHILDREN | FOLD_SIBLINGS)) {
        // ln/lvNode: line and level of the source of this fold action
        DocLn const lnNode = ln;
        int const lvNode = SciCall_GetFoldLevel(lnNode) & SC_FOLDLEVELNUMBERMASK;
        DocLn const lnTotal = SciCall_GetLineCount();

        // lvStop: the level over which we should not cross
        int lvStop = lvNode;

        if (mode & FOLD_SIBLINGS) {
            ln = SciCall_GetFoldParent(lnNode) + 1;  // -1 + 1 = 0 if no parent
            --lvStop;
        }

        for (; ln < lnTotal; ++ln) {
            int lv = SciCall_GetFoldLevel(ln);
            bool fHeader = lv & SC_FOLDLEVELHEADERFLAG;
            lv &= SC_FOLDLEVELNUMBERMASK;

            if (lv < lvStop || (lv == lvStop && fHeader && ln != lnNode)) {
                return;
            }
            if (fHeader && (lv == lvNode || (lv > lvNode && mode & FOLD_CHILDREN))) {
                fToggled |= _FoldToggleNode(ln, action);
            }
        }
    } else {
        fToggled = _FoldToggleNode(ln, action);
    }
}


void EditToggleFolds(FOLD_ACTION action, bool bForceAll)
{
    DocLn const iBegLn = SciCall_LineFromPosition(SciCall_GetSelectionStart());
    DocLn const iEndLn = SciCall_LineFromPosition(SciCall_GetSelectionEnd());

    if (bForceAll) {
        SciCall_FoldAll(action);
    } else { // in selection
        if (iBegLn == iEndLn) {
            // single line
            DocLn const ln = (SciCall_GetFoldLevel(iBegLn) & SC_FOLDLEVELHEADERFLAG) ? iBegLn : SciCall_GetFoldParent(iBegLn);
            if (_FoldToggleNode(ln, action)) {
                SciCall_ScrollCaret();
            }
        } else {
            // selection range spans at least two lines
            bool fToggled = bForceAll;
            for (DocLn ln = iBegLn; ln <= iEndLn; ++ln) {
                if (SciCall_GetFoldLevel(ln) & SC_FOLDLEVELHEADERFLAG) {
                    fToggled |= _FoldToggleNode(ln, action);
                }
            }
            if (fToggled) {
                EditEnsureSelectionVisible();
            }
        }
    }
    Sci_ColouriseAll();
}


void EditFoldClick(DocLn ln, int mode)
{
    static struct {
        DocLn ln;
        int mode;
        DWORD dwTickCount;
    } prev = { 0, 0, 0 };

    bool fGotoFoldPoint = mode & FOLD_SIBLINGS;

    if (!(SciCall_GetFoldLevel(ln) & SC_FOLDLEVELHEADERFLAG)) {
        // Not a fold point: need to look for a double-click
        if (prev.ln == ln && prev.mode == mode &&
                GetTickCount() - prev.dwTickCount <= GetDoubleClickTime()) {
            prev.ln = (DocLn)-1;  // Prevent re-triggering on a triple-click

            ln = SciCall_GetFoldParent(ln);

            if (ln >= 0 && SciCall_GetFoldExpanded(ln)) {
                fGotoFoldPoint = true;
            } else {
                return;
            }
        } else {
            // Save the info needed to match this click with the next click
            prev.ln = ln;
            prev.mode = mode;
            prev.dwTickCount = GetTickCount();
            return;
        }
    }

    EditFoldPerformAction(ln, mode, SNIFF);

    if (fGotoFoldPoint) {
        EditJumpTo(ln + 1, 0);
    }
}


void EditFoldCmdKey(FOLD_MOVE move, FOLD_ACTION action)
{
    if (FocusedView.CodeFoldingAvailable && FocusedView.ShowCodeFolding) {
        DocLn ln = SciCall_LineFromPosition(SciCall_GetCurrentPos());

        // Jump to the next visible fold point
        if (move == DOWN) {
            DocLn lnTotal = SciCall_GetLineCount();
            for (ln = ln + 1; ln < lnTotal; ++ln) {
                if ((SciCall_GetFoldLevel(ln) & SC_FOLDLEVELHEADERFLAG) && SciCall_GetLineVisible(ln)) {
                    EditJumpTo(ln + 1, 0);
                    return;
                }
            }
        } else if (move == UP) { // Jump to the previous visible fold point
            for (ln = ln - 1; ln >= 0; --ln) {
                if ((SciCall_GetFoldLevel(ln) & SC_FOLDLEVELHEADERFLAG) && SciCall_GetLineVisible(ln)) {
                    EditJumpTo(ln + 1, 0);
                    return;
                }
            }
        }

        // Perform a fold/unfold operation
        DocLn const iBegLn = SciCall_LineFromPosition(SciCall_GetSelectionStart());
        DocLn const iEndLn = SciCall_LineFromPosition(SciCall_GetSelectionEnd());

        // selection range must span at least two lines for action
        if (iBegLn != iEndLn) {
            EditToggleFolds(action, false);
        } else {
            ln = (SciCall_GetFoldLevel(iBegLn) & SC_FOLDLEVELHEADERFLAG) ? iBegLn : SciCall_GetFoldParent(iBegLn);
            if (action != SNIFF) {
                if (_FoldToggleNode(ln, action)) {
                    SciCall_ScrollCaret();
                }
            }
        }
    }
}

///   End of Edit.c   ///
