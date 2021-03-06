/*
	Copyright © 2013-2020, Keelan Stuart (hereafter referenced as AUTHOR). All Rights Reserved.
	Permission to use, copy, modify, and distribute this software is hereby granted, without fee and without a signed licensing agreement,
	provided that the above copyright notice appears in all copies, modifications, and distributions.
	Furthermore, AUTHOR assumes no responsibility for any damages caused either directly or indirectly by the use of this software, nor vouches for
	any fitness of purpose of this software.
	All other copyrighted material contained herein is noted and rights attributed to individual copyright holders.
	
	For inquiries, contact: keelanstuart@gmail.com
*/


// sfxPackagerDoc.cpp : implementation of the CSfxPackagerDoc class
//

#include "stdafx.h"
// SHARED_HANDLERS can be defined in an ATL project implementing preview, thumbnail
// and search filter handlers and allows sharing of document code with that project.
#ifndef SHARED_HANDLERS
#include "sfxPackager.h"
#endif

#include "GenParser.h"

#include "sfxPackagerDoc.h"
#include "sfxPackagerView.h"
#include "ChildFrm.h"
#include "MainFrm.h"
#include "PropertiesWnd.h"

#include <propkey.h>

#include "../sfxFlags.h"

#include <vector>
#include <chrono>
#include <ctime>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define SFXRESID_ICON		130

bool CreateDirectories(const TCHAR *dir)
{
	if (!dir || !*dir)
		return false;

	if (PathIsRoot(dir) || PathFileExists(dir))
		return false;

	TCHAR _dir[MAX_PATH];
	_tcscpy_s(_dir, MAX_PATH, dir);
	PathRemoveFileSpec(_dir);
	CreateDirectories(_dir);

	CreateDirectory(dir, NULL);

	return true;
}

bool GetFileVersion(const TCHAR *filename, int &major, int &minor, int &release, int &build)
{
	bool ret = false;

	DWORD  verHandle = 0;
	UINT   size = 0;
	LPBYTE lpBuffer = NULL;
	DWORD  verSize = GetFileVersionInfoSize(filename, &verHandle);

	if (verSize != NULL)
	{
		LPSTR verData = new char[verSize];

		if (GetFileVersionInfo(filename, verHandle, verSize, verData))
		{
			if (VerQueryValue(verData, _T("\\"), (VOID FAR * FAR *) & lpBuffer, &size))
			{
				if (size)
				{
					VS_FIXEDFILEINFO *verInfo = (VS_FIXEDFILEINFO *)lpBuffer;
					if (verInfo->dwSignature == 0xfeef04bd)
					{
						major = (verInfo->dwFileVersionMS >> 16) & 0xffff;
						minor = (verInfo->dwFileVersionMS >> 0) & 0xffff;
						release = (verInfo->dwFileVersionLS >> 16) & 0xffff;
						build = (verInfo->dwFileVersionLS >> 0) & 0xffff;

						ret = true;
					}
				}
			}
		}

		delete[] verData;
	}

	return ret;
}

#pragma pack(push, 1)
typedef struct
{
	WORD Reserved1;       // reserved, must be 0
	WORD ResourceType;    // type is 1 for icons
	WORD ImageCount;      // number of icons in structure (1)
} GROUPICON;

typedef struct
{
	BYTE Width;           // icon width
	BYTE Height;          // icon height
	BYTE Colors;          // colors (0 means more than 8 bits per pixel)
	BYTE Reserved2;       // reserved, must be 0
	WORD Planes;          // color planes
	WORD BitsPerPixel;    // bit depth
	DWORD ImageSize;      // size of structure
	WORD ResourceID;      // resource ID
} ICONDATA_RES;

typedef struct
{
	BYTE Width;           // icon width
	BYTE Height;          // icon height
	BYTE Colors;          // colors (0 means more than 8 bits per pixel)
	BYTE Reserved2;       // reserved, must be 0
	WORD Planes;          // color planes
	WORD BitsPerPixel;    // bit depth
	DWORD ImageSize;      // size of structure
	DWORD ImageOffset;      // resource ID
} ICONDATA_FILE;
#pragma pack(pop)

