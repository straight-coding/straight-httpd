// straight-buildfs.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "Shlwapi.h"

#include <iostream>
#include <vector>
#include <map>

using namespace std;

#pragma warning(disable:4996) //_CRT_SECURE_NO_WARNINGS

#define ATTRI_GZIP	0x00000001

typedef struct _http_file_info
{
	unsigned long nIndex;
	unsigned long nSize;
	unsigned long long tLastModified;
	unsigned long nAttributes;
	unsigned long nReserved;
	unsigned long nOffsetFileData; //file content offset
	unsigned long nOffsetNextFile; // ==> _http_file_info*
}http_file_info;

void TimetToFileTime(time_t t, LPFILETIME pft);
time_t filetime_to_timet(FILETIME* ft);

void* LWIP_firstdir(const char* filter, int* isFolder, char* name, int maxLen, int* size, time_t* date);
int  LWIP_readdir(void* hFind, int* isFolder, char* name, int maxLen, int* size, time_t* date);
void LWIP_closedir(void* hFind);
void LWIP_sprintf(char* buf, const char* format, ...);

char pRootFolder[512];// = "../straight-httpd/httpd/cncweb/";
char fsBin[512];// = "../straight-httpd/httpd/fs.bin";
char fsData[512];// = "../straight-httpd/httpd/fs_data.c";
BYTE szBuffer[8192];

int main(int argc, char** argv)
{
	int isFolder;
	char name[MAX_PATH];
	char path[MAX_PATH];
	int size;
	time_t date;
	vector<CString> folderList;
	map<int, vector<CString>> filesByNameLen;
	CString strPath;

	memset(pRootFolder, 0, sizeof(pRootFolder));
	memset(fsBin, 0, sizeof(fsBin));
	memset(fsData, 0, sizeof(fsData));

	if ((argc != 3) || (strlen(argv[argc-2]) == 0) || (strlen(argv[argc-1]) == 0))
	{
		printf("Usage: straight-buildfs.exe <webroot folder> <output source folder>\r\n");
		exit(0);
	}

	strPath = argv[argc-2];
	strPath.Trim();
	strPath.Replace("\\", "/");
	if (strPath.Right(1) != "/")
		strPath += "/";
	strcpy(pRootFolder, strPath);

	strPath = argv[argc-1];
	strPath.Trim();
	strPath.Replace("\\", "/");
	if (strPath.Right(1) != "/")
		strPath += "/";
	strPath += "fs.bin";
	strcpy(fsBin, strPath);

	strPath = argv[argc-1];
	strPath.Trim();
	strPath.Replace("\\", "/");
	if (strPath.Right(1) != "/")
		strPath += "/";
	strPath += "fs_data.c";
	strcpy(fsData, strPath);

	printf("Web root: %s\r\n", pRootFolder);
	printf("fs.bin: %s\r\n", fsBin);
	printf("fs-data.c: %s\r\n", fsData);
	printf("sizeof(http_file_info) = %d\r\n", sizeof(http_file_info));

	int rootLen = strlen(pRootFolder)-1;

	folderList.push_back(pRootFolder);
	while (folderList.size() > 0)
	{
		CString strFilter = folderList.front();
		folderList.erase(folderList.begin());

		HANDLE fp = LWIP_firstdir(strFilter + "*.*", &isFolder, name, sizeof(name), &size, &date);
		while(fp != NULL)
		{
			if ((name[0] == '.') && (name[1] == 0))
			{
			}
			else if ((name[0] == '.') && (name[1] == '.') && (name[2] == 0))
			{
			}
			else if (isFolder > 0)
			{
				CString strSubFolder;
				//TRACE("%s%s\r\n", strFilter.Mid(rootLen), name);

				strSubFolder.Format("%s%s/", strFilter, name);
				if (strSubFolder.Right(11) != "/app/cache/")
					folderList.push_back(strSubFolder);
			}
			else
			{
				CString filePath;
				filePath.Format("%s%s", strFilter.Mid(rootLen), name);
				if (stricmp(filePath.Right(4), ".bak") != 0)
				{
					int nLen = filePath.GetLength();
					if (filesByNameLen.find(nLen) == filesByNameLen.end())
					{
						vector<CString> fileList;
						filesByNameLen[nLen] = fileList;
					}
					printf("%s\r\n", filePath);
					filesByNameLen[nLen].push_back(filePath);
				}
			}

			if (LWIP_readdir(fp, &isFolder, name, sizeof(name), &size, &date) <= 0)
				break;
		}
		LWIP_closedir(fp);
	}

	map<int, vector<CString>>::reverse_iterator iter; //long file name first

	CFile fBin;
	long nFileIndex = 0;
	long nOffsetWrite = 0;
	http_file_info fileInfo;

	fBin.Open(fsBin, CFile::modeCreate | CFile::modeReadWrite | CFile::typeBinary | CFile::shareDenyNone, NULL);
	for (iter = filesByNameLen.rbegin(); iter != filesByNameLen.rend(); iter++)
	{
		if (iter->second.size() > 0)
		{
			for (int i = 0; i < (int)iter->second.size(); i++)
			{
				CFile file;
				CFileStatus status;

				CString strFilePath;
				strFilePath.Format("%s%s", pRootFolder, iter->second[i].Mid(1));
				printf("%s\r\n", iter->second[i]);

				file.Open(strFilePath, CFile::modeRead | CFile::typeBinary | CFile::shareDenyNone, NULL);
				if (file.m_hFile != CFile::hFileNull)
				{
					memset(&fileInfo, 0, sizeof(http_file_info));

					file.GetStatus(status);

					//fileInfo.nAttributes |= ATTRI_GZIP;
					fileInfo.tLastModified = status.m_mtime.GetTime();
					fileInfo.nSize = (unsigned long)status.m_size;
					fileInfo.nIndex = nFileIndex ++; // status.m_attribute;

					memset(path, 0, sizeof(path));
					sprintf(path, "%s", iter->second[i]);

					int nPathLen4 = strlen(path)+1;
					nPathLen4 = ((nPathLen4 + 3) & 0xFFFFFFFC); //4-byte alignment

					int nIndexSize = sizeof(fileInfo) + nPathLen4;

					nOffsetWrite += nIndexSize;
					fileInfo.nOffsetFileData = nOffsetWrite;

					nOffsetWrite += ((fileInfo.nSize + 3) & 0xFFFFFFFC); //4-byte alignment
					int nPadding = ((fileInfo.nSize + 3) & 0xFFFFFFFC) - fileInfo.nSize;

					fileInfo.nOffsetNextFile = nOffsetWrite;

					if (fBin.m_hFile != CFile::hFileNull)
					{
						fBin.Write(&fileInfo, sizeof(fileInfo));
						fBin.Write(path, nPathLen4);
					}

					while (TRUE)
					{
						int nRead = file.Read(szBuffer, sizeof(szBuffer));
						if (nRead <= 0)
						{
							if (nPadding > 0)
							{
								long lPad = 0;
								fBin.Write(&lPad, nPadding);
							}
							break;
						}

						if (fBin.m_hFile != CFile::hFileNull)
						{
							fBin.Write(szBuffer, nRead);
						}
					}
					file.Close();
				}
			}
		}
	}

	if (fBin.m_hFile != CFile::hFileNull)
	{
		memset(&fileInfo, 0, sizeof(http_file_info));
		fBin.Write(&fileInfo, sizeof(http_file_info));
	}

	if (fBin.m_hFile != CFile::hFileNull)
	{
		CFileStatus status;
		fBin.GetStatus(status);

		fBin.SeekToBegin();

		CFile file;
		file.Open(fsData, CFile::modeCreate | CFile::modeReadWrite | CFile::typeBinary | CFile::shareDenyNone, NULL);
		if (file.m_hFile != CFile::hFileNull)
		{
			CString strLine = "const unsigned char g_szWebRoot[] __attribute__((aligned(4))) = {\r\n";
			file.Write(strLine, strLine.GetLength());

			int nCount = 0;
			while (TRUE)
			{
				int nToRead = sizeof(szBuffer);
				int nRead = fBin.Read(szBuffer, nToRead);
				if (nRead <= 0)
					break;

				if (fBin.m_hFile != CFile::hFileNull)
				{
					CString strByte = "";
					strLine = "  ";
					for (int i = 0; i < nRead; i++)
					{
						strByte.Format("0x%02X", szBuffer[i]);
						strLine += strByte;

						if (nCount != status.m_size - 1)
							strLine += ",";

						if ((((nCount + 1) & 0x0F) == 0) || (nCount == status.m_size - 1))
						{
							strLine += "\r\n";
							file.Write(strLine, strLine.GetLength());
							strLine = "  ";
						}
						nCount++;
					}
				}
			}

			strLine = "};\r\n";
			file.Write(strLine, strLine.GetLength());
			file.Close();
		}
		fBin.Close();
	}
}

