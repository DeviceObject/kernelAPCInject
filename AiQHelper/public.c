#include "public.h"
#include "inject.h"

NTSTATUS GlobalInit(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryString)
{
	NTSTATUS Status          = STATUS_UNSUCCESSFUL;
	OBJECT_ATTRIBUTES oa;

	g_pKernelbase            = 0;
	g_dwKernelSize           = 0;
	g_pNtdllbase             = 0;
	g_dwNtdllSize            = 0;
	g_bInitProcessNotify     = FALSE;
	g_bInitLoadImageNotify   = FALSE;

	PsGetVersion(&g_dwOsMajorVer, &g_dwOsMinorVer, &g_dwOsBuildNumber, NULL);
	Status = GetKernelModuleBase(NULL, &g_pKernelbase, &g_dwKernelSize);
	if (!NT_SUCCESS(Status)){
		return Status;
	}

	// Win8֮ǰ���У�Win8֮����Ҫ�ڽ��̿ռ�PEB����
	if (IS_WINDOWS7_OR_BEFORE())
	{
		Status = GetKernelModuleBase("ntdll.dll", &g_pNtdllbase, &g_dwNtdllSize);
		if (!NT_SUCCESS(Status)){
			return Status;
		}
	}
 
 	Status = PsSetCreateProcessNotifyRoutine(CreateProcessNotifyRoutine, FALSE);
	if (NT_SUCCESS(Status)) 
	{
		if (IS_WINDOWSXP_OR_BEFORE())
		{
			ExInitializeResourceLite(&g_XProcessResource);
			InitializeListHead(&g_XProcessList);

			Status = PsSetLoadImageNotifyRoutine(LoadImageNotifyRoutinue);
			if (NT_SUCCESS(Status)) {
				g_bInitLoadImageNotify = TRUE;
			}
		}
		g_bInitProcessNotify = TRUE;
	}

	return Status;
}

NTSTATUS GlobalUnInit(PDRIVER_OBJECT DriverObject)
{
	NTSTATUS    Status    = STATUS_SUCCESS;
	PProcInfo    pProcInfo = NULL;
	PLIST_ENTRY ListPtr, ListHeader;


	if (g_bInitProcessNotify) {
 		Status = PsSetCreateProcessNotifyRoutine(CreateProcessNotifyRoutine, TRUE);
	}

	///////////////////////////////////////////////////////////////////////////
	//************************ж��XP�µ�LoadImage�ص�*************************//
	///////////////////////////////////////////////////////////////////////////

	if (IS_WINDOWSXP_OR_BEFORE())
	{
		KeEnterCriticalRegion();
		if (ExAcquireResourceSharedLite(&g_XProcessResource, TRUE))
		{
			while (!IsListEmpty(&g_XProcessList))
			{
				ListPtr = RemoveTailList(&g_XProcessList);
				pProcInfo = CONTAINING_RECORD(ListPtr, ProcInfo, ActiveListEntry);
				ExFreePool(pProcInfo);
			}

			ExReleaseResource(&g_XProcessResource);
			ExDeleteResourceLite(&g_XProcessResource);
		}
		KeLeaveCriticalRegion();

		if (g_bInitLoadImageNotify) {
			Status = PsRemoveLoadImageNotifyRoutine(LoadImageNotifyRoutinue);
		}
	}

	return Status;
}

