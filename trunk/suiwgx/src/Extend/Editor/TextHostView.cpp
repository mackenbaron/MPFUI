// ======================================================================
//
// Copyright (c) 2008-2012 汪荣, Inc. All rights reserved.
//
// suiwgx库源码遵循CPL协议进行开源，任何个人或团体可以免费使用，但不能居于此库衍生任何商业性质的库或代码。
//
// ======================================================================

// TextHostView.cpp

#include "stdafx.h"

#include "GifDecoder.h"
#include "TextHostView.h"
#include <TOM.h>

#include <Common/SSE.h>
#include <System/Graphics/Bitmap.h>

#include <System/Tools/HwndHelper.h>
#include <System/Tools/CoreHelper.h>

#include <Extend/Editor/TextContainer.h>
#include <Extend/Editor/PasswordBox.h>

#include <Extend/Editor/OleEmbbed.h>
#include <Extend/Editor/ImageEmbbed.h>
#include <Extend/Editor/RichInterface.h>

#pragma comment(lib,"imm32.lib")

#define TIMER_INVALIDATE 30

#define DEFINE_GUIDXXX(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    EXTERN_C const GUID CDECL name \
    = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

DEFINE_GUIDXXX(IID_ITextDocument,0x8CC497C0,0xA1DF,0x11CE,0x80,0x98,0x00,0xAA,0x00,0x47,0xBE,0x5D);

