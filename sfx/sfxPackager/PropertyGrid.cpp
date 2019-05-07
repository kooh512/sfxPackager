/*
	Copyright ©2016. Authored by Keelan Stuart (hereafter referenced as AUTHOR). All Rights Reserved.
	Permission to use, copy, modify, and distribute this software is hereby granted, without fee and without a signed licensing agreement,
	provided that the above copyright notice appears in all copies, modifications, and distributions.
	Furthermore, AUTHOR assumes no responsibility for any damages caused either directly or indirectly by the use of this software, nor vouches for
	any fitness of purpose of this software.
	All other copyrighted material contained herein is noted and rights attributed to individual copyright holders.
	
	For inquiries, contact: keelanstuart@gmail.com
*/

// PropertyGrid.cpp : implementation file
//

#include "stdafx.h"
#include "sfxPackager.h"
#include "PropertyGrid.h"

#include "sfxPackagerDoc.h"
#include "sfxPackagerView.h"

// CPropertyGrid

IMPLEMENT_DYNAMIC(CPropertyGrid, CMFCPropertyGridCtrl)

CPropertyGrid::CPropertyGrid()
{
	m_bLocked = false;
}

CPropertyGrid::~CPropertyGrid()
{
}

void CPropertyGrid::OnClickButton(CPoint point)
{
	m_bLocked = true;

	__super::OnClickButton(point);

	m_bLocked = false;
}

BEGIN_MESSAGE_MAP(CPropertyGrid, CMFCPropertyGridCtrl)
END_MESSAGE_MAP()



// CPropertyGrid message handlers




void CPropertyGrid::OnPropertyChanged(CMFCPropertyGridProperty* pProp) const
{
	CSfxPackagerDoc *pd = CSfxPackagerDoc::GetDoc();
	CSfxPackagerView *pv = CSfxPackagerView::GetView();
	if (!pd || !pv)
		return;

	if (!_tcsicmp(pProp->GetName(), _T("Caption")))
	{
		pd->m_Caption = pProp->GetValue();
	}
	else if (!_tcsicmp(pProp->GetName(), _T("Require Admin")))
	{
		pd->m_bRequireAdmin = pProp->GetValue().boolVal;
	}
	else if (!_tcsicmp(pProp->GetName(), _T("Allow Destination Change")))
	{
		pd->m_bAllowDestChg = pProp->GetValue().boolVal;
	}
	else if (!_tcsicmp(pProp->GetName(), _T("Description")))
	{
		pd->m_Description = pProp->GetValue();
	}
	else if (!_tcsicmp(pProp->GetName(), _T("Icon")))
	{
		pd->m_IconFile = pProp->GetValue();
	}
	else if (!_tcsicmp(pProp->GetName(), _T("Image")))
	{
		pd->m_ImageFile = pProp->GetValue();
	}
	else if (!_tcsicmp(pProp->GetName(), _T("Explore")))
	{
		pd->m_bExploreOnComplete = (pProp->GetValue().intVal != 0);
	}
	else if (!_tcsicmp(pProp->GetName(), _T("Launch")))
	{
		pd->m_LaunchCmd = pProp->GetValue();
	}
	else if (!_tcsicmp(pProp->GetName(), _T("Default Path")))
	{
		pd->m_DefaultPath = pProp->GetValue();
	}
	else if (!_tcsicmp(pProp->GetName(), _T("Version ID")))
	{
		pd->m_VersionID = pProp->GetValue();
	}
	else if (!_tcsicmp(pProp->GetName(), _T("Maximum Size (MB)")))
	{
		pd->m_MaxSize = pProp->GetValue().intVal;
	}
	else if (!_tcsicmp(pProp->GetName(), _T("Output File")))
	{
		pd->m_SfxOutputFile = pProp->GetValue();
	}
	else if (!_tcsicmp(pProp->GetName(), _T("Destination")))
	{
		CString newdst = pProp->GetValue();
		CString rootdst = pProp->GetOriginalValue();

		pv->SetDestFolderForSelection(newdst, rootdst);
	}
	else if (!_tcsicmp(pProp->GetName(), _T("Source")))
	{
		CString newsrc = pProp->GetValue();
		CString rootsrc = pProp->GetOriginalValue();

		pv->SetSrcFolderForSelection(newsrc, rootsrc);
	}
	else if (!_tcsicmp(pProp->GetName(), _T("Filename")))
	{
		CString newname = pProp->GetValue();

		pv->SetFilenameForSelection(newname);
	}
	else if (!_tcsicmp(pProp->GetName(), _T("Temporary Directory")))
	{
		CString tempdir = pProp->GetValue();

		theApp.m_sTempPath = tempdir;
	}
	else if (!_tcsicmp(pProp->GetName(), _T("7-Zip Executable")))
	{
		CString exepath = pProp->GetValue();

		theApp.m_s7ZipPath = exepath;
	}
	else
		CMFCPropertyGridCtrl::OnPropertyChanged(pProp);
}