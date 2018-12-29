// TDIMonDlg.cpp : implementation file
//

#include "stdafx.h"
#include "TDIMon.h"
#include "TDIMonDlg.h"

#include <process.h>
#include <Tlhelp32.h>
#include <winioctl.h>    //ʹ��CTL_CODE�������winioctl.h
#include <winsock.h>     //�ֽ���Ļ���
#include <afxwin.h>

#include "NetInfoDlg.h"

#pragma comment(lib,"Ws2_32.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


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

#define BEGINTHREADEX(psa, cbStack, pfnStartAddr, \
	pvParam, fdwCreate, pdwThreadId)              \
	((HANDLE)_beginthreadex(                      \
	(void *)        (psa),                        \
	(unsigned)      (cbStack),                    \
	(PTHREAD_START) (pfnStartAddr),               \
	(void *)        (pvParam),                    \
	(unsigned)      (fdwCreate),                  \
(unsigned *)    (pdwThreadId)))

typedef unsigned (__stdcall *PTHREAD_START) (void *);

DWORD WINAPI EnumTcp(PVOID lpParameter);
DWORD WINAPI NetMon(PVOID lpParameter);

typedef struct _ListData_ {
	ULONG pid;            /* PID          */
	int   Direction;      /* ����            */
	int   proto;          /* see IPPROTO_xxx */
	int   type;           /* see TYPE_xxx    */
	
	/* addr */
	struct {
		struct  sockaddr from;
		struct  sockaddr to;
		int     len;
	} addr;
	
	LARGE_INTEGER time;
	LIST_ENTRY ListEntry;
} ListData, *PListData;


typedef struct _NetInfo_{
	ULONG pid;
	
	/* ��ַ */
	struct {
		struct  sockaddr from;
		struct  sockaddr to;
		int     len;
	} addr;
	
	LARGE_INTEGER time;
	LIST_ENTRY ListEntry;
	
}NetInfo, *PNetInfo;


typedef struct _IdData_{
	ULONG pid;
	int port;
}IDDATA,*PIDDATA;


/* types of request */
enum {
	TYPE_CONNECT = 1,
	TYPE_DATAGRAM,
	TYPE_RESOLVE_PID,
	TYPE_CONNECT_ERROR,
	TYPE_LISTEN,
	TYPE_NOT_LISTEN,
	TYPE_CONNECT_CANCELED,
	TYPE_CONNECT_RESET,
	TYPE_CONNECT_TIMEOUT,
	TYPE_CONNECT_UNREACH,
	TYPE_PROCESS_CREATE,		// add by tan wen
	TYPE_PROCESS_TERMINATE		// add by tan wen
};

/*
 * Protocols
 */
#define IPPROTO_IP              0               /* dummy for IP */
#define IPPROTO_ICMP            1               /* control message protocol */
#define IPPROTO_TCP             6               /* tcp */
#define IPPROTO_UDP             17              /* user datagram protocol */

//
//ͳ�Ƽ����������ȫ�ֱ���ֻ��Ϊ�˷�������б�����
//
int i = 0;
HANDLE hDevice;
HANDLE g_hNetAppEvent;
/////////////////////////////////////////////////////////////////////////////
// CAboutDlg dialog used for App About

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// Dialog Data
	//{{AFX_DATA(CAboutDlg)
	enum { IDD = IDD_ABOUTBOX };
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAboutDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	//{{AFX_MSG(CAboutDlg)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
	//{{AFX_MSG_MAP(CAboutDlg)
		// No message handlers
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CTDIMonDlg dialog

CTDIMonDlg::CTDIMonDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CTDIMonDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CTDIMonDlg)
	m_Check = FALSE;
	//}}AFX_DATA_INIT
	// Note that LoadIcon does not require a subsequent DestroyIcon in Win32
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CTDIMonDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CTDIMonDlg)
	DDX_Control(pDX, IDC_LOG, m_LogList);
	DDX_Check(pDX, IDC_CONN_MON, m_Check);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CTDIMonDlg, CDialog)
	//{{AFX_MSG_MAP(CTDIMonDlg)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_OK, OnOk)
	ON_NOTIFY(NM_RCLICK, IDC_LOG, OnRclickLog)
	ON_COMMAND(IDM_CLEARLOG, OnClearlog)
	ON_BN_CLICKED(IDC_CONN_MON, OnConnMon)
	ON_WM_CLOSE()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CTDIMonDlg message handlers