static FORMATETC ui_g_rgFETC[] =
{
	{CF_METAFILEPICT,	NULL, DVASPECT_CONTENT, -1, TYMED_MFPICT},
	{CF_DIB,			NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
	{CF_BITMAP,			NULL, DVASPECT_CONTENT, -1, TYMED_GDI},
};

static int ui_g_rgCount = 3;

LONG DXtoHimetricX(LONG dx, LONG xPerInch)
{
    return (LONG) MulDiv(dx, HIMETRIC_PER_INCH, xPerInch);
}

LONG DYtoHimetricY(LONG dy, LONG yPerInch)
{
    return (LONG) MulDiv(dy, HIMETRIC_PER_INCH, yPerInch);
}

LONG HimetricXtoDX(LONG xHimetric, LONG xPerInch)
{
    return (LONG) MulDiv(xHimetric, xPerInch, HIMETRIC_PER_INCH);
}

LONG HimetricYtoDY(LONG yHimetric, LONG yPerInch)
{
    return (LONG) MulDiv(yHimetric, yPerInch, HIMETRIC_PER_INCH);
}

void InternalInitCharFormat(DWORD dwColor, CHARFORMAT2W* pcf, HFONT hfont)
{
    memset(pcf, 0, sizeof(CHARFORMAT2W));
    LOGFONT lf;

    if (!hfont)
    {
        hfont = (HFONT)(DWORD_PTR)suic::GetUIFont();
    }

    ::GetObject(hfont, sizeof(LOGFONT), &lf);

    pcf->cbSize = sizeof(CHARFORMAT2W);
    pcf->crTextColor = RGB(GetBValue(dwColor), GetGValue(dwColor), GetRValue(dwColor));
    pcf->yHeight = -lf.lfHeight * LY_PER_INCH / SystemParameters::DpiY;
    pcf->yOffset = 0;
    pcf->dwEffects = 0;
    pcf->dwMask = CFM_OFFSET | CFM_COLOR | CFM_SIZE | CFM_FACE | CFM_CHARSET | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT;
    
    if (lf.lfWeight >= FW_BOLD)
    {
        pcf->dwEffects |= CFE_BOLD;
    }

    if (lf.lfItalic)
    {
        pcf->dwEffects |= CFE_ITALIC;
    }

    if (lf.lfUnderline)
    {
        pcf->dwEffects |= CFE_UNDERLINE;
    }

    if (lf.lfStrikeOut)
    {
        pcf->dwEffects |= CFE_STRIKEOUT;
    }

    pcf->bCharSet = lf.lfCharSet;
    pcf->bPitchAndFamily = lf.lfPitchAndFamily;
    _tcscpy(pcf->szFaceName, lf.lfFaceName);
}

TextFontFormat::TextFontFormat()
    : _textSrv(NULL)
    , _textCb(NULL)
    , _flag(0)
{
    InternalInitCharFormat(0, &_charf, NULL);
}

TextFontFormat::~TextFontFormat()
{
}

void TextFontFormat::Commit()
{
    if (_textCb != NULL)
    {
        _textCb->NotifyTextFontChanged(_flag);
    }
    
    if (_textSrv)
    {
        _textSrv->OnTxPropertyBitsChange(TXTBIT_CHARFORMATCHANGE, TXTBIT_CHARFORMATCHANGE);
    }

    _flag = 0;
}

float TextFontFormat::GetSize()
{
    float size = UILXtoDX(_charf.yHeight);
    size = CoreHelper::FontPoundFromPixel(size);
    return size;
}

static Float FontPoundToPixel(Float size)
{
    // 当前显示设备的垂直单位英寸像素
    // 1英寸 == 72磅
    const Float fCaps = (Float)SystemParameters::DpiY / 72.0f;
    Float iSize = (Float)size * fCaps;
    return iSize;
}

void TextFontFormat::SetSize(float size)
{
    size = FontPoundToPixel(size);
    _charf.yHeight = UIDXtoLX(size);
    _flag |= eFontChangedFlag::fcfSize;
}

TextParaFormat::TextParaFormat()
    : _textSrv(NULL)
{
    InitParaFormat(PFM_ALL2);
    SetLineSpace(suic::TPLineRule::tpLineAtLeast, 16);
}

TextParaFormat::~TextParaFormat()
{
}

void TextParaFormat::InitParaFormat(DWORD dwMask)
{	
    memset(&_paraf, 0, sizeof(PARAFORMAT2));
    _paraf.cbSize = sizeof(PARAFORMAT2);

    _paraf.dwMask = dwMask | PFM_EFFECTS | PFM_OFFSETINDENT | PFM_STYLE;
    _paraf.dwMask |= PFM_ALIGNMENT | PFM_TABSTOPS | PFM_NUMBERING;

    _paraf.wAlignment = PFA_LEFT;
    _paraf.dxRightIndent = 0;
    // ppf->wNumbering = 7;//PFN_BULLET;
    // ppf->wNumberingStart = -1;//(WORD)_T('0');
    // ppf->wNumberingStyle = PFNS_NONUMBER;
    _paraf.cTabCount = 1;
    _paraf.rgxTabs[0] = lDefaultTab;
}

void TextParaFormat::AddParaMask(DWORD dwMask)
{
    _paraf.dwMask |= dwMask;
}

long TextParaFormat::GetLineSpace()
{
    return UILXtoDX(_paraf.dyLineSpacing);
}

suic::TPLineRule TextParaFormat::GetLineRule()
{
    return (suic::TPLineRule)(_paraf.bLineSpacingRule);
}

long TextParaFormat::GetListTab()
{
    return UILXtoDX(_paraf.wNumberingTab);
}

void TextParaFormat::SetListTab(long value)
{
    _paraf.wNumberingTab = UIDXtoLX(value);
    AddParaMask(PFM_NUMBERINGTAB);
}

void TextParaFormat::SetLineSpace(suic::TPLineRule lineSpaceRule, long lineSpace)
{
    _paraf.bLineSpacingRule = lineSpaceRule;
    if (lineSpaceRule == suic::TPLineRule::tpLineMultiple)
    {
        _paraf.dyLineSpacing = lineSpace;
    }
    else
    {
        _paraf.dyLineSpacing = UIDXtoLX(lineSpace);
    }
    AddParaMask(PFM_LINESPACING);
}

long TextParaFormat::GetRightIndent()
{
    return UILXtoDX(_paraf.dxRightIndent);
}

void TextParaFormat::SetRightIndent(long value)
{
    _paraf.dxRightIndent = UIDXtoLX(value);
    AddParaMask(PFM_RIGHTINDENT);
}

long TextParaFormat::GetFirstLineIndent()
{
    return UILXtoDX(_paraf.dxStartIndent);
}

long TextParaFormat::GetLeftIndent()
{
    return UILXtoDX(_paraf.dxOffset);
}

void TextParaFormat::SetIndents(long startIndent, long leftIndent, long rightIndent)
{
    _paraf.dxStartIndent = UIDXtoLX(startIndent);
    _paraf.dxOffset = UIDXtoLX(leftIndent);
    _paraf.dxRightIndent = UIDXtoLX(rightIndent);
    AddParaMask(PFM_STARTINDENT | PFM_RIGHTINDENT | PFM_OFFSET);
}

long TextParaFormat::GetSpaceAfter()
{
    return UILXtoDX(_paraf.dySpaceAfter);
}

void TextParaFormat::SetSpaceAfter(long value)
{
    _paraf.dySpaceAfter = UIDXtoLX(value);
    AddParaMask(PFM_SPACEAFTER);
}

long TextParaFormat::GetSpaceBefore()
{
    return UILXtoDX(_paraf.dySpaceBefore);
}

void TextParaFormat::SetSpaceBefore(long value)
{
    _paraf.dySpaceBefore = UIDXtoLX(value);
    AddParaMask(PFM_SPACEBEFORE);
}

void TextParaFormat::Begin()
{
    // InitParaFormat(0);
}

void TextParaFormat::Commit()
{
    if (_textSrv)
    {
        _textSrv->OnTxPropertyBitsChange(TXTBIT_PARAFORMATCHANGE, TXTBIT_PARAFORMATCHANGE);
        //_paraf.dwMask = 0;
    }
}

//----------------------------------------------------

TextRenderHost::TextRenderHost()
    : _textSrv(NULL)
    , _textDoc(NULL)
    , _holder(NULL)
    , _himc(NULL)
    , _isScrollPending(false)
{
    ::ZeroMemory(&_refCount, sizeof(TextRenderHost) - offsetof(TextRenderHost, _refCount));
    
     _refCount = 1;
    _lMaxLength = 1024 * 1024 * 64;
    _laccelpos = -1;

    _lMaxLines = 1;
    _isWordWrap = 0;
    _textColor = Colors::Black;
    _selTextColor = Color::FromRgb(::GetSysColor(COLOR_HIGHLIGHTTEXT));
    _selBackground = Color::FromRgb(::GetSysColor(COLOR_HIGHLIGHT));

    SetFontSize(9);
    SetPadding(suic::Rect(2, 2));
}

TextRenderHost::~TextRenderHost(void)
{
    if (NULL != _textSrv)
    {
        _textSrv->Release();
        _textDoc->Release();
    }
}

void TextRenderHost::EnableImmStatus(bool bEnable)
{
    if (_himc == NULL)
    {
        _himc = ::ImmGetContext(_hwnd);
    }
    ::ImmSetOpenStatus(_himc, bEnable ? TRUE : FALSE);
    if (!bEnable)
    {
        ::ImmReleaseContext(_hwnd, _himc);
        _himc = NULL;
    }
}

bool TextRenderHost::GetOleImageRect(const REOBJECT& reo, IRichEditOle *pRichEditOle, RECT* lpRect)
{
    return true;
}

bool TextRenderHost::InsertEmbbedObj(IOleObject* embbed, DWORD dwUserData)
{
    SCODE sc;
    IOleClientSite *pOleClientSite = NULL;
    IRichEditOle* pRichEditOle = GetRichEditOle();

    pRichEditOle->GetClientSite(&pOleClientSite);
    IStorage *pStorage = NULL;
    LPLOCKBYTES lpLockBytes = NULL;
    HRESULT hr = S_OK;

    UINT flag = STGM_SHARE_EXCLUSIVE | STGM_CREATE | STGM_READWRITE;
    IOleObject *pOleObject = embbed;

    sc = ::CreateILockBytesOnHGlobal(NULL, TRUE, &lpLockBytes);

    if (sc != S_OK)
    {
        return false;
    }

    sc = ::StgCreateDocfileOnILockBytes(lpLockBytes, flag, 0, &pStorage);

    if (sc != S_OK)
    {
        lpLockBytes->Release();
        lpLockBytes = NULL;
        return false;
    }

    pOleObject->SetClientSite(pOleClientSite);

    hr = ::OleSetContainedObject(pOleObject, TRUE);

    REOBJECT reobject;
    ZeroMemory(&reobject, sizeof(REOBJECT));
    reobject.cbStruct = sizeof(REOBJECT);

    // reobject.clsid = IID_IImageOle;
    reobject.cp = REO_CP_SELECTION;
    reobject.dvaspect = DVASPECT_CONTENT;
    reobject.poleobj = pOleObject;
    reobject.polesite = pOleClientSite;
    reobject.pstg = pStorage;
    reobject.dwUser = dwUserData;
    reobject.dwFlags = REO_BELOWBASELINE;

    pRichEditOle->InsertObject(&reobject);

    pOleObject->Release();
    pOleClientSite->Release();

    return true;
}

IRichEditOle* TextRenderHost::GetRichEditOle()
{
    IRichEditOle* richeditole = NULL;
    TxSendMessage(EM_GETOLEINTERFACE, 0, (LPARAM)&richeditole);
    return richeditole;
}

bool TextRenderHost::SetRichEditOleCallback(IRichEditOleCallback* callback)
{
    LRESULT lr = TxSendMessage(EM_SETOLECALLBACK, 0, (LPARAM)callback);
    return (lr != 0);
}

void TextRenderHost::ShowVertScrollBar(BOOL val)
{

}

BOOL TextRenderHost::IsShowVertScrollBar() const
{
    return TRUE;
}

void TextRenderHost::ShowHoriScrollBar(BOOL val)
{

}

BOOL TextRenderHost::IsShowHoriScrollBar() const
{
    return TRUE;
}

void TextRenderHost::SetAutoHoriScrollBar(BOOL val)
{

}

BOOL TextRenderHost::IsAutoHoriScrollBar() const
{
    return TRUE;
}

void TextRenderHost::SetAutoVertScrollBar(BOOL val)
{

}

BOOL TextRenderHost::IsAutoVertScrollBar() const
{
    return TRUE;
}

void TextRenderHost::SetTextMode(int iMode)
{
    TxSendMessage(EM_SETTEXTMODE, iMode, 0);
}

int TextRenderHost::GetTextMode() const
{
    return (int)TxSendMessage(EM_GETTEXTMODE, 0, 0);
}

bool TextRenderHost::IsRichMode() const
{
    return ((GetTextMode() & TM_PLAINTEXT) == 0);
}

void TextRenderHost::OnTxPropertyBitsChange(UINT key, UINT val)
{
    if (_textSrv)
    {
        _textSrv->OnTxPropertyBitsChange(key, val);
    }
}

void TextRenderHost::SetTextCallback(suic::ITextCallback* textCb)
{
    _cf.SetTextCb(textCb);
}

DWORD TextRenderHost::GetEventMask() const
{
    LRESULT lResult = 0;
    lResult = TxSendMessage(EM_GETEVENTMASK, 0, 0);
    return (DWORD)lResult;
}

DWORD TextRenderHost::SetEventMask(DWORD dwEventMask)
{
    LRESULT lResult = TxSendMessage(EM_SETEVENTMASK, 0, dwEventMask);
    return (DWORD)lResult;
}

int TextRenderHost::GetLineIndexFromCharIndex(int charIndex) const
{
    return (int)TxSendMessage(EM_EXLINEFROMCHAR, 0, charIndex);
}

int TextRenderHost::GetCharacterIndexFromLineIndex(int lineIndex) const
{
    return (int)TxSendMessage(EM_LINEINDEX, lineIndex, 0);
}

int TextRenderHost::GetCharIndexFromPoint(Point point, bool snapToText) const
{
    POINTL ptl = {point.x, point.y}; 
    return (int)TxSendMessage(EM_CHARFROMPOS, 0, (LPARAM)&ptl);
}

Point TextRenderHost::GetPointFromCharIndex(int charIndex) const
{ 
    Point pt;
    TxSendMessage(EM_POSFROMCHAR, (WPARAM)&pt, charIndex); 
    return pt; 
}

int TextRenderHost::GetFirstVisibleLineIndex() const
{
    return (int)TxSendMessage(EM_GETFIRSTVISIBLELINE, 0, 0);
}

int TextRenderHost::GetLastVisibleLineIndex() const
{
    suic::Rect rect = GetViewRect();
    rect.Deflate(GetPadding());
    POINTL ptl = {rect.right, rect.bottom}; 

    int iLastVisibleCp = (int)TxSendMessage(EM_CHARFROMPOS, 0, (LPARAM)&ptl);
    return GetLineIndexFromCharIndex(iLastVisibleCp); 
}

int TextRenderHost::GetLineCount() const
{
    int lineCount = (int)TxSendMessage(EM_GETLINECOUNT, 0, 0);
    if (1 == lineCount && GetLineLength(0) == 0)
    {
        --lineCount;
    }
    return lineCount;
}

int TextRenderHost::GetLineLength(int lineIndex) const
{
    int cp = this->GetCharacterIndexFromLineIndex(lineIndex);
    return (int)(TxSendMessage(EM_LINELENGTH, cp, 0));
}

String TextRenderHost::GetLineText(int lineIndex) const
{
    int iLineSize = GetLineLength(lineIndex);
    suic::String strText;
    strText.Resize(iLineSize + 1);
    TxSendMessage(EM_GETLINE, lineIndex, (LPARAM)strText.c_str());

    return strText;
}

long TextRenderHost::GetTextLength() const
{
    GETTEXTLENGTHEX textLenEx;
    textLenEx.flags = 0;
    textLenEx.codepage = 1200;
    return (long)TxSendMessage(EM_GETTEXTLENGTHEX, (WPARAM)&textLenEx, 0);
}

void TextRenderHost::SetLineLeftOffset(int index, int xLeft)
{
    GetTextService()->TxSetLineLeftOffset(index, xLeft);
}

void TextRenderHost::GetLineDetail(int index, suic::LineDetail* info, int* iYPos)
{
    GetTextService()->TxGetLineDetail(index, info->xCount, info->xInPara, info->xWhite, info->xLeft, info->xWidth, info->xHeight, iYPos);
}

suic::Point TextRenderHost::GetPoint(suic::Uint32 cp, bool bLeftTop)
{
    suic::Point point;
    if (bLeftTop)
    {
        GetTextService()->TxGetPointFromCp(cp, TRUE, &point);
    }
    else
    {
        GetTextService()->TxGetPointFromCp(cp, FALSE, &point);
    }

    suic::Rect rcWnd;
    HWND hwnd = GetHostHwnd();
    ::GetWindowRect(hwnd, &rcWnd);
    point.x -= rcWnd.left;
    point.y -= rcWnd.top;

    return point;
}

void TextRenderHost::SetWordWrap(bool fWordWrap)
{
    if (_isWordWrap != fWordWrap)
    {
        _isWordWrap = fWordWrap;
        OnTxPropertyBitsChange(TXTBIT_WORDWRAP, fWordWrap ? TXTBIT_WORDWRAP : 0);
    }
}

void TextRenderHost::SetMultiLine(bool fMultiLine)
{
    if (_isMultiLine != fMultiLine)
    {
        _isMultiLine = fMultiLine;
        OnTxPropertyBitsChange(TXTBIT_MULTILINE, fMultiLine ? TXTBIT_MULTILINE : 0);
    }
}

bool TextRenderHost::GetReadOnly() const
{
    return _isReadOnly;
}

void TextRenderHost::SetReadOnly(bool fReadOnly)
{
    _isReadOnly = fReadOnly;
    OnTxPropertyBitsChange(TXTBIT_READONLY, fReadOnly ? TXTBIT_READONLY : 0);
}

bool TextRenderHost::CanEnableDrag() const
{
    return _isCanDrag;
}

void TextRenderHost::SetEnableDrag(bool fCanDrag)
{
    _isCanDrag = fCanDrag;
    OnTxPropertyBitsChange(TXTBIT_DISABLEDRAG, fCanDrag ? TXTBIT_DISABLEDRAG : 0);
}
bool TextRenderHost::GetAcceptsReturn()
{
    return _isAcceptsReturn != 0;
}

void TextRenderHost::SetAcceptsReturn(bool val)
{
    _isAcceptsReturn = val;
}

bool TextRenderHost::GetAutoWordSelection()
{
    return _isAutoWordSelection != 0;
}

void TextRenderHost::SetAutoWordSelection(bool val)
{
    _isAutoWordSelection = val;
}

bool TextRenderHost::GetAcceptsTab()
{
    return _isAcceptsTab != 0;
}

void TextRenderHost::SetAcceptsTab(bool val)
{
    _isAcceptsTab = val;
}

bool TextRenderHost::GetIsReadOnlyCaretVisible()
{
    return _isReadOnlyCaretVisible != 0;
}

void TextRenderHost::SetIsReadOnlyCaretVisible(bool val)
{
    _isReadOnlyCaretVisible = val;
}

void TextRenderHost::SetFont(HFONT hFont) 
{
    if (hFont == NULL) 
    {
        LOGFONT lf;
        LONG yPixPerInch = SystemParameters::DpiY;

        ::GetObject(hFont, sizeof(LOGFONT), &lf);
        
        GetCF().dwEffects = 0;
        GetCF().yHeight = -lf.lfHeight * LY_PER_INCH / yPixPerInch;

        if (lf.lfWeight >= FW_BOLD)
        {
            GetCF().dwEffects |= CFE_BOLD;
        }

        if (lf.lfItalic)
        {
            GetCF().dwEffects |= CFE_ITALIC;
        }

        if (lf.lfUnderline)
        {
            GetCF().dwEffects |= CFE_UNDERLINE;
        }

        if (lf.lfStrikeOut)
        {
            GetCF().dwEffects |= CFE_STRIKEOUT;
        }

        GetCF().bCharSet = lf.lfCharSet;
        GetCF().bPitchAndFamily = lf.lfPitchAndFamily;
        _tcscpy(GetCF().szFaceName, lf.lfFaceName);

        OnTxPropertyBitsChange(TXTBIT_CHARFORMATCHANGE, TXTBIT_CHARFORMATCHANGE);
    }
}

void TextRenderHost::SetTextColor(suic::Color clr)
{
    _textColor = clr;
    // 设置文本颜色
    GetCF().crTextColor = suic::Color::ToRgb(_textColor);
    OnTxPropertyBitsChange(TXTBIT_CHARFORMATCHANGE, TXTBIT_CHARFORMATCHANGE);
}

suic::String TextRenderHost::GetFontFamily()
{
    return GetCF().szFaceName;
}

int TextRenderHost::GetFontWeight()
{
    return GetCF().wWeight;
}

suic::Color TextRenderHost::GetTextColor()
{
    return suic::Color::FromRgb(_textColor);
}


void TextRenderHost::SetFontFamily(String name)
{
    wcscpy_s(GetCF().szFaceName, name.c_str());
    OnTxPropertyBitsChange(TXTBIT_CHARFORMATCHANGE, TXTBIT_CHARFORMATCHANGE);
}

void TextRenderHost::SetFontWeight(int weight)
{
    GetCF().wWeight = weight;
    if (weight >= FW_BOLD)
    {
        GetCF().dwEffects |= CFE_BOLD;
    }
    else
    {
        GetCF().dwEffects &= ~CFE_BOLD;
    }

    OnTxPropertyBitsChange(TXTBIT_CHARFORMATCHANGE, TXTBIT_CHARFORMATCHANGE);
}

void TextRenderHost::SetFontSize(int size)
{
    size *= CoreHelper::GetScaleRatio();
    // 字体磅数转换为像素
    _cf.SetSize(size);
    OnTxPropertyBitsChange(TXTBIT_CHARFORMATCHANGE, TXTBIT_CHARFORMATCHANGE);
}

void TextRenderHost::SetFontItalic(bool val)
{
    if (val)
    {
        GetCF().dwEffects |= CFE_ITALIC;
    }
    else
    {
        GetCF().dwEffects &= ~CFE_ITALIC;
    }

    OnTxPropertyBitsChange(TXTBIT_CHARFORMATCHANGE, TXTBIT_CHARFORMATCHANGE);
}

void TextRenderHost::SetUnderline(bool val)
{
    if (val)
    {
        GetCF().dwEffects |= CFE_UNDERLINE;
    }
    else
    {
        GetCF().dwEffects &= ~CFE_UNDERLINE;
    }

    OnTxPropertyBitsChange(TXTBIT_CHARFORMATCHANGE, TXTBIT_CHARFORMATCHANGE);
}

void TextRenderHost::SetStrikeout(bool val)
{
    if (val)
    {
        GetCF().dwEffects |= CFE_STRIKEOUT;
    }
    else
    {
        GetCF().dwEffects &= ~CFE_STRIKEOUT;
    }

    OnTxPropertyBitsChange(TXTBIT_CHARFORMATCHANGE, TXTBIT_CHARFORMATCHANGE);
}

SIZEL TextRenderHost::GetExtent() 
{
    SIZEL sizelExtent;

    sizelExtent.cx = DXtoHimetricX(_rcClnt.Width(), SystemParameters::DpiX);
    sizelExtent.cy = DYtoHimetricY(_rcClnt.Height(), SystemParameters::DpiY);

    return sizelExtent;
}

void TextRenderHost::NotifyExtentChange() 
{
    OnTxPropertyBitsChange(TXTBIT_EXTENTCHANGE, TXTBIT_EXTENTCHANGE);
}

void TextRenderHost::NotifyAllChange() 
{
    DWORD dwMask = TXTBIT_RICHTEXT | TXTBIT_MULTILINE | TXTBIT_READONLY | TXTBIT_READONLY | TXTBIT_HIDESELECTION |
        TXTBIT_AUTOWORDSEL | TXTBIT_WORDWRAP | TXTBIT_SAVESELECTION;
    DWORD dwVal = 0;

    TxGetPropertyBits(dwMask, &dwVal);

    OnTxPropertyBitsChange(dwMask, dwVal);
}

void TextRenderHost::SetMaxLength(int nChars)
{
    _lMaxLength = nChars;
    if (_lMaxLength <= 0) 
    {
        _lMaxLength = 1024 * 1024 * 4;
    }

    OnTxPropertyBitsChange(TXTBIT_MAXLENGTHCHANGE, TXTBIT_MAXLENGTHCHANGE);
}

int TextRenderHost::GetMaxLength() const
{
    return _lMaxLength;
}

void TextRenderHost::SetMaxLines(LONG val)
{
    _lMaxLines = max(1, val);

    if (_lMaxLines > 1)
    {
        SetMultiLine(true);
    }
    else
    {
        SetMultiLine(false);
    }
}

LONG TextRenderHost::GetMaxLines() const
{
    return _lMaxLines;
}

void TextRenderHost::SetMinLines(LONG val)
{
}

void TextRenderHost::SetUndoLimit(LONG val)
{
    val = max(0, val);
    TxSendMessage(EM_SETUNDOLIMIT, val, 0);
}

BOOL TextRenderHost::GetAllowBeep()
{
    return _isAllowBeep;
}

void TextRenderHost::SetAllowBeep(BOOL fAllowBeep)
{
    _isAllowBeep = fAllowBeep;

    OnTxPropertyBitsChange(TXTBIT_ALLOWBEEP, fAllowBeep ? TXTBIT_ALLOWBEEP : 0);
}

WORD TextRenderHost::GetDefaultAlign()
{
    return GetPF().wAlignment;
}

void TextRenderHost::SetDefaultAlign(WORD wNewAlign)
{
    GetPF().wAlignment = wNewAlign;
    OnTxPropertyBitsChange(TXTBIT_PARAFORMATCHANGE, 0);
}

BOOL TextRenderHost::GetRichTextFlag()
{
    return _isRichMode;
}

void TextRenderHost::SetRichTextFlag(BOOL fNew)
{
    _isRichMode = fNew;
    OnTxPropertyBitsChange(TXTBIT_RICHTEXT, fNew ? TXTBIT_RICHTEXT : 0);
}

LONG TextRenderHost::GetDefaultLeftIndent()
{
    return GetPF().dxOffset;
}

void TextRenderHost::SetDefaultLeftIndent(LONG lNewIndent)
{
    GetPF().dxOffset = lNewIndent;
    OnTxPropertyBitsChange(TXTBIT_PARAFORMATCHANGE, 0);
}

BOOL TextRenderHost::SetSaveSelection(BOOL isSaveSelection)
{
    BOOL fResult = _isSaveSelection;
    _isSaveSelection = isSaveSelection;
    OnTxPropertyBitsChange(TXTBIT_SAVESELECTION, isSaveSelection ? TXTBIT_SAVESELECTION : 0);
    return fResult;		
}

void TextRenderHost::SetTransparent(BOOL isTransparent)
{
    _isTransparent = isTransparent;
    OnTxPropertyBitsChange(TXTBIT_BACKSTYLECHANGE, 0);
}

LONG TextRenderHost::SetAccelPos(LONG laccelpos)
{
    LONG laccelposOld = _laccelpos;
    _laccelpos = laccelpos;
    OnTxPropertyBitsChange(TXTBIT_SHOWACCELERATOR, 0);
    return laccelposOld;
}

WCHAR TextRenderHost::SetPasswordChar(WCHAR chPasswordChar)
{
    WCHAR chOldPasswordChar = _chPasswordChar;
    _chPasswordChar = chPasswordChar;
    return chOldPasswordChar;
}

void TextRenderHost::SetUsePassword(bool fUsePassword)
{
    _isUsePassword = fUsePassword;
    OnTxPropertyBitsChange(TXTBIT_USEPASSWORD, fUsePassword ? TXTBIT_USEPASSWORD : 0);
}

void TextRenderHost::SetDisabled(BOOL fOn)
{
    GetCF().dwMask |= CFM_COLOR | CFM_DISABLED;
    GetCF().dwEffects |= CFE_AUTOCOLOR | CFE_DISABLED;
    if (!fOn)
    {
        GetCF().dwEffects &= ~CFE_DISABLED;
    }
    OnTxPropertyBitsChange(TXTBIT_CHARFORMATCHANGE, TXTBIT_CHARFORMATCHANGE);
}

LONG TextRenderHost::SetSelBarWidth(LONG lSelBarWidth)
{
    OnTxPropertyBitsChange(TXTBIT_SELBARCHANGE, TXTBIT_SELBARCHANGE);
    return lSelBarWidth;
}

HRESULT	TextRenderHost::OnTxInPlaceDeactivate()
{
    HRESULT hr = _textSrv->OnTxInPlaceDeactivate();
    if (SUCCEEDED(hr))
    {
        _isInplaceActive = FALSE;
    }

    return hr;
}

HRESULT	TextRenderHost::OnTxInPlaceActivate(LPCRECT prcClient)
{
    _isInplaceActive = TRUE;
    HRESULT hr = _textSrv->OnTxInPlaceActivate(prcClient);
    if (FAILED(hr))
    {
        _isInplaceActive = FALSE;
    }
    return hr;
}

void TextRenderHost::UpdateHostUI()
{
    if (!_isRenderPending && _holder != NULL)
    {
        GetHolder()->InvalidateVisual();
    }
}

//=========================================================================
HRESULT _stdcall TextRenderHost::QueryInterface( REFIID riid, void **ppvObject )
{
    HRESULT hr = E_NOINTERFACE;
    *ppvObject = NULL;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITextHost)
        /*|| IsEqualIID(riid, IID_ITextHost2)*/) 
    {
        AddRef();
        *ppvObject = (ITextHost *)this;
        hr = S_OK;
    }

    return hr;
}

