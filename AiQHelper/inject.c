#include "inject.h"
#include "public.h"

/*
 * ע��������ע���EThread���������
 */
NTSTATUS GetProcessFirstEThread(HANDLE ProcessId, PETHREAD* pEThread)
{
	NTSTATUS                    Status              = STATUS_SUCCESS;
	ULONG                       ulRetLength         = 0;
	PETHREAD                    EThread             = NULL;
	PSYSTEM_PROCESS_INFORMATION pTmp                = NULL;
	PSYSTEM_THREAD_INFORMATION  pSysThread          = NULL;
	PSYSTEM_PROCESS_INFORMATION pSysProcessesInfo   = NULL;

	Status = ZwQuerySystemInformation(SystemProcessInformation, NULL, 0, &ulRetLength);
	if (Status == STATUS_INFO_LENGTH_MISMATCH) {

		pSysProcessesInfo = (PSYSTEM_PROCESS_INFORMATION)ExAllocatePoolWithTag(PagedPool, ulRetLength, 'ija1');
		if (pSysProcessesInfo == NULL)
			return STATUS_INSUFFICIENT_RESOURCES;

		Status = ZwQuerySystemInformation(SystemProcessInformation, pSysProcessesInfo, ulRetLength, &ulRetLength);
		if (!NT_SUCCESS(Status)) {

			ExFreePool(pSysProcessesInfo);
			return Status;
		}
	}
	else if (Status != STATUS_SUCCESS)
	{
		return Status;
	}

	pTmp = pSysProcessesInfo;
	while (TRUE)
	{
		if (pTmp->UniqueProcessId == ProcessId) 
		{
			pSysThread = (PSYSTEM_THREAD_INFORMATION)(pTmp + 1);
			break;
		}

		if (!pTmp->NextEntryOffset)
			break;

		pTmp = (PSYSTEM_PROCESS_INFORMATION)((char *)pTmp + pTmp->NextEntryOffset);
	}

	if (pSysThread && pEThread)
	{
		Status = PsLookupThreadByThreadId(pSysThread->ClientId.UniqueThread, pEThread);
	}
	else
	{
		Status = STATUS_UNSUCCESSFUL;
	}

	ExFreePool(pSysProcessesInfo);
	return Status;
}

/*
 * Just For Test
 */