BOOL CTDIMonDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon
	
	// TODO: Add extra initialization here
	//��ʼ���б�
	m_LogList.InsertColumn( 0, _T("Э��"), LVCFMT_LEFT, 50);
	m_LogList.InsertColumn( 1, _T("Դ��ַ"), LVCFMT_LEFT, 135);
	m_LogList.InsertColumn( 2, _T("Ŀ�ĵ�ַ"), LVCFMT_LEFT, 135);
	m_LogList.InsertColumn( 3, _T("����״̬"), LVCFMT_LEFT, 90);
	m_LogList.InsertColumn( 4, _T("PID"), LVCFMT_LEFT, 100);

	//����������
    m_LogList.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
	
	//�����̻߳�ȡTCP��Ϣ
	HANDLE hThread = BEGINTHREADEX(	NULL, 0, EnumTcp, this, 0, NULL);
	//��������߳�
	HANDLE hNetMonThread = BEGINTHREADEX( NULL, 0, NetMon, this, 0, NULL);

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CTDIMonDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CTDIMonDlg::OnPaint() 
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, (WPARAM) dc.GetSafeHdc(), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

// The system calls this to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CTDIMonDlg::OnQueryDragIcon()
{
	return (HCURSOR) m_hIcon;
}

//
//��ȡTCP���������Ϣ
//
DWORD WINAPI EnumTcp(PVOID lpParameter){
	
	CTDIMonDlg *pDlg = (CTDIMonDlg*)lpParameter;
	
	//���豸
	hDevice = CreateFile("\\\\.\\TDI_Firewall",
						GENERIC_READ | GENERIC_WRITE,
						0,		    // share mode none
						NULL,   	// no security
						OPEN_EXISTING,
						FILE_ATTRIBUTE_NORMAL,
						NULL );		// no template
	
	if (hDevice == INVALID_HANDLE_VALUE)
		return false;

	DWORD dwOutput;
	BOOL  bRet;
	ListData data;
	//
	char  szLocalAddr[128];
	char  szRemoteAddr[128];
	char  szProto[128];
	char  szType[128];

	while(true)
	{
		bRet = DeviceIoControl(hDevice, IOCTL_READ_LIST, NULL, 0, &data, sizeof(data), &dwOutput, NULL);
		
		if (bRet && dwOutput!=0)
		{
			struct sockaddr_in* from = (struct sockaddr_in*)&data.addr.from;
			struct sockaddr_in* to = (struct sockaddr_in*)&data.addr.to;
			
			//��ʽ������
			sprintf(szLocalAddr, "%s:%u", inet_ntoa(from->sin_addr), 
				ntohs((unsigned short)(0x0000FFFF & from->sin_port)));
			
			sprintf(szRemoteAddr, "%s:%u", inet_ntoa(to->sin_addr),
				ntohs((unsigned short)(0x0000FFFF & to->sin_port)));

			//�Խ���ID���и�ʽ��
			CString pid;
			pid.Format("%d",data.pid);

			//Э���ʽ��			
			switch(data.proto)
			{
			case IPPROTO_IP:
				strcpy(szProto,"IP");
				break;
			case IPPROTO_ICMP:
				strcpy(szProto,"ICMP");
				break;
			case IPPROTO_TCP:
				strcpy(szProto,"TCP");
				break;
			case IPPROTO_UDP:
				strcpy(szProto,"UDP");
				break;
			default:
				break;
			}

			//״̬��ʽ��
			switch(data.type)
			{
			case TYPE_CONNECT:
				strcpy(szType, "����");
				break;
			case TYPE_DATAGRAM:
				strcpy(szType, "���ݱ�");
				break;
			case TYPE_CONNECT_ERROR:
				strcpy(szType, "���Ӵ���");
				break;
			case TYPE_LISTEN:
				strcpy(szType, "����");
				break;
			case TYPE_NOT_LISTEN:
				strcpy(szType, "ֹͣ����");
				break;
			case TYPE_CONNECT_CANCELED:
				strcpy(szType, "ȡ������");
				break;
			case TYPE_CONNECT_RESET:
				strcpy(szType, "���Ӹ�λ");
				break;
			case TYPE_CONNECT_TIMEOUT:
				strcpy(szType, "���ӳ�ʱ");
				break;
			case TYPE_CONNECT_UNREACH:
				strcpy(szType, "���Ӳ��ɴ�");
				break;
			default:
				break;
			}
			
			//������к�����
			pDlg->m_LogList.InsertItem(i,szProto,1);
			pDlg->m_LogList.SetItemText(i,1,szLocalAddr);
			pDlg->m_LogList.SetItemText(i,2,szRemoteAddr);
			pDlg->m_LogList.SetItemText(i,3,szType);
			pDlg->m_LogList.SetItemText(i,4,pid);
			++i;
			
			//����ͳ�Ƽ���
			CString szCount;
			szCount.Format("%d",i);
			SetDlgItemText(AfxGetMainWnd()->m_hWnd,IDC_COUNT,szCount);
			
		}
		else{
			Sleep(1000);
			continue;
		}
	}
	
	CloseHandle(hDevice);
	return 0;
}

