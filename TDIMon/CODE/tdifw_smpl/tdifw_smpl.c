///
/// @file         tdifw_smpl.c
/// @author    HueHue
/// @date       2012-6-9
/// @brief      
/// 
/// ��������
/// ������Ϊʾ�����롣δ���꾡���ԣ�����֤�ɿ��ԡ����߶�
/// �κ���ʹ�ô˴��뵼�µ�ֱ�Ӻͼ����ʧ�������Ρ�
/// 

#include "..\inc\tdi_fw\tdi_fw_lib.h"


#define IOCTL_READ_LIST\
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0xa01, METHOD_BUFFERED,\
FILE_READ_DATA )

#define IOCTL_PID_NUM\
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0xa02, METHOD_BUFFERED,\
FILE_READ_DATA )

#define IOCTL_START_NETMON\
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0xa03, METHOD_BUFFERED,\
FILE_READ_DATA )
	
#define IOCTL_STOP_NETMON\
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0xa04, METHOD_BUFFERED,\
FILE_READ_DATA )

#define IOCTRL_GETNETINFO\
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0xa05, METHOD_BUFFERED,\
FILE_READ_DATA )

#define IOCTRL_NETPASS\
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0xa06, METHOD_BUFFERED,\
FILE_READ_DATA )

#define IOCTRL_NETNOTPASS\
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0xa07, METHOD_BUFFERED,\
FILE_READ_DATA )

#define IOCTL_NETAPPEVENT\
	CTL_CODE(FILE_DEVICE_UNKNOWN, 0xa08, METHOD_BUFFERED,\
FILE_READ_DATA )

#define PAGEDCODE code_seg("PAGE")

typedef struct _NetInfo_{
	ULONG pid;
	
	struct {
		struct  sockaddr from;
		struct  sockaddr to;
		int     len;
	} addr;
	
	LARGE_INTEGER time;
	LIST_ENTRY ListEntry;
	
}NetInfo, *PNetInfo;

typedef struct _ListData_ {
	ULONG pid;
	int   Direction;
	int   proto;
	int   type;
	
	/* addr */
	struct {
		struct  sockaddr from;
		struct  sockaddr to;
		int     len;
	} addr;
	
	LARGE_INTEGER time;
	LIST_ENTRY ListEntry;
} ListData, *PListData;

typedef struct _IdData_{
	ULONG pid;
	int port;
}IDDATA,*PIDDATA;

//LIST
LIST_ENTRY g_LinkListHead;
NPAGED_LOOKASIDE_LIST g_nPageList;

//NetMon
LIST_ENTRY g_NetlinkListHead;
NPAGED_LOOKASIDE_LIST g_nNetPageList;

// ȫ��
int DenyPid;
int DenyPort;

int			bNetPass = 0;				//Ӧ�ò�������Ϊ1, ����Ϊ0
int			nStartNetMon = 0;			//���������Ϊ1,����Ϊ0
PKEVENT		pNetAppEvent = NULL;		//Ӧ�ò���ں˲�ͬ���¼�
KEVENT		kNetEvent;					//�ں˲�ͬ���¼�


NTSTATUS
tdifw_driver_entry(
			IN PDRIVER_OBJECT theDriverObject,
            IN PUNICODE_STRING theRegistryPath)
{
	UNICODE_STRING usDevName, usSymLinkName;
	PDEVICE_OBJECT pDevObj;
	NTSTATUS status;
	
	//IoCreateDevice
	RtlInitUnicodeString(&usDevName, L"\\Device\\TDI_Firewall");
	status = IoCreateDevice(theDriverObject, 0, &usDevName, FILE_DEVICE_UNKNOWN, 
							FILE_DEVICE_SECURE_OPEN, TRUE, &pDevObj);
	
	if (!NT_SUCCESS(status)) {
		return status;
	}
	
	//IoCreateSymbolicLink
	RtlInitUnicodeString(&usSymLinkName, L"\\DosDevices\\TDI_Firewall");
	status = IoCreateSymbolicLink(&usSymLinkName, &usDevName);
	if (!NT_SUCCESS(status)) {
		IoDeleteDevice(pDevObj);
		return status;
	}
	
	//tdifw_register_user_device
	tdifw_register_user_device(pDevObj);
	
	
	// ��ʼ������
	InitializeListHead(&g_LinkListHead);
	InitializeListHead(&g_NetlinkListHead);

	// LookasideList    �ڴ��
	ExInitializeNPagedLookasideList(&g_nPageList, NULL, NULL, 0, sizeof(ListData), 'List', 0);
	ExInitializeNPagedLookasideList(&g_nNetPageList, NULL, NULL, 0, sizeof(NetInfo), 'XNet', 0);

	//��ʼ���ں�ͬ���¼�
	KeInitializeEvent(&kNetEvent, SynchronizationEvent, FALSE);

	return STATUS_SUCCESS;
}