NTSTATUS InjectDllIndirectByUserApc(HANDLE ProcessId)
{
	NTSTATUS                    Status              = STATUS_SUCCESS;
	PVOID                       NtdllBase = NULL;
	PShellCodeInjectApc         ShellCode = NULL;
	ULONG_PTR                   ShellCodeSize = sizeof(ShellCodeInjectApc);
	ULONG_PTR                   NtdllSize = 0;
	ULONG_PTR                   LdrLoadDllFunc = 0;
	ULONG_PTR                   LdrGetProcedureAddressFunc = 0;
	PEPROCESS                   EProcess = NULL;
	PETHREAD                    EThread = NULL;
	PKAPC                       Apc = NULL;
	HANDLE                      ProcessHandle = NULL;
	KAPC_STATE                  ApcState;
	BOOLEAN                     bAttach = FALSE;
	BOOLEAN                     bExcept = FALSE;

	Status = GetProcessFirstEThread(ProcessId, &EThread);
	if (!NT_SUCCESS(Status)){
		return Status;
	}
	
	if (!g_pNtdllbase || !g_dwNtdllSize) {
		ObDereferenceObject(EThread);
		return Status;
	}

	LdrLoadDllFunc = (ULONG_PTR)GetExportFuncAddress(g_pNtdllbase, "LdrLoadDll");
	LdrGetProcedureAddressFunc = (ULONG_PTR)GetExportFuncAddress(g_pNtdllbase, "LdrGetProcedureAddress");
	if (!LdrLoadDllFunc || !LdrGetProcedureAddressFunc) {
		return Status;
	}

	Status = PsLookupProcessByProcessId(ProcessId, &EProcess);
	if (!NT_SUCCESS(Status)) {
		ObDereferenceObject(EThread);
		return Status;
	}

	Status = ObOpenObjectByPointer(EProcess,
		OBJ_KERNEL_HANDLE,
		NULL,
		PROCESS_QUERY_INFORMATION,
		*PsProcessType,
		KernelMode,
		&ProcessHandle
		);

	if (!NT_SUCCESS(Status)){
		ObDereferenceObject(EProcess);
		ObDereferenceObject(EThread);
		return Status;
	}

	Status = ZwAllocateVirtualMemory(ProcessHandle, (PVOID*)&ShellCode, 0, &ShellCodeSize, MEM_TOP_DOWN | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	DbgPrint("InjectDllIndirectByUserApc ZwAllocateVirtualMemory:%x\r\n", ShellCode);
	if (!NT_SUCCESS(Status)){
		ZwClose(ProcessHandle);
		ObDereferenceObject(EProcess);
		ObDereferenceObject(EThread);
		return Status;
	}

	if (EProcess != PsGetCurrentProcess()) {
		bAttach = TRUE;
		KeStackAttachProcess(EProcess, &ApcState);
	}

	__try{

		RtlCopyMemory(ShellCode->ShellCodeApc, (PVOID)InjectDllUserMode, (ULONG)_nopfunc_userapc_end - (ULONG)InjectDllUserMode);
		RtlCopyMemory(ShellCode->szDllFullPath, L"C:\\AiQ3Helper.dll", wcslen(L"C:\\AiQ3Helper.dll") * sizeof(WCHAR));
		RtlCopyMemory(ShellCode->szProcedureName, "AiQ3Helper001", strlen("AiQ3Helper001"));
		ShellCode->LdrLoadDllFunc = LdrLoadDllFunc;
		ShellCode->LdrGetProcedureAddressFunc = LdrGetProcedureAddressFunc;

		Apc = (PKAPC)ExAllocatePoolWithTag(PagedPool, sizeof(KAPC), 'ijan');
		if (Apc == NULL) {
			ZwFreeVirtualMemory((HANDLE)ProcessHandle, (PVOID*)&ShellCode, &ShellCodeSize, MEM_RELEASE);
			ZwClose(ProcessHandle);
			ObDereferenceObject(EProcess);
			ObDereferenceObject(EThread);
			return Status;
		}

		KeInitializeApc(
			Apc,
			EThread,
			OriginalApcEnvironment,
			InjectDllByApcS1KernelRoutine,
			NULL,
			(PKNORMAL_ROUTINE)ShellCode->ShellCodeApc,
			UserMode,
			(PVOID)ShellCode);

		/*
		* Σ�ն�����XP�Լ�XP֮ǰ��Ȼʹ��APC��ΪLdrInitializeThunk��������ʽ���ڽ��̻ص������APC
		* ��δ������ᵼ�¼��ؿ�ǰ�����п��ܳ��ִ�ע�붯̬�����Dll��������һЩϵͳDllû�г�ʼ�����
		* ���µ����⣬Win7�Լ�֮����ֱ���޸�TrapFrame.EIP�ķ�ʽ����ִ��LdrInitializeThunk
		*/
		if (!KeInsertQueueApc(Apc, NULL, NULL, IO_NO_INCREMENT)) {
			ExFreePoolWithTag(Apc, 0);
		}

	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		ZwFreeVirtualMemory((HANDLE)ProcessHandle, (PVOID*)&ShellCode, &ShellCodeSize, MEM_RELEASE);
	}

	if (bAttach) {
		KeUnstackDetachProcess(&ApcState);
	}

	ZwClose(ProcessHandle);
	ObDereferenceObject(EProcess);
	ObDereferenceObject(EThread);

	return Status;
}

NTSTATUS InjectDllByApc_Step1(HANDLE ProcessId)
{
	NTSTATUS                    Status            = STATUS_SUCCESS;
	ULONG                       ulRetLength       = 0;
	PETHREAD                    EThread           = NULL;
	PKAPC                       Apc               = NULL;
	PPEB                        Peb               = NULL;

	// Win8֮��ntdllֻ��ͨ����������Peb��ʽ�õ�����ǰ�������ڸ����� PEB��ͳ�ʼ������
	if (IS_WINDOWS8_OR_LATER() && !g_pNtdllbase )
	{
		Peb = GetProcessPeb(PsGetCurrentProcess());
		if (Peb) {
			GetProcessModuleInfo(Peb, L"ntdll.dll", &g_pNtdllbase, &g_dwNtdllSize);
			DbgPrint("InjectDllByApc_Step1 g_pNtdllbase:%x, g_dwNtdllSize:%x", g_pNtdllbase, g_dwNtdllSize);
		}
	}

	Status = GetProcessFirstEThread(ProcessId, &EThread);
	if (!NT_SUCCESS(Status)) {
		return Status;
	}

	Apc = (PKAPC)ExAllocatePoolWithTag(PagedPool, sizeof(KAPC), 'ija1');
	if (Apc == NULL) {
		ObDereferenceObject(EThread);
		return Status;
	}

	KeInitializeApc(
		Apc,
		EThread,
		OriginalApcEnvironment,
		InjectDllByApcS1KernelRoutine,
		NULL,
		InjectDllByApcS1NormalRoutine,
		KernelMode,
		NULL);

	if (!KeInsertQueueApc(Apc, NULL, NULL, IO_NO_INCREMENT)) {
		ExFreePool(Apc);
	}

	ObDereferenceObject(EThread);
	
	return Status;
}


VOID InjectDllByApcS1KernelRoutine (
	__in struct _KAPC *Apc,
	__deref_inout_opt PKNORMAL_ROUTINE *NormalRoutine,
	__deref_inout_opt PVOID *NormalContext,
	__deref_inout_opt PVOID *SystemArgument1,
	__deref_inout_opt PVOID *SystemArgument2
	)
{
	UNREFERENCED_PARAMETER(NormalRoutine);
	UNREFERENCED_PARAMETER(NormalContext);
	UNREFERENCED_PARAMETER(SystemArgument1);
	UNREFERENCED_PARAMETER(SystemArgument2);
	
	ExFreePool(Apc);
}

VOID InjectDllByApcS1NormalRoutine (
	__in_opt PVOID NormalContext,
	__in_opt PVOID SystemArgument1,
	__in_opt PVOID SystemArgument2
	)
{
	// ע�� ��ǰ�߳����ӽ��̵ĳ�ʼ���̵߳õ�ִ��ʱ

	// 1. д��ShellCode
	NTSTATUS                Status;
	PVOID                   NtdllBase = NULL;
	PShellCodeInjectApc     ShellCode = NULL;
	ULONG_PTR               ShellCodeSize = sizeof(ShellCodeInjectApc);
	ULONG_PTR               NtdllSize = 0;
	ULONG_PTR               LdrLoadDllFunc = 0;
	ULONG_PTR               LdrGetProcedureAddressFunc = 0;
	PKAPC                   Apc = NULL;

	if (g_pNtdllbase && g_dwNtdllSize){

		LdrLoadDllFunc = (ULONG_PTR)GetExportFuncAddress(g_pNtdllbase, "LdrLoadDll");
		LdrGetProcedureAddressFunc = (ULONG_PTR)GetExportFuncAddress(g_pNtdllbase, "LdrGetProcedureAddress");

		if (LdrLoadDllFunc && LdrGetProcedureAddressFunc) {

			Status = ZwAllocateVirtualMemory((HANDLE)-1, (PVOID*)&ShellCode, 0, &ShellCodeSize, MEM_TOP_DOWN | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
			if (NT_SUCCESS(Status) && ShellCode) {

				__try{

					RtlCopyMemory(ShellCode->ShellCodeApc, (PVOID)InjectDllUserMode, (ULONG)_nopfunc_userapc_end - (ULONG)InjectDllUserMode);
					RtlCopyMemory(ShellCode->szDllFullPath, L"C:\\AiQ3Helper.dll", wcslen(L"C:\\AiQ3Helper.dll") * sizeof(WCHAR));
					RtlCopyMemory(ShellCode->szProcedureName, "AiQ3Helper001", strlen("AiQ3Helper001"));
					ShellCode->LdrLoadDllFunc = LdrLoadDllFunc;
					ShellCode->LdrGetProcedureAddressFunc = LdrGetProcedureAddressFunc;

					Apc = (PKAPC)ExAllocatePoolWithTag(PagedPool, sizeof(KAPC), 'ijan');

					KeInitializeApc(
						Apc,
						PsGetCurrentThread(),
						OriginalApcEnvironment,
						InjectDllByApcS1KernelRoutine,
						NULL,
						(PKNORMAL_ROUTINE)ShellCode->ShellCodeApc,
						UserMode,
						(PVOID)ShellCode);

					if (!KeInsertQueueApc(Apc, NULL, NULL, IO_NO_INCREMENT))
					{
						ExFreePoolWithTag(Apc, 0);
						ZwFreeVirtualMemory((HANDLE)-1, (PVOID*)&ShellCode, &ShellCodeSize, MEM_RELEASE);
					}

				}
				__except(EXCEPTION_EXECUTE_HANDLER){
					ZwFreeVirtualMemory((HANDLE)-1, (PVOID*)&ShellCode, &ShellCodeSize, MEM_RELEASE);
				}
			}
		}
	}
}

BOOL InjectDllUserMode(PShellCodeInjectApc BaseData)
{
	NTSTATUS       Status;
	HANDLE         hModule = NULL;
	UNICODE_STRING uniDllName;
	ANSI_STRING    ansiProcName;
	ULONG          ulLength;
	ULONG          FuncAddr = 0;
	CHAR*          szPos = BaseData->szProcedureName;
	WCHAR*         wzPos = BaseData->szDllFullPath;

	while (*wzPos) wzPos++;
	uniDllName.Length = (USHORT)((wzPos - BaseData->szDllFullPath) * sizeof(WCHAR));
	uniDllName.MaximumLength = MAX_PATH * 2;
	uniDllName.Buffer = BaseData->szDllFullPath;

	Status = ((LDRLOADDLL)BaseData->LdrLoadDllFunc)(NULL, NULL, &uniDllName, &hModule);
	if (NT_SUCCESS(Status) && hModule)
	{
		while (*szPos) szPos++;
		ansiProcName.Length = (USHORT)(szPos - BaseData->szProcedureName);
		ansiProcName.MaximumLength = MAX_PATH;
		ansiProcName.Buffer = BaseData->szProcedureName;
		Status = ((LDRGETPROCEDUREADDRESS)BaseData->LdrGetProcedureAddressFunc)((PVOID)hModule, &ansiProcName, 0, (PVOID*)&FuncAddr);

		if (NT_SUCCESS(Status) && FuncAddr)
		{
			typedef VOID (*AiQ3Helper001Ptr)();
			((AiQ3Helper001Ptr)FuncAddr)();
		}
	}

	return TRUE;
}

/*
 * ע�� ��ֹ�������Ż��ú���
 */
VOID _nopfunc_userapc_end()
{
	return;
}