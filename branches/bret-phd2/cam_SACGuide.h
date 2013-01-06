 /*
 * cam_SACGuide.h
 *
 *  Created by Craig Stark .
 *  Copyright (c) 2006-2009 Craig Stark.
 *  All rights reserved.
 *
 *  This source code is distributed under the following "BSD" license
 *  Redistribution and use in source and binary forms, with or without 
 *  modification, are permitted provided that the following conditions are met:
 *    Redistributions of source code must retain the above copyright notice, 
 *     this list of conditions and the following disclaimer.
 *    Redistributions in binary form must reproduce the above copyright notice, 
 *     this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *    Neither the name of Craig Stark, Stark Labs nor the names of its 
 *     contributors may be used to endorse or promote products derived from 
 *     this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */
 #if defined (SAC_CMOS_GUIDE)
#include "cmosDLL.h"
#endif
#if defined (SAC_FCLAB_GUIDE)
#include "FcApiUser.h"
class Camera_SACGuiderClass : public Camera_SAC42Class {
public:
	Camera_SACGuiderClass();
};

#elif defined (SAC_CMOS_GUIDE)
class Camera_SACGuiderClass : public GuideCamera {
public:
	virtual bool	Capture(int duration, usImage& img, wxRect subFrame = wxRect(0,0,0,0), bool recon=false);
	bool	Connect();
	bool	Disconnect();
	void	InitCapture();

	bool	SetGlobalGain(unsigned char gain);
	bool	PulseGuideScope(int direction, int duration);
	void	ClearGuidePort();
	//bool	SetColorGain(unsigned char r_gain, unsigned char g_gain, unsigned char b_gain);
	//int	FindCameras(int DevNums[8]);
	
	Camera_SACGuiderClass(); 
private:
	bool GenericCapture(int duration, usImage& img, int xsize, int ysize, int xpos, int ypos);
	B_V_DLLFUNC CloseUSB;
	B_Cp_DLLFUNC OpenUSB;
	V_Cp_DLLFUNC CmosReset;
	Uc_CpUCp_DLLFUNC SendI2C;
	Uc_CpUCUC_DLLFUNC SendGuideCommand;
	GUIDEREG_DLLFUNC GetFrame;
	HINSTANCE CameraDLL;
	char DevName[32];
};
#endif