VOID
tdifw_driver_unload(
			IN PDRIVER_OBJECT DriverObject)
{
	// û����Դ��Ҫ�ͷš�
	return;
}

#pragma PAGEDCODE
NTSTATUS tdifw_user_device_dispatch(
	IN PDEVICE_OBJECT DeviceObject, IN PIRP pIrp)
{
	PIO_STACK_LOCATION stack;
	NTSTATUS status;
	
	// IRP_MJ_DEVICE_CONTROL
	ULONG code;
	ULONG inlen;
	ULONG outlen;
	int* Input;
	
	// IOCTL_READ_LIST
	ULONG UserDataSize;
	PListData pData;
	PListData pBuffer;
	PLIST_ENTRY pEntry;
	
	//NetInfo
	ULONG NetUserDataSize;
	PNetInfo pNetData;
	PNetInfo pNetBuffer;
	PLIST_ENTRY pNetEntry;
	
	//APP EVENT
	HANDLE hEvent;
	
	// PID  &  PORT
	PIDDATA pIdBuffer;
	
	// Get the IRP stack
	stack = IoGetCurrentIrpStackLocation(pIrp);
	
	switch(stack->MajorFunction)
	{
		case IRP_MJ_CREATE:
			
			break;
		case IRP_MJ_DEVICE_CONTROL:
			//�õ�IOCTL��
			code = stack->Parameters.DeviceIoControl.IoControlCode;
			//�õ����뻺������С
			inlen = stack->Parameters.DeviceIoControl.InputBufferLength;
			//�õ������������С
			outlen = stack->Parameters.DeviceIoControl.OutputBufferLength;
			
			UserDataSize = sizeof(ListData);
			NetUserDataSize = sizeof(NetInfo);
			
			switch (code)
			{
				case IOCTL_READ_LIST:
					
					if(!IsListEmpty(&g_LinkListHead))
					{
						pData = NULL;
						
						pBuffer = pIrp->AssociatedIrp.SystemBuffer;
						
						pEntry = RemoveTailList(&g_LinkListHead);
						pData = CONTAINING_RECORD(pEntry, ListData, ListEntry);
						
						// ��д����
						RtlCopyMemory(pBuffer, pData, UserDataSize);
						ExFreeToNPagedLookasideList(&g_nPageList, pData);
						pIrp->IoStatus.Information = UserDataSize;
						pIrp->IoStatus.Status = STATUS_SUCCESS;
						
						KdPrint(("[TDI_Firewall]  Read List \n"));
					}
					break;
				
				case IOCTL_PID_NUM:
					
					pIdBuffer = pIrp->AssociatedIrp.SystemBuffer;
					
					DenyPid = pIdBuffer->pid;
					DenyPort = pIdBuffer->port;
					
					pIrp->IoStatus.Information = 0;
					pIrp->IoStatus.Status = STATUS_SUCCESS;
					
					break;
				case IOCTL_START_NETMON:
					
					//����ȫ�ֱ����������
					nStartNetMon = 1;
					KdPrint(("[TDI_Firewall]  ��ؿ����� %d",nStartNetMon));
					pIrp->IoStatus.Information = 0;
					pIrp->IoStatus.Status = STATUS_SUCCESS;
					break;
					
				case IOCTL_STOP_NETMON:
					
					//����ȫ�ֱ���ֹͣ���
					nStartNetMon = 0;
					KdPrint(("[TDI_Firewall]  ���ֹͣ�� %d",nStartNetMon));
					pIrp->IoStatus.Information = 0;
					pIrp->IoStatus.Status = STATUS_SUCCESS;
					break;
					
				case IOCTRL_GETNETINFO:
					
					if(!IsListEmpty(&g_NetlinkListHead))
					{
						pNetData = NULL;
						
						pNetBuffer = pIrp->AssociatedIrp.SystemBuffer;
						
						pNetEntry = RemoveTailList(&g_NetlinkListHead);
						pNetData = CONTAINING_RECORD(pNetEntry, NetInfo, ListEntry);
						
						// ��д����
						RtlCopyMemory(pNetBuffer, pNetData, NetUserDataSize);
						ExFreeToNPagedLookasideList(&g_nNetPageList, pNetData);
						pIrp->IoStatus.Information = NetUserDataSize;
						pIrp->IoStatus.Status = STATUS_SUCCESS;
						
						KdPrint(("[TDI_Firewall]  Read NET List \n"));
					}
					break;
					
				case IOCTRL_NETPASS:
					KdPrint(("[TDI_Firewall]  NET PASS"));
					bNetPass = 1;
					KeSetEvent(&kNetEvent, IO_NO_INCREMENT, FALSE);
					pIrp->IoStatus.Information = 0;
					pIrp->IoStatus.Status = STATUS_SUCCESS;
					break;
				
				case IOCTRL_NETNOTPASS:
					KdPrint(("[TDI_Firewall]  NET NOT PASS"));
					bNetPass = 0;
					KeSetEvent(&kNetEvent, IO_NO_INCREMENT, FALSE);
					pIrp->IoStatus.Information = 0;
					pIrp->IoStatus.Status = STATUS_SUCCESS;
					break;
				case IOCTL_NETAPPEVENT:
					
					hEvent = *(PHANDLE)pIrp->AssociatedIrp.SystemBuffer;
					status = ObReferenceObjectByHandle(hEvent, EVENT_MODIFY_STATE, 
						*ExEventObjectType, KernelMode, (PVOID*)&pNetAppEvent, NULL);
					
					if (NT_SUCCESS(status))
					{
						KdPrint(("[TDI_Firewall]  ��ʼ���ں�ͬ���¼��ɹ�!"));
					}
					break;
			}
			break;
	}
	
	IoCompleteRequest( pIrp, IO_NO_INCREMENT );
	return STATUS_SUCCESS;
}