ULONG _stdcall TextRenderHost::AddRef( void )
{
    return ++_refCount;
}

ULONG _stdcall TextRenderHost::Release( void )
{
    ULONG lRefs = --_refCount;

    if (lRefs == 0)
    {
        delete this;
    }

    return lRefs;
}


//////////////////////////////////////////////////////////////////////////
// ITextHost
HRESULT TextRenderHost::TxGetViewInset(LPRECT prc)
{
    prc->left = DXtoHimetricX(_rcInset.left, SystemParameters::DpiX);
    prc->top = DYtoHimetricY(_rcInset.top, SystemParameters::DpiY);
    prc->right = DXtoHimetricX(_rcInset.right, SystemParameters::DpiX);
    prc->bottom = DYtoHimetricY(_rcInset.bottom, SystemParameters::DpiY);

    return NOERROR;
}

HRESULT TextRenderHost::TxGetCharFormat(const CHARFORMATW **ppCF)
{
    *ppCF = &(GetCF());
    return NOERROR;
}

HRESULT TextRenderHost::TxGetParaFormat( const PARAFORMAT **ppPF )
{
    *ppPF = &(GetPF());
    return NOERROR;
}

HRESULT TextRenderHost::TxGetClientRect(LPRECT prc)
{
    *prc = _rcClnt;
    return NOERROR;
}