void LWIP_sprintf(char* buf, const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	vsnprintf(buf, 2048, format, ap);
	va_end(ap);
}

void TimetToFileTime(time_t t, LPFILETIME pft)
{
	LONGLONG ll = Int32x32To64(t, 10000000) + 116444736000000000;
	pft->dwLowDateTime = (DWORD)ll;
	pft->dwHighDateTime = ll >> 32;
}

time_t filetime_to_timet(FILETIME* ft)
{
	ULARGE_INTEGER ull;
	ull.LowPart = ft->dwLowDateTime;
	ull.HighPart = ft->dwHighDateTime;
	return ull.QuadPart / 10000000ULL - 11644473600ULL;
}

void* LWIP_firstdir(const char* filter, int* isFolder, char* name, int maxLen, int* size, time_t* date)
{
	WIN32_FIND_DATA fd;
	HANDLE hFind = FindFirstFile(filter, &fd);

	*isFolder = 0;
	memset(name, 0, maxLen);
	if (hFind != INVALID_HANDLE_VALUE)
	{
		if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			*isFolder = 1;
		else
			*isFolder = 0;

		strncpy(name, fd.cFileName, maxLen - 1);
		*size = fd.nFileSizeLow;
		*date = filetime_to_timet(&fd.ftLastWriteTime);
		return (void*)hFind;
	}
	return NULL;
}

int LWIP_readdir(void* hFind, int* isFolder, char* name, int maxLen, int* size, time_t* date)
{
	WIN32_FIND_DATA fd;
	memset(name, 0, maxLen);
	if (hFind != INVALID_HANDLE_VALUE)
	{
		if (FindNextFile((HANDLE)hFind, &fd))
		{
			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
				*isFolder = 1;
			else
				*isFolder = 0;

			strncpy(name, fd.cFileName, maxLen - 1);
			*size = fd.nFileSizeLow;
			*date = filetime_to_timet(&fd.ftLastWriteTime);
			return 1;
		}
	}
	return 0;
}

void LWIP_closedir(void* hFind)
{
	if (hFind != NULL)
		FindClose((HANDLE)hFind);
}