bool SetupSfxExecutable(const TCHAR *filename, CSfxPackagerDoc *pDoc, HANDLE &hFile, size_t spanIdx)
{
	bool ret = false;

	TCHAR docpath[MAX_PATH];
	_tcscpy_s(docpath, pDoc->GetPathName());
	PathRemoveFileSpec(docpath);

	HRSRC hsfxres = FindResource(NULL, MAKEINTRESOURCE(IDR_EXE_SFX), _T("EXE"));
	if (hsfxres)
	{
		HGLOBAL hsfxload = LoadResource(NULL, hsfxres);
		if (hsfxload)
		{
			BYTE *pbuf = (BYTE *)LockResource(hsfxload);
			if (pbuf)
			{
				DWORD sfxsize = SizeofResource(NULL, hsfxres);

				TCHAR dir[MAX_PATH];
				_tcscpy_s(dir, MAX_PATH, filename);
				PathRemoveFileSpec(dir);
				CreateDirectories(dir);

				hFile = CreateFile(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				if (hFile != INVALID_HANDLE_VALUE)
				{
					DWORD cw;
					WriteFile(hFile, pbuf, sfxsize, &cw, NULL);

					CloseHandle(hFile);

					ret = (sfxsize == cw);
				}

				FreeResource(hsfxload);
			}
		}
	}

	HMODULE hmod = LoadLibraryEx(filename, NULL, DONT_RESOLVE_DLL_REFERENCES | LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE);
	EnumResourceTypes(hmod, (ENUMRESTYPEPROC)CSfxPackagerDoc::EnumTypesFunc, (LONG_PTR)pDoc);

	if (hmod)
	{
		HRSRC hrinf = FindResource(hmod, _T("VERSION_INFO"), RT_VERSION);
		HGLOBAL hginf = LoadResource(hmod, hrinf);
		BYTE *peinf = (BYTE *)LockResource(hginf);

		UnlockResource(hginf);

		HRSRC hii = FindResource(hmod, _T("ICON"), RT_GROUP_ICON);
		HGLOBAL hgi = LoadResource(hmod, hii);
		BYTE *pei = (BYTE *)LockResource(hgi); // existing icon

		GROUPICON *pei_hdr = (GROUPICON *)pei;
		ICONDATA_RES *pei_icon = (ICONDATA_RES *)(pei + sizeof(GROUPICON));

		std::vector<ICONDATA_RES> oid; // original icons data
		oid.reserve(pei_hdr->ImageCount);
		for (WORD k = 0; k < pei_hdr->ImageCount; k++)
			oid.push_back(pei_icon[k]);

		UnlockResource(hgi);

		FreeLibrary(hmod);

		HANDLE hbur = BeginUpdateResource(filename, FALSE);
		BOOL bresult = FALSE;

		if (hbur)
		{
			if (!pDoc->m_IconFile.IsEmpty())
			{
				TCHAR iconpath[MAX_PATH];

				if (PathIsRelative(pDoc->m_IconFile))
				{
					PathCombine(iconpath, docpath, pDoc->m_IconFile);
				}
				else
				{
					_tcscpy_s(iconpath, MAX_PATH, pDoc->m_IconFile);
				}

				if (PathFileExists(iconpath))
				{
					HANDLE hicobin = CreateFile(iconpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
					if (hicobin)
					{
						DWORD fsz = GetFileSize(hicobin, NULL);
						BYTE *pbin = (BYTE *)malloc(fsz);
						if (pbin)
						{
							DWORD rb;
							if (ReadFile(hicobin, pbin, fsz, &rb, NULL))
							{
								WORD last_id = oid.back().ResourceID;

								GROUPICON *hdr = (GROUPICON *)pbin;

								BYTE *pbin_r = (BYTE *)malloc(sizeof(GROUPICON) + (sizeof(ICONDATA_RES) * hdr->ImageCount));
								if (pbin_r)
								{
									memcpy(pbin_r, pbin, sizeof(GROUPICON));

									WORD hdrsz = sizeof(GROUPICON) + (sizeof(ICONDATA_RES) * hdr->ImageCount);

									ICONDATA_FILE *icon = (ICONDATA_FILE *)(pbin + sizeof(GROUPICON));
									ICONDATA_RES *icon_r = (ICONDATA_RES *)(pbin_r + sizeof(GROUPICON));
									for (WORD i = 0; i < hdr->ImageCount; i++)
									{
										memcpy(&icon_r[i], &icon[i], sizeof(ICONDATA_RES));
										icon_r[i].ResourceID = (i < oid.size()) ? oid[i].ResourceID : ++last_id;
									}

									bresult = UpdateResource(hbur, RT_GROUP_ICON, _T("ICON"), MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), pbin_r, hdrsz);

									for (size_t i = 0, maxi = hdr->ImageCount; i < maxi; i++)
									{
										bresult = UpdateResource(hbur, RT_ICON, MAKEINTRESOURCE(icon_r[i].ResourceID), MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), &pbin[icon[i].ImageOffset], icon_r[i].ImageSize);
									}

									free(pbin_r);
								}
							}

							free(pbin);
						}

						CloseHandle(hicobin);
					}
				}
			}

			if (!pDoc->m_ImageFile.IsEmpty())
			{
				TCHAR imgpath[MAX_PATH];

				if (PathIsRelative(pDoc->m_ImageFile))
				{
					PathCombine(imgpath, docpath, pDoc->m_ImageFile);
				}
				else
				{
					_tcscpy_s(imgpath, pDoc->m_ImageFile);
				}

				if (PathFileExists(imgpath))
				{
					HANDLE himgbin = CreateFile(imgpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
					if (himgbin)
					{
						DWORD fsz = GetFileSize(himgbin, NULL);
						BYTE *pbin = (BYTE *)malloc(fsz);
						BITMAPINFOHEADER *pbmphdr = (BITMAPINFOHEADER *)(pbin + sizeof(BITMAPFILEHEADER));
						if (pbin)
						{
							DWORD rb;
							ReadFile(himgbin, pbin, fsz, &rb, NULL);

							if (pbmphdr->biBitCount <= 24)
							{
								bresult = UpdateResource(hbur, RT_BITMAP, _T("PACKAGE"), MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), pbin + sizeof(BITMAPFILEHEADER), fsz - sizeof(BITMAPFILEHEADER));
							}
							else
							{
								CMainFrame *pmf = (CMainFrame *)(AfxGetApp()->m_pMainWnd);
								pmf->GetOutputWnd().AppendMessage(COutputWnd::OT_BUILD, _T("WARNING: Image file may not be more than 24bpp!\r\n"));
							}

							free(pbin);
						}

						CloseHandle(himgbin);
					}
				}
			}

			CString spanstr;
			spanstr.Format(_T(" (part %d)"), spanIdx + 1);
			CString caption;
			caption.Format(_T("%s%s"), pDoc->m_Caption, spanIdx ? spanstr : _T(""));

			bresult = UpdateResource(hbur, _T("SFX"), _T("SFX_CAPTION"), MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), (void *)((LPCTSTR)caption), (caption.GetLength() + 1) * sizeof(TCHAR));
			bresult = UpdateResource(hbur, _T("SFX"), _T("SFX_DEFAULTPATH"), MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), (void *)((LPCTSTR)pDoc->m_DefaultPath), (pDoc->m_DefaultPath.GetLength() + 1) * sizeof(TCHAR));

			{
				char *shtml;
				bool created_shtml = false;

				TCHAR welcomepath[MAX_PATH];

				if (PathIsRelative(pDoc->m_Description))
				{
					PathCombine(welcomepath, docpath, pDoc->m_Description);
				}
				else
				{
					_tcscpy_s(welcomepath, pDoc->m_Description);
				}

				if (PathFileExists(welcomepath))
				{
					HANDLE hhtmlfile = CreateFile(welcomepath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
					if (hhtmlfile)
					{
						DWORD fsz = GetFileSize(hhtmlfile, NULL);
						shtml = (char *)malloc(fsz + 1);
						if (shtml)
						{
							created_shtml = true;
							DWORD rb;
							ReadFile(hhtmlfile, shtml, fsz, &rb, NULL);
							shtml[fsz] = 0;
						}

						CloseHandle(hhtmlfile);
					}
				}
				else
				{
					LOCAL_TCS2MBCS((LPCTSTR)pDoc->m_Description, shtml);
				}

				bresult = UpdateResource(hbur, RT_HTML, _T("welcome"), MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), (void *)shtml, DWORD(strlen(shtml) * sizeof(char)));

				if (created_shtml)
				{
					free(shtml);
				}
			}

			if (!pDoc->m_LicenseMessage.IsEmpty())
			{
				char *shtml;
				bool created_shtml = false;

				TCHAR licensepath[MAX_PATH];

				if (PathIsRelative(pDoc->m_LicenseMessage))
				{
					PathCombine(licensepath, docpath, pDoc->m_LicenseMessage);
				}
				else
				{
					_tcscpy_s(licensepath, pDoc->m_LicenseMessage);
				}

				if (PathFileExists(licensepath))
				{
					HANDLE hhtmlfile = CreateFile(licensepath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
					if (hhtmlfile)
					{
						DWORD fsz = GetFileSize(hhtmlfile, NULL);
						shtml = (char *)malloc(fsz + 1);
						if (shtml)
						{
							created_shtml = true;
							DWORD rb;
							ReadFile(hhtmlfile, shtml, fsz, &rb, NULL);
							shtml[fsz] = 0;
						}

						CloseHandle(hhtmlfile);
					}
				}
				else
				{
					LOCAL_TCS2MBCS((LPCTSTR)pDoc->m_LicenseMessage, shtml);
				}

				bresult = UpdateResource(hbur, RT_HTML, _T("license"), MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), (void *)shtml, DWORD(strlen(shtml) * sizeof(char)));

				if (created_shtml)
				{
					free(shtml);
				}
			}

			bresult = UpdateResource(hbur, _T("SFX"), _T("SFX_SCRIPT_INIT"), MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT),
									 (void *)((LPCTSTR)pDoc->m_Script[CSfxPackagerDoc::EScriptType::INIT]),
									 (pDoc->m_Script[CSfxPackagerDoc::EScriptType::INIT].GetLength() + 1) * sizeof(TCHAR));

			bresult = UpdateResource(hbur, _T("SFX"), _T("SFX_SCRIPT_PERFILE"), MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT),
									 (void *)((LPCTSTR)pDoc->m_Script[CSfxPackagerDoc::EScriptType::PERFILE]),
									 (pDoc->m_Script[CSfxPackagerDoc::EScriptType::PERFILE].GetLength() + 1) * sizeof(TCHAR));

			bresult = UpdateResource(hbur, _T("SFX"), _T("SFX_SCRIPT_FINISH"), MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT),
									 (void *)((LPCTSTR)pDoc->m_Script[CSfxPackagerDoc::EScriptType::FINISH]),
									 (pDoc->m_Script[CSfxPackagerDoc::EScriptType::FINISH].GetLength() + 1) * sizeof(TCHAR));

			SFixupResourceData furd;
			ZeroMemory(&furd, sizeof(SFixupResourceData));
			strcpy(furd.m_Ident, SFX_FIXUP_SEARCH_STRING);

			bresult = UpdateResource(hbur, _T("SFX"), _T("SFX_FIXUPDATA"), MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), &furd, sizeof(SFixupResourceData));

			bresult = EndUpdateResource(hbur, FALSE);
		}
	}

	hFile = CreateFile(filename, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	LARGE_INTEGER p = {0}, z = {0};
	SetFilePointerEx(hFile, z, &p, FILE_END);

	return (hFile != INVALID_HANDLE_VALUE);
}

bool FixupSfxExecutable(CSfxPackagerDoc *pDoc, const TCHAR *filename, const TCHAR *launchcmd, bool span, UINT32 filecount)
{
	bool bresult = true;

	HANDLE hf = CreateFile(filename, GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hf == INVALID_HANDLE_VALUE)
		return false;

	LARGE_INTEGER ofs;
	DWORD cb;
	if (!pDoc->m_bExternalArchive)
	{
		SetFilePointer(hf, -(int)(sizeof(LONGLONG)), NULL, FILE_END);
		ReadFile(hf, &(ofs.QuadPart), sizeof(LONGLONG), &cb, NULL);
	}
	else
	{
		ofs.LowPart = GetFileSize(hf, (LPDWORD)&ofs.HighPart);
	}

	BYTE *pdata = (BYTE *)malloc(ofs.QuadPart);
	if (pdata)
	{
		SetFilePointer(hf, 0, NULL, FILE_BEGIN);
		ReadFile(hf, pdata, (DWORD)ofs.QuadPart, &cb, NULL);

		char ss[16];
		strcpy(ss, SFX_FIXUP_SEARCH_STRING);

		// search for our identifier string in the exe file... when we find it, then replace the data ourselves instead of using the resource API
		BYTE *p = pdata;
		bool found = false;
		while (p && ((uint64_t)(p - pdata) < (uint64_t)(ofs.QuadPart - sizeof(SFixupResourceData))))
		{
			p = (BYTE *)memchr((char *)p, ss[0], ofs.QuadPart - (p - pdata));

			if (!p)
			{
				break;
			}
			else if (!strcmp((const char *)p, SFX_FIXUP_SEARCH_STRING))
			{
				found = true;
				break;
			}

			p++;
		}

		if (found)
		{
			SetFilePointer(hf, (LONG)(p - pdata), NULL, FILE_BEGIN);

			SFixupResourceData *furd = (SFixupResourceData *)p;
			_tcscpy_s(furd->m_LaunchCmd, MAX_PATH, launchcmd);

			CString vers = pDoc->m_VersionID;
			if (PathFileExists(vers))
			{
				int major, minor, release, build;
				if (GetFileVersion(vers, major, minor, release, build))
				{
					vers.Format(_T("Version %d.%d.%d.%d"), major, minor, release, build);
				}
			}

			_tcscpy_s(furd->m_VersionID, MAX_PATH, vers);

			UINT32 flags = 0;

			if (pDoc->m_bExploreOnComplete && !span)
				flags |= SFX_FLAG_EXPLORE;

			if (span)
				flags |= SFX_FLAG_SPAN;

			if (pDoc->m_bAllowDestChg)
				flags |= SFX_FLAG_ALLOWDESTCHG;

			if (pDoc->m_bRequireAdmin)
				flags |= SFX_FLAG_ADMINONLY;

			if (pDoc->m_bRequireReboot)
				flags |= SFX_FLAG_REBOOTNEEDED;

			if (pDoc->m_bExternalArchive)
				flags |= SFX_FLAG_EXTERNALARCHIVE;

			furd->m_Flags = flags;
			furd->m_SpaceRequired = pDoc->m_UncompressedSize;

			furd->m_CompressedFileCount = filecount;

			WriteFile(hf, furd, sizeof(SFixupResourceData), &cb, NULL);
		}

		free(pdata);
	}

	CloseHandle(hf);

	return bresult;
}

class CPackagerArchiveHandle : public IArchiveHandle
{
protected:
	IArchiver *m_pArc;
	CSfxPackagerDoc *m_pDoc;

	TCHAR m_BaseFilename[MAX_PATH];
	TCHAR m_CurrentFilename[MAX_PATH];
	HANDLE m_hFile;

