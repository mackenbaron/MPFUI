// ======================================================================
//
// Copyright (c) 2008-2020 汪荣(视点UI), Inc. All rights reserved.
//
// MPF界面库个人免费使用，团体或公司请购买授权，但不能居于此库衍生任何商业性质的库或代码。
//
// ======================================================================

//////////////////////////////////////////////////////////////////////////////
// WrapPanel.cpp

#include "uvsize.h"
#include <Extend/Controls/WrapPanel.h>
#include <Framework/Controls/ScrollViewer.h>

namespace suic
{

ImplementRTTIOfClass(WrapPanel, Panel)

DpProperty* WrapPanel::ItemWidthProperty;
DpProperty* WrapPanel::ItemHeightProperty;
DpProperty* WrapPanel::OrientationProperty;

WrapPanel::WrapPanel()
{
}

WrapPanel::~WrapPanel()
{
}

void WrapPanel::OnOrientationPropChanged(DpObject* d, DpPropChangedEventArg* e)
{

}

bool WrapPanel::StaticInit()
{
    if (ItemWidthProperty != NULL)
    {
        return true;
    }

    ItemWidthProperty = DpProperty::Register(_U("ItemWidth"), RTTIType(), Integer::RTTIType()
        , DpPropMemory::GetPropMeta(Integer::GetZeroInt(), PropMetadataOptions::AffectsMeasure));
    ItemHeightProperty = DpProperty::Register(_U("ItemHeight"), RTTIType(), Integer::RTTIType()
        , DpPropMemory::GetPropMeta(Integer::GetZeroInt(), PropMetadataOptions::AffectsMeasure));

    OrientationProperty = DpProperty::Register(_T("Orientation"), RTTIType(), Integer::RTTIType()
        , DpPropMemory::GetPropMeta(OrientationBox::HorizontalBox, PropMetadataOptions::AffectsMeasure, &WrapPanel::OnOrientationPropChanged));
    OrientationProperty->SetConvertValueCb(OrientationConvert::Convert);

    return true;
}

ScrollData* WrapPanel::GetScrollData()
{
    GetScrollInfo();
    return _scrollInfo.GetScrollData();
}

void WrapPanel::EnsureScrollingData(Size viewport, Size extent, Point offset)
{
    bool flag = true;
    flag &= viewport == GetScrollData()->viewport;
    flag &= extent == GetScrollData()->extent;
    flag &= offset == GetScrollData()->computedOffset;
    GetScrollData()->offset = offset;

    if (!flag)
    {
        GetScrollData()->viewport = viewport;
        GetScrollData()->extent = extent;
        GetScrollData()->SetComputedOffset(offset);

        OnScrollChanged();
    }
}

void WrapPanel::OnScrollChanged()
{
    if (GetScrollInfo()->GetScrollOwner() != NULL)
    {
        GetScrollInfo()->GetScrollOwner()->InvalidateScrollInfo(true);
    }
}

Size WrapPanel::OnMeasure(const Size& constraint)
{
    UVSize curLineSize(GetOrientation());
    UVSize panelSize(GetOrientation());

    UVSize uvConstraint(GetOrientation(), constraint.Width(), constraint.Height());
    int itemWidth = GetItemWidth(); 
    int itemHeight = GetItemHeight(); 
    bool itemWidthSet =  itemWidth > 0;
    bool itemHeightSet = itemHeight > 0; 

    ScrollData* scrollData = GetScrollData();
    Point offset(scrollData->offset);

    Size childConstraint(
        (itemWidthSet ?  itemWidth  : constraint.Width()),
        (itemHeightSet ? itemHeight : constraint.Height())); 

    ElementColl* children = GetChildren(); 

    for(int i = 0, count = children->GetCount(); i < count; i++)
    { 
        Element* child = children->GetAt(i);

        if (child == NULL) 
        {
            continue;
        }

        MeasureData* measureData = child->GetMeasureData();

        measureData->viewPort.right = measureData->viewPort.left + childConstraint.cx;
        measureData->viewPort.bottom = measureData->viewPort.top + childConstraint.cy;

        child->Measure(childConstraint);

        UVSize sz(GetOrientation(), 
            (itemWidthSet ?  itemWidth  : child->GetDesiredSize().Width()),
            (itemHeightSet ? itemHeight : child->GetDesiredSize().Height()));

        // 需要换行
        if (curLineSize.U + sz.U > uvConstraint.U)
        {
            panelSize.U = max(curLineSize.U, panelSize.U); 
            panelSize.V += curLineSize.V; 
            curLineSize = sz;

            if (sz.U > uvConstraint.U)
            {
                panelSize.U = max(sz.U, panelSize.U);
                panelSize.V += sz.V; 
                curLineSize = UVSize(GetOrientation());
            }
        }
        else
        {
            curLineSize.U += sz.U;
            curLineSize.V = max(sz.V, curLineSize.V);
        }
    } 

    panelSize.U = max(curLineSize.U, panelSize.U); 
    panelSize.V += curLineSize.V;

    Size extent = Size(panelSize.GetWidth(), panelSize.GetHeight());

    EnsureScrollingData(constraint, extent, offset);

    return extent;
}

void WrapPanel::OnArrange(const Size& finalSize)
{
    int i = 0;
    int count = 0;
    ScrollData* scrollData = GetScrollData();
    bool bHori = GetOrientation() == Orientation::Horizontal;

    _visibleCount = 0;
    _firstVisible = -1;

    int scrollOffset = !bHori ? scrollData->computedOffset.x : scrollData->computedOffset.y;
    int allSize = !bHori ? finalSize.Width() : finalSize.Height();

    int firstInLine = 0;
    // 当前总高度
    int accumulatedV = 0;
    int itemWidth = GetItemWidth();
    int itemHeight = GetItemHeight();
    int itemU = (bHori ? itemWidth : itemHeight);
    UVSize curLineSize(GetOrientation()); 
    UVSize uvFinalSize(GetOrientation(), finalSize.Width(), finalSize.Height()); 
    bool itemWidthSet = itemWidth > 0;
    bool itemHeightSet = itemHeight > 0; 
    bool useItemU = (bHori ? itemWidthSet : itemHeightSet);

    allSize += scrollOffset;

    ElementColl* children = GetChildren();

    for(i = 0, count = children->GetCount(); i < count; i++)
    { 
        Element* child = children->GetAt(i);

        if (child == NULL) 
        {
            continue;
        }

        UVSize sz(GetOrientation(),
            (itemWidthSet ?  itemWidth  : child->GetDesiredSize().Width()),
            (itemHeightSet ? itemHeight : child->GetDesiredSize().Height())); 

        // 换行
        if (curLineSize.U + sz.U > uvFinalSize.U)
        {
            int inFirst = firstInLine;

            ArrangeLine(accumulatedV, curLineSize.V, firstInLine, i, useItemU, itemU);

            accumulatedV += curLineSize.V;
            curLineSize = sz;

            if (sz.U > uvFinalSize.U)
            {
                inFirst = i;
                ArrangeLine(accumulatedV, sz.V, i, i + 1, useItemU, itemU);
                firstInLine = i + 1;
                accumulatedV += sz.V;
                curLineSize = UVSize(GetOrientation());
            }
            else
            {
                firstInLine = i;
            }

            if (accumulatedV < scrollOffset)
            {

            }
            // 可视的第一行
            else 
            {
                if (_firstVisible == -1)
                {
                    _firstVisible = inFirst;                    
                }
            }

            if (accumulatedV >= allSize)
            {
                _visibleCount = i + 1 - _firstVisible;
                break;
            }
        } 
        else
        { 
            curLineSize.U += sz.U; 
            curLineSize.V = max(sz.V, curLineSize.V);
        } 
    }

    // 布局最后一行
    if (firstInLine < children->GetCount()) 
    {
        ArrangeLine(accumulatedV, curLineSize.V, firstInLine, children->GetCount(), useItemU, itemU); 
    }

    if (_firstVisible == -1)
    {
        _firstVisible = 0;
        _visibleCount = children->GetCount();
    }
    else if (_visibleCount == 0)
    {
        _visibleCount = children->GetCount() - _firstVisible;
    }
}

void WrapPanel::ArrangeLine(int v, int lineV, int start, int end, bool useItemU, int itemU)
{ 
    ScrollData* scrollData = GetScrollData();
    bool bHori = (GetOrientation() == Orientation::Horizontal); 
    int u = bHori ? -scrollData->computedOffset.x : -scrollData->computedOffset.y;
    v -= bHori ? scrollData->computedOffset.y : scrollData->computedOffset.x;

    ElementColl* children = GetChildren();

    for (int i = start; i < end; i++) 
    {
        Element* child = children->GetAt(i);
        
        if (child != NULL)
        {
            UVSize childSize(GetOrientation(), child->GetDesiredSize().Width(), child->GetDesiredSize().Height());
            int layoutSlotU = (useItemU ? itemU : childSize.U);

            child->Arrange(Rect( 
                (bHori ? u : v),
                (bHori ? v : u), 
                (bHori ? layoutSlotU : lineV),
                (bHori ? lineV : layoutSlotU)));

            u += layoutSlotU;
        } 
    }
}

}
