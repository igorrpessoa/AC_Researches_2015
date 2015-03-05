/*******************************************************************************

INTEL CORPORATION PROPRIETARY INFORMATION
This software is supplied under the terms of a license agreement or nondisclosure
agreement with Intel Corporation and may not be copied or disclosed except in
accordance with the terms of that agreement
Copyright(c) 2012-2013 Intel Corporation. All Rights Reserved.

nao
*******************************************************************************/
#include <stdio.h>
#include <Windows.h>
#include <WindowsX.h>
#include <commctrl.h>
#include "resource.h"
#include "pxcemotion.h"
#include "pxcmetadata.h"
#include "service/pxcsessionservice.h"
#include <string.h>


#include "time.h"
time_t rawtime;
struct tm * timeinfo;
char buffer[100];


#define IDC_STATUS   10000
#define ID_DEVICEX   21000
#define ID_MODULEX   22000

#define TEXT_HEIGHT 16
const int NUM_PRIMARY_EMOTIONS = 7;

static WCHAR *EmotionLabels[] = {
	L"ANGER", 
	L"CONTEMPT", 
	L"DISGUST",
	L"FEAR",
	L"JOY",
	L"SADNESS",
	L"SURPRISE"
};
static WCHAR *SentimentLabels[] = {
	L"NEGATIVE", 
	L"POSITIVE", 
	L"NEUTRAL"
};
HINSTANCE   g_hInst=0;
PXCSession *g_session=0;
pxcCHAR     g_file[1024]={0};

/* Panel Bitmap */
HBITMAP     g_bitmap=0;


/* Threading control */
volatile bool g_running=false;
volatile bool g_stop=true;

/* Control Layout */
int g_controls[]={ IDC_SCALE, IDC_LOCATION, ID_START, ID_STOP };
RECT g_layout[3+sizeof(g_controls)/sizeof(g_controls[0])];

void SaveLayout(HWND hwndDlg) {
	GetClientRect(hwndDlg,&g_layout[0]);
	ClientToScreen(hwndDlg,(LPPOINT)&g_layout[0].left);
	ClientToScreen(hwndDlg,(LPPOINT)&g_layout[0].right);
	GetWindowRect(GetDlgItem(hwndDlg,IDC_PANEL),&g_layout[1]);
	GetWindowRect(GetDlgItem(hwndDlg,IDC_STATUS),&g_layout[2]);
	for (int i=0;i<sizeof(g_controls)/sizeof(g_controls[0]);i++)
		GetWindowRect(GetDlgItem(hwndDlg,g_controls[i]),&g_layout[3+i]);
}

void RedoLayout(HWND hwndDlg) {
	RECT rect;
	GetClientRect(hwndDlg,&rect);

	/* Status */
	SetWindowPos(GetDlgItem(hwndDlg,IDC_STATUS),hwndDlg,
		0,
		rect.bottom-(g_layout[2].bottom-g_layout[2].top),
		rect.right-rect.left,
		(g_layout[2].bottom-g_layout[2].top),SWP_NOZORDER);

	/* Panel */
	SetWindowPos(GetDlgItem(hwndDlg,IDC_PANEL),hwndDlg,
		(g_layout[1].left-g_layout[0].left),
		(g_layout[1].top-g_layout[0].top),
		rect.right-(g_layout[1].left-g_layout[0].left)-(g_layout[0].right-g_layout[1].right),
		rect.bottom-(g_layout[1].top-g_layout[0].top)-(g_layout[0].bottom-g_layout[1].bottom),
		SWP_NOZORDER);

	/* Buttons & CheckBoxes */
	for (int i=0;i<sizeof(g_controls)/sizeof(g_controls[0]);i++) {
		SetWindowPos(GetDlgItem(hwndDlg,g_controls[i]),hwndDlg,
			rect.right-(g_layout[0].right-g_layout[3+i].left),
			(g_layout[3+i].top-g_layout[0].top),
			(g_layout[3+i].right-g_layout[3+i].left),
			(g_layout[3+i].bottom-g_layout[3+i].top),
			SWP_NOZORDER);
	}
}