VOID LoadImageNotifyRoutinue(
	__in PUNICODE_STRING FullImageName,
	__in HANDLE ProcessId,                // pid into which image is being mapped
	__in PIMAGE_INFO ImageInfo
	)
{
	NTSTATUS    Status                      = STATUS_SUCCESS;
	PProcInfo   pProcInfo                   = NULL;
	PLIST_ENTRY ListPtr                     = NULL;
	BOOLEAN     bInject                     = FALSE;
	CHAR        lpszFullImageName[MAX_PATH] = { 0 };

	if (ProcessId != 0)
	{
		//  XP�¼���exeʱ�ĵ��ö�ջ��

		// 	b1c88c08 80644526 nt!PsCallImageNotifyRoutines+0x36
		// 	b1c88d0c 805d0e6b nt!DbgkCreateThread+0xa2
		// 	b1c88d50 805470de nt!PspUserThreadStartup+0x9d
		// 	00000000 00000000 nt!KiThreadStartup+0x16

		if (FullImageName) {

			// 1. �ڽ���List�в��ҵ�ǰ�����Ƿ����
			KeEnterCriticalRegion();
			if (ExAcquireResourceSharedLite(&g_XProcessResource, TRUE))
			{
				ListPtr = g_XProcessList.Flink;
				for ( ; ListPtr != &g_XProcessList; ListPtr = ListPtr->Flink )
				{
					pProcInfo = CONTAINING_RECORD(ListPtr, ProcInfo, ActiveListEntry);
					if (pProcInfo->ProcessId == ProcessId) {

						Status = ConvertWCharToChar(GetFileNameByFullPath(FullImageName->Buffer), 
							lpszFullImageName, MAX_PATH);

						DbgPrint("[LoadImageNotifyRoutinue] ProcessId:%d, MapImageName:%wZ, pProcInfo->lpszProcessImageName:%s", 
							FullImageName, pProcInfo->lpszProcessImageName);

						if (NT_SUCCESS(Status) && !_stricmp(lpszFullImageName, pProcInfo->lpszProcessImageName)) {
							bInject = TRUE;
						}

						RemoveEntryList(ListPtr);
						ExFreePool(pProcInfo);
						break;
					}
				}
				ExAcquireResourceSharedLite(&g_XProcessResource, FALSE);
			}
			KeLeaveCriticalRegion();

			if (bInject)
			{
				InjectDllByApc_Step1(ProcessId);
			}
		}
	}
}

VOID CreateProcessNotifyRoutine(
	__in HANDLE ParentId,
	__in HANDLE ProcessId,
	__in BOOLEAN Create
	)
{
	NTSTATUS Status = STATUS_SUCCESS;
	PEPROCESS EProcess = NULL;
	ULONG ulLength = MAX_PATH;
	UCHAR* szImageName = NULL;
	PKAPC Apc = NULL;

	if (Create == FALSE)
		return;

	Status = PsLookupProcessByProcessId(ProcessId, &EProcess);
	if (!NT_SUCCESS(Status) || !EProcess)
		return;

	do
	{

		szImageName = PsGetProcessImageFileName(EProcess);
		if (NULL == szImageName)
			break;

		DbgPrint("[CreateProcessNotifyRoutine] szImageName: %s", szImageName);
		if (_stricmp((char*)szImageName, "qq.exe") == 0 ||
			_stricmp((char*)szImageName, "iexplore.exe") == 0 ||
			_stricmp((char*)szImageName, "360se.exe") == 0) 
		{
			
			/* 
			 * ע����ǰ�߳�Ϊ�����̴����ӽ��̵��Ǹ��̣߳���ǰ�ӽ��̵ĳ�ʼ���̻߳�û�еõ�ִ�л��ᣬ�ȴ�����ִ�л���
			 * XP�Լ�XP֮ǰ���ڽ���/�̻߳ص���ʹ��PsLookupThreadByThreadId����ɹ����õȵ�PspCreateThreadִ����
			 * ��֮���߳���GrantAccess����ʼ�����ܳɹ�
			 */
			if (IS_WINDOWSXP_OR_BEFORE())
			{
				KeEnterCriticalRegion();
				if (ExAcquireResourceSharedLite(&g_XProcessResource, TRUE))
				{
					PProcInfo Info = (PProcInfo)ExAllocatePoolWithTag(PagedPool, sizeof(ProcInfo), 'idba');
					if (Info) {

						memset(Info, 0, sizeof(ProcInfo));
						Info->ProcessId = ProcessId;
						strncpy(Info->lpszProcessImageName, (char*)szImageName, strlen((char*)szImageName));
						InsertTailList(&g_XProcessList, &Info->ActiveListEntry);
					}
					ExReleaseResource(&g_XProcessResource);
				}
				KeLeaveCriticalRegion();
			}
			else
			{
				// XP�µ��ý��̻ص�ʱ����PspCreateThread�����ʱ��Dll���ص�UserApcϵͳAPC��δ������
				// ���ʱ���ᵼ�¼���ʱ���ǳ���ǰ���ܿ���crash��
				// Ϊ�˷�ֹXP֮���ƽ̨Ҳ������������������ڽ��̻ص�����UserMode��APC

				Status = InjectDllByApc_Step1(ProcessId);
				//Status = InjectDllIndirectByUserApc(ProcessId);
			}
		}

	} while (FALSE);

	ObDereferenceObject(EProcess);
}