HRESULT TextRenderHost::TxDeactivate( LONG lNewState )
{
    return S_OK;
}

HRESULT TextRenderHost::TxActivate( LONG * plOldState )
{
    return S_OK;
}

BOOL TextRenderHost::TxClientToScreen(LPPOINT lppt)
{
    return ::ClientToScreen(GetHostHwnd(), lppt);
}

HWND TextRenderHost::GetHostHwnd()
{
    if (NULL == _hwnd)
    {
        _hwnd = HANDLETOHWND(HwndHelper::GetHostHwnd(GetHolder()));
    }

    return _hwnd;
}

BOOL TextRenderHost::TxScreenToClient(LPPOINT lppt)
{
    return ::ScreenToClient(GetHostHwnd(), lppt);
}

void TextRenderHost::TxSetCursor(HCURSOR hcur, BOOL fText)
{
    ::SetCursor(hcur);
}

void TextRenderHost::TxSetFocus()
{
}

void TextRenderHost::TxSetCapture(BOOL fCapture)
{
    if (fCapture) 
    {
        GetHolder()->SetCaptureMouse();
    }
    else
    {
        GetHolder()->ReleaseCaptureMouse();
    }
}

void TextRenderHost::TxScrollWindowEx( INT dx, INT dy, LPCRECT lprcScroll, LPCRECT lprcClip, HRGN hrgnUpdate, LPRECT lprcUpdate, UINT fuScroll )
{
}