static void PopulateDevice(HMENU menu) {
	DeleteMenu(menu,0,MF_BYPOSITION);

	PXCSession::ImplDesc desc;
	memset(&desc,0,sizeof(desc));
	desc.group=PXCSession::IMPL_GROUP_SENSOR;
	desc.subgroup=PXCSession::IMPL_SUBGROUP_VIDEO_CAPTURE;
	HMENU menu1=CreatePopupMenu();
	for (int i=0,k=ID_DEVICEX;;i++) {
		PXCSession::ImplDesc desc1;
		if (g_session->QueryImpl(&desc,i,&desc1)<PXC_STATUS_NO_ERROR) break;
		PXCCapture *capture;
		if (g_session->CreateImpl<PXCCapture>(&desc1,&capture)<PXC_STATUS_NO_ERROR) continue;
		for (int j=0;;j++) {
			PXCCapture::DeviceInfo dinfo;
			if (capture->QueryDeviceInfo(j,&dinfo)<PXC_STATUS_NO_ERROR) break;
			AppendMenu(menu1,MF_STRING,k++,dinfo.name);
		}
	}
	CheckMenuRadioItem(menu1,0,GetMenuItemCount(menu1),0,MF_BYPOSITION);
	InsertMenu(menu,0,MF_BYPOSITION|MF_POPUP,(UINT_PTR)menu1,L"Device");
}

static int GetChecked(HMENU menu) {
	for (int i=0;i<GetMenuItemCount(menu);i++)
		if (GetMenuState(menu,i,MF_BYPOSITION)&MF_CHECKED) return i;
	return 0;
}

pxcCHAR* GetCheckedDevice(HWND hwndDlg) {
	HMENU menu=GetSubMenu(GetMenu(hwndDlg),0);	// ID_DEVICE
	static pxcCHAR line[256];
	GetMenuString(menu,GetChecked(menu),line,sizeof(line)/sizeof(pxcCHAR),MF_BYPOSITION);
	return line;
}

static void PopulateModule(HMENU menu) {
	DeleteMenu(menu,1,MF_BYPOSITION);

	PXCSession::ImplDesc desc, desc1;
	memset(&desc,0,sizeof(desc));
	desc.cuids[0]=PXCEmotion::CUID;
	HMENU menu1=CreatePopupMenu();
	int i;
	for (i=0;;i++) {
		if (g_session->QueryImpl(&desc,i,&desc1)<PXC_STATUS_NO_ERROR) break;
		AppendMenu(menu1,MF_STRING,ID_MODULEX+i,desc1.friendlyName);
	}
	CheckMenuRadioItem(menu1,0,i,0,MF_BYPOSITION);
	InsertMenu(menu,1,MF_BYPOSITION|MF_POPUP,(UINT_PTR)menu1,L"Module");
}

pxcCHAR *GetCheckedModule(HWND hwndDlg) {
	HMENU menu=GetSubMenu(GetMenu(hwndDlg),1);	// ID_MODULE
	static pxcCHAR line[256];
	GetMenuString(menu,GetChecked(menu),line,sizeof(line)/sizeof(pxcCHAR),MF_BYPOSITION);
	return line;
}

static DWORD WINAPI ThreadProc(LPVOID arg) {
	void SimplePipeline(HWND hwndDlg);
	SimplePipeline((HWND)arg);

	PostMessage((HWND)arg,WM_COMMAND,ID_STOP,0);
	g_running=false;
	return 0;
}

void SetStatus(HWND hwndDlg, pxcCHAR *line) {
	HWND hwndStatus=GetDlgItem(hwndDlg,IDC_STATUS);
	SetWindowText(hwndStatus,line);
}

bool GetPlaybackState(HWND hwndDlg) {
	return (GetMenuState(GetMenu(hwndDlg),ID_MODE_PLAYBACK,MF_BYCOMMAND)&MF_CHECKED)!=0;
}

bool GetRecordState(HWND hwndDlg) {
	return (GetMenuState(GetMenu(hwndDlg),ID_MODE_RECORD,MF_BYCOMMAND)&MF_CHECKED)!=0;
}