u_short
tdifw_ntohs (u_short netshort)
{
	u_short result = 0;
	((char *)&result)[0] = ((char *)&netshort)[1];
	((char *)&result)[1] = ((char *)&netshort)[0];
	return result;
}


int tdifw_filter(struct flt_request *request)
{
	u_short FromPort;
	u_short ToPort;
	PNetInfo pNetData;
	
	if(request->proto == IPPROTO_TCP)
	{
		struct sockaddr_in* from = (struct sockaddr_in*)&request->addr.from;
		struct sockaddr_in* to = (struct sockaddr_in*)&request->addr.to;
		
		PListData pData=(PListData)ExAllocateFromNPagedLookasideList(&g_nPageList);
		if(!pData)
			return 0;

		//��д���ݽṹ
		pData->Direction = request->direction;
		pData->proto = request->proto;
		pData->addr.from = request->addr.from;
		pData->addr.to = request->addr.to;
		pData->type = request->type;
		pData->pid = request->pid;
		//��������
		KeQuerySystemTime(&pData->time);
		InsertHeadList(&g_LinkListHead,&pData->ListEntry);

		if(nStartNetMon)
		{
			pNetData=(PNetInfo)ExAllocateFromNPagedLookasideList(&g_nNetPageList);
			if(!pNetData)
				return 0;
			
			//��дNet���ݽṹ
			pNetData->pid = request->pid;
			pNetData->addr.from = request->addr.from;
			pNetData->addr.to = request->addr.to;
			
			//����Net����
			KeQuerySystemTime(&pNetData->time);
			InsertHeadList(&g_NetlinkListHead,&pNetData->ListEntry);
			
			KeSetEvent(pNetAppEvent, IO_NO_INCREMENT, TRUE); 
			KeWaitForSingleObject(&kNetEvent, Executive, KernelMode, FALSE, NULL);
			if (bNetPass)
			{
				KdPrint(("[TDI_Firewall]  �����������"));
				return FILTER_ALLOW;
			}
			else
			{
				KdPrint(("[TDI_Firewall]  ��ֹ��������"));
				return FILTER_DENY;
			}
			
		}
		
		FromPort = tdifw_ntohs(from->sin_port);
		ToPort = tdifw_ntohs(to->sin_port);
		
	}
	
	if(request->pid == DenyPid || FromPort == DenyPort || ToPort == DenyPort)
	{
		KdPrint(("[TDI_Firewall]  ���� PID :%d \n",DenyPid));
		KdPrint(("[TDI_Firewall]  ���� Port:%d \n",DenyPort));
		return FILTER_DENY;
	}
	return FILTER_ALLOW;
}