void TextRenderHost::TxKillTimer(UINT idTimer)
{
    ::KillTimer(GetHostHwnd(), idTimer);
}

bool TextRenderHost::TxOnSetTimer(UINT idTimer)
{
    return true;
}

BOOL TextRenderHost::TxSetTimer(UINT idTimer, UINT uTimeout)
{
    ::SetTimer(GetHostHwnd(), idTimer, uTimeout, NULL);
    return TRUE;
}

BOOL TextRenderHost::TxSetCaretPos(INT x, INT y)
{
    Point pt =  GetHolder()->PointToScreen(Point());

    pt.x = x - pt.x;
    pt.y = y - pt.y;

    _holder->ResetCaret(pt);

    return TRUE;
}

BOOL TextRenderHost::TxShowCaret(BOOL fShow)
{
    _holder->ShowCaret(fShow ? true : false);

    return TRUE;
}

BOOL TextRenderHost::TxCreateCaret(HBITMAP hbmp, INT xWidth, INT yHeight)
{
    ::HideCaret(GetHostHwnd());

    fPoint pt(xWidth, yHeight);
    pt.x = SystemParameters::TransformXFromDevice(pt.x);
    pt.y = SystemParameters::TransformXFromDevice(pt.y);

    _holder->InitCaret(pt.x + 0.5f, pt.y + 0.5f);

    return TRUE;
}

HDC TextRenderHost::TxGetDC()
{
    return GetDC(GetHostHwnd());
}

INT TextRenderHost::TxReleaseDC(HDC hdc)
{
    return ReleaseDC(GetHostHwnd(), hdc);
}

void TextRenderHost::DoVertScrollBarShow(BOOL fShow)
{
    ScrollViewer* scrollViewer = _holder->GetScrollViewer();

    if (!fShow)
    {
        TxSetScrollRange(SB_VERT, 0, 1, FALSE);
    }
}

BOOL TextRenderHost::TxShowScrollBar(INT fnBar, BOOL fShow)
{
    if (_holder == NULL || GetMaxLines() <= 1)
    {
        return FALSE;
    }

    ScrollViewer* scrollViewer = _holder->GetScrollViewer();

    if (scrollViewer != NULL)
    {
        bool bHori = _isHoriScrollBarVisible;
        bool bVert = _isVertScrollBarVisible;

        if (fnBar == SB_HORZ)
        {   
            _isHoriScrollBarVisible = fShow ? true : false;

            if (!fShow)
            {
                TxSetScrollRange(fnBar, 0, 1, FALSE);
            }
        }
        else
        {            
            _isVertScrollBarVisible = fShow ? true : false;
            DoVertScrollBarShow(fShow);
        }

        if (bHori != _isHoriScrollBarVisible || bVert != _isVertScrollBarVisible)
        {
            scrollViewer->UpdateLayout();
        }
        else
        {
            scrollViewer->InvalidateVisual();
        }
    }

    return TRUE;
}

BOOL TextRenderHost::TxEnableScrollBar(INT fuSBFlags, INT fuArrowflags)
{
    ScrollViewer* scrollViewer = _holder->GetScrollViewer();
    if (scrollViewer != NULL)
    {
        if (ESB_DISABLE_BOTH == fuArrowflags)
        {
            TxSetScrollRange(fuSBFlags, 0, 1, FALSE);
        }

        if (fuSBFlags == SB_VERT)
        {
            DoVertScrollBarShow(ESB_DISABLE_BOTH != fuArrowflags);
        }

        scrollViewer->UpdateLayout();
    }

    return TRUE;
}

BOOL TextRenderHost::TxSetScrollRange(INT fnBar, LONG nMinPos, INT nMaxPos, BOOL fRedraw)
{
    if (NULL != _holder)
    {
        //fPoint pt = SystemParameters::TransformFromDevice(nMinPos, nMaxPos);
        _holder->UpdateScrollRange(fnBar == SB_HORZ, nMinPos, nMaxPos, fRedraw ? true : false);
    }

    return TRUE;
}

BOOL TextRenderHost::TxSetScrollPos(INT fnBar, INT nPos, BOOL fRedraw)
{
    if (GetMaxLines() > 1 && NULL != _holder && 
        !IsScrollPending() && !_isRecacViewPending)
    {
        //fPoint pt = SystemParameters::TransformFromDevice(nPos, nPos);
        _holder->UpdateScrollPos(fnBar == SB_HORZ, nPos, fRedraw ? true : false);
    }
    return TRUE;
}

void TextRenderHost::TxInvalidateRect(LPCRECT prc, BOOL fMode)
{
    if (prc && !fMode)
    {
        if (prc->right > prc->left)
        {
            UpdateHostUI();
        }
    }
    else
    {
        UpdateHostUI();
    }
}