void DrawBitmap(HWND hwndDlg, PXCImage *image) {
	if (!image) return;
    if (g_bitmap) {
        DeleteObject(g_bitmap);
		g_bitmap=0;
    }

	// Calcualte FPS
    static int g_fps_nframes;
    static double g_fps_first;
    if ((g_fps_nframes++)==0) {
        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        g_fps_first=(double)now.QuadPart/(double)freq.QuadPart;
    }
    if (g_fps_nframes>30) {
        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        int fps=(int)((double)g_fps_nframes/((double)now.QuadPart/(double)freq.QuadPart-g_fps_first));

        pxcCHAR line[1024];
        swprintf_s<1024>(line,L"Rate (%d fps)", fps);
        SetStatus(hwndDlg,line);
        g_fps_nframes=0;
    }

    PXCImage::ImageInfo info = image->QueryInfo();
    PXCImage::ImageData data;
    if (image->AcquireAccess(PXCImage::ACCESS_READ,PXCImage::PIXEL_FORMAT_RGB32, &data)>=PXC_STATUS_NO_ERROR) {
		HWND hwndPanel=GetDlgItem(hwndDlg,IDC_PANEL);
        HDC dc=GetDC(hwndPanel);
		BITMAPINFO binfo;
		memset(&binfo,0,sizeof(binfo));
		binfo.bmiHeader.biWidth= data.pitches[0]/4;
		binfo.bmiHeader.biHeight= - (int)info.height;
		binfo.bmiHeader.biBitCount=32;
		binfo.bmiHeader.biPlanes=1;
		binfo.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
		binfo.bmiHeader.biCompression=BI_RGB;
        g_bitmap=CreateDIBitmap(dc, &binfo.bmiHeader, CBM_INIT, data.planes[0], &binfo, DIB_RGB_COLORS);
        ReleaseDC(hwndPanel, dc);
		image->ReleaseAccess(&data);
    }
}

static RECT GetResizeRect(RECT rc, BITMAP bm) { /* Keep the aspect ratio */
	RECT rc1;
	float sx=(float)rc.right/(float)bm.bmWidth;
	float sy=(float)rc.bottom/(float)bm.bmHeight;
	float sxy=sx<sy?sx:sy;
	rc1.right=(int)(bm.bmWidth*sxy);
	rc1.left=(rc.right-rc1.right)/2+rc.left;
	rc1.bottom=(int)(bm.bmHeight*sxy);
	rc1.top=(rc.bottom-rc1.bottom)/2+rc.top;
	return rc1;
}

void UpdatePanel(HWND hwndDlg) {
	if (!g_bitmap) return;

	HWND panel=GetDlgItem(hwndDlg,IDC_PANEL);
	RECT rc;
	GetClientRect(panel,&rc);

	HDC dc=GetDC(panel); if(!dc) return;
	HBITMAP bitmap=CreateCompatibleBitmap(dc,rc.right,rc.bottom);
	HDC dc2=CreateCompatibleDC(dc);
	if(!dc2)
	{
		ReleaseDC(hwndDlg, dc);
		DeleteObject(bitmap);
		return;
	}

	SelectObject(dc2,bitmap);
	FillRect(dc2,&rc,(HBRUSH)GetStockObject(GRAY_BRUSH));
	SetStretchBltMode(dc2, HALFTONE);

	/* Draw the main window */
	HDC dc3=CreateCompatibleDC(dc);
	if(!dc3)
	{
		ReleaseDC(hwndDlg, dc);
		DeleteObject(dc2);
		DeleteObject(bitmap);
		return;
	}

	SelectObject(dc3,g_bitmap); 
	BITMAP bm;
	GetObject(g_bitmap,sizeof(BITMAP),&bm);

	bool scale=Button_GetState(GetDlgItem(hwndDlg,IDC_SCALE))&BST_CHECKED;
	bool mirror=Button_GetState(GetDlgItem(hwndDlg,IDC_MIRROR))&BST_CHECKED;
	if (mirror) {
		if (scale) {
			RECT rc1=GetResizeRect(rc,bm);
			StretchBlt(dc2,rc1.left+rc1.right-1,rc1.top,-rc1.right,rc1.bottom,dc3,0,0,bm.bmWidth,bm.bmHeight,SRCCOPY);
		} else {
			StretchBlt(dc2,bm.bmWidth-1,0,-bm.bmWidth,bm.bmHeight,dc3,0,0,bm.bmWidth,bm.bmHeight,SRCCOPY);
		}
	} else {
		if (scale) {
			RECT rc1=GetResizeRect(rc,bm);
			StretchBlt(dc2,rc1.left,rc1.top,rc1.right,rc1.bottom,dc3,0,0,bm.bmWidth,bm.bmHeight,SRCCOPY);
		} else {
			BitBlt(dc2,0,0,rc.right,rc.bottom,dc3,0,0,SRCCOPY);
		}
	}

	DeleteObject(dc3);
	DeleteObject(dc2);
	ReleaseDC(hwndDlg,dc);

	HBITMAP bitmap2=(HBITMAP)SendMessage(panel,STM_GETIMAGE,0,0);
	if (bitmap2) DeleteObject(bitmap2);
	SendMessage(panel,STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)bitmap);
	InvalidateRect(panel,0,TRUE);
	DeleteObject(bitmap);
}

