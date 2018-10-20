// stdafx.cpp : ֻ������׼�����ļ���Դ�ļ�
// AiQ3Helper.pch ����ΪԤ����ͷ
// stdafx.obj ������Ԥ����������Ϣ

#include "stdafx.h"

// TODO: �� STDAFX.H ��
// �����κ�����ĸ���ͷ�ļ����������ڴ��ļ�������

void _DbgPrintW(const WCHAR* pszFormat, ...) 
{ 
	va_list args;
	ULONG ulIndex = 0;
	WCHAR szDebugInfo[MAX_PATH] = {0};
	va_start(args, pszFormat);

	HRESULT hr = StringCchCopyW(szDebugInfo, MAX_PATH, L"[AiQ3Helper]");
	if (FAILED(hr)){
		return;
	}
	ulIndex = wcslen(L"[AiQ3Helper]");
	hr = StringCchVPrintfW(szDebugInfo + ulIndex, MAX_PATH - ulIndex, pszFormat, args);
	if (FAILED(hr)){
		return;
	}

	OutputDebugStringW(szDebugInfo);
	va_end(args);
}

void _DbgPrintA(const char* pszFormat, ...) 
{ 
	va_list args;
	ULONG ulIndex = 0;
	char szDebugInfo[MAX_PATH] = {0};
	va_start(args, pszFormat);

	HRESULT hr = StringCchCopyA(szDebugInfo, MAX_PATH, "[AiQ3Helper]");
	if (FAILED(hr)){
		return;
	}

	ulIndex = wcslen(L"[AiQ3Helper]");
	hr = StringCchVPrintfA(szDebugInfo + ulIndex, MAX_PATH - ulIndex, pszFormat, args);
	if (FAILED(hr)){
		return;
	}

	OutputDebugStringA(szDebugInfo);
	va_end(args);
}