void TextRenderHost::TxViewChange(BOOL fUpdate)
{
}

COLORREF TextRenderHost::TxGetSysColor(int nIndex)
{
    switch (nIndex)
    {
        // 高亮文本颜色
        case COLOR_HIGHLIGHTTEXT:
            return _selTextColor.ToRgb();

            // 高亮文本背景颜色
        case COLOR_HIGHLIGHT:
            return _selBackground.ToRgb();

            // 背景颜色
        case COLOR_WINDOW:
            break;
            // 正常文本颜色
        case COLOR_WINDOWTEXT:
            break;
            // 失去焦点文本颜色
        case COLOR_GRAYTEXT:
            break;
    }

    return ::GetSysColor(nIndex);
}

HRESULT TextRenderHost::TxGetBackStyle(TXTBACKSTYLE *pstyle)
{
    *pstyle = _isTransparent ? TXTBACK_TRANSPARENT: TXTBACK_OPAQUE;
    return S_OK;
}

HRESULT TextRenderHost::TxGetMaxLength(DWORD *plength)
{
    *plength = _lMaxLength;
    return S_OK;
}

HRESULT TextRenderHost::TxGetScrollBars(DWORD *pdwScrollBar)
{
    const DWORD scrStyle = WS_VSCROLL | WS_HSCROLL | ES_AUTOVSCROLL | ES_AUTOHSCROLL/* | ES_DISABLENOSCROLL*/;
    *pdwScrollBar = scrStyle;//_dwStyle & (WS_VSCROLL | WS_HSCROLL | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_DISABLENOSCROLL);

    return S_OK;
}

HRESULT TextRenderHost::TxGetPasswordChar(TCHAR *pch)
{
    *pch = _chPasswordChar;
    return S_OK;
}

HRESULT TextRenderHost::TxGetAcceleratorPos(LONG *pcp)
{
    *pcp = _laccelpos;
    return S_OK;
}

HRESULT TextRenderHost::TxGetExtent(LPSIZEL lpExtent)
{
    *lpExtent = GetExtent();
    return S_OK;
}

HRESULT TextRenderHost::OnTxCharFormatChange(const CHARFORMATW * pcf)
{
    return S_FALSE;
}

HRESULT TextRenderHost::OnTxParaFormatChange(const PARAFORMAT * ppf)
{
    return S_FALSE;
}

HRESULT TextRenderHost::TxGetPropertyBits(DWORD dwMask, DWORD *pdwBits)
{
    DWORD dwProperties = 0;

    if (_isRichMode)
    {
        dwProperties = TXTBIT_RICHTEXT;
    }

    if (_isMultiLine)
    {
        dwProperties |= TXTBIT_MULTILINE;
    }

    if (_isReadOnly)
    {
        dwProperties |= TXTBIT_READONLY;
    }

    if (_isUsePassword)
    {
        dwProperties |= TXTBIT_USEPASSWORD;
    }

    if (_isHideSelection)
    {
        dwProperties |= TXTBIT_HIDESELECTION;
    }

    if (_isEnableAutoWordSel)
    {
        dwProperties |= TXTBIT_AUTOWORDSEL;
    }

    if (_isWordWrap)
    {
        dwProperties |= TXTBIT_WORDWRAP;
    }

    if (_isAllowBeep)
    {
        dwProperties |= TXTBIT_ALLOWBEEP;
    }

    if (_isSaveSelection)
    {
        dwProperties |= TXTBIT_SAVESELECTION;
    }

    *pdwBits = dwProperties & dwMask; 

    return NOERROR;
}

HRESULT TextRenderHost::TxNotify(DWORD iNotify, void *pv)
{
    //return E_NOTIMPL;
    switch (iNotify)
    {
        // 达到最大长度
    case EN_MAXTEXT:
        break;
        // 选择变化，需要事件掩码ENM_SELCHANGE
    case EN_SELCHANGE:
        {
            SELCHANGE* s = (SELCHANGE*)pv;            

            if (_holder != NULL)
            {
                suic::ITextCallback* txtCb = _cf.GetTextCb();

                if (NULL != txtCb)
                {
                    txtCb->NotifyTextSelectionChanged();
                }
            }
        }
        break;
        // 内存溢出
    case EN_ERRSPACE:
        break;
        // 输入变化，需要事件ENM_CHANGE掩码
    case EN_CHANGE:
        {
            CHANGENOTIFY* c = (CHANGENOTIFY*)pv;

            if (_holder != NULL)
            {
                suic::ITextCallback* txtCb = _cf.GetTextCb();

                if (NULL != txtCb)
                {
                    _isUpdateTextPending = TRUE;
                    txtCb->NotifyTextChanged();
                    _isUpdateTextPending = FALSE;
                }
            }
        }
        break;

        // 文件拖动，需要事件ENM_DROPFILES掩码
    case EN_DROPFILES:
        {
            ENDROPFILES* e = (ENDROPFILES*)pv;
        }
        break;

        // 超链接，dwEffects需要CFE_LINK
    case EN_LINK:
        {
            ENLINK* enlink = (ENLINK*)pv;
            int i = 0;
        }
        break;

        // 需要事件掩码ENM_REQUESTRESIZE
    case EN_REQUESTRESIZE:
        {
            break;
    }
        // ole对象操作失败
    case EN_OLEOPFAILED:
        break;

        // 准备绘制
    case EN_UPDATE:
        break;

        // 失去焦点
    case EN_KILLFOCUS:
        break;

        // 数据保护, CFE_PROTECTED
    case EN_PROTECTED:
        {
            ENPROTECTED* enp = (ENPROTECTED*)pv;
        }
        return S_FALSE;

        // 消息过滤
    case EN_MSGFILTER:
        {
            MSGFILTER* mf = (MSGFILTER*)pv;
        }
        break;

        // 存储剪贴板
    case EN_SAVECLIPBOARD:
        {
            ENSAVECLIPBOARD* ens = (ENSAVECLIPBOARD*)pv;
        }
        break;

        // 拖动完成，需要事件掩码ENM_DRAGDROPDONE
    case EN_DRAGDROPDONE:
        // RevokeDragDrop();
        break;
    }

    return S_OK;
}

HIMC TextRenderHost::TxImmGetContext()
{
    return ImmGetContext(GetHostHwnd());
}

void TextRenderHost::TxImmReleaseContext(HIMC himc)
{
    ImmReleaseContext(GetHostHwnd(), himc);
}

HRESULT TextRenderHost::TxGetSelectionBarWidth(LONG *plSelBarWidth)
{
    *plSelBarWidth = 0;
    return S_OK;
}

BOOL TextRenderHost::TxIsDoubleClickPending()
{
    return FALSE;
}

HRESULT TextRenderHost::TxGetWindow(HWND *phwnd)
{
    *phwnd = GetHostHwnd();
    return S_OK;
}

HRESULT TextRenderHost::TxSetForegroundWindow()
{
    return S_OK;
}

HPALETTE TextRenderHost::TxGetPalette()
{
    return S_OK;
}

HRESULT TextRenderHost::TxObjectWrapping(BOOL& val)
{
    val = TRUE;
    return S_OK;
}

static BOOL LPtoDP(HDC memdc, LPSIZE lpSize)
{
    SIZE sizeWinExt = { 0, 0 };
    if (!GetWindowExtEx(memdc, &sizeWinExt))
    {
        return FALSE;
    }

    SIZE sizeVpExt = { 0, 0 };
    if (!GetViewportExtEx(memdc, &sizeVpExt))
    {
        return FALSE;
    }

    lpSize->cx = ::MulDiv(lpSize->cx, abs(sizeVpExt.cx), abs(sizeWinExt.cx));
    lpSize->cy = ::MulDiv(lpSize->cy, abs(sizeVpExt.cy), abs(sizeWinExt.cy));
    return TRUE;
}