//
//�������ζ˿ںͽ���ID
//
void CTDIMonDlg::OnOk() 
{
	CString szPID;
	CString szPORT;
	DWORD dwOutput;
	IDDATA pIdBuffer;

	// ��ȡ�˿������ID
	GetDlgItemText(IDC_PID,szPID);
	GetDlgItemText(IDC_PORT,szPORT);

	//CString -> int
	pIdBuffer.pid = atoi(szPID);
	pIdBuffer.port = atoi(szPORT);
	
	if (pIdBuffer.port>65535 || pIdBuffer.port<0)
	{
		MessageBox("Port number error!");
		return;
	}

	// PID��¯��˵�����16777216,������Ҳû���Թ�.
	if (pIdBuffer.pid>16777216 || pIdBuffer.pid<0)
	{
		MessageBox("PID number error!");
		return;
	}

	BOOL bRet = DeviceIoControl(hDevice, IOCTL_PID_NUM, &pIdBuffer, sizeof(pIdBuffer), NULL, 0, &dwOutput, NULL);
	if (!bRet)
	{
		MessageBox("DeviceIoControl false!");
		return;
	}
}

void CTDIMonDlg::OnRclickLog(NMHDR* pNMHDR, LRESULT* pResult) 
{
	// TODO: Add your control notification handler code here
	LPNMITEMACTIVATE pNMItemActivate = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);

	int nItem = m_LogList.GetNextItem( -1, LVNI_ALL | LVNI_SELECTED);
	
	if(nItem != -1)
	{	
		CPoint point;
		GetCursorPos( & point );
		CMenu m_menu;
		VERIFY( m_menu.LoadMenu( IDR_LOG) );
		CMenu* popup = m_menu.GetSubMenu(0);
		ASSERT( popup != NULL );
		popup->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this );		
	}

	*pResult = 0;
}

void CTDIMonDlg::OnClearlog() 
{
	// TODO: Add your command handler code here
	
	// ɾ�������б���Ŀ�����ü���Ϊ0
	if(m_LogList.DeleteAllItems())
	{
		i = 0;
	}
	SetDlgItemText(IDC_COUNT,"0");
}

void CTDIMonDlg::OnConnMon() 
{
	// TODO: Add your control notification handler code here

	DWORD dwOutput;

	if (m_Check)
	{
		m_Check = FALSE;
		//�رռ��
		DeviceIoControl(hDevice, IOCTL_STOP_NETMON, NULL, 0, NULL, 0, &dwOutput, NULL);
	}
	else
	{
		m_Check = TRUE;
		//�������
		DeviceIoControl(hDevice, IOCTL_START_NETMON, NULL, 0, NULL, 0, &dwOutput, NULL);
	}
}

DWORD WINAPI NetMon(PVOID lpParameter){
	
	CTDIMonDlg *pDlg = (CTDIMonDlg*)lpParameter;
	AFX_MANAGE_STATE(AfxGetStaticModuleState());

	DWORD dwOutput;
	NetInfo pNetInfo;
	
	//�����¼�
	g_hNetAppEvent = CreateEvent( NULL, FALSE, FALSE, NULL);	
	DeviceIoControl(hDevice, IOCTL_NETAPPEVENT, &g_hNetAppEvent, sizeof(HANDLE), NULL, 0, &dwOutput, NULL);

	while (true)
	{	
		WaitForSingleObject(g_hNetAppEvent, INFINITE);
		
		memset(&pNetInfo, 0, sizeof(NetInfo));
		
		BOOL bRet = DeviceIoControl(hDevice, IOCTRL_GETNETINFO, NULL, 0, &pNetInfo, sizeof(NetInfo), &dwOutput, NULL);
		if (bRet && dwOutput!=0)
		{		
			if (pDlg->m_Check) 
			{
				//��ȡԶ�̵�ַ
				struct sockaddr_in* to = (struct sockaddr_in*)&pNetInfo.addr.to;

				CString szNetInfor;
				szNetInfor.Format("PID:%d  Զ�̵�ַ:%s:%u",pNetInfo.pid,inet_ntoa(to->sin_addr),
					ntohs((unsigned short)(0x0000FFFF & to->sin_port)));

				//ȷ�����У�ȡ����ֹ
				if (IDOK == MessageBox(NULL,szNetInfor,"NetMon",MB_OK || IDCANCEL))
				{
					DeviceIoControl(hDevice, IOCTRL_NETPASS, NULL, 0, NULL, 0, &dwOutput, NULL);
					MessageBox(NULL,"�ѷ���!","NetMon",MB_OK);
				}
				else
				{
					DeviceIoControl(hDevice, IOCTRL_NETNOTPASS, NULL, 0, NULL, 0, &dwOutput, NULL);
					MessageBox(NULL,"�ɹ���ֹ!","NetMon",MB_OK);
					
				}
			}
		}
		else
		{
			Sleep(1000);
			continue;
		}
	}
}

void CTDIMonDlg::OnClose() 
{
	// TODO: Add your message handler code here and/or call default
	//��������ʱ�رռ��
	DWORD dwOutput;
	DeviceIoControl(hDevice, IOCTL_STOP_NETMON, NULL, 0, NULL, 0, &dwOutput, NULL);
	CDialog::OnClose();
	
}