void DrawEmotion(HWND hwndDlg, int numEmotions, PXCEmotion::EmotionData *arrData)
{
	if (!g_bitmap) return;
	if(arrData->rectangle.w == 0) return;

	HWND hwndPanel=GetDlgItem(hwndDlg,IDC_PANEL);
	HDC dc=GetDC(hwndPanel);
	HDC dc2=CreateCompatibleDC(dc);
	if(!dc2)
	{
		ReleaseDC(hwndDlg, dc);
		return;
	}
	SelectObject(dc2,g_bitmap);

	BITMAP bm;
	GetObject(g_bitmap,sizeof(bm),&bm);

	HPEN cyan=CreatePen(PS_SOLID,3,RGB(255,0,0));
	if (Button_GetState(GetDlgItem(hwndDlg,IDC_LOCATION))&BST_CHECKED) {
		if (cyan) SelectObject(dc2,cyan);

		MoveToEx(dc2, arrData->rectangle.x, arrData->rectangle.y, 0);
		LineTo(dc2,   arrData->rectangle.x                       , arrData->rectangle.y+arrData->rectangle.h);
		LineTo(dc2,   arrData->rectangle.x+arrData->rectangle.w  , arrData->rectangle.y+arrData->rectangle.h);
		LineTo(dc2,  arrData->rectangle.x+arrData->rectangle.w  , arrData->rectangle.y);
		LineTo(dc2,  arrData->rectangle.x                       , arrData->rectangle.y);
	}

	bool emotionPresent=false;
	WCHAR line1[64];
	int epidx= -1; pxcI32 maxscoreE = -3; pxcF32 maxscoreI = 0;
	for (int i=0;i<NUM_PRIMARY_EMOTIONS;i++) {
		if (arrData[i].evidence < maxscoreE)  continue;
		if (arrData[i].intensity < maxscoreI) continue;
		maxscoreE=arrData[i].evidence;
		maxscoreI=arrData[i].intensity;
		epidx=i;
	}
	if(epidx!=-1) {
		wcscpy_s(line1,sizeof(line1)/sizeof(pxcCHAR), EmotionLabels[epidx]);
	}

	//20150302copied from old EmotionViewer code


	// ************************************** File IO END of Testing section here *********************************************************************

	
	if (maxscoreI > 0.4){


		TextOut(dc2, arrData->rectangle.x + arrData->rectangle.w, arrData->rectangle.y, line1, wcslen(line1));
		emotionPresent = true;

		// INSERTED 20140502 15:02

		{  // 20140502 Start of file and timestamp block here


			// ************************************************************ 20140402 File IO Testing Here ***********************************************
			// 20140402 code to capture recognised emotions and output them to a file for recording.
			// More work needed to also capture the sentiment labels.  All worked 100% today with hours of testing.
			// See MSDN link used for code at http://msdn.microsoft.com/en-us/library/yeby3zcb.aspx
			// 201404502 Code was re-located from above location as it was writing every emotion it thought to the file.
			//      This re-location has led to only the emotions that appear with confidence on the screen being written to the file.

			wchar_t str[100];
			wchar_t str2[20];
			//wchar_t str4[10]; // 20140502 Used for the faceID parameter
			size_t  strSize;
			FILE*   fileHandle;

			// Create a file in text and Unicode encoding mode.  
			// The a+ is Append:  open or create a file for update; writing is done at the end of the file.  See D&D 475
			//if ((fileHandle = _wfopen( L"emotion_test.txt",L"a+,ccs=UNICODE")) == NULL) // C4996
			// 20150204 Changed attribute for the file here to w see http://www.cplusplus.com/reference/cstdio/fopen/
			// 20150204  This only writes out the most current emotion to the file.  VITAL.  Worked 100% today.
			if ((fileHandle = _wfopen(L"emotion_test.txt", L"w,ccs=UNICODE")) == NULL) // C4996

				// Note: _wfopen is deprecated; consider using _wfopen_s instead.  AK No action on this yet 20140425
			{
				wprintf(L"_wfopen failed!\n");
				//return(0);
			}


			// *************************  Code to capture a time stamp under own program code control *************************************************
			// Time stamp sample code from this is available of this reference link http://www.cplusplus.com/reference/ctime/strftime/

			time(&rawtime);  // returns the raw time stamp
			timeinfo = localtime(&rawtime);  // converts the raw time to local time

			// Code here picks out the hour,  minutes and seconds from the time struct.  See link for full set of specifiers for time
			strftime(buffer, 100, "\n\nTime stamp %I:%M:%S%p.", timeinfo);


			//See this link http://stackoverflow.com/questions/3074776/how-to-convert-char-array-to-wchar-t-array
			// 20140425  This code is vital in order to add the time stamp to the file with the emotion detected.
			size_t origsize = strlen(buffer) + 1;
			const size_t newsize = 100;
			size_t convertedChars = 0;
			wchar_t wcstring[newsize];

			// 20140425  Function to do the conversion here
			mbstowcs_s(&convertedChars, wcstring, origsize, buffer, _TRUNCATE);

			// 20140611 This code print out the time stamp on the video window underneath the emotion valance/affect

			TextOut(dc2, arrData->rectangle.x + arrData->rectangle.w, arrData->rectangle.y + TEXT_HEIGHT * 2, wcstring, wcslen(wcstring));

			// 20140425  Adds on "Emotion ID =" to the text string with the time stamp
			// wcscat_s(wcstring,L"Emotion ID ="); // AK 20140502 added on faceID here.

			// 20140425  Copies wcstring into str
			// wcscpy_s(str, sizeof(str)/sizeof(wchar_t),wcstring);


			// strSize = wcslen(str);
			// This code writes out the "Time stamp H:M:S Emotion ID = " which is in 'str'  out to the file 'emotion_test.txt'

			// 20150204 Delete time stamp action here today for testing.
			//if (fwrite(str, sizeof(wchar_t), strSize, fileHandle) != strSize)
			//{
			//	wprintf(L"fwrite failed!\n");
			//}

			// This code here is copying line1 'containing the emotion number and name' string from above into str. 
			wcscat_s(line1, L"\n");

			wcscpy_s(str, sizeof(str) / sizeof(wchar_t), line1);
			//wcscat_s(str,L"\n");

			strSize = wcslen(str);
			// This code does the write but fails if str is too long

			if (fwrite(str, sizeof(wchar_t), strSize, fileHandle) != strSize)
			{
				wprintf(L"fwrite failed!\n");
			}


			// Code reference 20140502  ww.delphigroups.info/3/8/231235.html
			//   This need further testing and update.
			// 20140613 This code prints out faceID for multiple faces for recognised emotions

			//wchar_t w[sizeof(faceID)]= {0};
			//wchar_t str4[20]={0};
			// Converts an integer to a string 
			//_itow(faceID, w, 10);
			// Puts a label into str4 here
			// 20150204  - Deleted for ENACT activity testing
			//wcscat_s(str4,L"\t\tFace ID =");
			// Adds w onto str4 for writing to the file
			// wcscat_s(str4,w);

			//if (fwrite(str4, sizeof(wchar_t), 20, fileHandle) != strSize)
			//{
			//	wprintf(L"fwrite failed!\n");
			//} 
			// End of faceID code section here.  Remember if one face is not tracked then the faceID value reduces or swaps 
			//     This happened with the multi face recording on the Tab once the tab was taken away.



			// Close the file.

			if (fclose(fileHandle))
			{
				wprintf(L"fclose failed!\n");
			}

		}  // 20140502 End of file and timestamp block here





	}

	WCHAR line2[64];
	WCHAR line3[64];
	WCHAR test[64]; // 20140613 Does not seem to be used in the code.
	// If maxscoreI is > 0.4 the test below will be true.
	if (emotionPresent){
		int spidx = -1;
		maxscoreE = -3; maxscoreI = 0;
		for (int i = 0; i<(numEmotions - NUM_PRIMARY_EMOTIONS); i++) {
			// 20140331 Alfie above is 10 - 7 = 3.  Below is 7 + 0 (last three are 7, 8 and 9 in arrData
			//   does same processing as above but for the SentimentLabels

			if (arrData[NUM_PRIMARY_EMOTIONS + i].evidence  < maxscoreE) continue;
			if (arrData[NUM_PRIMARY_EMOTIONS + i].intensity < maxscoreI) continue;
			// As above for primary emotions the code will end up with the emotion of highest evidence 
			//                and intensity in both variables
			maxscoreE = arrData[NUM_PRIMARY_EMOTIONS + i].evidence;
			maxscoreI = arrData[NUM_PRIMARY_EMOTIONS + i].intensity;
			spidx = i;
		}
		if (spidx != -1){
			wcscpy_s(line2, sizeof(line2) / sizeof(pxcCHAR), SentimentLabels[spidx]);
		}



		TextOut(dc2, arrData->rectangle.x + arrData->rectangle.w, arrData->rectangle.y + TEXT_HEIGHT, line2, wcslen(line2));

		// 20140423 Screen outputs test here.  Prints out contents of line3
		// Worked and not used.  See below wcscpy_s(line3,sizeof(line3)/sizeof(pxcCHAR),L"Alfie Keary");

		// 20140425  ***************  START Test section to read file created above and output to window ********************************
		// 20140425  Code below is a test to pick up text from the file created above to capture emotions
		//              This worked 100% today and prints out ANGER-JOY-HAPPY from the emotion_test.txt file.  This is the first 15 bytes.  See below

		FILE *fp;  // C file pointer declared
		char buffer[15]; // buffer to store 15 bytes read
		wchar_t bufferw[15]; // buffer to store wchar_t for functions below
		fp = fopen("emotion_test.txt", "r"); // opens the file for reading and returns the file pointer
		fseek(fp, SEEK_SET, 0); // goes to start of file
		fread(buffer, 15, 1, fp); // reads in 15 bytes from file
		mbstowcs(bufferw, buffer, 15); // copies from char buffer to wchar_t bufferw 
		wcscpy_s(line3, sizeof(line3) / sizeof(pxcCHAR), bufferw); // copy bufferw to line 3
		fclose(fp); // closes the file here


		// 20140425  ***************  END Test section to read file created above and output to window ********************************
		// 20140425  The code below worked 100% today 20140425. The 15 bytes from the file was displayed next to video face frame.
		// 20140611 Three lines below provide areas for additional text data if required.  Takes data from the emotion text file created
		//TextOut(dc2, arrData->rectangle.x + arrData->rectangle.w, arrData->rectangle.y + TEXT_HEIGHT * 3, line3, wcslen(line3));

		//TextOut(dc2, arrData->rectangle.x + arrData->rectangle.w, arrData->rectangle.y + TEXT_HEIGHT * 4, line3, wcslen(line3));

		//TextOut(dc2, arrData->rectangle.x + arrData->rectangle.w, arrData->rectangle.y + TEXT_HEIGHT * 5, line3, wcslen(line3));


	}


	if (cyan) DeleteObject(cyan);
	DeleteObject(dc2);
	ReleaseDC(hwndPanel,dc);
}