PPEB GetProcessPeb(PEPROCESS Process)
{
	PPEB Peb = PsGetProcessPeb(Process);
	if ((ULONG_PTR)Peb > MmUserProbeAddress)
	{
		Peb = NULL;
	}
	return Peb;
}

/*
 * ע: ���øú�����ȡ��Ľ���ģ�鵼������һ��Ҫ�пռ䣡����
 */
PVOID GetExportFuncAddress(PVOID pImageBase, LPSTR lpszFunName)
{
	PIMAGE_NT_HEADERS       pNtHeaders;
	PIMAGE_EXPORT_DIRECTORY pExportTable;
	DWORD*                  pAddressesArray;
	DWORD*                  pNamesArray;
	WORD*                   pOrdinalsArray;
	DWORD                   dwFuncIndex;
	ULONG                   i;
	CHAR*                   szFunName;
	ULONG_PTR               FunAddress = 0;

	__try
	{
		pNtHeaders = RtlImageNtHeader(pImageBase);
		if (pNtHeaders && pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress) 
		{

			pExportTable =(IMAGE_EXPORT_DIRECTORY *)((ULONG_PTR)pImageBase + pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
			pAddressesArray = (DWORD* )((ULONG_PTR)pImageBase + pExportTable->AddressOfFunctions);
			pNamesArray     = (DWORD* )((ULONG_PTR)pImageBase + pExportTable->AddressOfNames);
			pOrdinalsArray  = (WORD* )((ULONG_PTR)pImageBase + pExportTable->AddressOfNameOrdinals);

			for(i = 0; i < pExportTable->NumberOfNames; i++){
				szFunName = (LPSTR)((ULONG_PTR)pImageBase + pNamesArray[i]);
				dwFuncIndex = pOrdinalsArray[i]; 
				if (_stricmp(szFunName, lpszFunName) == 0) {
					FunAddress = (ULONG_PTR)((ULONG_PTR)pImageBase + pAddressesArray[dwFuncIndex]);	
					break;
				}
			}
		}
	}
	__except( EXCEPTION_EXECUTE_HANDLER )
	{
		FunAddress = 0;
	}

	return (PVOID)FunAddress;
}

/*
 * ע: ImageName����NULL��ȡ��ǰntģ����Ϣ
 */
NTSTATUS GetKernelModuleBase(LPCSTR ImageName, PVOID* pImageBaseAddr, PULONG pImageSize)
{
	NTSTATUS         					ntStatus = STATUS_UNSUCCESSFUL;
	PVOID            					pBuffer	= NULL;
	ULONG            					ulNeed = sizeof(SYSTEM_MODULE_INFORMATION) + 30 * sizeof(SYSTEM_MODULE_INFORMATION_ENTRY);
	ULONG			 					ulIndex = 0;
	PSYSTEM_MODULE_INFORMATION   		pSysModInfo = NULL;
	PSYSTEM_MODULE_INFORMATION_ENTRY	pModEntry = NULL;

	pBuffer = ExAllocatePoolWithTag(PagedPool, ulNeed, 'gkmb');	
	if (pBuffer == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	ntStatus = ZwQuerySystemInformation(SystemModuleInformation, pBuffer, ulNeed, &ulNeed);
	if( ntStatus == STATUS_INFO_LENGTH_MISMATCH )
	{
		ExFreePool(pBuffer);

		pBuffer = ExAllocatePoolWithTag(PagedPool, ulNeed, 'kgmb');	
		if( pBuffer == NULL ) {
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		ntStatus = ZwQuerySystemInformation(SystemModuleInformation, pBuffer, ulNeed, &ulNeed);
		if (ntStatus != STATUS_SUCCESS ) {
			ExFreePool(pBuffer);
			return ntStatus;
		}
	}
	else if( ntStatus != STATUS_SUCCESS )
	{
		ExFreePool(pBuffer);	
		return ntStatus;
	}

	pSysModInfo 	= (PSYSTEM_MODULE_INFORMATION)pBuffer;
	pModEntry 	    = pSysModInfo->Module;

	if (ImageName == NULL) {
		if (pImageBaseAddr) {
			*pImageBaseAddr = pModEntry[0].Base;
		}	
		if (pImageSize) {
			*pImageSize = pModEntry[0].Size;
		}

		ntStatus = STATUS_SUCCESS;
		DbgPrint("[GetKernelModuleBase] nt name:%s", pModEntry[0].ImageName + pModEntry[0].ModuleNameOffset);
	}
	else
	{
		for( ulIndex = 0; ulIndex < pSysModInfo->Count; ulIndex ++ ) 
		{
			if( _stricmp(pModEntry[ulIndex].ImageName + pModEntry[ulIndex].ModuleNameOffset, ImageName) == 0 )
			{
				if (pImageBaseAddr)
					*pImageBaseAddr = pModEntry[ulIndex].Base;	

				if (pImageSize)
					*pImageSize = pModEntry[ulIndex].Size;

				ntStatus = STATUS_SUCCESS;
				break;
			}
		}
	}

	ExFreePool(pBuffer);
	return ntStatus;
}

BOOLEAN GetProcessModuleInfo(PPEB Peb, LPWSTR lpszModuleName, PVOID *pImageBase, PSIZE_T pSize)
{
	BOOLEAN bRet=FALSE;
	PPEB_LDR_DATA Ldr;
	PLIST_ENTRY ListHead,ListPtr;
	PLDR_DATA_TABLE_ENTRY pLdrDataEntry;
	ULONG Length;

	Length = wcslen(lpszModuleName) * sizeof(WCHAR);

	do 
	{
		if (NULL == Peb)
			break;

		__try
		{

			Ldr = Peb->Ldr;
			if (!Ldr || !Ldr->Initialized)
				break;

			if (IsListEmpty(&Ldr->InLoadOrderModuleList))
				break;

			ListPtr = ListHead = Ldr->InLoadOrderModuleList.Flink;

			do 
			{
				pLdrDataEntry = CONTAINING_RECORD(ListPtr,LDR_DATA_TABLE_ENTRY,InLoadOrderLinks);
				if (Length == pLdrDataEntry->BaseDllName.Length &&
					pLdrDataEntry->BaseDllName.Buffer) 
				{
					if (!_wcsnicmp(pLdrDataEntry->BaseDllName.Buffer, lpszModuleName, Length / sizeof(WCHAR)))
					{															
						*pImageBase = pLdrDataEntry->DllBase;
						if (pSize)
							*pSize = pLdrDataEntry->SizeOfImage;
						
						bRet = TRUE;
						break;													
					}
				}
				ListPtr = ListPtr->Flink;

			} while (ListPtr->Flink != ListHead);

		}
		__except(EXCEPTION_EXECUTE_HANDLER) 
		{
		}

	} while (FALSE);

	return bRet;
}

NTSTATUS ConvertWCharToChar(WCHAR* lpwzBuffer, CHAR* lpszBuffer, DWORD dwOutCchLength)
{
	
	ANSI_STRING    ansiOutString;
	UNICODE_STRING uniInString;
	NTSTATUS       Status       = STATUS_SUCCESS;
	WCHAR*         wzPtr        = lpwzBuffer;

	while(*wzPtr) wzPtr++;
	uniInString.Length = (USHORT)((wzPtr - lpwzBuffer) * sizeof(WCHAR));
	uniInString.MaximumLength = uniInString.Length + 2;
	uniInString.Buffer = lpwzBuffer;

	Status = RtlUnicodeStringToAnsiString(&ansiOutString, &uniInString, TRUE);
	if (NT_SUCCESS(Status)) {
		
		
		// ������ô���ֽڵ�Buffer������û�취ֻ�ܿ���ô��
		memcpy(lpszBuffer, ansiOutString.Buffer, dwOutCchLength);
		RtlFreeAnsiString(&ansiOutString);
	}

	return Status;
}

WCHAR* GetFileNameByFullPath(WCHAR* lpszFullImagePath)
{
	LPWSTR lpPos     = lpszFullImagePath;
	if (wcsstr(lpPos, L"\\"))
	{
		lpPos += wcslen(lpszFullImagePath) - 1;
		while(*lpPos != L'\\' && lpPos > lpszFullImagePath){
			lpPos--;
		}
		lpPos += 1;
	}
	return lpPos;
}