static void HIMETRICtoDP(HDC memdc, LPSIZE lpSize)
{
    int nMapMode;
    if((nMapMode = GetMapMode(memdc)) < MM_ISOTROPIC && nMapMode != MM_TEXT)
    {
        SetMapMode(memdc, MM_HIMETRIC);
        LPtoDP(memdc, lpSize);
        SetMapMode(memdc, nMapMode);
    }
    else
    {
        const int HIMETRIC_INCH = 2540;
        int cxPerInch = SystemParameters::DpiX;
        int cyPerInch = SystemParameters::DpiY;
        lpSize->cx = ::MulDiv(lpSize->cx, cxPerInch, HIMETRIC_INCH);
        lpSize->cy = ::MulDiv(lpSize->cy, cyPerInch, HIMETRIC_INCH);
    }
}

HRESULT TextRenderHost::TxInsertOleObject(LPDATAOBJECT pdo, STGMEDIUM* pStdMedium, FORMATETC* fetc, REOBJECT* reobj)
{
    HRESULT hr = S_FALSE;

    if (pStdMedium || !UIOleObject::IsSharpuiClsid(reobj->clsid))
    {
        STGMEDIUM* pValidStd = pStdMedium;
        STGMEDIUM medium = {0, NULL};
        suic::ImageEmbbed* embbed = NULL;

        if (NULL == pValidStd)
        {
            pdo->GetData(fetc, &medium);
            pValidStd = &medium;
        }

        switch (pValidStd->tymed)  
        {  
        case TYMED_HGLOBAL:  
            {
                if (fetc->cfFormat == CF_DIB)
                {
                    bool bSucc = false;
                    suic::Byte* pBytes = (suic::Byte*)::GlobalLock(pValidStd->hGlobal);
                    int iSize = ::GlobalSize(pValidStd->hGlobal);
                    embbed = new suic::ImageEmbbed();

                    LPBITMAPINFO pbmi = (LPBITMAPINFO)pBytes;

                    if (pbmi->bmiHeader.biSize == sizeof(BITMAPINFOHEADER))
                    {
                        int iBitCount = pbmi->bmiHeader.biBitCount;
                        int iColors = 0;

                        if (iBitCount <= 8)
                        {
                            iColors = 1 <<iBitCount;
                        }

                        int nHeaderSize = sizeof(BITMAPINFOHEADER) + (iColors * sizeof(RGBQUAD));

                        BITMAPFILEHEADER bmfh={0}; 
                        // 计算文件头信息  
                        bmfh.bfType = *(WORD *)"BM";  
                        bmfh.bfSize = sizeof(BITMAPFILEHEADER) + iSize;  
                        bmfh.bfOffBits = sizeof(BITMAPFILEHEADER) + nHeaderSize;  

                        pbmi->bmiHeader.biSizeImage = iSize - nHeaderSize;
                    }

                    bSucc = embbed->GetBitmap()->LoadDib(pBytes, iSize);
                    embbed->ref();

                    if (!bSucc)
                    {
                        embbed->unref();
                        embbed = NULL;
                    }

                    ::GlobalUnlock(pValidStd->hGlobal);
                }

                break;
            }
        case TYMED_GDI:
            if (fetc->cfFormat == CF_BITMAP)
            {
                embbed = new suic::ImageEmbbed();
                embbed->ref();
                if (!embbed->GetBitmap()->LoadHandle((suic::Handle)pValidStd->hBitmap))
                {
                    embbed->unref();
                    embbed = NULL;
                }
            }
            break;

        case TYMED_MFPICT:
            if (fetc->cfFormat == CF_METAFILEPICT)
            {
                SIZE size;
                LPMETAFILEPICT pMFP = (LPMETAFILEPICT)GlobalLock(pValidStd->hMetaFilePict);

                size.cx = pMFP->xExt;
                size.cy = pMFP->yExt;

                HDC hdc = ::GetDC(NULL);

                ::HIMETRICtoDP(hdc, &size);
                embbed = new suic::ImageEmbbed();
                embbed->ref();

                HDC memdc= ::CreateCompatibleDC(hdc);
                HBITMAP bmp = ::CreateCompatibleBitmap(hdc, size.cx, size.cy);
                HBITMAP hOld = (HBITMAP)::SelectObject(memdc, bmp);
                suic::Uint32 iSize = 0;

                ::SetMapMode(memdc, pMFP->mm);
                ::SetViewportExtEx(memdc, size.cx, size.cy, NULL);
                ::SetViewportOrgEx(memdc, 0, 0, NULL);

                BOOL bSucc = ::PlayMetaFile(memdc, pMFP->hMF);
                suic::Byte* lpDib = DIBHelper::DIBFromBitmap(bmp, iSize);

                if (lpDib != NULL)
                {
                    embbed->GetBitmap()->LoadDib(lpDib, iSize);
                    DIBHelper::FreeBytes(lpDib);
                }

                ::SelectObject(memdc, hOld);
                ::DeleteObject(bmp);
                ::DeleteDC(memdc);

                ::ReleaseDC(NULL, hdc);
            }

            break;
        }

        if (pValidStd != pStdMedium)
        {
            ReleaseStgMedium(pValidStd);
        }

        if (embbed != NULL)
        {
            UIOleObject *pOleImage = NULL;
            pOleImage = new UIOleObject(embbed);
            reobj->poleobj = pOleImage;
            pOleImage->GetUserClassID(&(reobj->clsid));
            reobj->dvaspect = DVASPECT_CONTENT;
            reobj->dwUser = 0;
            reobj->dwFlags = REO_BELOWBASELINE;
            embbed->unref();

            hr = S_OK;
        }
    }

    return hr;
}

void TextRenderHost::RegisterDragDrop(void)
{
    IDropTarget *pdt;

    if (!_isRegisteredForDrop && _textSrv->TxGetDropTarget(&pdt) == NOERROR)
    {
        HRESULT hr = ::RegisterDragDrop(GetHostHwnd(), pdt);

        if (hr == NOERROR)
        {	
            _isRegisteredForDrop = TRUE;
        }

        pdt->Release();
    }
}

void TextRenderHost::RevokeDragDrop(void)
{
    if (_isRegisteredForDrop)
    {
        //::RevokeDragDrop(GetHostHwnd());
        _isRegisteredForDrop = FALSE;
    }
}

BOOL TextRenderHost::InitializeTextService(suic::TextContainer* holder)
{
    IUnknown *pUnk = NULL;
    HRESULT hr = S_OK;
    DWORD dwColor = ARGB(255,0,0,0);

    _holder = holder;
    _isInplaceActive = TRUE;

    if (FAILED(CreateTextServices(NULL, this, &pUnk))) 
    {
        return FALSE;
    }

    hr = pUnk->QueryInterface(IID_ITextServices,(void **)&_textSrv);

    if (FAILED(hr)) 
    {
        pUnk->Release();
        return FALSE;
    }

    _cf._textSrv = _textSrv;
    _pf._textSrv = _textSrv;

    hr = pUnk->QueryInterface(IID_ITextDocument,(void **)&_textDoc);
    pUnk->Release();

    if (FAILED(hr)) 
    {
        _textSrv->Release();
        return FALSE;
    }

    //
    // 设置需要接收的事件类型
    //
    SetEventMask(ENM_OBJECTPOSITIONS | ENM_PROTECTED | ENM_DROPFILES | ENM_CHANGE | ENM_LINK | ENM_SELCHANGE | ENM_DRAGDROPDONE);

    LRESULT lResult = 0;

    // 设置ClipBoard
    RegisterClipboardFormat(CF_RETEXTOBJ);
    // _textSrv->TxSendMessage(EM_SETLANGOPTIONS, 0, 0, &lResult);

    SetMultiLine(GetMaxLines() > 1);
    SetWordWrap(_isWordWrap);

    SetTransparent(TRUE);
    SetClntRect(_rcClnt);
    SetPadding(_rcInset);

    //
    // 激活
    //
    OnTxInPlaceActivate(NULL);
    _pf.Commit();

    LRESULT dwLr = 0;
    DWORD dw = 0;

    //#define IMF_DUALFONT			0x0080
    
    _textSrv->TxSendMessage(EM_GETLANGOPTIONS, 0, 0, &dwLr);
    dw = (DWORD)dwLr;
    dw |= IMF_AUTOKEYBOARD;
    dw &= ~IMF_AUTOFONT;
    _textSrv->TxSendMessage(EM_SETLANGOPTIONS, 0, dw, NULL);

    // TM_PLAINTEXT			= 1,
    // TM_RICHTEXT		    = 2,	// Default behavior 
    // TM_SINGLELEVELUNDO	= 4,
    // TM_MULTILEVELUNDO	= 8,	// Default behavior 
    // TM_SINGLECODEPAGE	= 16,
    // TM_MULTICODEPAGE		= 32	// Default behavior 

    return TRUE;
}