static void GetPlaybackFile(void) {
	OPENFILENAME ofn;
	memset(&ofn,0,sizeof(ofn));
	ofn.lStructSize=sizeof(ofn);
	ofn.lpstrFilter=L"RSSDK clip (*.rssdk)\0*.rssdk\0All Files (*.*)\0*.*\0\0";
	ofn.lpstrFile=g_file; g_file[0]=0;
	ofn.nMaxFile=sizeof(g_file)/sizeof(pxcCHAR);
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
	if (!GetOpenFileName(&ofn)) g_file[0]=0;
}

static void GetRecordFile(void) {
	OPENFILENAME ofn;
	memset(&ofn,0,sizeof(ofn));
	ofn.lStructSize=sizeof(ofn);
	ofn.lpstrFilter=L"RSSDK clip (*.rssdk)\0*.rssdk\0All Files (*.*)\0*.*\0\0";
	ofn.lpstrFile=g_file; g_file[0]=0;
	ofn.nMaxFile=sizeof(g_file)/sizeof(pxcCHAR);
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
	if (!GetSaveFileName(&ofn)) g_file[0]=0;
	if (ofn.nFilterIndex==1 && ofn.nFileExtension==0) {
		int len = wcslen(g_file);
		if (len>1 && len<sizeof(g_file)/sizeof(pxcCHAR)-7) {
			wcscpy_s(&g_file[len], rsize_t(7), L".rssdk\0");
		}
	}
}

INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM) { 
	HMENU menu=GetMenu(hwndDlg);
	HMENU menu1;

    switch (message) { 
    case WM_INITDIALOG:
		CheckDlgButton(hwndDlg,IDC_LOCATION,BST_CHECKED);
		CheckDlgButton(hwndDlg,IDC_SCALE,BST_CHECKED);
		PopulateDevice(menu);
		PopulateModule(menu);
		SaveLayout(hwndDlg);
        return TRUE; 
    case WM_COMMAND: 
		menu1=GetSubMenu(menu,0);
		if (LOWORD(wParam)>=ID_DEVICEX && LOWORD(wParam)<ID_DEVICEX+GetMenuItemCount(menu1)) {
			CheckMenuRadioItem(menu1,0,GetMenuItemCount(menu1),LOWORD(wParam)-ID_DEVICEX,MF_BYPOSITION);
			return TRUE;
		}
		menu1=GetSubMenu(menu,1);
		if (LOWORD(wParam)>=ID_MODULEX && LOWORD(wParam)<ID_MODULEX+GetMenuItemCount(menu1)) {
			CheckMenuRadioItem(menu1,0,GetMenuItemCount(menu1),LOWORD(wParam)-ID_MODULEX,MF_BYPOSITION);
			return TRUE;
		}
        switch (LOWORD(wParam)) {
        case IDCANCEL:
			g_stop=true;
			if (g_running) {
				PostMessage(hwndDlg,WM_COMMAND,IDCANCEL,0);
			} else {
				DestroyWindow(hwndDlg); 
				PostQuitMessage(0);
			}
            return TRUE;
 		case ID_PIPELINE_SIMPLE:
			CheckMenuItem(menu,ID_PIPELINE_SIMPLE,MF_CHECKED);
			CheckMenuItem(menu,ID_PIPELINE_ADVANCED,MF_UNCHECKED);
			return TRUE;
		case ID_PIPELINE_ADVANCED:
			CheckMenuItem(menu,ID_PIPELINE_SIMPLE,MF_UNCHECKED);
			CheckMenuItem(menu,ID_PIPELINE_ADVANCED,MF_CHECKED);
			return TRUE;
		case ID_START:
			Button_Enable(GetDlgItem(hwndDlg,ID_START),false);
			Button_Enable(GetDlgItem(hwndDlg,ID_STOP),true);
			for (int i=0;i<GetMenuItemCount(menu);i++)
				EnableMenuItem(menu,i,MF_BYPOSITION|MF_GRAYED);
			DrawMenuBar(hwndDlg);
			g_stop=false;
			g_running=true;
			CreateThread(0,0,ThreadProc,hwndDlg,0,0);
			Sleep(0);
			return TRUE;
		case ID_STOP:
			g_stop=true;
			if (g_running) {
				PostMessage(hwndDlg,WM_COMMAND,ID_STOP,0);
			} else {
				for (int i=0;i<GetMenuItemCount(menu);i++)
					EnableMenuItem(menu,i,MF_BYPOSITION|MF_ENABLED);
				DrawMenuBar(hwndDlg);
				Button_Enable(GetDlgItem(hwndDlg,ID_START),true);
				Button_Enable(GetDlgItem(hwndDlg,ID_STOP),false);
			}
			return TRUE;
		case ID_MODE_LIVE:
			CheckMenuItem(menu,ID_MODE_LIVE,MF_CHECKED);
			CheckMenuItem(menu,ID_MODE_PLAYBACK,MF_UNCHECKED);
			CheckMenuItem(menu,ID_MODE_RECORD,MF_UNCHECKED);
			return TRUE;
		case ID_MODE_PLAYBACK:
			CheckMenuItem(menu,ID_MODE_LIVE,MF_UNCHECKED);
			CheckMenuItem(menu,ID_MODE_PLAYBACK,MF_CHECKED);
			CheckMenuItem(menu,ID_MODE_RECORD,MF_UNCHECKED);
			GetPlaybackFile();
			return TRUE;
		case ID_MODE_RECORD:
			CheckMenuItem(menu,ID_MODE_LIVE,MF_UNCHECKED);
			CheckMenuItem(menu,ID_MODE_PLAYBACK,MF_UNCHECKED);
			CheckMenuItem(menu,ID_MODE_RECORD,MF_CHECKED);
			GetRecordFile();
			return TRUE;
        } 
		break;
	case WM_SIZE:
		RedoLayout(hwndDlg);
		return TRUE;
    } 
    return FALSE; 
} 

