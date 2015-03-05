/*******************************************************************************

INTEL CORPORATION PROPRIETARY INFORMATION
This software is supplied under the terms of a license agreement or nondisclosure
agreement with Intel Corporation and may not be copied or disclosed except in
accordance with the terms of that agreement
Copyright(c) 2012-2013 Intel Corporation. All Rights Reserved.

*******************************************************************************/
#include <Windows.h>
#include <vector>
#include "pxcsensemanager.h"
#include "pxcemotion.h"

#define  ARRAY_SIZE 512
const int NUM_EMOTIONS = 10;

extern PXCSession *g_session; // from main function
extern volatile bool g_stop;
volatile bool g_disconnected=false;
extern pxcCHAR g_file[1024];

void SetStatus(HWND hwndDlg, pxcCHAR *line);
pxcCHAR* GetCheckedDevice(HWND);
pxcCHAR* GetCheckedModule(HWND);
void DrawBitmap(HWND,PXCImage*);
void DrawEmotion(HWND hwndDlg, int numEmotions, PXCEmotion::EmotionData *arrData);
void UpdatePanel(HWND);
bool GetPlaybackState(HWND hwndDlg);
bool GetRecordState(HWND hwndDlg);

static bool DisplayDeviceConnection(HWND hwndDlg, bool state) {
    if (state) {
        if (!g_disconnected) SetStatus(hwndDlg,L"Device Disconnected");
        g_disconnected = true;
    } else {
        if (g_disconnected) SetStatus(hwndDlg, L"Device Reconnected");
        g_disconnected = false;
    }
    return g_disconnected;
}

class MyHandler: public PXCSenseManager::Handler {
public:

	MyHandler(int pidx, HWND hwndDlg):m_pidx(pidx),m_hwndDlg(hwndDlg) {
	}

	virtual pxcStatus PXCAPI OnModuleQueryProfile(pxcUID, PXCBase*, pxcI32 pidx) {
		return pidx==m_pidx?PXC_STATUS_NO_ERROR:PXC_STATUS_PARAM_UNSUPPORTED;
	}

protected:
	HWND   m_hwndDlg;
	pxcI32 m_pidx;
};

void SimplePipeline(HWND hwndDlg) {
	PXCSenseManager *putil= g_session->CreateSenseManager();
	if (!putil) {
		SetStatus(hwndDlg,L"Failed to create an SDK SenseManager");
		return;
	}

	/* Set Mode & Source */
	pxcStatus sts=PXC_STATUS_NO_ERROR;
	PXCCaptureManager *captureMgr = putil->QueryCaptureManager(); //no need to Release it is released with putil
	pxcCHAR* device = NULL;
	if (GetRecordState(hwndDlg)) {
		sts = captureMgr->SetFileName(g_file,true);
		captureMgr->FilterByDeviceInfo(GetCheckedDevice(hwndDlg),0,0);
	} else if (GetPlaybackState(hwndDlg)) {
		sts = captureMgr->SetFileName(g_file,false);
	} else {
		device = GetCheckedDevice(hwndDlg);
		captureMgr->FilterByDeviceInfo(device,0,0);
	}

	if (sts<PXC_STATUS_NO_ERROR) {
		SetStatus(hwndDlg,L"Failed to Set Record/Playback File");
		return;
	}

	bool stsFlag=true;

	/* Set Module */
	putil->EnableEmotion();

	/* Init */
	SetStatus(hwndDlg,L"Init Started");
	MyHandler handler(0,hwndDlg);

	if (putil->Init(&handler) >= PXC_STATUS_NO_ERROR){
		SetStatus(hwndDlg,L"Streaming");
		g_disconnected=false;

		int numFaces = 0;
		while (!g_stop) {
			if (putil->AcquireFrame(true)<PXC_STATUS_NO_ERROR) break;

			PXCEmotion *emotionDet=putil->QueryEmotion();
			if(emotionDet!=NULL){
				/* Display Results */
				PXCEmotion::EmotionData arrData[NUM_EMOTIONS];
				const PXCCapture::Sample *sample = putil->QueryEmotionSample();
				if (sample) DrawBitmap(hwndDlg, sample->color);
				numFaces = emotionDet->QueryNumFaces();
				for(int i=0; i < numFaces; i++){
					emotionDet->QueryAllEmotionData(i, &arrData[0]);
					DrawEmotion(hwndDlg, NUM_EMOTIONS, arrData);
				}
			}
	
			UpdatePanel(hwndDlg);

			putil->ReleaseFrame();
		}

	} else {
		SetStatus(hwndDlg,L"Init Failed");
		stsFlag=false;
	}

	putil->Close();
	putil->Release();
	if (stsFlag) SetStatus(hwndDlg,L"Stopped");
}