void TextRenderHost::SetSelTextColor(Color clr)
{
    _selTextColor = clr;
    _holder->InvalidateVisual();
}

void TextRenderHost::SetSelBackground(Color clr)
{
    _selBackground = clr;
    _holder->InvalidateVisual();
}

String TextRenderHost::GetText()
{
    String text;
    BSTR bstr = NULL;

    GetTextService()->TxGetText(&bstr);

    if (NULL != bstr)
    {
        text = (LPWSTR)bstr;
        ::SysFreeString(bstr);
    }

    return text;
}

void TextRenderHost::SetText(const suic::String& strText)
{
    if (!_isUpdateTextPending)
    {
        Select(0, -1);
        ReplaceSel(strText, false);
    }
}

void TextRenderHost::InitializeRichMode()
{
    SetRichTextFlag(TRUE);

    RichEditOleCallback* callback = new RichEditOleCallback();
    SetRichEditOleCallback(callback);
    callback->SetTextService(GetTextService(), GetHolder());
}

LRESULT TextRenderHost::TxSendMessage(UINT msg, WPARAM wparam, LPARAM lparam) const
{
    LRESULT lr = 0;
    _textSrv->TxSendMessage(msg, wparam, lparam, &lr);
    return lr;
}

HRESULT TextRenderHost::TxSendMessage(UINT msg, WPARAM wparam, LPARAM lparam, LRESULT& lr) const
{
    return _textSrv->TxSendMessage(msg, wparam, lparam, &lr);
}

LRESULT TextRenderHost::InternalSendMessage(UINT msg) const
{
    MessageParam& mp = Assigner::Current()->lastmp;
#ifdef _DEBUG
    if (msg != Assigner::Current()->lastmp.message)
    {
        throw "message don't equal";
        abort();
    }
#endif
    return TxSendMessage(mp.message, mp.wp, mp.lp);
}

void TextRenderHost::TxRender(HDC hdc, suic::Rect rcClip, suic::Point offset)
{
    suic::Rect rcClnt(Point(), _rcClnt.ToSize());
    rcClnt.Offset(offset.x, offset.y);
    GetTextService()->TxDraw(
            DVASPECT_CONTENT,
            0,
            NULL,
            NULL,
            hdc,
            NULL,
            (RECTL*)&rcClnt,
            NULL,
            &rcClip,
            NULL,
            NULL,
            IsInpaceActive() ? TXTVIEW_ACTIVE : TXTVIEW_INACTIVE);

    ::SelectClipRgn(hdc, NULL);
}

void TextRenderHost::Render(Drawing* drawing)
{
    _isRenderPending = true;

    suic::Rect rcClip = drawing->GetClipBound().ToRect(); // SystemParameters::TransformToDevice(drawing->GetClipBound()).ToRect();
    Bitmap* dBmp = drawing->GetBitmap();
    suic::Byte alpha = Color::A(_textColor);
    suic::Rect rcClnt(Point(), _rcClnt.ToSize());
    suic::fPoint offset = drawing->GetOffset();
    HWND hwnd = GetHostHwnd();
    HDC tmdc = ::GetDC(hwnd);

    rcClip.Intersect(&rcClnt);
    
    if (!rcClip.IsZero())
    {
        fSize ptSize(offset.x, offset.y);
        // ptSize = SystemParameters::TransformToDevice(ptSize);
        rcClip.Offset(ptSize.cx, ptSize.cy);

        offset.x = ptSize.cx;
        offset.y = ptSize.cy;

        if (alpha == 255)
        {
            suic::AlphaOp aOp(tmdc, dBmp, rcClip);
            aOp.Backup(drawing, rcClip);
            TxRender(aOp.GetDrawDc(), rcClip, offset.ToPoint());
            aOp.Restore(drawing);
        }
        else
        {
            suic::SelfAlphaOp aOp2(tmdc, dBmp, rcClip);
            aOp2.Backup(drawing, rcClip);
            TxRender(aOp2.GetDrawDc(), rcClip, offset.ToPoint());
            aOp2.Restore(drawing, alpha);
        }
    }

    ::ReleaseDC(hwnd, tmdc);

    _isRenderPending = false;
}

Rect TextRenderHost::GetViewRect() const
{
    return _rcClnt;
}

void TextRenderHost::SetClntRect(Rect val)
{
    if (!_rcClnt.Equal(&val))
    {
        _rcClnt = val;
        OnTxPropertyBitsChange(TXTBIT_CLIENTRECTCHANGE, TXTBIT_CLIENTRECTCHANGE);
    }
}

Rect TextRenderHost::GetPadding() const
{
    return _rcInset;
}

void TextRenderHost::SetPadding(Rect val)
{
    _rcInset = SystemParameters::TransformToDevice(val.TofRect()).ToRect();
    OnTxPropertyBitsChange(TXTBIT_VIEWINSETCHANGE, TXTBIT_VIEWINSETCHANGE);
}

void TextRenderHost::RefleshView()
{
    _textSrv->TxRecalcView(FALSE);
    _textSrv->TxUpdateCaret();
}

bool TextRenderHost::DoMessage(suic::Uint32 message, suic::Uint32 wp, suic::Uint32 lp, suic::Uint64* lr)
{
    LRESULT lres = 0;
    HRESULT hr = 0;

    if (message == WM_SETFOCUS)
    {
        _isFocused = 1;
    }
    else if (message == WM_KILLFOCUS)
    {
        _isFocused = 0;
    }
    
    hr = TxSendMessage(message, wp, lp, lres);

    if (lr != NULL)
    {
        *lr = (Uint64)lres;
    }

    suic::String strText = GetText();

    return (hr != S_FALSE);
}

//--------------------------------------------------------

int TextRenderHost::GetOleObjCount()
{
    IRichEditOle* pRichEditOle = GetRichEditOle();
    if (NULL == pRichEditOle)
    {
        return 0;
    }
    else
    {
        return pRichEditOle->GetObjectCount();
    }
}

bool TextRenderHost::GetOleObjOnIndex(LONG cp, REOBJECT* preobj)
{
    // WCH_EMBEDDING 
    IRichEditOle* pRichEditOle = GetRichEditOle();
    if (NULL == pRichEditOle)
    {
        return false;
    }
    else
    {
        return (NOERROR == pRichEditOle->GetObject(cp, preobj, REO_GETOBJ_POLEOBJ | REO_GETOBJ_PSTG | REO_GETOBJ_POLESITE));
    }
}

bool TextRenderHost::GetOleObjOnCp(REOBJECT* preobj)
{
    return GetOleObjOnIndex(REO_IOB_USE_CP, preobj);
}

bool TextRenderHost::GetOleObjOnSel(REOBJECT* preobj)
{
    return GetOleObjOnIndex(REO_IOB_SELECTION, preobj);
}