#pragma warning(disable:4706) /* assignment within conditional */
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR, int) {
	InitCommonControls();
	g_hInst=hInstance;

	g_session=PXCSession_Create();
	if (!g_session) {
		MessageBoxW(0,L"Failed to create an SDK session",L"Emotion Viewer",MB_ICONEXCLAMATION|MB_OK);
        return 1;
    }

    /* Optional steps to send feedback to Intel Corporation to understand how often each SDK sample is used. */
    PXCMetadata * md = g_session->QueryInstance<PXCMetadata>();
    if(md)
    {
        pxcCHAR sample_name[] = L"Emotion Viewer";
        md->AttachBuffer(PXCSessionService::FEEDBACK_SAMPLE_INFO, (pxcBYTE*)sample_name, sizeof(sample_name));
    }

    HWND hWnd=CreateDialogW(hInstance,MAKEINTRESOURCE(IDD_MAINFRAME),0,DialogProc);
    if (!hWnd)  {
		MessageBoxW(0,L"Failed to create a window",L"Emotion Viewer",MB_ICONEXCLAMATION|MB_OK);
        return 1;
    }

	HWND hWnd2=CreateStatusWindow(WS_CHILD|WS_VISIBLE,L"OK",hWnd,IDC_STATUS);
	if (!hWnd2) {
		MessageBoxW(0,L"Failed to create a status bar",L"Emotion Viewer",MB_ICONEXCLAMATION|MB_OK);
        return 1;
	}

	UpdateWindow(hWnd);

    MSG msg;
	for (int sts;(sts=GetMessageW(&msg,NULL,0,0));) {
        if (sts == -1) return sts;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

	g_stop=true;
	while (g_running) Sleep(5);
	g_session->Release();
    return (int)msg.wParam;
}