	UINT m_spanIdx;
	LARGE_INTEGER m_spanTotalSize;

public:
	CPackagerArchiveHandle(CSfxPackagerDoc *pdoc)
	{
		m_pArc = nullptr;
		m_hFile = INVALID_HANDLE_VALUE;
		m_spanIdx = 0;
		m_pDoc = pdoc;
		m_spanTotalSize.QuadPart = 0;
	}

	virtual ~CPackagerArchiveHandle()
	{
		if (m_hFile != INVALID_HANDLE_VALUE)
		{
			// we finalize by storing the file table and writing the starting offset of the archive in the stream
			size_t fc = m_pArc->GetFileCount(IArchiver::IM_WHOLE);

			m_pArc->Finalize();

			CloseHandle(m_hFile);
			m_hFile = INVALID_HANDLE_VALUE;

			// the last file is never spanned
			FixupSfxExecutable(m_pDoc, m_BaseFilename, m_pDoc->m_LaunchCmd, false, (UINT32)fc);
		}
	}

	void SetArchiver(IArchiver *parc)
	{
		m_pArc = parc;
	}

	virtual HANDLE GetHandle()
	{
		return m_hFile;
	}

	virtual uint64_t GetLength()
	{
		LARGE_INTEGER p;
		GetFileSizeEx(m_hFile, &p);
		return p.QuadPart;
	}

	virtual uint64_t GetOffset()
	{
		LARGE_INTEGER p, z;
		z.QuadPart = 0;
		SetFilePointerEx(m_hFile, z, &p, FILE_CURRENT);
		return p.QuadPart;
	}

	virtual UINT GetSpanCount()
	{
		return (m_spanIdx + 1);
	}

	virtual ULONGLONG GetSpanTotalSize()
	{
		return m_spanTotalSize.QuadPart;
	}

};

class CExtArcHandle : public CPackagerArchiveHandle
{

public:

	CExtArcHandle(const TCHAR *base_filename, CSfxPackagerDoc *pdoc) : CPackagerArchiveHandle(pdoc)
	{
		_tcscpy_s(m_BaseFilename, MAX_PATH, base_filename);
		_tcscpy_s(m_CurrentFilename, MAX_PATH, base_filename);
		PathRenameExtension(m_CurrentFilename, _T(".data"));

		m_hFile = INVALID_HANDLE_VALUE;
		if (!SetupSfxExecutable(m_BaseFilename, m_pDoc, m_hFile, 0))
		{
			CMainFrame *pmf = (CMainFrame *)(AfxGetApp()->m_pMainWnd);
			pmf->GetOutputWnd().AppendMessage(COutputWnd::OT_BUILD, _T("SFX setup failed; your output exe may be locked or the directory set to read-only.\r\n"));
		}

		ASSERT(m_hFile != INVALID_HANDLE_VALUE);

		CloseHandle(m_hFile);

		m_hFile = CreateFile(m_CurrentFilename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		ASSERT(m_hFile != INVALID_HANDLE_VALUE);
	}

	virtual ~CExtArcHandle() { }

	virtual void Release()
	{
		delete this;
	}

	virtual bool Span()
	{
		m_spanIdx++;

		TCHAR local_filename[MAX_PATH];
		_tcscpy_s(local_filename, MAX_PATH, m_BaseFilename);

		TCHAR *plext = PathFindExtension(local_filename);
		if (plext)
			*plext = _T('\0');

		_stprintf_s(m_CurrentFilename, MAX_PATH, _T("%s_part%d.data"), local_filename, m_spanIdx + 1);

		size_t fc = m_pArc->GetFileCount(IArchiver::IM_SPAN);

		// we finalize by storing the file table and writing the starting offset of the archive in the stream
		m_pArc->Finalize();

		LARGE_INTEGER sz;
		sz.LowPart = GetFileSize(m_hFile, (LPDWORD)&sz.HighPart);
		m_spanTotalSize.QuadPart += sz.QuadPart;

		// Finalize archive
		CloseHandle(m_hFile);
		m_hFile = INVALID_HANDLE_VALUE;

		m_hFile = CreateFile(m_CurrentFilename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		return (m_hFile != INVALID_HANDLE_VALUE);
	}

};

class CSfxHandle : public CPackagerArchiveHandle
{

public:
	CSfxHandle(const TCHAR *base_filename, CSfxPackagerDoc *pdoc) : CPackagerArchiveHandle(pdoc)
	{
		_tcscpy_s(m_BaseFilename, MAX_PATH, base_filename);
		_tcscpy_s(m_CurrentFilename, MAX_PATH, base_filename);

		m_hFile = INVALID_HANDLE_VALUE;
		if (!SetupSfxExecutable(m_BaseFilename, m_pDoc, m_hFile, 0))
		{
			CMainFrame *pmf = (CMainFrame *)(AfxGetApp()->m_pMainWnd);
			pmf->GetOutputWnd().AppendMessage(COutputWnd::OT_BUILD, _T("SFX setup failed; your output exe may be locked or the directory set to read-only.\r\n"));
		}
		ASSERT(m_hFile != INVALID_HANDLE_VALUE);
	}

	virtual ~CSfxHandle() { }

	virtual void Release()
	{
		delete this;
	}

	virtual bool Span()
	{
		m_spanIdx++;

		TCHAR local_filename[MAX_PATH];
		_tcscpy_s(local_filename, MAX_PATH, m_BaseFilename);

		TCHAR *pext = PathFindExtension(m_BaseFilename);
		TCHAR *plext = PathFindExtension(local_filename);
		_stprintf_s(plext, MAX_PATH, _T("_part%d"), m_spanIdx + 1);
		_tcscat_s(local_filename, MAX_PATH, pext);
		TCHAR *lcmd = PathFindFileName(local_filename);

		size_t fc = m_pArc->GetFileCount(IArchiver::IM_SPAN);

		// we finalize by storing the file table and writing the starting offset of the archive in the stream
		m_pArc->Finalize();

		LARGE_INTEGER sz;
		sz.LowPart = GetFileSize(m_hFile, (LPDWORD)&sz.HighPart);
		m_spanTotalSize.QuadPart += sz.QuadPart;

		// Finalize archive
		CloseHandle(m_hFile);
		m_hFile = INVALID_HANDLE_VALUE;

		FixupSfxExecutable(m_pDoc, m_CurrentFilename, lcmd, m_spanIdx, (UINT32)fc);

		_tcscpy_s(m_CurrentFilename, MAX_PATH, local_filename);

		return SetupSfxExecutable(m_CurrentFilename, m_pDoc, m_hFile, m_spanIdx);
	}

};


// CSfxPackagerDoc

IMPLEMENT_DYNCREATE(CSfxPackagerDoc, CDocument)

BEGIN_MESSAGE_MAP(CSfxPackagerDoc, CDocument)
END_MESSAGE_MAP()


// CSfxPackagerDoc construction/destruction

CSfxPackagerDoc::CSfxPackagerDoc()
{
	m_Key = 0;

	m_SfxOutputFile = _T("mySfx.exe");
	m_MaxSize = -1;
	m_Caption = _T("SFX Installer");
	m_VersionID = _T("");
	m_Description = _T("Provide a description for the files being installed");
	m_LicenseMessage = _T("");
	m_IconFile = _T("");
	m_ImageFile = _T("");
	m_bExploreOnComplete = false;
	m_LaunchCmd = _T("");
	m_DefaultPath = _T("");
	m_bAllowDestChg = true;
	m_bRequireAdmin = false;
	m_bRequireReboot = false;
	m_bAppendBuildDate = false;
	m_bAppendVersion = false;
	m_bExternalArchive = false;

	m_hCancelEvent = CreateEvent(NULL, true, false, NULL);
	m_hThread = NULL;
}

CSfxPackagerDoc::~CSfxPackagerDoc()
{
	if (m_hThread)
	{
		SignalObjectAndWait(m_hCancelEvent, m_hThread, INFINITE, FALSE);
	}

	CloseHandle(m_hCancelEvent);

	EndWaitCursor();
}

CSfxPackagerDoc *CSfxPackagerDoc::GetDoc()
{
	CMDIFrameWnd *pframe = (CMDIFrameWnd *)(AfxGetApp()->m_pMainWnd);
	CMDIChildWnd *pchild = pframe ? pframe->MDIGetActive() : NULL;
	if (pchild)
	{
		CDocument *pdoc = pchild->GetActiveDocument();

		if (pdoc && pdoc->IsKindOf(RUNTIME_CLASS(CSfxPackagerDoc)))
			return (CSfxPackagerDoc *)pdoc;
	}

	return NULL;
}

BOOL CSfxPackagerDoc::OnNewDocument()
{
	if (!CDocument::OnNewDocument())
		return FALSE;

	return TRUE;
}


BOOL CSfxPackagerDoc::EnumTypesFunc(HMODULE hModule, LPTSTR lpType, LONG_PTR lParam)
{
	CSfxPackagerDoc *_this = (CSfxPackagerDoc *)lParam;

	EnumResourceNames(hModule, lpType, (ENUMRESNAMEPROC)EnumNamesFunc, lParam);

	return TRUE;
}

BOOL CSfxPackagerDoc::EnumNamesFunc(HMODULE hModule, LPCTSTR lpType, LPTSTR lpName, LONG_PTR lParam)
{
	CSfxPackagerDoc *_this = (CSfxPackagerDoc *)lParam;

	if ((ULONGLONG)lpName & 0xFFFF0000)
	{
	}

	if (lpType == RT_ICON)
	{
		_this->m_IconName = lpName;
	}

	return TRUE;
}

BOOL CSfxPackagerDoc::EnumLangsFunc(HMODULE hModule, LPCTSTR lpType, LPCTSTR lpName, WORD wLang, LONG_PTR lParam)
{
	CSfxPackagerDoc *_this = (CSfxPackagerDoc *)lParam;

	HANDLE hResInfo = FindResourceEx(hModule, lpType, lpName, wLang); 
	
	return TRUE; 
}

DWORD CSfxPackagerDoc::RunCreateSFXPackage(LPVOID param)
{
	CSfxPackagerView *pv = (CSfxPackagerView *)param;

	CSfxPackagerDoc *pd = pv->GetDocument(); // should be this

	pd->m_hThread = GetCurrentThread();

	TCHAR *ext = PathFindExtension(pd->m_SfxOutputFile);
	if (ext)
	{
		if (!_tcsicmp(ext, _T(".exe")))
		{
			pd->CreateSFXPackage(NULL, pv);
		}
		else if (!_tcsicmp(ext, _T(".gz")) || !_tcsicmp(ext, _T(".gzip")))
		{
			pd->CreateTarGzipPackage(NULL, pv);
		}
	}

	pv->DonePackaging();

	pd->m_hThread = NULL;

	ResetEvent(pd->m_hCancelEvent);

	return 0;
}


DWORD GetFileSizeByName(const TCHAR *filename)
{
	HANDLE h = CreateFile(filename, FILE_READ_ATTRIBUTES, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (!h)
		return 0;

	DWORD ret = GetFileSize(h, NULL);

	CloseHandle(h);

	return ret;
}

bool ShouldExclude(const TCHAR *filename, const TCHAR *excludespec)
{
	// this SHOULD never happen...?
	if (!filename)
		return false;

	if (!excludespec || !*excludespec)
		return false;

	TCHAR *buf = (TCHAR *)_malloca((_tcslen(excludespec) + 1) * sizeof(TCHAR));
	if (!buf)
		return false;

	_tcscpy(buf, excludespec);

	bool ret = false;

	TCHAR *c = buf, *d;
	while (c && *c)
	{
		d = _tcschr(c, _T(';'));
		if (d)
			*d = _T('\0');

		if (PathMatchSpec(filename, c))
		{
			ret = true;
			break;
		}

		c = d;
		if (d)
			c++;
	}

	_freea(buf);

	return ret;
}

bool CSfxPackagerDoc::AddFileToArchive(CSfxPackagerView *pview, IArchiver *parc, TStringArray &created_archives, TSizeArray &created_archive_filecounts, const TCHAR *srcspec, const TCHAR *excludespec, const TCHAR *scriptsnippet, const TCHAR *dstpath, const TCHAR *dstfilename, uint64_t *sz_uncomp, uint64_t *sz_comp, UINT recursion)
{
	DWORD wr = WaitForSingleObject(m_hCancelEvent, 0);
	if ((wr == WAIT_OBJECT_0) || (wr == WAIT_ABANDONED))
	{
		return false;
	}

	if (!srcspec)
		return false;

	CString msg;

	CMainFrame *pmf = (CMainFrame *)(AfxGetApp()->m_pMainWnd);

	const TCHAR *pss = srcspec;
	if (!_tcsnicmp(pss, _T("http"), 4))
	{
		pss += 4;
		if (!_tcsnicmp(pss, _T("s"), 1))
			pss++;

		if (!_tcsnicmp(pss, _T("://"), 3))
		{
			TCHAR local_dstpath[MAX_PATH], *dp = local_dstpath;
			_tcscpy_s(local_dstpath, dstpath);
			if (_tcslen(local_dstpath) > 0)
				PathAddBackslash(local_dstpath);
			_tcscat(local_dstpath, dstfilename);

			while (dp && *(dp++)) { if (*dp == _T('/')) *dp = _T('\\'); }

			parc->AddFile(srcspec, local_dstpath, nullptr, nullptr, scriptsnippet);

			msg.Format(_T("    Adding download reference to \"%s\" from (%s) ...\r\n"), local_dstpath, srcspec);
			pmf->GetOutputWnd().AppendMessage(COutputWnd::OT_BUILD, msg);

			return true;
		}
	}

	TCHAR docpath[MAX_PATH];
	_tcscpy_s(docpath, GetPathName());
	PathRemoveFileSpec(docpath);

	TCHAR fullfilename[MAX_PATH];
	if (PathIsRelative(srcspec))
	{
		PathCombine(fullfilename, docpath, srcspec);
	}
	else
	{
		_tcscpy_s(fullfilename, srcspec);
	}

	TCHAR *filespec = PathFindFileName(srcspec);
	TCHAR *filename = PathFindFileName(fullfilename);

	bool wildcard = (_tcschr(filespec, _T('*')) != NULL);

	if (wildcard)
	{
		_tcscpy(filename, _T("*"));
		dstfilename = NULL;
	}

	// set marquee mode on the progress bar while in recursion
	if (!recursion && wildcard)
	{
		pmf->GetStatusBarWnd().PostMessage(CProgressStatusBar::WM_UPDATE_STATUS, 0xff, 1);
		Sleep(0);
	}

	bool ret = false;

	WIN32_FIND_DATA fd;
	HANDLE hfind = FindFirstFile(fullfilename, &fd);
	if (hfind != INVALID_HANDLE_VALUE)
	{
		ret = true;

		do
		{
			wr = WaitForSingleObject(m_hCancelEvent, 0);
			if ((wr == WAIT_OBJECT_0) || (wr == WAIT_ABANDONED))
			{
				return false;
			}

			if (_tcscmp(fd.cFileName, _T(".")) && _tcscmp(fd.cFileName, _T("..")))
			{
				_tcscpy(filename, fd.cFileName);

				uint64_t comp = 0, uncomp = 0;

				if (PathIsDirectory(fullfilename))
				{
					if (!PathIsDirectoryEmpty(fullfilename))
					{
						PathAddBackslash(filename);
						_tcscat(filename, filespec);

						TCHAR local_dstpath[MAX_PATH];
						_tcscpy_s(local_dstpath, dstpath);
						if (_tcslen(local_dstpath) > 0)
							PathAddBackslash(local_dstpath);
						_tcscat(local_dstpath, fd.cFileName);

						uint64_t rcomp = 0, runcomp = 0;
						ret &= AddFileToArchive(pview, parc, created_archives, created_archive_filecounts, fullfilename, excludespec, scriptsnippet, local_dstpath, NULL, &runcomp, &rcomp, recursion + 1);
						if (ret)
						{
							uncomp = runcomp;
							comp = rcomp;
						}
					}
				}
				else if (PathMatchSpec(fd.cFileName, filespec))
				{
					if (!ShouldExclude(fd.cFileName, excludespec))
					{
						if (!PathIsNetworkPath(dstpath) && *dstpath == _T('\\'))
							dstpath++;

						TCHAR dst[MAX_PATH];
						_tcscpy_s(dst, dstpath ? dstpath : _T(""));

						if (dst[0] != _T('\0'))
							PathAddBackslash(dst);

						_tcscat(dst, (dstfilename && !wildcard) ? dstfilename : fd.cFileName);

						if (PathFileExists(fullfilename))
						{
							msg.Format(_T("    Adding \"%s\" from \"%s\" ...\r\n"), dst, fullfilename);
							pmf->GetOutputWnd().AppendMessage(COutputWnd::OT_BUILD, msg);

							parc->AddFile(fullfilename, dst, &uncomp, &comp, scriptsnippet);
						}
						else
						{
							msg.Format(_T("    WARNING: \"%s\" NOT FOUND!\r\n"), fullfilename);
							pmf->GetOutputWnd().AppendMessage(COutputWnd::OT_BUILD, msg);

							return false;
						}
					}
				}

				if (sz_uncomp)
					*sz_uncomp += uncomp;

				if (sz_comp)
					*sz_comp += comp;
			}
		}
		while (FindNextFile(hfind, &fd));

		FindClose(hfind);
	}

	// set marquee mode on the progress bar while in recursion
	if (!recursion && wildcard)
	{
		pmf->GetStatusBarWnd().PostMessage(CProgressStatusBar::WM_UPDATE_STATUS, 0xff, 0);
		Sleep(0);
	}

	return ret;
}


bool CSfxPackagerDoc::CreateSFXPackage(const TCHAR *filename, CSfxPackagerView *pview)
{
	time_t start_op, finish_op;
	time(&start_op);

	UINT c = 0, maxc = (UINT)m_FileData.size();
	if (!maxc)
		return true;

	CString msg;

	if (!filename)
		filename = m_SfxOutputFile;

	CMainFrame *pmf = (CMainFrame *)(AfxGetApp()->m_pMainWnd);

	TCHAR docpath[MAX_PATH];
	_tcscpy_s(docpath, GetPathName());
	PathRemoveFileSpec(docpath);

	TCHAR fullfilename[MAX_PATH];

	if (PathIsRelative(filename))
	{
		PathCombine(fullfilename, docpath, filename);
	}
	else
	{
		_tcscpy(fullfilename, filename);
	}

	// if we're suppose to, append the current date to the filename before the extension
	if (m_bAppendVersion || m_bAppendBuildDate)
	{
		TCHAR extcpy[MAX_PATH];
		TCHAR *ext = PathFindExtension(fullfilename);
		_tcscpy(extcpy, ext);
		PathRemoveExtension(fullfilename);

		if (m_bAppendVersion)
		{
			if (PathFileExists(m_VersionID))
			{
				int major, minor, release, build;
				if (GetFileVersion(m_VersionID, major, minor, release, build))
				{
					_tcscat(fullfilename, _T("_"));

					TCHAR vb[16];
					_tcscat(fullfilename, _itot(major, vb, 10));
					_tcscat(fullfilename, _T("."));
					_tcscat(fullfilename, _itot(minor, vb, 10));
					if (release || build)
					{
						_tcscat(fullfilename, _T("."));
						_tcscat(fullfilename, _itot(release, vb, 10));

						if (build)
						{
							_tcscat(fullfilename, _T("."));
							_tcscat(fullfilename, _itot(build, vb, 10));
						}
					}
				}
			}
		}

		// if we're suppose to, append the current date to the filename before the extension
		if (m_bAppendBuildDate)
		{
			auto now = std::chrono::system_clock::now();
			std::time_t now_c = std::chrono::system_clock::to_time_t(now);
			struct tm *parts = std::localtime(&now_c);

			TCHAR dates[MAX_PATH];
			_stprintf(dates, _T("_%04d%02d%02d"), 1900 + parts->tm_year, 1 + parts->tm_mon, parts->tm_mday);
			_tcscat(fullfilename, dates);
		}

		_tcscat(fullfilename, extcpy);
	}

	msg.Format(_T("Beginning build of \"%s\" (%s) ...\r\n"), m_Caption, fullfilename);
	pmf->GetOutputWnd().AppendMessage(COutputWnd::OT_BUILD, msg);

	TStringArray created_archives;
	TSizeArray created_archive_filecounts;

	bool ret = true;

	HANDLE hf = NULL;

	IArchiver *parc = NULL;
	UINT spanct;
	uint64_t sz_uncomp = 0, sz_totalcomp = 0, sz_comp = 0;

	DWORD wr = 0;
	{
		CPackagerArchiveHandle *pah = nullptr;

		if (!m_bExternalArchive)
		{
			pah = new CSfxHandle(fullfilename, this);
		}
		else
		{
			pah = new CExtArcHandle(fullfilename, this);
		}

		ret = (IArchiver::CreateArchiver(&parc, pah, IArchiver::CT_FASTLZ) == IArchiver::CR_OK);

		if (pah)
			pah->SetArchiver(parc);

		parc->SetMaximumSize((m_MaxSize > 0) ? (m_MaxSize MB) : UINT64_MAX);

		m_UncompressedSize.QuadPart = 0;

		TFileDataMap::iterator it = m_FileData.begin();
		TFileDataMap::iterator last_it = m_FileData.end();

		while (it != last_it)
		{
			wr = WaitForSingleObject(m_hCancelEvent, 0);
			if ((wr == WAIT_OBJECT_0) || (wr == WAIT_ABANDONED))
			{
				break;
			}

			bool wildcard = ((_tcschr(it->second.name.c_str(), _T('*')) != NULL) || PathIsDirectory(it->second.srcpath.c_str()));

			c++;
			UINT pct = (c * 100) / maxc;
			pmf->GetStatusBarWnd().PostMessage(CProgressStatusBar::WM_UPDATE_STATUS, pct, 0);
			Sleep(0);

			if (wildcard)
			{
				// TODO: add include / exclude lists
				TCHAR srcpath[MAX_PATH];
				_tcscpy_s(srcpath, it->second.srcpath.c_str());
				PathAddBackslash(srcpath);
				_tcscat(srcpath, it->second.name.c_str());

				ret &= AddFileToArchive(pview, parc, created_archives, created_archive_filecounts, srcpath, it->second.exclude.c_str(), it->second.snippet.c_str(), it->second.dstpath.c_str(), nullptr, &sz_uncomp, &sz_comp);
			}
			else
			{
				ret &= AddFileToArchive(pview, parc, created_archives, created_archive_filecounts, it->second.srcpath.c_str(), nullptr, it->second.snippet.c_str(), it->second.dstpath.c_str(), it->second.name.c_str(), &sz_uncomp, &sz_comp);
			}

			sz_totalcomp = sz_comp;
			m_UncompressedSize.QuadPart = sz_uncomp;

			it++;
		}

		if (pah)
		{
			spanct = pah->GetSpanCount();
			sz_totalcomp = pah->GetSpanTotalSize();
		}

		LARGE_INTEGER tsz = {0};
		tsz.LowPart = GetFileSize(pah->GetHandle(), (LPDWORD)&tsz.HighPart);
		sz_totalcomp += tsz.QuadPart;

		if (pah)
		{
			pah->Release();
			pah = nullptr;
		}
	}

	time(&finish_op);
	int elapsed = (int)difftime(finish_op, start_op);

	int hours = elapsed / 3600;
	elapsed %= 3600;

	int minutes = elapsed / 60;
	int seconds = elapsed % 60;

	if ((wr == WAIT_OBJECT_0) || (wr == WAIT_ABANDONED))
	{
		msg.Format(_T("Cancelled. (after: %02d:%02d:%02d)\r\n"), hours, minutes, seconds);
	}
	else
	{
		msg.Format(_T("Done.\r\n\r\nAdded %d files, spanning %d archive(s).\r\n"), parc->GetFileCount(IArchiver::IM_WHOLE), spanct);
		pmf->GetOutputWnd().AppendMessage(COutputWnd::OT_BUILD, msg);

		double comp_pct = 0.0;
		double uncomp_sz = (double)m_UncompressedSize.QuadPart;
		double comp_sz = (double)sz_totalcomp;
		if (comp_sz > 0)
		{
			comp_pct = 100.0 * std::max<double>(0.0, ((uncomp_sz / comp_sz) - 1.0));
		}
		msg.Format(_T("Uncompressed Size: %1.02fMB\r\nCompressed Size: %1.02fMB\r\nCompression: %1.02f%%\r\n\r\n"), uncomp_sz / 1024.0f / 1024.0f, comp_sz / 1024.0f / 1024.0f, comp_pct);
		pmf->GetOutputWnd().AppendMessage(COutputWnd::OT_BUILD, msg);

		msg.Format(_T("Completed in: %02d:%02d:%02d\r\n\r\n\r\n"), hours, minutes, seconds);
	}

	pmf->GetOutputWnd().AppendMessage(COutputWnd::OT_BUILD, msg);

	pmf->GetStatusBarWnd().PostMessage(CProgressStatusBar::WM_UPDATE_STATUS, -1, 0);
	Sleep(0);

	IArchiver::DestroyArchiver(&parc);

	return ret;
}

bool CSfxPackagerDoc::CopyFileToTemp(CSfxPackagerView *pview, const TCHAR *srcspec, const TCHAR *dstpath, const TCHAR *dstfilename, const TCHAR *excludespec, UINT recursion)
{
	DWORD wr = WaitForSingleObject(m_hCancelEvent, 0);
	if ((wr == WAIT_OBJECT_0) || (wr == WAIT_ABANDONED))
	{
		return false;
	}

	if (!srcspec)
		return false;

	CMainFrame *pmf = (CMainFrame *)(AfxGetApp()->m_pMainWnd);

	TCHAR docpath[MAX_PATH];
	_tcscpy_s(docpath, GetPathName());
	PathRemoveFileSpec(docpath);

	TCHAR fullfilename[MAX_PATH];
	if (PathIsRelative(srcspec))
	{
		PathCombine(fullfilename, docpath, srcspec);
	}
	else
	{
		_tcscpy(fullfilename, srcspec);
	}

	TCHAR *filespec = PathFindFileName(srcspec);
	TCHAR *filename = PathFindFileName(fullfilename);

	bool wildcard = (_tcschr(filespec, _T('*')) != NULL);

	if (wildcard)
	{
		_tcscpy(filename, _T("*"));
		dstfilename = NULL;
	}

	// set marquee mode on the progress bar while in recursion
	if (!recursion && wildcard)
	{
		pmf->GetStatusBarWnd().PostMessage(CProgressStatusBar::WM_UPDATE_STATUS, 0xff, 1);
		Sleep(0);
	}

	bool ret = false;

	WIN32_FIND_DATA fd;
	HANDLE hfind = FindFirstFile(fullfilename, &fd);
	if (hfind != INVALID_HANDLE_VALUE)
	{
		ret = true;

		CString msg;

		do
		{
			wr = WaitForSingleObject(m_hCancelEvent, 0);
			if ((wr == WAIT_OBJECT_0) || (wr == WAIT_ABANDONED))
			{
				return false;
			}

			if (_tcscmp(fd.cFileName, _T(".")) && _tcscmp(fd.cFileName, _T("..")))
			{
				_tcscpy(filename, fd.cFileName);

				if (PathIsDirectory(fullfilename))
				{
					if (!PathIsDirectoryEmpty(fullfilename))
					{
						PathAddBackslash(filename);
						_tcscat(filename, filespec);

						TCHAR local_dstpath[MAX_PATH];
						_tcscpy_s(local_dstpath, dstpath);
						if (_tcslen(local_dstpath) > 0)
							PathAddBackslash(local_dstpath);
						_tcscat(local_dstpath, fd.cFileName);

						ret &= CopyFileToTemp(pview, fullfilename, local_dstpath, NULL, excludespec, recursion + 1);
					}
				}
				else if (PathMatchSpec(fd.cFileName, filespec) && !ShouldExclude(fd.cFileName, excludespec))
				{
					if (*dstpath == _T('\\'))
						dstpath++;

					TCHAR dst[MAX_PATH];
					_tcscpy_s(dst, dstpath ? dstpath : _T(""));

					CreateDirectories(dst);

					if (dst[0] != _T('\0'))
						PathAddBackslash(dst);

					_tcscat(dst, (dstfilename && !wildcard) ? dstfilename : fd.cFileName);

					msg.Format(_T("    Copying temp file \"%s\" from \"%s\"...\r\n"), dst, fullfilename);
					pmf->GetOutputWnd().AppendMessage(COutputWnd::OT_BUILD, msg);

					CopyFile(fullfilename, dst, FALSE);
				}
			}
		}
		while (FindNextFile(hfind, &fd));

		FindClose(hfind);
	}

	// set marquee mode on the progress bar while in recursion
	if (!recursion && wildcard)
	{
		pmf->GetStatusBarWnd().PostMessage(CProgressStatusBar::WM_UPDATE_STATUS, 0xff, 0);
		Sleep(0);
	}

	return ret;
}

bool CSfxPackagerDoc::CreateTarGzipPackage(const TCHAR *filename, CSfxPackagerView *pview)
{
	time_t start_op, finish_op;
	time(&start_op);

	UINT c = 0, maxc = (UINT)m_FileData.size();
	if (!maxc)
		return true;

	CString msg;

	if (!filename)
		filename = m_SfxOutputFile;

	CMainFrame *pmf = (CMainFrame *)(AfxGetApp()->m_pMainWnd);

	msg.Format(_T("Beginning build of \"%s\" (%s)...\r\n"), m_Caption, filename);
	pmf->GetOutputWnd().AppendMessage(COutputWnd::OT_BUILD, msg);

	TCHAR fullfilename[MAX_PATH];

	if (PathIsRelative(filename))
	{
		TCHAR docpath[MAX_PATH];
		_tcscpy_s(docpath, GetPathName());
		PathRemoveFileSpec(docpath);
		PathCombine(fullfilename, docpath, filename);
	}
	else
	{
		_tcscpy_s(fullfilename, filename);
	}

	TCHAR filename_sans_ext[MAX_PATH];
	_tcscpy_s(filename_sans_ext, MAX_PATH, fullfilename);
	TCHAR *pext = PathFindExtension(filename_sans_ext);
	do
	{
		if (pext && *pext)
			*pext = _T('\0');

		pext = PathFindExtension(filename_sans_ext);
	}
	while (pext && *pext);

	bool ret = true;

	HANDLE hf = NULL;

	CString limitstr = _T("");
	if (m_MaxSize > 0)
		limitstr.Format(_T(" -v%dm"), m_MaxSize);

	DWORD wr = 0;

	TFileDataMap::iterator it = m_FileData.begin();
	TFileDataMap::iterator last_it = m_FileData.end();

	msg.Format(_T("Pre-processing...\r\n"));
	pmf->GetOutputWnd().AppendMessage(COutputWnd::OT_BUILD, msg);

	TCHAR tempdir_base[MAX_PATH];
	_tcscpy_s(tempdir_base, MAX_PATH, theApp.m_sTempPath);
	PathAddBackslash(tempdir_base);
	PathAppend(tempdir_base, m_strTitle);

	while (it != last_it)
	{
		wr = WaitForSingleObject(m_hCancelEvent, 0);
		if ((wr == WAIT_OBJECT_0) || (wr == WAIT_ABANDONED))
		{
			break;
		}

		c++;
		UINT pct = (c * 100) / maxc;
		pmf->GetStatusBarWnd().PostMessage(CProgressStatusBar::WM_UPDATE_STATUS, pct, 0);

		bool wildcard = ((_tcschr(it->second.name.c_str(), _T('*')) != NULL) || PathIsDirectory(it->second.srcpath.c_str()));

		TCHAR tempdir[MAX_PATH];
		_tcscpy_s(tempdir, MAX_PATH, tempdir_base);
		PathAppend(tempdir, it->second.dstpath.c_str());

		if (wildcard)
		{
			TCHAR srcpath[MAX_PATH];
			_tcscpy(srcpath, it->second.srcpath.c_str());
			PathAddBackslash(srcpath);
			_tcscat(srcpath, it->second.name.c_str());

			ret &= CopyFileToTemp(pview, srcpath, tempdir, it->second.dstpath.c_str(), it->second.exclude.c_str());
		}
		else
		{
			ret &= CopyFileToTemp(pview, it->second.srcpath.c_str(), it->second.dstpath.c_str(), it->second.name.c_str(), it->second.exclude.c_str());
		}

		it++;
	}

	CString cmd, param;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	if (!((wr == WAIT_OBJECT_0) || (wr == WAIT_ABANDONED)))
	{
		param.Format(_T("%s.tar"), filename_sans_ext);
		DeleteFile(param);
		param.Format(_T("%s.tar.gzip"), filename_sans_ext);
		DeleteFile(param);

		cmd = theApp.m_s7ZipPath;
		TCHAR wd[MAX_PATH];
		_tcscpy_s(wd, MAX_PATH, (LPTSTR)((LPCTSTR)theApp.m_s7ZipPath));
		PathRemoveFileSpec(wd);
		PathRemoveBackslash(wd);

		msg.Format(_T("Creating tarball(s)...\r\n"));
		pmf->GetOutputWnd().AppendMessage(COutputWnd::OT_BUILD, msg);

		ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
		ZeroMemory(&si, sizeof(STARTUPINFO));
		si.cb = sizeof(STARTUPINFO);

		param.Format(_T("\"%s\" -ttar -r a \"%s.tar\" \"%s\\*\"%s"), cmd, filename_sans_ext, tempdir_base, limitstr);
		if (CreateProcess(NULL, (LPTSTR)((LPCTSTR)param), NULL, NULL, false, CREATE_NO_WINDOW, NULL, wd, &si, &pi))
		{
			WaitForSingleObject(pi.hProcess, INFINITE);

			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
		}

		msg.Format(_T("Performing final gzip operation...\r\n"));
		pmf->GetOutputWnd().AppendMessage(COutputWnd::OT_BUILD, msg);

		ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
		ZeroMemory(&si, sizeof(STARTUPINFO));
		si.cb = sizeof(STARTUPINFO);

		param.Format(_T("\"%s\" -tgzip a \"%s.tar.gzip\" \"%s.tar\""), cmd, filename_sans_ext, filename_sans_ext);
		if (CreateProcess(NULL, (LPTSTR)((LPCTSTR)param), NULL, NULL, false, CREATE_NO_WINDOW, NULL, wd, &si, &pi))
		{
			WaitForSingleObject(pi.hProcess, INFINITE);

			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
		}

		param.Format(_T("%s.tar"), filename_sans_ext);
		DeleteFile(param);
	}

	{
		msg.Format(_T("Removing temporary files...\r\n"));
		pmf->GetOutputWnd().AppendMessage(COutputWnd::OT_BUILD, msg);

		ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
		ZeroMemory(&si, sizeof(STARTUPINFO));
		si.cb = sizeof(STARTUPINFO);

		param.Format(_T("del /s /q %s\\*"), tempdir_base);
		if (CreateProcess(_T("cmd.exe"), (LPTSTR)((LPCTSTR)param), NULL, NULL, false, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
		{
			WaitForSingleObject(pi.hProcess, INFINITE);

			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
		}
	}

	time(&finish_op);
	int elapsed = (int)difftime(finish_op, start_op);

	int hours = elapsed / 3600;
	elapsed %= 3600;

	int minutes = elapsed / 60;
	int seconds = elapsed % 60;

	if ((wr == WAIT_OBJECT_0) || (wr == WAIT_ABANDONED))
	{
		msg.Format(_T("Cancelled. (after: %02d:%02d:%02d)\r\n"), hours, minutes, seconds);
	}
	else
	{
		msg.Format(_T("Done. (completed in: %02d:%02d:%02d)\r\n"), hours, minutes, seconds);
	}

	pmf->GetOutputWnd().AppendMessage(COutputWnd::OT_BUILD, msg);

	pmf->GetStatusBarWnd().PostMessage(CProgressStatusBar::WM_UPDATE_STATUS, -1, 0);
	Sleep(0);

	return ret;
}

UINT CSfxPackagerDoc::AddFile(const TCHAR *filename, const TCHAR *srcpath, const TCHAR *dstpath, const TCHAR *exclude, const TCHAR *scriptsnippet)
{
	TFileDataPair fd;
	fd.first = m_Key;
	fd.second.name = filename;
	fd.second.srcpath = srcpath;
	fd.second.dstpath = dstpath;
	fd.second.exclude = exclude ? exclude : _T("");
	fd.second.snippet = scriptsnippet ? scriptsnippet : _T("");

	m_FileData.insert(fd);

	UINT ret = m_Key;
	m_Key++;

	return ret;
}

void CSfxPackagerDoc::RemoveFile(UINT handle)
{
	TFileDataMap::iterator it = m_FileData.find(handle);
	if (it != m_FileData.end())
		m_FileData.erase(it);
}

bool CSfxPackagerDoc::AdjustFileOrder(UINT key, EMoveType mt, UINT *swap_key)
{
	TFileDataMap::iterator it = m_FileData.find(key);
	if (it == m_FileData.end())
		return false;

	TFileDataMap::iterator swap_it = it;
	TFileDataMap::iterator last_it = m_FileData.find(m_FileData.rbegin()->first);

	switch (mt)
	{
		case EMoveType::UP:
		{
			if (it == m_FileData.begin())
				return false;
			swap_it--;
			break;
		}

		case EMoveType::DOWN:
			swap_it++;
			break;

		case EMoveType::TOP:
			swap_it = m_FileData.begin();
			break;

		case EMoveType::BOTTOM:
			swap_it = last_it;
			break;
	}

	if (swap_it != m_FileData.end())
	{
		if (swap_key)
			*swap_key = swap_it->first;

		TFileDataMap::mapped_type tmp = swap_it->second;
		swap_it->second = it->second;
		it->second = tmp;

		return true;
	}

	return false;
}

UINT CSfxPackagerDoc::GetNumFiles()
{
	return (UINT)m_FileData.size();
}

const TCHAR *CSfxPackagerDoc::GetFileData(UINT handle, EFileDataType fdt)
{
	TFileDataMap::iterator it = m_FileData.find(handle);
	if (it == m_FileData.end())
		return NULL;

	switch (fdt)
	{
		case FDT_NAME:
			return it->second.name.c_str();
			break;

		case FDT_SRCPATH:
			return it->second.srcpath.c_str();
			break;

		case FDT_DSTPATH:
			return it->second.dstpath.c_str();
			break;

		case FDT_EXCLUDE:
			return it->second.exclude.c_str();
			break;

		case FDT_SNIPPET:
			return it->second.snippet.c_str();
			break;
	}

	return NULL;
}

void CSfxPackagerDoc::SetFileData(UINT handle, EFileDataType fdt, const TCHAR *data)
{
	TFileDataMap::iterator it = m_FileData.find(handle);
	if (it == m_FileData.end())
		return;

	switch (fdt)
	{
		case FDT_NAME:
			it->second.name = data;
			break;

		case FDT_SRCPATH:
			it->second.srcpath = data;
			break;

		case FDT_DSTPATH:
			it->second.dstpath = data;
			break;

		case FDT_EXCLUDE:
			it->second.exclude = data;
			break;

		case FDT_SNIPPET:
			it->second.snippet = data;
			break;
	}
}

// CSfxPackagerDoc serialization

void UnescapeString(const TCHAR *in, tstring &out)
{
	out.reserve(_tcslen(in));
	const TCHAR *c = in;
	while (c && *c)
	{
		if (*c == _T('&'))
		{
			if (!memcmp(c, _T("&lt;"), sizeof(TCHAR) * 4))
			{
				out += _T('<');
				c += 3;
			}
			else if (!memcmp(c, _T("&gt;"), sizeof(TCHAR) * 4))
			{
				out += _T('>');
				c += 3;
			}
			else if (!memcmp(c, _T("&amp;"), sizeof(TCHAR) * 5))
			{
				out += _T('&');
				c += 4;
			}
			else if (!memcmp(c, _T("&quot;"), sizeof(TCHAR) * 6))
			{
				out += _T('\"');
				c += 5;
			}
		}
		else
			out += *c;

		c++;
	}
}

void EscapeString(const TCHAR *in, tstring &out)
{
	out.clear();
	out.reserve(_tcslen(in) * 2);
	const TCHAR *c = in;
	while (c && *c)
	{
		switch (*c)
		{
			case _T('<'):
			{
				out += _T("&lt;");
				break;
			}
			case _T('>'):
			{
				out += _T("&gt;");
				break;
			}
			case _T('&'):
			{
				out += _T("&amp;");
				break;
			}
			case _T('\"'):
			{
				out += _T("&quot;");
				break;
			}
			default:
			{
				out += *c;
				break;
			}
		};

		c++;
	}
}


void CSfxPackagerDoc::ReadSettings(CGenParser &gp)
{
	tstring name, value;

	while (gp.NextToken())
	{
		if (gp.IsToken(_T("<")))
		{
			gp.NextToken();

			if (!gp.IsToken(_T("/")))
			{
				name = gp.GetCurrentTokenString();
			}
			else
			{
				gp.NextToken();
				if (gp.IsToken(_T("settings")))
				{
					while (gp.NextToken() && !gp.IsToken(_T(">"))) { }
					break;
				}
			}
		}
		else if (gp.IsToken(_T("value")))
		{
			gp.NextToken(); // skip '='

			gp.NextToken();
			value = gp.GetCurrentTokenString();
			tstring tmp;
			UnescapeString(value.c_str(), tmp);
			value = tmp;
		}
		else if (gp.IsToken(_T(">")))
		{
			if (!_tcsicmp(name.c_str(), _T("output")))
				m_SfxOutputFile = value.c_str();
			else if (!_tcsicmp(name.c_str(), _T("caption")))
				m_Caption = value.c_str();
			else if (!_tcsicmp(name.c_str(), _T("description")))
				m_Description = value.c_str();
			else if (!_tcsicmp(name.c_str(), _T("licensemsg")))
				m_LicenseMessage = value.c_str();
			else if (!_tcsicmp(name.c_str(), _T("icon")))
				m_IconFile = value.c_str();
			else if (!_tcsicmp(name.c_str(), _T("image")))
				m_ImageFile = value.c_str();
			else if (!_tcsicmp(name.c_str(), _T("launchcmd")))
				m_LaunchCmd = value.c_str();
			else if (!_tcsicmp(name.c_str(), _T("explore")))
				m_bExploreOnComplete = (!_tcsicmp(value.c_str(), _T("true")) ? true : false);
			else if (!_tcsicmp(name.c_str(), _T("maxsize")))
				m_MaxSize = _tstoi(value.c_str());
			else if (!_tcsicmp(name.c_str(), _T("defaultpath")))
				m_DefaultPath = value.c_str();
			else if (!_tcsicmp(name.c_str(), _T("versionid")))
				m_VersionID = value.c_str();
			else if (!_tcsicmp(name.c_str(), _T("requireadmin")))
				m_bRequireAdmin = (!_tcsicmp(value.c_str(), _T("true")) ? true : false);
			else if (!_tcsicmp(name.c_str(), _T("requirereboot")))
				m_bRequireReboot = (!_tcsicmp(value.c_str(), _T("true")) ? true : false);
			else if (!_tcsicmp(name.c_str(), _T("allowdestchg")))
				m_bAllowDestChg = (!_tcsicmp(value.c_str(), _T("true")) ? true : false);
			else if (!_tcsicmp(name.c_str(), _T("appendbuilddate")))
				m_bAppendBuildDate = (!_tcsicmp(value.c_str(), _T("true")) ? true : false);
			else if (!_tcsicmp(name.c_str(), _T("appendversion")))
				m_bAppendVersion = (!_tcsicmp(value.c_str(), _T("true")) ? true : false);
			else if (!_tcsicmp(name.c_str(), _T("externalarchive")))
				m_bExternalArchive = (!_tcsicmp(value.c_str(), _T("true")) ? true : false);
		}
	}
}

void CSfxPackagerDoc::ReadScripts(CGenParser &gp)
{
	tstring stype;

	while (gp.NextToken())
	{
		if (gp.IsToken(_T("<")))
		{
			gp.NextToken();

			if (!gp.IsToken(_T("/")))
			{
				if (gp.IsToken(_T("script")))
				{
					gp.NextToken();

					if (gp.IsToken(_T("type")))
					{
						gp.NextToken(); // skip '='

						gp.NextToken();
						tstring t = gp.GetCurrentTokenString();

						gp.NextToken(); // skip '>'

						gp.FindBoundedRawString(_T('<'));
						tstring s = gp.GetCurrentTokenString();
						tstring ues;
						UnescapeString(s.c_str(), ues);

						if (!_tcsicmp(t.c_str(), _T("init")))
						{
							m_Script[EScriptType::INIT] = ues.c_str();
						}
						else if (!_tcsicmp(t.c_str(), _T("perfile")))
						{
							m_Script[EScriptType::PERFILE] = ues.c_str();
						}
						else if (!_tcsicmp(t.c_str(), _T("finish")))
						{
							m_Script[EScriptType::FINISH] = ues.c_str();
						}
					}
				}
			}
			else
			{
				gp.NextToken();
				if (gp.IsToken(_T("scripts")))
				{
					while (gp.NextToken() && !gp.IsToken(_T(">"))) { }
					break;
				}
			}
		}
	}
}

void CSfxPackagerDoc::ReadFiles(CGenParser &gp)
{
	tstring name, src, dst, exclude, scriptsnippet;

	while (gp.NextToken())
	{
		if (gp.IsToken(_T("<")))
		{
			name.clear();
			src.clear();
			dst.clear();
			exclude.clear();
			scriptsnippet.clear();

			gp.NextToken();
			if (!gp.IsToken(_T("file")))
				break;
		}
		else if (gp.IsToken(_T("/")))
		{
			if (!name.empty() && !src.empty() && !dst.empty())
			{
				UINT handle = AddFile(name.c_str(), src.c_str(), dst.c_str(), exclude.c_str(), scriptsnippet.c_str());
			}

			while (gp.NextToken() && !gp.IsToken(_T(">"))) { }
		}
		else if (gp.IsToken(_T("name")))
		{
			gp.NextToken(); // skip '='

			gp.NextToken();
			name = gp.GetCurrentTokenString();
		}
		else if (gp.IsToken(_T("src")))
		{
			gp.NextToken(); // skip '='

			gp.NextToken();
			src = gp.GetCurrentTokenString();
		}
		else if (gp.IsToken(_T("dst")))
		{
			gp.NextToken(); // skip '='

			gp.NextToken();
			dst = gp.GetCurrentTokenString();
		}
		else if (gp.IsToken(_T("exclude")))
		{
			gp.NextToken(); // skip '='

			gp.NextToken();
			exclude = gp.GetCurrentTokenString();
		}
		else if (gp.IsToken(_T("snippet")))
		{
			gp.NextToken(); // skip '='

			gp.NextToken();
			UnescapeString(gp.GetCurrentTokenString(), scriptsnippet);
		}
	}
}

void CSfxPackagerDoc::ReadProject(CGenParser &gp)
{
	while (gp.NextToken())
	{
		if (gp.IsToken(_T("<")))
		{
			if (gp.NextToken())
			{
				if (gp.IsToken(_T("settings")))
				{
					ReadSettings(gp);
				}
				else if (gp.IsToken(_T("scripts")))
				{
					ReadScripts(gp);
				}
				else if (gp.IsToken(_T("files")))
				{
					ReadFiles(gp);
				}
			}
		}
		else if (gp.IsToken(_T("/")))
		{
			gp.NextToken();
			if (gp.IsToken(_T("sfxpackager")))
				break;
		}

	}
}

void CSfxPackagerDoc::Serialize(CArchive& ar)
{
	if (ar.IsStoring())
	{
		CString s;
		s += _T("<sfxpackager>\n");
		s += _T("\t<settings>");

		tstring tmp;

		s += _T("\n\t\t<output value=\""); s += m_SfxOutputFile; s += _T("\"/>");

		EscapeString(m_Caption, tmp);
		s += _T("\n\t\t<caption value=\""); s += tmp.c_str(); s += _T("\"/>");

		EscapeString(m_Description, tmp);
		s += _T("\n\t\t<description value=\""); s += tmp.c_str(); s += _T("\"/>");

		EscapeString(m_LicenseMessage, tmp);
		s += _T("\n\t\t<licensemsg value=\""); s += tmp.c_str(); s += _T("\"/>");

		s += _T("\n\t\t<icon value=\""); s += m_IconFile; s += _T("\"/>");

		s += _T("\n\t\t<image value=\""); s += m_ImageFile; s += _T("\"/>");

		s += _T("\n\t\t<launchcmd value=\""); s += m_LaunchCmd; s += _T("\"/>");

		s += _T("\n\t\t<explore value=\""); s += m_bExploreOnComplete ? _T("true") : _T("false"); s += _T("\"/>");

		s += _T("\n\t\t<defaultpath value=\""); s += m_DefaultPath; s += _T("\"/>");

		EscapeString(m_VersionID, tmp);
		s += _T("\n\t\t<versionid value=\""); s += tmp.c_str(); s += _T("\"/>");

		s += _T("\n\t\t<requireadmin value=\""); s += m_bRequireAdmin ? _T("true") : _T("false"); s += _T("\"/>");

		s += _T("\n\t\t<requirereboot value=\""); s += m_bRequireReboot ? _T("true") : _T("false"); s += _T("\"/>");

		s += _T("\n\t\t<allowdestchg value=\""); s += m_bAllowDestChg ? _T("true") : _T("false"); s += _T("\"/>");

		s += _T("\n\t\t<appendbuilddate value=\""); s += m_bAppendBuildDate ? _T("true") : _T("false"); s += _T("\"/>");

		s += _T("\n\t\t<appendversion value=\""); s += m_bAppendVersion ? _T("true") : _T("false"); s += _T("\"/>");

		s += _T("\n\t\t<externalarchive value=\""); s += m_bExternalArchive ? _T("true") : _T("false"); s += _T("\"/>");

		TCHAR msb[32];
		s += _T("\n\t\t<maxsize value=\""); _itot_s(m_MaxSize, msb, 32, 10); s += msb; s += _T("\"/>");

		s += _T("\n\t</settings>\n");

		s += _T("\n\t<scripts>");

		POSITION vp = GetFirstViewPosition();
		CView *pv = nullptr;
		CSfxPackagerView *ppv = nullptr;
		while ((pv = GetNextView(vp)) != nullptr)
		{
			if ((ppv = dynamic_cast<CSfxPackagerView *>(pv)) != nullptr)
				break;
		}

		CScriptEditView *pe = nullptr;
		if (ppv)
		{
			pe = dynamic_cast<CScriptEditView *>(ppv->GetSplitterWnd()->GetPane(1, 0));
			if (pe)
				pe->UpdateDocWithActiveScript();
		}

		tstring scr;

		if (!m_Script[EScriptType::INIT].IsEmpty())
		{
			EscapeString(m_Script[EScriptType::INIT], scr);
			if (!scr.empty())
				s += _T("\n\t\t<script type=\"init\">"); s += scr.c_str(); s += _T("</script>");
		}

		if (!m_Script[EScriptType::PERFILE].IsEmpty())
		{
			EscapeString(m_Script[EScriptType::PERFILE], scr);
			if (!scr.empty())
				s += _T("\n\t\t<script type=\"perfile\">"); s += scr.c_str(); s += _T("</script>");
		}

		if (!m_Script[EScriptType::FINISH].IsEmpty())
		{
			EscapeString(m_Script[EScriptType::FINISH], scr);
			if (!scr.empty())
				s += _T("\n\t\t<script type=\"finish\">"); s += scr.c_str(); s += _T("</script>");
		}

		s += _T("\n\t</scripts>\n");

		s += _T("\n\t<files>\n");

#if defined(UNICODE)
		int nLen;
		char *pbuf;

		nLen = WideCharToMultiByte(CP_UTF8, 0, (LPCTSTR)s, -1, NULL, NULL, NULL, NULL);
		pbuf = (char *)malloc(nLen);
		if (pbuf)
			WideCharToMultiByte(CP_UTF8, 0, (LPCTSTR)s, -1, pbuf, nLen, NULL, NULL);
#else
		pbuf = (LPCTSTR)s;
#endif

		if (pbuf)
			ar.Write(pbuf, nLen - 1);

		for (TFileDataMap::iterator it = m_FileData.begin(), last_it = m_FileData.end(); it != last_it; it++)
		{
			tstring esc_snip;
			EscapeString(it->second.snippet.c_str(), esc_snip);

			s.Format(_T("\t\t<file name=\"%s\" src=\"%s\" dst=\"%s\" exclude=\"%s\" snippet=\"%s\" />\n"), it->second.name.c_str(), it->second.srcpath.c_str(), it->second.dstpath.c_str(), it->second.exclude.c_str(), esc_snip.c_str());

#if defined(UNICODE)
			nLen = WideCharToMultiByte(CP_UTF8, 0, (LPCTSTR)s, -1, NULL, NULL, NULL, NULL);
			pbuf = (char *)realloc(pbuf, nLen);
			if (pbuf)
				WideCharToMultiByte(CP_UTF8, 0, (LPCTSTR)s, -1, pbuf, nLen, NULL, NULL);
#else
		pbuf = (LPCTSTR)s;
#endif

			if (pbuf)
				ar.Write(pbuf, nLen - 1);
		}

		s = _T("\t</files>\n");
		s += _T("</sfxpackager>\n");

#if defined(UNICODE)
		nLen = WideCharToMultiByte(CP_UTF8, 0, (LPCTSTR)s, -1, NULL, NULL, NULL, NULL);
		pbuf = (char *)realloc(pbuf, nLen);
		if (pbuf)
			WideCharToMultiByte(CP_UTF8, 0, (LPCTSTR)s, -1, pbuf, nLen, NULL, NULL);
#else
		pbuf = (LPCTSTR)s;
#endif

		if (pbuf)
		{
			ar.Write(pbuf, nLen - 1);

#if defined(UNICODE)
			free(pbuf);
#endif
		}
	}
	else
	{
		CFile *pf = ar.GetFile();

		ULONGLONG sz = pf->GetLength() * sizeof(char);
		char *pbuf = (char *)malloc(sz);
		if (pbuf)
		{
			if (sz == ar.Read(pbuf, (UINT)sz))
			{
				TCHAR *ptbuf;

#if defined(UNICODE)
				int nLen = MultiByteToWideChar(CP_UTF8, 0, pbuf, -1, NULL, NULL);
				ptbuf = (TCHAR *)malloc((nLen + 1) * sizeof(TCHAR));
				MultiByteToWideChar(CP_UTF8, 0, pbuf, -1, ptbuf, nLen);
#else
				ptbuf = pbuf;
#endif
				if (ptbuf)
				{
					CGenParser gp;
					gp.SetSourceData(ptbuf, (UINT)sz);
					ReadProject(gp);

#if defined(UNICODE)
					free(ptbuf);
#endif
				}

				CMDIFrameWnd *pframe = (CMDIFrameWnd *)(AfxGetApp()->m_pMainWnd);
				CMDIChildWnd *pchild = pframe ? pframe->MDIGetActive() : NULL;
				if (pchild)
				{
					pchild->RedrawWindow();
				}
			}

			free(pbuf);
		}
	}
}

#ifdef SHARED_HANDLERS

// Support for thumbnails
void CSfxPackagerDoc::OnDrawThumbnail(CDC& dc, LPRECT lprcBounds)
{
	// Modify this code to draw the document's data
	dc.FillSolidRect(lprcBounds, RGB(255, 255, 255));

	CString strText = _T("TODO: implement thumbnail drawing here");
	LOGFONT lf;

	CFont* pDefaultGUIFont = CFont::FromHandle((HFONT) GetStockObject(DEFAULT_GUI_FONT));
	pDefaultGUIFont->GetLogFont(&lf);
	lf.lfHeight = 36;

	CFont fontDraw;
	fontDraw.CreateFontIndirect(&lf);

	CFont* pOldFont = dc.SelectObject(&fontDraw);
	dc.DrawText(strText, lprcBounds, DT_CENTER | DT_WORDBREAK);
	dc.SelectObject(pOldFont);
}

// Support for Search Handlers
void CSfxPackagerDoc::InitializeSearchContent()
{
	CString strSearchContent;
	// Set search contents from document's data. 
	// The content parts should be separated by ";"

	// For example:  strSearchContent = _T("point;rectangle;circle;ole object;");
	SetSearchContent(strSearchContent);
}

void CSfxPackagerDoc::SetSearchContent(const CString& value)
{
	if (value.IsEmpty())
	{
		RemoveChunk(PKEY_Search_Contents.fmtid, PKEY_Search_Contents.pid);
	}
	else
	{
		CMFCFilterChunkValueImpl *pChunk = NULL;
		ATLTRY(pChunk = new CMFCFilterChunkValueImpl);
		if (pChunk != NULL)
		{
			pChunk->SetTextValue(PKEY_Search_Contents, value, CHUNK_TEXT);
			SetChunkValue(pChunk);
		}
	}
}

#endif // SHARED_HANDLERS

// CSfxPackagerDoc diagnostics

#ifdef _DEBUG
void CSfxPackagerDoc::AssertValid() const
{
	CDocument::AssertValid();
}

void CSfxPackagerDoc::Dump(CDumpContext& dc) const
{
	CDocument::Dump(dc);
}
#endif //_DEBUG


// CSfxPackagerDoc commands


void CSfxPackagerDoc::OnCloseDocument()
{
	BeginWaitCursor();

	CDocument::OnCloseDocument();

	CMainFrame *pmf = (CMainFrame *)(AfxGetApp()->m_pMainWnd);
	if (pmf && pmf->GetSafeHwnd())
	{
		CPropertiesWnd &propwnd = pmf->GetPropertiesWnd();
		propwnd.FillPropertyList(NULL);
	}
}
